/*
 * Ported from the bundled parser under src/xml to pugixml.
 *
 * The commented-out rp_spi_load / _print / _compare bodies are removed rather
 * than ported. They are commented out in rp-spi.h as well, so nothing can call
 * them, and they referred to the deleted XMLDocument API plus rp_write_to_spi /
 * rp_read_from_spi, which do not exist anywhere in the tree. read_header_length
 * went with them: it had no other caller, and no shipped config has a <header>
 * node. All of it stays available in history if it is ever wanted back.
 *
 * Fixed along the way:
 *  - device_addr is unsigned short, and the old code did
 *        sscanf(..., "%x", (unsigned int*)&device_addr)
 *    which writes four bytes into a two byte object -- a stack overwrite of
 *    whatever the compiler happened to place next to it.
 *  - The heap-allocated XMLDocument leaked on all seven early returns.
 *  - g_enable_verbous was a strong global with the same name in rp-i2c.cpp, so
 *    both shared libraries exported it and load order decided which one
 *    rp_spi_enable_verbous() actually flipped.
 *  - <iostream> is gone; it was there for one wcout call and pulled
 *    std::ios_base::Init, a static constructor, into every load of the library.
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <vector>

#include "rp-spi.h"
#include "spi/spi.h"
#include "xml/rp-xml.h"

namespace rp_spi_fpga {

static bool g_enable_verbous = false;

#define MSG(...)             \
    if (g_enable_verbous) {  \
        printf(__VA_ARGS__); \
    }
#define MSG_A(...) printf(__VA_ARGS__)

void rp_spi_enable_verbous() {
    g_enable_verbous = true;
}

void rp_spi_disable_verbous() {
    g_enable_verbous = false;
}

/* ------------------------------------------------------------------ header -- */

int prepareHeader(bool read_flag, char data_size, unsigned short address, char* buffer, char header_length) {
    if (header_length == 1) {
        if (address > 0x1F) return -1;
        buffer[0] = (char)address;
        if (read_flag) buffer[0] = 0x80 | buffer[0];
        if (data_size == 2) buffer[0] = 0x20 | buffer[0];
        if (data_size == 3) buffer[0] = 0x40 | buffer[0];
        if (data_size >= 4) buffer[0] = 0x60 | buffer[0];
        return 0;
    }

    if (header_length == 2) {
        if (address > 0x1FFF) return -1;
        ((short*)buffer)[0] = address;
        if (read_flag) buffer[0] = 0x80 | buffer[0];
        if (data_size == 2) buffer[0] = 0x20 | buffer[0];
        if (data_size == 3) buffer[0] = 0x40 | buffer[0];
        if (data_size >= 4) buffer[0] = 0x60 | buffer[0];
        return 0;
    }

    return -1;
}

/* ------------------------------------------------------------------- bus ---- */

int rp_write_to_spi_fpga(const char* spi_dev_path, unsigned int fpga_address, unsigned short dev_address, int reg_addr,
                         uint8_t spi_val_to_write) {
    return write_to_fpga_spi(spi_dev_path, fpga_address, dev_address, reg_addr, spi_val_to_write);
}

int rp_read_from_spi_fpga(const char* spi_dev_path, unsigned int fpga_address, unsigned short dev_address, int reg_addr,
                          uint8_t* value) {
    return read_from_fpga_spi(spi_dev_path, fpga_address, dev_address, reg_addr, value);
}

/* --------------------------------------------------------- config parsing --- */

namespace {

struct RegEntry {
    int         address;
    int         value;
    int         defaultValue;
    std::string writeMode;
    std::string description;
};

struct Config {
    std::string           busName;
    unsigned int          fpgaBase   = 0;
    unsigned short        deviceAddr = 0;
    int                   headerLength = 0;
    std::vector<RegEntry> registers;
};

bool readRegHex(const pugi::xml_node& reg, const char* name, int& out) {
    bool bad = false;
    if (!rp_xml::readHexAttr(reg, name, out, &bad)) {
        MSG_A("[rp_spi] Missing attribute %s in register\n", name);
        return false;
    }
    if (bad) {
        MSG_A("[rp_spi] Attribute %s in register is not a hex number, using 0\n", name);
    }
    return true;
}

bool readStrAttr(const pugi::xml_node& reg, const char* name, std::string& out) {
    const pugi::xml_attribute attr = reg.attribute(name);
    if (!attr) {
        MSG_A("[rp_spi] Missing attribute %s in register\n", name);
        return false;
    }
    out = attr.value();
    return true;
}

/* Reads <node address="..."> into a width-checked destination. The old code
 * cast the address of a narrower object to unsigned int* for sscanf. */
bool readNodeAddress(const pugi::xml_document& doc, const char* nodeName, unsigned int& out, unsigned int limit) {
    const pugi::xml_node node = rp_xml::findFirstByName(doc, nodeName);
    if (!node) {
        MSG_A("[rp_spi] Missing node %s in configuration file\n", nodeName);
        return false;
    }

    int v = 0;
    if (!rp_xml::readHexAttr(node, "address", v)) {
        MSG_A("[rp_spi] Missing attribute address in %s\n", nodeName);
        return false;
    }
    if ((unsigned int)v > limit) {
        MSG_A("[rp_spi] Attribute address in %s is out of range: 0x%X\n", nodeName, (unsigned int)v);
        return false;
    }
    out = (unsigned int)v;
    return true;
}

bool readBusName(const pugi::xml_document& doc, std::string& out) {
    const pugi::xml_node node = rp_xml::findFirstByName(doc, "bus_name");
    if (!node) {
        MSG_A("[rp_spi] Missing node bus_name in configuration file\n");
        return false;
    }
    out = rp_xml::innerText(node);
    if (out.empty()) {
        MSG_A("[rp_spi] Node bus_name is empty\n");
        return false;
    }
    return true;
}

bool parseRegisters(const pugi::xml_document& doc, Config& cfg) {
    const pugi::xml_node regSet = rp_xml::findFirstByName(doc, "reg_set");
    if (!regSet) {
        MSG_A("[rp_spi] Missing node reg_set in configuration file\n");
        return false;
    }

    for (pugi::xml_node reg : regSet.children("register")) {
        RegEntry e{};
        if (!readRegHex(reg, "address", e.address) || !readRegHex(reg, "value", e.value) ||
            !readRegHex(reg, "default", e.defaultValue) || !readStrAttr(reg, "write", e.writeMode) ||
            !readStrAttr(reg, "decription", e.description)) {
            return false;
        }
        cfg.registers.push_back(std::move(e));
    }
    return true;
}

int wantedValue(const RegEntry& e) {
    if (e.writeMode == "value") {
        return e.value;
    }
    if (e.writeMode == "default") {
        return e.defaultValue;
    }
    return -1;
}

}  // namespace

/* ----------------------------------------------------------- public entry --- */

int rp_spi_load_via_fpga(const char* configuration_file) {
    pugi::xml_document doc;
    std::string        err;

    if (!rp_xml::loadFile(doc, configuration_file, &err)) {
        MSG_A("[rp_spi] %s\n", err.c_str());
        return -1;
    }

    Config cfg;
    if (!readBusName(doc, cfg.busName)) {
        return -1;
    }

    unsigned int fpgaBase = 0;
    if (!readNodeAddress(doc, "fpga_base", fpgaBase, 0xFFFFFFFFu)) {
        return -1;
    }
    cfg.fpgaBase = fpgaBase;

    unsigned int devAddr = 0;
    if (!readNodeAddress(doc, "device_on_bus", devAddr, 0xFFFFu)) {
        return -1;
    }
    cfg.deviceAddr = (unsigned short)devAddr;

    if (!parseRegisters(doc, cfg)) {
        return -1;
    }

    for (const RegEntry& e : cfg.registers) {
        const int want = wantedValue(e);
        if (want < 0) {
            MSG("[rp_spi] Skip write %s to spi\n", e.description.c_str());
            continue;
        }

        const uint8_t data = (uint8_t)want;
        if (rp_write_to_spi_fpga(cfg.busName.c_str(), cfg.fpgaBase, cfg.deviceAddr, e.address, data) != 0) {
            MSG_A("[rp_spi] ERROR write value of %s to spi\n", e.description.c_str());
        } else {
            MSG("[rp_spi] Success write %s value 0x%.2X by address 0x%.2X \n", e.description.c_str(), data, e.address);
        }
    }
    return 0;
}

}  // namespace rp_spi_fpga

/*
 * Ported from the bundled parser under src/xml to pugixml.
 *
 * Behavioural notes
 * -----------------
 * - The document is now a stack value. Every early return in the old code
 *   leaked the heap-allocated XMLDocument; there were nine such paths.
 * - The three public entry points duplicated the same ~40 line attribute block.
 *   It is factored into parseRegisters() now, which is what let the copy-paste
 *   defects show: two of the three copies reported a missing "decription"
 *   attribute as 'Missing attribute write'.
 * - An unparsable (as opposed to absent) hex value used to be silently taken as
 *   0, because the sscanf return value was ignored. It is still 0, so shipped
 *   configs with value="" keep working, but now it is reported.
 * - g_enable_verbous is static. It was a strong global defined identically in
 *   both rp-i2c.cpp and rp-spi.cpp, so the two shared libraries exported the
 *   same symbol and whichever loaded first won.
 * - <iostream> is gone; it was pulled in only for one wcout error print and
 *   dragged std::ios_base::Init -- a static constructor -- into every load.
 */

#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <vector>

#include <linux/i2c-dev.h>

#include "rp-i2c.h"
#include "rp_hw.h"
#include "xml/rp-xml.h"

namespace rp_i2c {

pthread_mutex_t g_rp_i2c_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool g_enable_verbous = false;

#define MSG(...)               \
    if (g_enable_verbous) {    \
        printf(__VA_ARGS__);   \
    }
#define MSG_A(...) printf(__VA_ARGS__)

void rp_i2c_enable_verbous() {
    g_enable_verbous = true;
}

void rp_i2c_disable_verbous() {
    g_enable_verbous = false;
}

/* ------------------------------------------------------------ bus access -- */

int rp_write_to_i2c(const char* i2c_dev_path, int i2c_dev_address, int i2c_dev_reg_addr, unsigned short i2c_val_to_write,
                    bool force) {
    pthread_mutex_lock(&g_rp_i2c_mutex);
    int ret = rp_I2C_InitDevice(i2c_dev_path, i2c_dev_address);
    if (ret != RP_HW_OK) {
        pthread_mutex_unlock(&g_rp_i2c_mutex);
        return ret;
    }
    rp_I2C_setForceMode(force);

    ret = rp_I2C_SMBUS_Write(i2c_dev_reg_addr, i2c_val_to_write);
    pthread_mutex_unlock(&g_rp_i2c_mutex);
    return ret;
}

int rp_read_from_i2c(const char* i2c_dev_path, int i2c_dev_address, int i2c_dev_reg_addr, uint8_t& value, bool force) {
    pthread_mutex_lock(&g_rp_i2c_mutex);
    int ret = rp_I2C_InitDevice(i2c_dev_path, i2c_dev_address);
    if (ret != RP_HW_OK) {
        pthread_mutex_unlock(&g_rp_i2c_mutex);
        return ret;
    }
    rp_I2C_setForceMode(force);

    ret = rp_I2C_SMBUS_Read(i2c_dev_reg_addr, &value);
    pthread_mutex_unlock(&g_rp_i2c_mutex);
    return ret;
}

/* --------------------------------------------------------- config parsing -- */

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
    int                   deviceAddr = 0;
    std::vector<RegEntry> registers;
};

/* Reads bus_name and device_on_bus/@address. */
bool readAddress(const pugi::xml_document& doc, Config& cfg) {
    const pugi::xml_node busNode = rp_xml::findFirstByName(doc, "bus_name");
    if (!busNode) {
        MSG_A("[rp_i2c] Missing node bus_name in configuration file\n");
        return false;
    }

    const pugi::xml_node devNode = rp_xml::findFirstByName(doc, "device_on_bus");
    if (!devNode) {
        MSG_A("[rp_i2c] Missing node device_on_bus in configuration file\n");
        return false;
    }

    if (!rp_xml::readHexAttr(devNode, "address", cfg.deviceAddr)) {
        MSG_A("[rp_i2c] Missing attribute address in device_on_bus\n");
        return false;
    }

    cfg.busName = rp_xml::innerText(busNode);
    if (cfg.busName.empty()) {
        MSG_A("[rp_i2c] Node bus_name is empty\n");
        return false;
    }
    return true;
}

/* Reads a required hex attribute and reports it the way the callers expect. */
bool readRegHex(const pugi::xml_node& reg, const char* name, int& out) {
    bool bad = false;
    if (!rp_xml::readHexAttr(reg, name, out, &bad)) {
        MSG_A("[rp_i2c] Missing attribute %s in register\n", name);
        return false;
    }
    if (bad) {
        /* Not fatal: keeps the old behaviour of treating a broken value as 0,
         * but no longer silently. */
        MSG_A("[rp_i2c] Attribute %s in register is not a hex number, using 0\n", name);
    }
    return true;
}

bool readStrAttr(const pugi::xml_node& reg, const char* name, std::string& out) {
    const pugi::xml_attribute attr = reg.attribute(name);
    if (!attr) {
        MSG_A("[rp_i2c] Missing attribute %s in register\n", name);
        return false;
    }
    out = attr.value();
    return true;
}

bool parseRegisters(const pugi::xml_document& doc, Config& cfg) {
    const pugi::xml_node regSet = rp_xml::findFirstByName(doc, "reg_set");
    if (!regSet) {
        MSG_A("[rp_i2c] Missing node reg_set in configuration file\n");
        return false;
    }

    /* children("register") instead of every child: robust even if someone turns
     * on comment parsing later. */
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

/* Loads the file and parses it whole. Replaces readFile() + the per-function
 * copies of the attribute loop. */
bool loadConfig(const char* path, Config& cfg) {
    pugi::xml_document doc;
    std::string        err;

    if (!rp_xml::loadFile(doc, path, &err)) {
        MSG_A("[rp_i2c] %s\n", err.c_str());
        return false;
    }
    return readAddress(doc, cfg) && parseRegisters(doc, cfg);
}

/* Value the config wants in the register, or -1 when it says not to touch it. */
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

/* ---------------------------------------------------------- public entry -- */

int rp_i2c_load(const char* configuration_file, bool force) {
    Config cfg;
    if (!loadConfig(configuration_file, cfg)) {
        return -1;
    }

    for (const RegEntry& e : cfg.registers) {
        const int want = wantedValue(e);
        if (want < 0) {
            MSG("[rp_i2c] Skip write %s to i2c\n", e.description.c_str());
            continue;
        }

        if (rp_write_to_i2c(cfg.busName.c_str(), cfg.deviceAddr, e.address, (unsigned short)(uint8_t)want, force) !=
            RP_HW_OK) {
            MSG_A("[rp_i2c] ERROR write value of %s to i2c\n", e.description.c_str());
            continue;
        }
        MSG("[rp_i2c] Success write %s value 0x%.2X by address 0x%.2X \n", e.description.c_str(), (uint8_t)want,
            e.address);
    }
    return 0;
}

int rp_i2c_print(const char* configuration_file, bool force) {
    Config cfg;
    if (!loadConfig(configuration_file, cfg)) {
        return -1;
    }

    for (const RegEntry& e : cfg.registers) {
        uint8_t data = 0;
        if (rp_read_from_i2c(cfg.busName.c_str(), cfg.deviceAddr, e.address, data, force) != RP_HW_OK) {
            MSG_A("[rp_i2c] ERROR read value of %s from i2c\n", e.description.c_str());
            continue;
        }
        MSG_A("[rp_i2c] Addr: 0x%.2X\tval: 0x%.2X\t%s\n", e.address, data, e.description.c_str());
    }
    return 0;
}

int rp_i2c_compare(const char* configuration_file, bool force) {
    Config cfg;
    if (!loadConfig(configuration_file, cfg)) {
        return -1;
    }

    bool equal = true;
    for (const RegEntry& e : cfg.registers) {
        const int want = wantedValue(e);
        if (want < 0) {
            continue;
        }

        uint8_t data = 0;
        if (rp_read_from_i2c(cfg.busName.c_str(), cfg.deviceAddr, e.address, data, force) != RP_HW_OK) {
            MSG_A("[rp_i2c] ERROR read value of %s from i2c\n", e.description.c_str());
            equal = false;
            continue;
        }

        if ((uint8_t)want != data) {
            equal = false;
        }
        MSG("[rp_i2c] Addr: 0x%.2X\tvalue in i2c: 0x%.2X\tvalue in xml: 0x%.2X\t%s\n", e.address, data, (uint8_t)want,
            e.description.c_str());
    }
    return equal ? 0 : 1;
}

}  // namespace rp_i2c

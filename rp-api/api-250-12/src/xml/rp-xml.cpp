#include "rp-xml.h"

#include <cctype>
#include <cstring>
#include <cstdlib>

namespace rp_xml {

bool loadFile(pugi::xml_document& doc, const char* path, std::string* error) {
    if (path == nullptr) {
        if (error != nullptr) {
            *error = "no configuration file given";
        }
        return false;
    }

    /* load_file reports a missing file and a malformed document through the same
     * result object, so the old "open, seek, tellg, new char[length]" dance --
     * which allocated new char[-1] when the file was missing -- is gone. */
    const pugi::xml_parse_result res = doc.load_file(path);
    if (!res) {
        if (error != nullptr) {
            *error = std::string(path) + ": " + res.description() + " at offset " + std::to_string(res.offset);
        }
        return false;
    }
    return true;
}

pugi::xml_node findFirstByName(const pugi::xml_node& root, const char* name) {
    for (pugi::xml_node child : root.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        if (std::strcmp(child.name(), name) == 0) {
            return child;
        }
        pugi::xml_node found = findFirstByName(child, name);
        if (found) {
            return found;
        }
    }
    return pugi::xml_node();
}

bool readHexAttr(const pugi::xml_node& node, const char* name, int& out, bool* bad) {
    if (bad != nullptr) {
        *bad = false;
    }
    out = 0;

    const pugi::xml_attribute attr = node.attribute(name);
    if (!attr) {
        return false;
    }

    const char* text = attr.value();
    while (*text != '\0' && std::isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return true; /* empty value -- 0, as the shipped configs expect */
    }

    char*               end = nullptr;
    const unsigned long v   = std::strtoul(text, &end, 16);

    while (end != nullptr && *end != '\0' && std::isspace((unsigned char)*end)) {
        ++end;
    }
    if (end == text || (end != nullptr && *end != '\0')) {
        if (bad != nullptr) {
            *bad = true;
        }
        return true; /* present but unparsable -- 0, and the caller may warn */
    }

    out = (int)v;
    return true;
}

std::string innerText(const pugi::xml_node& node) {
    std::string s = node.child_value();

    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) {
        ++b;
    }
    while (e > b && std::isspace((unsigned char)s[e - 1])) {
        --e;
    }
    return s.substr(b, e - b);
}

}  // namespace rp_xml

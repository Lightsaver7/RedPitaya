#pragma once

#include <string>
#include "pugixml.hpp"

/*
 * Replacement for the bundled XML parser under src/xml.
 *
 * The old code only ever needed five operations, so this is deliberately a thin
 * layer over pugixml rather than a wrapper class hierarchy. Documents are now
 * value types held on the stack, which removes the `delete doc` leaks that the
 * previous code had on every early-return path.
 */

namespace rp_xml {

/* Loads and parses a configuration file. On failure returns false and, when
 * `error` is non-null, fills it with a human readable reason. */
bool loadFile(pugi::xml_document& doc, const char* path, std::string* error);

/* Depth-first search for the first element with the given name.
 * The old XMLDocument::FindFirstNodeByName searched the whole tree, not just
 * direct children, so configs must keep working at any nesting depth. */
pugi::xml_node findFirstByName(const pugi::xml_node& root, const char* name);

/* Reads a hexadecimal attribute ("0x1f" or "1f").
 *
 * Returns false only when the attribute is absent -- that is the one case the
 * callers treat as a configuration error.
 *
 * An empty value yields out = 0 and true, because the shipped SPI configs rely
 * on it: <register address="0x02" value="" default="" .../>. The old code
 * behaved the same way by accident (sscanf failed and left the caller's
 * zero-initialised variable untouched); here it is explicit.
 *
 * A malformed non-empty value also yields 0, but reports it through `bad` so
 * callers can warn instead of silently programming register 0. */
bool readHexAttr(const pugi::xml_node& node, const char* name, int& out, bool* bad = nullptr);

/* Inner text of a node with surrounding whitespace removed.
 * Trimming is new: <bus_name> is used as a device path, and a stray newline in
 * a hand-edited config used to turn into an unopenable filename. */
std::string innerText(const pugi::xml_node& node);

}  // namespace rp_xml

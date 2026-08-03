#include "rp_json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

namespace {

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const unsigned char* _data, size_t _bytes) {
    std::string out;
    out.reserve(((_bytes + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= _bytes) {
        const unsigned v = ((unsigned)_data[i] << 16) | ((unsigned)_data[i + 1] << 8) | (unsigned)_data[i + 2];
        out += kBase64Alphabet[(v >> 18) & 0x3F];
        out += kBase64Alphabet[(v >> 12) & 0x3F];
        out += kBase64Alphabet[(v >> 6) & 0x3F];
        out += kBase64Alphabet[v & 0x3F];
        i += 3;
    }

    const size_t rest = _bytes - i;
    if (rest == 1) {
        const unsigned v = (unsigned)_data[i] << 16;
        out += kBase64Alphabet[(v >> 18) & 0x3F];
        out += kBase64Alphabet[(v >> 12) & 0x3F];
        out += '=';
        out += '=';
    } else if (rest == 2) {
        const unsigned v = ((unsigned)_data[i] << 16) | ((unsigned)_data[i + 1] << 8);
        out += kBase64Alphabet[(v >> 18) & 0x3F];
        out += kBase64Alphabet[(v >> 12) & 0x3F];
        out += kBase64Alphabet[(v >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::string base64Decode(const std::string& _in) {
    std::string out;
    out.reserve((_in.size() / 4) * 3);

    int vals[4];
    size_t have = 0;
    for (size_t i = 0; i < _in.size(); ++i) {
        const char c = _in[i];
        int v;
        if (c >= 'A' && c <= 'Z')      v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+')             v = 62;
        else if (c == '/')             v = 63;
        else                           continue;

        vals[have++] = v;
        if (have == 4) {
            const unsigned n = ((unsigned)vals[0] << 18) | ((unsigned)vals[1] << 12) |
                               ((unsigned)vals[2] << 6) | (unsigned)vals[3];
            out += (char)((n >> 16) & 0xFF);
            out += (char)((n >> 8) & 0xFF);
            out += (char)(n & 0xFF);
            have = 0;
        }
    }
    if (have == 3) {
        const unsigned n = ((unsigned)vals[0] << 18) | ((unsigned)vals[1] << 12) | ((unsigned)vals[2] << 6);
        out += (char)((n >> 16) & 0xFF);
        out += (char)((n >> 8) & 0xFF);
    } else if (have == 2) {
        const unsigned n = ((unsigned)vals[0] << 18) | ((unsigned)vals[1] << 12);
        out += (char)((n >> 16) & 0xFF);
    }
    return out;
}

std::string numberToString(double _value) {
    if (_value >= 0.0 && _value == (double)(unsigned long long)_value) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)_value);
        return buf;
    }
    if (_value == (double)(long long)_value) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld", (long long)_value);
        return buf;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "%Lf", (long double)_value);

    char* dot = strchr(buf, '.');
    if (dot != NULL) {
        char* last = buf + strlen(buf) - 1;
        while (last > dot && *last == '0')
            *last-- = '\0';
        if (last == dot)
            *last = '\0';
    }
    return buf;
}

void escapeTo(std::string& _out, const std::string& _in) {
    char buf[8];
    for (size_t i = 0; i < _in.size(); ++i) {
        const unsigned char c = (unsigned char)_in[i];
        switch (c) {
            case '\"': _out += "\\\""; break;
            case '\\': _out += "\\\\"; break;
            case '/': _out += "\\/"; break;
            case '\b': _out += "\\b"; break;
            case '\f': _out += "\\f"; break;
            case '\n': _out += "\\n"; break;
            case '\r': _out += "\\r"; break;
            case '\t': _out += "\\t"; break;
            default:
                if (c < 0x20 || c > 0x7E) {
                    snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
                    _out += buf;
                } else {
                    _out += (char)c;
                }
        }
    }
}

JSONNode fromRapid(const rapidjson::Value& _v, const std::string& _name) {
    if (_v.IsObject()) {
        JSONNode n(JSON_NODE);
        n.set_name(_name);
        for (rapidjson::Value::ConstMemberIterator it = _v.MemberBegin(); it != _v.MemberEnd(); ++it)
            n.push_back(fromRapid(it->value, std::string(it->name.GetString(), it->name.GetStringLength())));
        return n;
    }
    if (_v.IsArray()) {
        JSONNode n(JSON_ARRAY);
        n.set_name(_name);
        for (rapidjson::Value::ConstValueIterator it = _v.Begin(); it != _v.End(); ++it)
            n.push_back(fromRapid(*it, std::string()));
        return n;
    }
    if (_v.IsBool())
        return JSONNode(_name, _v.GetBool());
    if (_v.IsNumber())
        return JSONNode(_name, _v.GetDouble());
    if (_v.IsString())
        return JSONNode(_name, std::string(_v.GetString(), _v.GetStringLength()));

    JSONNode n(JSON_NULL);
    n.set_name(_name);
    return n;
}

}  // namespace

JSONNode::JSONNode() : m_type(JSON_NODE), m_number(0.0), m_bool(false) {}

JSONNode::JSONNode(char _type) : m_type(_type), m_number(0.0), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, const std::string& _value)
    : m_type(JSON_STRING), m_name(_name), m_string(_value), m_number(0.0), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, const char* _value)
    : m_type(JSON_STRING), m_name(_name), m_string(_value != NULL ? _value : ""), m_number(0.0), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, int _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number((double)_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, unsigned int _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number((double)_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, long _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number((double)_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, unsigned long _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number((double)_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, long long _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number((double)_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, unsigned long long _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number((double)_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, float _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number((double)_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, double _value)
    : m_type(JSON_NUMBER), m_name(_name), m_number(_value), m_bool(false) {}

JSONNode::JSONNode(const std::string& _name, bool _value)
    : m_type(JSON_BOOL), m_name(_name), m_number(0.0), m_bool(_value) {}

JSONNode& JSONNode::at(size_t _index) {
    if (_index >= m_children.size())
        throw std::out_of_range("JSONNode::at: index out of range");
    return m_children[_index];
}

const JSONNode& JSONNode::at(size_t _index) const {
    if (_index >= m_children.size())
        throw std::out_of_range("JSONNode::at: index out of range");
    return m_children[_index];
}

JSONNode& JSONNode::at(const std::string& _name) {
    for (size_t i = 0; i < m_children.size(); ++i)
        if (m_children[i].m_name == _name)
            return m_children[i];
    throw std::out_of_range("JSONNode::at: no child named " + _name);
}

const JSONNode& JSONNode::at(const std::string& _name) const {
    for (size_t i = 0; i < m_children.size(); ++i)
        if (m_children[i].m_name == _name)
            return m_children[i];
    throw std::out_of_range("JSONNode::at: no child named " + _name);
}

JSONNode::iterator JSONNode::find(const std::string& _name) {
    for (iterator it = m_children.begin(); it != m_children.end(); ++it)
        if (it->m_name == _name)
            return it;
    return m_children.end();
}

JSONNode::const_iterator JSONNode::find(const std::string& _name) const {
    for (const_iterator it = m_children.begin(); it != m_children.end(); ++it)
        if (it->m_name == _name)
            return it;
    return m_children.end();
}

void JSONNode::swap(JSONNode& _other) {
    std::swap(m_type, _other.m_type);
    m_name.swap(_other.m_name);
    m_string.swap(_other.m_string);
    std::swap(m_number, _other.m_number);
    std::swap(m_bool, _other.m_bool);
    m_children.swap(_other.m_children);
}

JSONNode JSONNode::pop_back(size_t _index) {
    if (_index >= m_children.size())
        throw std::out_of_range("JSONNode::pop_back: index out of range");
    JSONNode n = m_children[_index];
    m_children.erase(m_children.begin() + (long)_index);
    return n;
}

JSONNode JSONNode::pop_back(const std::string& _name) {
    for (size_t i = 0; i < m_children.size(); ++i)
        if (m_children[i].m_name == _name)
            return pop_back(i);
    throw std::out_of_range("JSONNode::pop_back: no child named " + _name);
}

std::string JSONNode::as_binary() const {
    return (m_type == JSON_STRING) ? base64Decode(m_string) : std::string();
}

std::string JSONNode::as_string() const {
    switch (m_type) {
        case JSON_STRING: return m_string;
        case JSON_NUMBER: return numberToString(m_number);
        case JSON_BOOL: return m_bool ? "true" : "false";
        default: return std::string();
    }
}

int JSONNode::as_int() const {
    switch (m_type) {
        case JSON_NUMBER: return (int)m_number;
        case JSON_BOOL: return m_bool ? 1 : 0;
        case JSON_STRING: return atoi(m_string.c_str());
        default: return 0;
    }
}

float JSONNode::as_float() const {
    return (float)as_double();
}

double JSONNode::as_double() const {
    switch (m_type) {
        case JSON_NUMBER: return m_number;
        case JSON_BOOL: return m_bool ? 1.0 : 0.0;
        case JSON_STRING: return atof(m_string.c_str());
        default: return 0.0;
    }
}

bool JSONNode::as_bool() const {
    switch (m_type) {
        case JSON_BOOL: return m_bool;
        case JSON_NUMBER: return m_number != 0.0;
        case JSON_STRING: return !m_string.empty() && m_string != "false";
        default: return false;
    }
}

void JSONNode::set_binary(const unsigned char* _data, size_t _bytes) {
    m_type = JSON_STRING;
    m_string = (_data != NULL) ? base64Encode(_data, _bytes) : std::string();
}

void JSONNode::writeTo(std::string& _out, bool _named) const {
    if (_named && !m_name.empty()) {
        _out += '\"';
        escapeTo(_out, m_name);
        _out += "\":";
    }

    switch (m_type) {
        case JSON_NODE: {
            _out += '{';
            for (size_t i = 0; i < m_children.size(); ++i) {
                if (i)
                    _out += ',';
                m_children[i].writeTo(_out, true);
            }
            _out += '}';
            break;
        }
        case JSON_ARRAY: {
            _out += '[';
            for (size_t i = 0; i < m_children.size(); ++i) {
                if (i)
                    _out += ',';
                m_children[i].writeTo(_out, false);
            }
            _out += ']';
            break;
        }
        case JSON_STRING: {
            _out += '\"';
            escapeTo(_out, m_string);
            _out += '\"';
            break;
        }
        case JSON_NUMBER: _out += numberToString(m_number); break;
        case JSON_BOOL: _out += (m_bool ? "true" : "false"); break;
        default: _out += "null"; break;
    }
}

void JSONNode::writeFormattedTo(std::string& _out, size_t _depth, bool _named) const {
    if (_named && !m_name.empty()) {
        _out += '\"';
        escapeTo(_out, m_name);
        _out += "\" : ";
    }

    switch (m_type) {
        case JSON_NODE:
        case JSON_ARRAY: {
            const bool isArray = (m_type == JSON_ARRAY);
            if (m_children.empty()) {
                _out += (isArray ? "[]" : "{}");
                break;
            }
            _out += (isArray ? "[\n" : "{\n");
            for (size_t i = 0; i < m_children.size(); ++i) {
                _out.append(_depth + 1, '\t');
                m_children[i].writeFormattedTo(_out, _depth + 1, !isArray);
                if (i + 1 < m_children.size())
                    _out += ',';
                _out += '\n';
            }
            _out.append(_depth, '\t');
            _out += (isArray ? ']' : '}');
            break;
        }
        case JSON_STRING: {
            _out += '\"';
            escapeTo(_out, m_string);
            _out += '\"';
            break;
        }
        case JSON_NUMBER: _out += numberToString(m_number); break;
        case JSON_BOOL: _out += (m_bool ? "true" : "false"); break;
        default: _out += "null"; break;
    }
}

std::string JSONNode::write_formatted() const {
    std::string out;
    writeFormattedTo(out, 0, false);
    return out;
}

std::string JSONNode::write() const {
    std::string out;
    writeTo(out, false);
    return out;
}

namespace libjson {

JSONNode parse(const std::string& _text) {
    rapidjson::Document doc;
    doc.Parse(_text.c_str(), _text.size());
    if (doc.HasParseError())
        throw std::invalid_argument(std::string("libjson::parse: ") + rapidjson::GetParseError_En(doc.GetParseError()));
    return fromRapid(doc, std::string());
}

}  // namespace libjson

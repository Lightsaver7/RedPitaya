#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#define JSON_NULL '\0'
#define JSON_STRING '\1'
#define JSON_NUMBER '\2'
#define JSON_BOOL '\3'
#define JSON_ARRAY '\4'
#define JSON_NODE '\5'

class JSONNode {
   public:
    typedef std::vector<JSONNode>::iterator iterator;
    typedef std::vector<JSONNode>::const_iterator const_iterator;

    JSONNode();
    explicit JSONNode(char _type);
    JSONNode(const std::string& _name, const std::string& _value);
    JSONNode(const std::string& _name, const char* _value);
    JSONNode(const std::string& _name, int _value);
    JSONNode(const std::string& _name, unsigned int _value);
    JSONNode(const std::string& _name, long _value);
    JSONNode(const std::string& _name, unsigned long _value);
    JSONNode(const std::string& _name, long long _value);
    JSONNode(const std::string& _name, unsigned long long _value);
    JSONNode(const std::string& _name, float _value);
    JSONNode(const std::string& _name, double _value);
    JSONNode(const std::string& _name, bool _value);

    char type() const { return m_type; }
    std::string name() const { return m_name; }
    void set_name(const std::string& _name) { m_name = _name; }

    void push_back(const JSONNode& _child) { m_children.push_back(_child); }
    size_t size() const { return m_children.size(); }
    bool empty() const { return m_children.empty(); }

    iterator begin() { return m_children.begin(); }
    iterator end() { return m_children.end(); }
    const_iterator begin() const { return m_children.begin(); }
    const_iterator end() const { return m_children.end(); }

    JSONNode& at(size_t _index);
    const JSONNode& at(size_t _index) const;
    JSONNode& at(const std::string& _name);
    const JSONNode& at(const std::string& _name) const;

    JSONNode& operator[](size_t _index) { return m_children[_index]; }
    const JSONNode& operator[](size_t _index) const { return m_children[_index]; }
    JSONNode& operator[](const std::string& _name) { return at(_name); }
    const JSONNode& operator[](const std::string& _name) const { return at(_name); }

    iterator find(const std::string& _name);
    const_iterator find(const std::string& _name) const;

    void clear() { m_children.clear(); }
    void reserve(size_t _n) { m_children.reserve(_n); }
    void cast(char _type) { m_type = _type; }
    void nullify() { m_type = JSON_NULL; m_children.clear(); m_string.clear(); m_number = 0.0; m_bool = false; }
    void swap(JSONNode& _other);
    JSONNode duplicate() const { return *this; }
    JSONNode pop_back(size_t _index);
    JSONNode pop_back(const std::string& _name);

    std::string as_string() const;
    std::string as_binary() const;
    int as_int() const;
    float as_float() const;
    double as_double() const;
    bool as_bool() const;

    void set_binary(const unsigned char* _data, size_t _bytes);

    std::string write() const;
    std::string write_formatted() const;

   private:
    void writeTo(std::string& _out, bool _named) const;
    void writeFormattedTo(std::string& _out, size_t _depth, bool _named) const;

    char m_type;
    std::string m_name;
    std::string m_string;
    double m_number;
    bool m_bool;
    std::vector<JSONNode> m_children;
};

namespace libjson {
JSONNode parse(const std::string& _text);
}

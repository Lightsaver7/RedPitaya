#ifndef SETTINGS_LIB_ENUMS_H
#define SETTINGS_LIB_ENUMS_H

#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace enum_detail {

// One immortal string table per enum and per accessor.
//
// `trim` says whether the entry carries its own initialiser and has to be cut:
// the identifiers produced by STRINGIZE come through as "WAV = 0", so a name
// stops at the first separator, while the DISPLAY strings are free text that
// must be kept whole - cutting them turned "16 Bit" into "16".
struct EnumTable {
    std::vector<std::string> storage;
    std::vector<const char*> pointers;
};

inline auto MakeEnumTable(const char* const* raw, std::size_t count, bool trim) -> const EnumTable& {
    EnumTable* table = new EnumTable();
    table->storage.reserve(count);
    for (std::size_t index = 0; index < count; index++) {
        const char* entry = raw[index];
        table->storage.push_back(trim ? std::string(entry, std::strcspn(entry, " =\t\n\r")) : std::string(entry));
    }
    table->pointers.reserve(count);
    for (const std::string& text : table->storage) {
        table->pointers.push_back(text.c_str());
    }
    return *table;
}

}  // namespace enum_detail

#define PARENS ()

#define EXPAND(...) EXPAND4(EXPAND4(EXPAND4(EXPAND4(__VA_ARGS__))))
#define EXPAND4(...) EXPAND3(EXPAND3(EXPAND3(EXPAND3(__VA_ARGS__))))
#define EXPAND3(...) EXPAND2(EXPAND2(EXPAND2(EXPAND2(__VA_ARGS__))))
#define EXPAND2(...) EXPAND1(EXPAND1(EXPAND1(EXPAND1(__VA_ARGS__))))
#define EXPAND1(...) __VA_ARGS__

#define FOR_EACH(macro, ...) __VA_OPT__(EXPAND(FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define FOR_EACH_HELPER(macro, a1, a2, ...) macro(a1, a2) __VA_OPT__(, FOR_EACH_AGAIN PARENS(macro, __VA_ARGS__))
#define FOR_EACH_AGAIN() FOR_EACH_HELPER
// Magic ends

#define EXTRACT_SECOND(x, y) y

#define EXTRACT_FIRST(x, y) x

#define EXTRACT_EACH_SECOND(...) FOR_EACH(EXTRACT_SECOND, __VA_ARGS__)

#define EXTRACT_EACH_FIRST(...) FOR_EACH(EXTRACT_FIRST, __VA_ARGS__)

#define MAP(macro, ...) IDENTITY(APPLY(CHOOSE_MAP_START, COUNT(__VA_ARGS__))(macro, __VA_ARGS__))

#define CHOOSE_MAP_START(count) MAP##count

#define APPLY(macro, ...) IDENTITY(macro(__VA_ARGS__))

// Needed to expand __VA_ARGS__ "eagerly" on the MSVC preprocessor.
#define IDENTITY(x) x

#define MAP1(m, x) m(x)
#define MAP2(m, x, ...) m(x) IDENTITY(MAP1(m, __VA_ARGS__))
#define MAP3(m, x, ...) m(x) IDENTITY(MAP2(m, __VA_ARGS__))
#define MAP4(m, x, ...) m(x) IDENTITY(MAP3(m, __VA_ARGS__))
#define MAP5(m, x, ...) m(x) IDENTITY(MAP4(m, __VA_ARGS__))
#define MAP6(m, x, ...) m(x) IDENTITY(MAP5(m, __VA_ARGS__))
#define MAP7(m, x, ...) m(x) IDENTITY(MAP6(m, __VA_ARGS__))
#define MAP8(m, x, ...) m(x) IDENTITY(MAP7(m, __VA_ARGS__))

#define EVALUATE_COUNT(_1, _2, _3, _4, _5, _6, _7, _8, count, ...) count

#define COUNT(...) IDENTITY(EVALUATE_COUNT(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1))

struct ignore_assign {
    ignore_assign(int value) : _value(value) {}
    operator int() const { return _value; }

    const ignore_assign& operator=(int) { return *this; }

    int _value;
};

#define IGNORE_ASSIGN_SINGLE(expression) (ignore_assign) expression,
#define IGNORE_ASSIGN(...) IDENTITY(MAP(IGNORE_ASSIGN_SINGLE, __VA_ARGS__))

#define STRINGIZE_SINGLE(expression) #expression,
#define STRINGIZE(...) IDENTITY(MAP(STRINGIZE_SINGLE, __VA_ARGS__))

#define ENUM(EnumName, ...)                                                                                     \
    struct EnumName {                                                                                           \
        enum _enumerated { EXTRACT_EACH_FIRST(__VA_ARGS__) };                                                   \
                                                                                                                \
        _enumerated value;                                                                                      \
                                                                                                                \
        EnumName(_enumerated _value) : value(_value) {}                                                         \
        operator _enumerated() const {                                                                          \
            return value;                                                                                       \
        }                                                                                                       \
                                                                                                                \
        const char* name() const {                                                                              \
            for (size_t index = 0; index < count; ++index) {                                                    \
                if (values()[index] == value)                                                                   \
                    return names()[index];                                                                      \
            }                                                                                                   \
                                                                                                                \
            return NULL;                                                                                        \
        }                                                                                                       \
                                                                                                                \
        const char* to_string() const noexcept(false) {                                                         \
            for (size_t index = 0; index < count; ++index) {                                                    \
                if (values()[index] == value)                                                                   \
                    return strings()[index];                                                                    \
            }                                                                                                   \
                                                                                                                \
            throw std::invalid_argument("Not found");                                                           \
        }                                                                                                       \
        static _enumerated from_string(const std::string& name) noexcept(false) {                               \
            for (size_t index = 0; index < count; ++index) {                                                    \
                if (strcmp(names()[index], name.c_str()) == 0)                                                  \
                    return (_enumerated)values()[index];                                                        \
            }                                                                                                   \
            throw std::invalid_argument("Not found");                                                           \
        }                                                                                                       \
                                                                                                                \
        /* constexpr, not `static const`: a non-constexpr static data member is  \
         * not implicitly inline, so it needs an out-of-class definition that    \
         * this macro cannot provide. Reading the value worked, but binding it   \
         * to a reference - which is what passing it to any function taking      \
         * `const size_t&` does - failed to link. */                              \
        static constexpr size_t count = IDENTITY(COUNT(EXTRACT_EACH_FIRST(__VA_ARGS__)));                       \
                                                                                                                \
        static const int* values() {                                                                            \
            static const int _values[] = {IDENTITY(IGNORE_ASSIGN(EXTRACT_EACH_FIRST(__VA_ARGS__)))};            \
            return _values;                                                                                     \
        }                                                                                                       \
                                                                                                                \
        static const char* const* names() {                                                                     \
            static const char* const raw_names[] = {IDENTITY(STRINGIZE(EXTRACT_EACH_FIRST(__VA_ARGS__)))};      \
            /* trim: STRINGIZE keeps the initialiser, so "WAV = 0" -> "WAV". */                                 \
            static const enum_detail::EnumTable& table = enum_detail::MakeEnumTable(raw_names, count, true);    \
            return table.pointers.data();                                                                       \
        }                                                                                                       \
        static const char* const* strings() {                                                                   \
            static const char* const raw_strings[] = {IDENTITY(EXTRACT_EACH_SECOND(__VA_ARGS__))};              \
            /* No trim: these are the display strings and may contain spaces.                              \
             * Trimming them here is what turned Resolution's "8 Bit" into "8". */    \
            static const enum_detail::EnumTable& table = enum_detail::MakeEnumTable(raw_strings, count, false); \
            return table.pointers.data();                                                                       \
        }                                                                                                       \
                                                                                                                \
        /* No operator== of our own. It used to be a non-const member taking     \
         * `const EnumName&`, which C++20 made unusable: the reversed candidate  \
         * `b == a` is the same non-const function, so comparing two of these    \
         * was ambiguous, and comparing one against a bare enumerator was a hard \
         * error because the member tied with the built-in comparison. Adding    \
         * const only moves the tie, it does not break it.                       \
         *                                                                       \
         * `operator _enumerated()` above already makes every comparison work    \
         * through the built-in one - enum vs enum, enum vs enumerator, either   \
         * order, const or not - so the member is not just broken but redundant. \
         * Removing it changes no result, only which operator is selected. */                              \
        static bool IsValid(int _value) {                                                                       \
            for (size_t index = 0; index < count; ++index) {                                                    \
                if (values()[index] == _value)                                                                  \
                    return true;                                                                                \
            }                                                                                                   \
            return false;                                                                                       \
        }                                                                                                       \
    };

#endif
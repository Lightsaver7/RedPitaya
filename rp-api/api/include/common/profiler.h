#ifndef LOGGER_PROFILER_H
#define LOGGER_PROFILER_H

#include <string>

namespace profiler {

auto resetAll() -> void;
auto clearHistory(const std::string& name) -> void;
auto setTimePoint(const std::string& name) -> void;
auto saveTimePointnS(const std::string& name, const char* format, ...) -> void __attribute__((format(printf, 2, 3)));
auto saveTimePointuS(const std::string& name, const char* format, ...) -> void __attribute__((format(printf, 2, 3)));
auto saveTimePointmS(const std::string& name, const char* format, ...) -> void __attribute__((format(printf, 2, 3)));
auto print(const std::string name = "") -> void;
auto printnS(const std::string& name, const char* format, ...) -> void __attribute__((format(printf, 2, 3)));
auto printuS(const std::string& name, const char* format, ...) -> void __attribute__((format(printf, 2, 3)));
auto printmS(const std::string& name, const char* format, ...) -> void __attribute__((format(printf, 2, 3)));
}  // namespace profiler

#endif

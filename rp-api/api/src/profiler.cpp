#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <string>

#include "profiler.h"
#include "rp_log.h"

//#define PROFILE_ENABLED

namespace profiler {

#ifdef PROFILE_ENABLED
std::map<std::string, std::chrono::steady_clock::time_point> g_times;
std::map<std::string, std::list<std::string>> g_saved;
std::mutex g_mutex;
#endif

auto resetAll() -> void {
#ifdef PROFILE_ENABLED
    std::lock_guard<std::mutex> lock(g_mutex);
    g_times.clear();
    g_saved.clear();
#endif
}

auto clearHistory(__attribute__((unused)) const std::string& name) -> void {
#ifdef PROFILE_ENABLED
    std::lock_guard<std::mutex> lock(g_mutex);
    g_saved[name] = {};
#endif
}

auto setTimePoint(__attribute__((unused)) const std::string& name) -> void {
#ifdef PROFILE_ENABLED
    std::lock_guard<std::mutex> lock(g_mutex);
    g_times[name] = std::chrono::steady_clock::now();
#endif
}

namespace {
#ifdef PROFILE_ENABLED

template <typename Duration>
void saveTimePointImpl(const std::string& name, const char* unit, const char* format, va_list args) {
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_times.find(name);
    if (it == g_times.end()) {
        WARNING("Key not found %s", name.c_str())
        return;
    }

    auto current = std::chrono::steady_clock::now();

    char buffer[1024];
    int written = vsnprintf(buffer, sizeof(buffer), format, args);
    if (written < 0) {
        buffer[0] = '\0';
    } else if (static_cast<size_t>(written) >= sizeof(buffer)) {
        std::memcpy(buffer + sizeof(buffer) - 4, "...", 4);
    }

    auto diff = std::chrono::duration_cast<Duration>(current - it->second);
    std::string s = "[P] " + std::to_string(diff.count()) + " " + unit + ". Info: " + buffer;
    g_saved[name].push_back(std::move(s));
}

template <typename Duration>
void printImpl(const std::string& name, const char* unit, const char* format, va_list args) {
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_times.find(name);
    if (it == g_times.end()) {
        WARNING("Key not found %s", name.c_str())
        return;
    }

    auto current = std::chrono::steady_clock::now();

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    auto diff = std::chrono::duration_cast<Duration>(current - it->second);
    fprintf(stderr, "[P] %lld %s. Info: %s\n", static_cast<long long>(diff.count()), unit, buffer);
}

#endif
}  // namespace

auto saveTimePointmS(__attribute__((unused)) const std::string& name, __attribute__((unused)) const char* format, ...) -> void {
#ifdef PROFILE_ENABLED
    va_list args;
    va_start(args, format);
    saveTimePointImpl<std::chrono::milliseconds>(name, "mS", format, args);
    va_end(args);
#endif
}

auto saveTimePointuS(__attribute__((unused)) const std::string& name, __attribute__((unused)) const char* format, ...) -> void {
#ifdef PROFILE_ENABLED
    va_list args;
    va_start(args, format);
    saveTimePointImpl<std::chrono::microseconds>(name, "uS", format, args);
    va_end(args);
#endif
}

auto saveTimePointnS(__attribute__((unused)) const std::string& name, __attribute__((unused)) const char* format, ...) -> void {
#ifdef PROFILE_ENABLED
    va_list args;
    va_start(args, format);
    saveTimePointImpl<std::chrono::nanoseconds>(name, "nS", format, args);
    va_end(args);
#endif
}

auto print(__attribute__((unused)) const std::string name) -> void {
#ifdef PROFILE_ENABLED
    std::lock_guard<std::mutex> lock(g_mutex);
    if (name.empty()) {
        for (auto const& item : g_saved) {
            for (auto const& str : item.second) {
                fprintf(stderr, "%s\n", str.c_str());
            }
        }
        return;
    }
    auto it = g_saved.find(name);
    if (it != g_saved.end()) {
        for (auto const& str : it->second) {
            fprintf(stderr, "%s\n", str.c_str());
        }
    }
#endif
}

auto printnS(__attribute__((unused)) const std::string& name, __attribute__((unused)) const char* format, ...) -> void {
#ifdef PROFILE_ENABLED
    va_list args;
    va_start(args, format);
    printImpl<std::chrono::nanoseconds>(name, "nS", format, args);
    va_end(args);
#endif
}

auto printuS(__attribute__((unused)) const std::string& name, __attribute__((unused)) const char* format, ...) -> void {
#ifdef PROFILE_ENABLED
    va_list args;
    va_start(args, format);
    printImpl<std::chrono::microseconds>(name, "uS", format, args);
    va_end(args);
#endif
}

auto printmS(__attribute__((unused)) const std::string& name, __attribute__((unused)) const char* format, ...) -> void {
#ifdef PROFILE_ENABLED
    va_list args;
    va_start(args, format);
    printImpl<std::chrono::milliseconds>(name, "mS", format, args);
    va_end(args);
#endif
}

}  // namespace profiler
#include "common/clock.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <timeapi.h>
#endif

namespace common {

std::uint64_t NowUs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

namespace {

std::tm LocalTime(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

}  // namespace

std::string WallClockText() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::tm tm = LocalTime(system_clock::to_time_t(now));
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buffer;
}

std::string DateText() {
    return WallClockText().substr(0, 10);
}

HighResolutionTimerScope::HighResolutionTimerScope() {
#ifdef _WIN32
    active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
#endif
}

HighResolutionTimerScope::~HighResolutionTimerScope() {
#ifdef _WIN32
    if (active_) timeEndPeriod(1);
#endif
}

}  // namespace common

#include "common/logger.hpp"

#include "common/clock.hpp"

#include <cstdio>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace common {

Logger::Logger(const std::string& filePath, bool append) {
    if (!filePath.empty()) {
        wantFile_ = true;
        file_.open(filePath, append ? std::ios::app : std::ios::trunc);
    }
}

void Logger::Info(const std::string& message) {
    Write("INFO", message, true);
}

void Logger::Warn(const std::string& message) {
    Write("WARN", message, true);
}

void Logger::Detail(const std::string& message) {
    Write("INFO", message, consoleDetails_);
}

void Logger::Write(const char* level, const std::string& message, bool toConsole) {
    const std::string line = "[" + WallClockText() + "] " + level + " " + message;
    if (toConsole) {
        std::fprintf(stdout, "%s\n", line.c_str());
        std::fflush(stdout);
    }
    if (file_.is_open()) {
        file_ << line << '\n';
        file_.flush();
    }
}

void SetupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

}  // namespace common

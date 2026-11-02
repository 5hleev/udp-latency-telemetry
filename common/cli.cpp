#include "common/cli.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace common {

std::optional<std::int64_t> ParseInt(const std::string& text) {
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end != text.c_str() + text.size()) return std::nullopt;
    return static_cast<std::int64_t>(value);
}

std::optional<double> ParseDouble(const std::string& text) {
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (errno != 0 || end != text.c_str() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

ArgParser::ArgParser(int argc, char** argv, std::set<std::string> flags) {
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0) {
            errors_.push_back("неожиданный аргумент '" + key + "'");
            continue;
        }
        key = key.substr(2);
        if (flags.count(key) != 0) {
            values_[key] = "1";
        } else if (i + 1 < argc) {
            values_[key] = argv[++i];
        } else {
            errors_.push_back("для --" + key + " не указано значение");
        }
    }
}

bool ArgParser::Has(const std::string& key) const {
    return values_.count(key) != 0;
}

std::string ArgParser::GetString(const std::string& key, const std::string& fallback) {
    auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

std::int64_t ArgParser::GetInt(const std::string& key, std::int64_t fallback, std::int64_t min,
                               std::int64_t max) {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    auto value = ParseInt(it->second);
    if (!value || *value < min || *value > max) {
        errors_.push_back("--" + key + " должно быть целым числом в диапазоне [" + std::to_string(min) +
                          ", " + std::to_string(max) + "], получено '" + it->second + "'");
        return fallback;
    }
    return *value;
}

double ArgParser::GetDouble(const std::string& key, double fallback, double min, double max) {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    auto value = ParseDouble(it->second);
    if (!value || *value < min || *value > max) {
        errors_.push_back("--" + key + " должно быть числом в диапазоне [" + std::to_string(min) + ", " +
                          std::to_string(max) + "], получено '" + it->second + "'");
        return fallback;
    }
    return *value;
}

void ArgParser::RejectUnknown(const std::set<std::string>& known) {
    for (const auto& [key, value] : values_) {
        (void)value;
        if (known.count(key) == 0) errors_.push_back("неизвестный параметр --" + key);
    }
}

}  // namespace common

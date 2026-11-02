#include "telemetry/sample_log.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace telemetry {

namespace {

std::string FormatMs(const std::optional<double>& value) {
    if (!value) return {};
    char buffer[64];
    // %.3f не зависит от локали только для "C"-локали; программа её не меняет.
    std::snprintf(buffer, sizeof(buffer), "%.3f", *value);
    return buffer;
}

std::vector<std::string> Split(const std::string& line, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : line) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else if (c != '\r' && c != '\n') {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::optional<unsigned long> ParseUnsigned(const std::string& text, unsigned long max) {
    if (text.empty() || text.size() > 10) return std::nullopt;
    for (char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
    }
    const unsigned long value = std::strtoul(text.c_str(), nullptr, 10);
    if (value > max) return std::nullopt;
    return value;
}

// Пустая строка -> "значения нет" (ok = true), мусор -> ok = false.
std::optional<double> ParseOptionalDouble(const std::string& text, bool& ok) {
    ok = true;
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (errno != 0 || end != text.c_str() + text.size() || !std::isfinite(value) || value < 0) {
        ok = false;
        return std::nullopt;
    }
    return value;
}

}  // namespace

const char* CsvHeader() {
    return "experiment_id;sample;sequence;sent_at_ms;rtt_ms;srtt_ms;status";
}

std::string FormatCsvRow(const Sample& s) {
    std::string row = s.experimentId;
    row += ';';
    if (s.sample) row += std::to_string(*s.sample);
    row += ';';
    row += std::to_string(s.sequence);
    row += ';';
    row += FormatMs(s.sentAtMs);
    row += ';';
    row += FormatMs(s.rttMs);
    row += ';';
    row += FormatMs(s.srttMs);
    row += ';';
    row += ToCsvStatus(s.status);
    return row;
}

std::optional<Sample> ParseCsvRow(const std::string& line) {
    const auto parts = Split(line, ';');
    if (parts.size() != 7 || parts[0].empty()) return std::nullopt;

    Sample s;
    s.experimentId = parts[0];

    if (!parts[1].empty()) {
        auto sample = ParseUnsigned(parts[1], std::numeric_limits<std::uint32_t>::max());
        if (!sample) return std::nullopt;
        s.sample = static_cast<std::uint32_t>(*sample);
    }
    auto sequence = ParseUnsigned(parts[2], std::numeric_limits<std::uint16_t>::max());
    if (!sequence) return std::nullopt;
    s.sequence = static_cast<std::uint16_t>(*sequence);

    bool ok = true;
    s.sentAtMs = ParseOptionalDouble(parts[3], ok);
    if (!ok) return std::nullopt;
    s.rttMs = ParseOptionalDouble(parts[4], ok);
    if (!ok) return std::nullopt;
    s.srttMs = ParseOptionalDouble(parts[5], ok);
    if (!ok) return std::nullopt;

    auto status = ParseCsvStatus(parts[6]);
    if (!status) return std::nullopt;
    s.status = *status;

    // Согласованность полей со статусом.
    switch (s.status) {
        case ResponseStatus::Received:
            if (!s.sample || !s.sentAtMs || !s.rttMs || !s.srttMs) return std::nullopt;
            break;
        case ResponseStatus::Timeout:
            if (!s.sample || !s.sentAtMs || s.rttMs || s.srttMs) return std::nullopt;
            break;
        case ResponseStatus::Late:
        case ResponseStatus::Duplicate:
        case ResponseStatus::Unknown:
            break;
    }
    return s;
}

}  // namespace telemetry

// telemetry/sample_log.hpp - строка журнала измерений и её CSV-представление.
//
// Формат docs/latency_samples.csv (разделитель ';', десятичная точка):
//   experiment_id;sample;sequence;sent_at_ms;rtt_ms;srtt_ms;status
//   baseline;1;42;0.000;1.730;1.730;received
//   loss_5;8;57;3500.000;;;timeout
//   combined;12;61;5500.000;1203.114;;late_response
//
// Строки received/timeout - по одной на каждый отправленный PING (по ним считаются
// метрики). Строки late_response/duplicate_response/unknown_response - события:
// дополнительные ответы, которые не меняют RTT/SRTT, но фиксируются в журнале.
// Для late/duplicate rtt_ms - фактическое время прихода ответа (информативно).
#pragma once

#include "telemetry/telemetry.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace telemetry {

struct Sample {
    std::string experimentId;
    std::optional<std::uint32_t> sample;  // пусто для unknown_response
    std::uint16_t sequence = 0;
    std::optional<double> sentAtMs;       // от начала серии; пусто для unknown_response
    std::optional<double> rttMs;
    std::optional<double> srttMs;
    ResponseStatus status = ResponseStatus::Received;
};

const char* CsvHeader();
std::string FormatCsvRow(const Sample& sample);

// Разбор строки CSV с проверкой каждого поля. Некорректная строка -> nullopt.
std::optional<Sample> ParseCsvRow(const std::string& line);

}  // namespace telemetry

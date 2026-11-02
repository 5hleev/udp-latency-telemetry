#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "telemetry/sample_log.hpp"

using namespace telemetry;
using Catch::Approx;

TEST_CASE("CSV header matches the required format") {
    REQUIRE(std::string(CsvHeader()) == "experiment_id;sample;sequence;sent_at_ms;rtt_ms;srtt_ms;status");
}

TEST_CASE("Received and timeout rows are formatted like the example") {
    Sample received{"baseline", 1, 42, 0.0, 1.73, 1.73, ResponseStatus::Received};
    REQUIRE(FormatCsvRow(received) == "baseline;1;42;0.000;1.730;1.730;received");

    Sample timeout{"loss_5", 8, 57, 7000.0, std::nullopt, std::nullopt, ResponseStatus::Timeout};
    REQUIRE(FormatCsvRow(timeout) == "loss_5;8;57;7000.000;;;timeout");

    Sample unknown{"faults", std::nullopt, 9, std::nullopt, std::nullopt, std::nullopt, ResponseStatus::Unknown};
    REQUIRE(FormatCsvRow(unknown) == "faults;;9;;;;unknown_response");
}

TEST_CASE("CSV rows round trip") {
    const Sample rows[] = {
        {"baseline", 3, 65535, 1000.5, 0.25, 0.3, ResponseStatus::Received},
        {"combined", 12, 61, 5500.0, std::nullopt, std::nullopt, ResponseStatus::Timeout},
        {"combined", 12, 61, 5500.0, 1203.114, std::nullopt, ResponseStatus::Late},
        {"faults", 2, 7, 500.0, 10.0, std::nullopt, ResponseStatus::Duplicate},
        {"faults", std::nullopt, 32775, std::nullopt, std::nullopt, std::nullopt, ResponseStatus::Unknown},
    };
    for (const auto& row : rows) {
        const auto parsed = ParseCsvRow(FormatCsvRow(row));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->experimentId == row.experimentId);
        REQUIRE(parsed->sample == row.sample);
        REQUIRE(parsed->sequence == row.sequence);
        REQUIRE(parsed->status == row.status);
        REQUIRE(parsed->rttMs.has_value() == row.rttMs.has_value());
        if (row.rttMs) REQUIRE(*parsed->rttMs == Approx(*row.rttMs).margin(0.0005));
    }
}

TEST_CASE("CRLF line endings are accepted") {
    REQUIRE(ParseCsvRow("baseline;1;42;0.000;1.730;1.730;received\r").has_value());
}

TEST_CASE("Malformed CSV rows are rejected") {
    const char* bad[] = {
        "",
        "baseline;1;42;0.000;1.73;1.73",                  // мало полей
        "baseline;1;42;0.000;1.73;1.73;received;extra",   // много полей
        ";1;42;0.000;1.73;1.73;received",                 // нет experiment_id
        "baseline;x;42;0.000;1.73;1.73;received",         // sample не число
        "baseline;1;65536;0.000;1.73;1.73;received",      // sequence > uint16
        "baseline;1;-1;0.000;1.73;1.73;received",         // отрицательный sequence
        "baseline;1;42;0.000;abc;1.73;received",          // rtt не число
        "baseline;1;42;0.000;-5;1.73;received",           // отрицательный RTT
        "baseline;1;42;0.000;nan;1.73;received",          // NaN
        "baseline;1;42;0.000;1.73;1.73;lost",             // неизвестный статус
        "baseline;1;42;0.000;;;received",                 // received без RTT
        "loss_5;8;57;7000.000;12.0;;timeout",             // timeout с RTT
        "loss_5;;57;7000.000;;;timeout",                  // timeout без sample
    };
    for (const char* line : bad) {
        INFO(line);
        REQUIRE_FALSE(ParseCsvRow(line).has_value());
    }
}

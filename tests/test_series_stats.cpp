#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "telemetry/series_stats.hpp"
#include "telemetry/telemetry.hpp"

using namespace telemetry;
using Catch::Approx;

namespace {

Sample Received(const std::string& id, std::uint32_t sample, double rtt) {
    return Sample{id, sample, static_cast<std::uint16_t>(sample), sample * 500.0, rtt, rtt, ResponseStatus::Received};
}

Sample Timeout(const std::string& id, std::uint32_t sample) {
    return Sample{id, sample, static_cast<std::uint16_t>(sample), sample * 500.0, std::nullopt, std::nullopt,
                  ResponseStatus::Timeout};
}

Sample Event(const std::string& id, std::uint32_t sample, ResponseStatus status) {
    return Sample{id, sample, static_cast<std::uint16_t>(sample), sample * 500.0, 1500.0, std::nullopt, status};
}

}  // namespace

TEST_CASE("Median and percentile") {
    REQUIRE(Median({}) == 0.0);
    REQUIRE(Median({5}) == 5.0);
    REQUIRE(Median({3, 1, 2}) == 2.0);
    REQUIRE(Median({4, 1, 3, 2}) == Approx(2.5));
    REQUIRE(Percentile({1, 2, 3, 4, 5}, 100) == 5.0);
    REQUIRE(Percentile({1, 2, 3, 4, 5}, 0) == 1.0);
    REQUIRE(Percentile({10, 20}, 95) == Approx(19.5));
}

TEST_CASE("Jitter formula J = sum|RTT_i - RTT_{i-1}| / (n - 1)") {
    REQUIRE(MeanAbsoluteSuccessiveDifference({}) == 0.0);
    REQUIRE(MeanAbsoluteSuccessiveDifference({7}) == 0.0);
    REQUIRE(MeanAbsoluteSuccessiveDifference({10, 30, 20, 20, 50}) == Approx(15.0));
}

TEST_CASE("Series statistics count sent, received, timeouts and loss rate") {
    std::vector<Sample> rows;
    for (std::uint32_t i = 1; i <= 19; ++i) rows.push_back(Received("loss_5", i, 10.0 + i));
    rows.push_back(Timeout("loss_5", 20));
    rows.push_back(Event("loss_5", 20, ResponseStatus::Late));
    rows.push_back(Event("loss_5", 3, ResponseStatus::Duplicate));
    rows.push_back(Sample{"loss_5", std::nullopt, 999, std::nullopt, std::nullopt, std::nullopt, ResponseStatus::Unknown});
    rows.push_back(Received("other", 1, 1000.0));  // чужая серия не учитывается

    const auto stats = ComputeSeriesStats("loss_5", rows);
    REQUIRE(stats.sent == 20);
    REQUIRE(stats.received == 19);
    REQUIRE(stats.timeouts == 1);
    REQUIRE(stats.late == 1);
    REQUIRE(stats.duplicates == 1);
    REQUIRE(stats.unknown == 1);
    REQUIRE(stats.lossRatePct == Approx(5.0));
    REQUIRE(stats.minRttMs == Approx(11.0));
    REQUIRE(stats.maxRttMs == Approx(29.0));
    REQUIRE(stats.meanRttMs == Approx(20.0));
    REQUIRE(stats.medianRttMs == Approx(20.0));
    REQUIRE(stats.jitterMs == Approx(1.0));  // соседние RTT отличаются на 1 мс
}

TEST_CASE("Statistics use sample order, not journal order") {
    // Журнал пишется по мере событий: строка timeout #1 может оказаться после received #2.
    std::vector<Sample> rows{Received("s", 3, 30), Received("s", 1, 10), Received("s", 2, 20)};
    const auto stats = ComputeSeriesStats("s", rows);
    REQUIRE(stats.jitterMs == Approx(10.0));  // 10 -> 20 -> 30, а не 30 -> 10 -> 20 (15)
    REQUIRE(stats.finalSrttMs == Approx(0.875 * (0.875 * 10 + 0.125 * 20) + 0.125 * 30));
}

TEST_CASE("Offline SRTT and jitter match the online Telemetry estimates") {
    Telemetry telemetry("s");
    std::vector<Sample> rows;
    const double rtts[] = {12.0, 15.5, 11.25, 40.0, 13.0, 12.5};
    for (std::uint32_t i = 0; i < 6; ++i) {
        const std::uint64_t sentUs = i * 500'000ULL;
        telemetry.OnPingSent(static_cast<std::uint16_t>(i), sentUs);
        const auto result = telemetry.OnPong(static_cast<std::uint16_t>(i), sentUs + static_cast<std::uint64_t>(rtts[i] * 1000));
        rows.push_back(Received("s", i + 1, result.rttMs));
    }
    const auto stats = ComputeSeriesStats("s", rows);
    REQUIRE(stats.finalSrttMs == Approx(telemetry.SrttMs()));
    REQUIRE(stats.jitterMs == Approx(telemetry.MeanJitterMs()));
}

TEST_CASE("Series with only timeouts has 100% loss and zero RTT metrics") {
    std::vector<Sample> rows{Timeout("dead", 1), Timeout("dead", 2)};
    const auto stats = ComputeSeriesStats("dead", rows);
    REQUIRE(stats.sent == 2);
    REQUIRE(stats.received == 0);
    REQUIRE(stats.lossRatePct == Approx(100.0));
    REQUIRE(stats.meanRttMs == 0.0);
    REQUIRE(stats.jitterMs == 0.0);
}

TEST_CASE("Empty series yields zeros without division by zero") {
    const auto stats = ComputeSeriesStats("none", {});
    REQUIRE(stats.sent == 0);
    REQUIRE(stats.lossRatePct == 0.0);
}

TEST_CASE("ComputeAllSeries keeps the order of first appearance") {
    std::vector<Sample> rows{Received("b", 1, 1), Received("a", 1, 2), Received("b", 2, 3)};
    const auto all = ComputeAllSeries(rows);
    REQUIRE(all.size() == 2);
    REQUIRE(all[0].experimentId == "b");
    REQUIRE(all[0].sent == 2);
    REQUIRE(all[1].experimentId == "a");
}

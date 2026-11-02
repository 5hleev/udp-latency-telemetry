#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "telemetry/telemetry.hpp"

#include <stdexcept>

using namespace telemetry;
using Catch::Approx;

TEST_CASE("Successful PONG within timeout is Received") {
    Telemetry telemetry("t", 1'000'000);
    const auto sent = telemetry.OnPingSent(1, 0);
    REQUIRE(sent.record.sample == 1);
    REQUIRE(sent.record.attempts == 1);
    REQUIRE(sent.record.experimentId == "t");
    REQUIRE_FALSE(sent.evicted.has_value());

    const auto result = telemetry.OnPong(1, 50'000);  // 50 мс
    REQUIRE(result.status == ResponseStatus::Received);
    REQUIRE(result.rttMs == Approx(50.0));
    REQUIRE(telemetry.SrttMs() == Approx(50.0));  // первое RTT инициализирует SRTT
    REQUIRE(result.record->state == FlightState::Received);
    REQUIRE(telemetry.InFlightCount() == 0);
}

TEST_CASE("SRTT follows 0.875 * SRTT + 0.125 * RTT") {
    Telemetry telemetry("t");
    telemetry.OnPingSent(1, 0);
    telemetry.OnPong(1, 100'000);  // 100 мс
    telemetry.OnPingSent(2, 1'000'000);
    telemetry.OnPong(2, 1'200'000);  // 200 мс
    REQUIRE(telemetry.SrttMs() == Approx(0.875 * 100 + 0.125 * 200));  // 112.5
    telemetry.OnPingSent(3, 2'000'000);
    telemetry.OnPong(3, 2'020'000);  // 20 мс
    REQUIRE(telemetry.SrttMs() == Approx(0.875 * 112.5 + 0.125 * 20));
}

TEST_CASE("Mean jitter is average absolute difference of successive RTTs") {
    Telemetry telemetry("t");
    const double rtts[] = {10, 30, 20, 20, 50};  // |20|+|10|+|0|+|30| = 60 / 4 = 15
    for (int i = 0; i < 5; ++i) {
        const auto sentUs = static_cast<std::uint64_t>(i) * 1'000'000;
        telemetry.OnPingSent(static_cast<std::uint16_t>(i), sentUs);
        telemetry.OnPong(static_cast<std::uint16_t>(i), sentUs + static_cast<std::uint64_t>(rtts[i] * 1000));
    }
    REQUIRE(telemetry.SampleCount() == 5);
    REQUIRE(telemetry.MeanJitterMs() == Approx(15.0));
}

TEST_CASE("Jitter is zero with fewer than two samples") {
    Telemetry telemetry("t");
    REQUIRE(telemetry.MeanJitterMs() == 0.0);
    telemetry.OnPingSent(1, 0);
    telemetry.OnPong(1, 5'000);
    REQUIRE(telemetry.MeanJitterMs() == 0.0);
}

TEST_CASE("PONG for unknown sequence is Unknown") {
    Telemetry telemetry("t");
    const auto result = telemetry.OnPong(999, 100);
    REQUIRE(result.status == ResponseStatus::Unknown);
    REQUIRE_FALSE(result.record.has_value());
    REQUIRE(telemetry.GetCounters().unknown == 1);
}

TEST_CASE("PONG with foreign echoed timestamp is Unknown and does not complete the flight") {
    Telemetry telemetry("t");
    telemetry.OnPingSent(5, 1'000);
    REQUIRE(telemetry.OnPong(5, 2'000, 999).status == ResponseStatus::Unknown);
    REQUIRE(telemetry.InFlightCount() == 1);
    REQUIRE(telemetry.OnPong(5, 3'000, 1'000).status == ResponseStatus::Received);
}

TEST_CASE("Second PONG for the same sequence is Duplicate") {
    Telemetry telemetry("t", 1'000'000);
    telemetry.OnPingSent(5, 0);
    telemetry.OnPong(5, 10'000);
    const auto result = telemetry.OnPong(5, 20'000);
    REQUIRE(result.status == ResponseStatus::Duplicate);
    // Дубль не меняет ни SRTT, ни число засчитанных RTT.
    REQUIRE(telemetry.SrttMs() == Approx(10.0));
    REQUIRE(telemetry.SampleCount() == 1);
    REQUIRE(telemetry.GetCounters().duplicates == 1);
}

TEST_CASE("ExpireOverdue marks only flights older than timeout") {
    Telemetry telemetry("t", 1'000'000);
    telemetry.OnPingSent(1, 0);
    telemetry.OnPingSent(2, 500'000);
    REQUIRE(telemetry.ExpireOverdue(1'000'000).empty());  // ровно timeout - ещё не просрочено

    const auto expired = telemetry.ExpireOverdue(1'000'001);
    REQUIRE(expired.size() == 1);
    REQUIRE(expired[0].sequence == 1);
    REQUIRE(expired[0].state == FlightState::TimedOut);
    REQUIRE(telemetry.InFlightCount() == 1);
    REQUIRE(telemetry.ExpireOverdue(1'000'001).empty());  // повторно не считается

    REQUIRE(telemetry.ExpireOverdue(2'000'000).size() == 1);
    REQUIRE(telemetry.GetCounters().timeouts == 2);
}

TEST_CASE("PONG after ExpireOverdue is Late and does not affect RTT statistics") {
    Telemetry telemetry("t", 1'000'000);
    telemetry.OnPingSent(1, 0);
    telemetry.ExpireOverdue(1'500'000);
    const auto result = telemetry.OnPong(1, 1'600'000);
    REQUIRE(result.status == ResponseStatus::Late);
    REQUIRE(result.rttMs == Approx(1600.0));
    REQUIRE(telemetry.SampleCount() == 0);
    REQUIRE(telemetry.SrttMs() == 0.0);
    REQUIRE(telemetry.GetCounters().timeouts == 1);
    REQUIRE(telemetry.GetCounters().late == 1);
    // Ещё одна копия того же ответа - тоже late (запись уже TimedOut), а не received.
    REQUIRE(telemetry.OnPong(1, 1'700'000).status == ResponseStatus::Late);
}

TEST_CASE("PONG past timeout before ExpireOverdue is Late and counts as timeout") {
    Telemetry telemetry("t", 1'000'000);
    telemetry.OnPingSent(1, 0);
    const auto result = telemetry.OnPong(1, 1'000'001);
    REQUIRE(result.status == ResponseStatus::Late);
    REQUIRE(result.record->state == FlightState::TimedOut);
    REQUIRE(telemetry.GetCounters().timeouts == 1);
    REQUIRE(telemetry.ExpireOverdue(5'000'000).empty());  // уже не в полёте
}

TEST_CASE("RTT exactly equal to timeout is still Received") {
    Telemetry telemetry("t", 1'000'000);
    telemetry.OnPingSent(1, 0);
    REQUIRE(telemetry.OnPong(1, 1'000'000).status == ResponseStatus::Received);
}

TEST_CASE("NextDeadlineUs points to the oldest in-flight record") {
    Telemetry telemetry("t", 1'000'000);
    REQUIRE_FALSE(telemetry.NextDeadlineUs().has_value());
    telemetry.OnPingSent(1, 100);
    telemetry.OnPingSent(2, 500);
    REQUIRE(telemetry.NextDeadlineUs() == 100 + 1'000'000 + 1);
    telemetry.OnPong(1, 200);
    REQUIRE(telemetry.NextDeadlineUs() == 500 + 1'000'000 + 1);
}

TEST_CASE("inFlight is bounded: overwritten unfinished flight is reported as evicted timeout") {
    Telemetry telemetry("t", 10'000'000, 4);
    telemetry.OnPingSent(0, 0);
    telemetry.OnPingSent(1, 1);
    telemetry.OnPingSent(2, 2);
    telemetry.OnPong(2, 3);
    telemetry.OnPingSent(3, 4);
    REQUIRE(telemetry.InFlightCount() == 3);

    const auto evicting = telemetry.OnPingSent(4, 5);  // слот 4 % 4 = 0 занят seq 0
    REQUIRE(evicting.evicted.has_value());
    REQUIRE(evicting.evicted->sequence == 0);
    REQUIRE(evicting.evicted->state == FlightState::TimedOut);
    REQUIRE(telemetry.GetCounters().timeouts == 1);

    const auto reuse = telemetry.OnPingSent(6, 6);  // слот 2 занят завершённым seq 2 - не вытеснение
    REQUIRE_FALSE(reuse.evicted.has_value());

    // Ответ на вытесненный номер уже не найти - unknown_response.
    REQUIRE(telemetry.OnPong(0, 7).status == ResponseStatus::Unknown);
    REQUIRE(telemetry.Capacity() == 4);
}

TEST_CASE("Sequence wrap-around 65535 -> 0 is handled") {
    Telemetry telemetry("t");
    telemetry.OnPingSent(65535, 0);
    telemetry.OnPingSent(0, 1000);
    REQUIRE(telemetry.OnPong(0, 3000).status == ResponseStatus::Received);
    REQUIRE(telemetry.OnPong(65535, 4000).status == ResponseStatus::Received);
    REQUIRE(telemetry.SampleCount() == 2);
}

TEST_CASE("PONG with time earlier than send is rejected as Unknown") {
    Telemetry telemetry("t");
    telemetry.OnPingSent(1, 10'000);
    REQUIRE(telemetry.OnPong(1, 5'000).status == ResponseStatus::Unknown);
}

TEST_CASE("Invalid constructor arguments are rejected") {
    REQUIRE_THROWS_AS(Telemetry("t", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(Telemetry("t", 1000, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(Telemetry("t", 1000, 40000), std::invalid_argument);
}

TEST_CASE("CSV status names match the assignment") {
    REQUIRE(std::string(ToCsvStatus(ResponseStatus::Received)) == "received");
    REQUIRE(std::string(ToCsvStatus(ResponseStatus::Timeout)) == "timeout");
    REQUIRE(std::string(ToCsvStatus(ResponseStatus::Late)) == "late_response");
    REQUIRE(std::string(ToCsvStatus(ResponseStatus::Duplicate)) == "duplicate_response");
    REQUIRE(std::string(ToCsvStatus(ResponseStatus::Unknown)) == "unknown_response");
    REQUIRE(ParseCsvStatus("late_response") == ResponseStatus::Late);
    REQUIRE_FALSE(ParseCsvStatus("lost").has_value());
}

#include <catch2/catch_test_macros.hpp>
#include "telemetry/telemetry.hpp"

TEST_CASE("Successful PONG within timeout is Received") {
    Telemetry telemetry(1'000'000);
    telemetry.OnPingSent(1, 0);
    auto status = telemetry.OnPong(1, 50'000); // 50 мс
    // TODO(student): после реализации OnPong раскомментируйте проверки.
    // REQUIRE(status == ResponseStatus::Received);
    // REQUIRE(telemetry.SrttMs() > 0.0);
}

TEST_CASE("PONG for unknown sequence is Unknown") {
    Telemetry telemetry;
    auto status = telemetry.OnPong(999, 100);
    REQUIRE(status == ResponseStatus::Unknown);
}

TEST_CASE("Second PONG for the same sequence is Duplicate") {
    Telemetry telemetry(1'000'000);
    telemetry.OnPingSent(5, 0);
    telemetry.OnPong(5, 10'000);
    auto status = telemetry.OnPong(5, 20'000);
    // TODO(student): после реализации OnPong раскомментируйте проверку.
    // REQUIRE(status == ResponseStatus::Duplicate);
}

// TODO(student): добавить тест на ExpireOverdue (PING без ответа дольше timeoutUs_
// должен считаться Timeout) и тест на MeanJitterMs с несколькими измерениями.

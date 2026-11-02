#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>

// Статусы, которые телеметрия присваивает каждому измерению.
// received          — PONG получен в пределах тайм-аута, RTT засчитан;
// timeout           — ответ не пришёл за отведённое время;
// late_response      — PONG пришёл после того, как измерение уже помечено timeout;
// duplicate_response — повторный PONG на уже завершённое измерение;
// unknown_response   — PONG с sequenceNumber, которого нет в inFlight (например, после
//                       перезапуска клиента).
enum class ResponseStatus {
    Received,
    Timeout,
    Late,
    Duplicate,
    Unknown
};

struct FlightRecord {
    std::uint64_t sentAtUs = 0;
    bool completed = false;
};

class Telemetry {
public:
    explicit Telemetry(std::uint64_t timeoutUs = 1'000'000) : timeoutUs_(timeoutUs) {}

    // TODO(student): зарегистрировать отправленный PING в inFlight_.
    void OnPingSent(std::uint16_t sequenceNumber, std::uint64_t nowUs);

    // TODO(student): обработать полученный PONG:
    // 1. найти sequenceNumber в inFlight_, при отсутствии вернуть ResponseStatus::Unknown;
    // 2. если запись уже завершена — ResponseStatus::Duplicate;
    // 3. если прошло больше timeoutUs_ — пометить завершённой и вернуть ResponseStatus::Late;
    // 4. иначе рассчитать RTT (мс), обновить srttMs_ по формуле экспоненциального
    //    сглаживания (alpha = 0.125), обновить накопленный джиттер (среднее абсолютное
    //    отклонение между соседними RTT), пометить запись завершённой и вернуть
    //    ResponseStatus::Received.
    ResponseStatus OnPong(std::uint16_t sequenceNumber, std::uint64_t nowUs);

    // TODO(student): пройти по inFlight_, пометить завершёнными все записи старше
    // timeoutUs_, которые ещё не завершены, и вернуть их количество.
    std::size_t ExpireOverdue(std::uint64_t nowUs);

    double SrttMs() const { return srttMs_; }
    double MeanJitterMs() const;
    std::size_t SampleCount() const { return sampleCount_; }

private:
    std::uint64_t timeoutUs_;
    std::unordered_map<std::uint16_t, FlightRecord> inFlight_;

    double srttMs_ = 0.0;
    double previousRttMs_ = 0.0;
    double jitterAccumulatorMs_ = 0.0;
    std::size_t sampleCount_ = 0;
    bool hasPreviousRtt_ = false;
};

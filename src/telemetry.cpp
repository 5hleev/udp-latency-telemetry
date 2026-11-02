#include "telemetry/telemetry.hpp"
#include <cmath>

void Telemetry::OnPingSent(std::uint16_t sequenceNumber, std::uint64_t nowUs) {
    // TODO(student): создать/обновить запись в inFlight_.
}

ResponseStatus Telemetry::OnPong(std::uint16_t sequenceNumber, std::uint64_t nowUs) {
    // TODO(student): реализовать логику согласно комментарию в telemetry.hpp.
    return ResponseStatus::Unknown;
}

std::size_t Telemetry::ExpireOverdue(std::uint64_t nowUs) {
    // TODO(student): пометить просроченные записи и вернуть их число.
    return 0;
}

double Telemetry::MeanJitterMs() const {
    // TODO(student): вернуть среднее абсолютное отклонение RTT.
    // Подсказка: используйте jitterAccumulatorMs_ и sampleCount_.
    return 0.0;
}

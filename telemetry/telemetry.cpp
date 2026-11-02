#include "telemetry/telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace telemetry {

const char* ToCsvStatus(ResponseStatus status) {
    switch (status) {
        case ResponseStatus::Received:  return "received";
        case ResponseStatus::Timeout:   return "timeout";
        case ResponseStatus::Late:      return "late_response";
        case ResponseStatus::Duplicate: return "duplicate_response";
        case ResponseStatus::Unknown:   return "unknown_response";
    }
    return "?";
}

std::optional<ResponseStatus> ParseCsvStatus(const std::string& text) {
    for (auto status : {ResponseStatus::Received, ResponseStatus::Timeout, ResponseStatus::Late,
                        ResponseStatus::Duplicate, ResponseStatus::Unknown}) {
        if (text == ToCsvStatus(status)) return status;
    }
    return std::nullopt;
}

Telemetry::Telemetry(std::string experimentId, std::uint64_t timeoutUs, std::size_t capacity)
    : experimentId_(std::move(experimentId)), timeoutUs_(timeoutUs), slots_(capacity) {
    if (timeoutUs_ == 0) throw std::invalid_argument("timeout must be positive");
    // Ёмкость не больше половины пространства номеров: иначе ответ на старый номер
    // нельзя было бы отличить от ответа на новый.
    if (capacity == 0 || capacity > 32768) throw std::invalid_argument("capacity must be in [1, 32768]");
}

FlightRecord* Telemetry::Find(std::uint16_t sequence) {
    auto& slot = slots_[sequence % slots_.size()];
    if (slot && slot->sequence == sequence) return &*slot;
    return nullptr;
}

void Telemetry::MarkTimedOut(FlightRecord& record) {
    record.state = FlightState::TimedOut;
    ++counters_.timeouts;
}

SendResult Telemetry::OnPingSent(std::uint16_t sequence, std::uint64_t nowUs) {
    SendResult result;
    auto& slot = slots_[sequence % slots_.size()];
    if (slot && slot->state == FlightState::InFlight) {
        MarkTimedOut(*slot);
        result.evicted = *slot;
    }

    FlightRecord record;
    record.experimentId = experimentId_;
    record.sample = nextSample_++;
    record.sequence = sequence;
    record.sentAtUs = nowUs;
    record.state = FlightState::InFlight;
    record.attempts = 1;
    slot = record;
    ++counters_.sent;

    result.record = std::move(record);
    return result;
}

PongResult Telemetry::OnPong(std::uint16_t sequence, std::uint64_t nowUs,
                             std::optional<std::uint64_t> echoedSendUs) {
    PongResult result;
    result.srttMs = srttMs_;

    FlightRecord* record = Find(sequence);
    if (record == nullptr || (echoedSendUs && *echoedSendUs != record->sentAtUs) ||
        nowUs < record->sentAtUs) {
        ++counters_.unknown;
        result.status = ResponseStatus::Unknown;
        return result;
    }

    const std::uint64_t elapsedUs = nowUs - record->sentAtUs;
    result.rttMs = static_cast<double>(elapsedUs) / 1000.0;

    switch (record->state) {
        case FlightState::Received:
            ++counters_.duplicates;
            result.status = ResponseStatus::Duplicate;
            result.record = *record;
            return result;

        case FlightState::TimedOut:  // уже засчитан как потеря
            ++counters_.late;
            result.status = ResponseStatus::Late;
            result.record = *record;
            return result;

        case FlightState::InFlight:
            break;
    }

    if (elapsedUs > timeoutUs_) {
        // Тайм-аут истёк, но ExpireOverdue ещё не успел его отметить:
        // измерение засчитывается потерянным, а сам ответ - запоздалым.
        MarkTimedOut(*record);
        ++counters_.late;
        result.status = ResponseStatus::Late;
        result.record = *record;
        return result;
    }

    const double rttMs = result.rttMs;
    if (hasPreviousRtt_) jitterAccumulatorMs_ += std::fabs(rttMs - lastRttMs_);
    srttMs_ = hasPreviousRtt_ ? (1.0 - kSrttAlpha) * srttMs_ + kSrttAlpha * rttMs : rttMs;
    lastRttMs_ = rttMs;
    hasPreviousRtt_ = true;

    record->state = FlightState::Received;
    record->rttUs = elapsedUs;
    ++counters_.received;

    result.status = ResponseStatus::Received;
    result.record = *record;
    result.srttMs = srttMs_;
    return result;
}

std::vector<FlightRecord> Telemetry::ExpireOverdue(std::uint64_t nowUs) {
    std::vector<FlightRecord> expired;
    for (auto& slot : slots_) {
        if (slot && slot->state == FlightState::InFlight && nowUs > slot->sentAtUs &&
            nowUs - slot->sentAtUs > timeoutUs_) {
            MarkTimedOut(*slot);
            expired.push_back(*slot);
        }
    }
    std::sort(expired.begin(), expired.end(),
              [](const FlightRecord& a, const FlightRecord& b) { return a.sample < b.sample; });
    return expired;
}

std::optional<std::uint64_t> Telemetry::NextDeadlineUs() const {
    std::optional<std::uint64_t> deadline;
    for (const auto& slot : slots_) {
        if (slot && slot->state == FlightState::InFlight) {
            // +1: запись считается просроченной при elapsed > timeout (строго)
            const std::uint64_t candidate = slot->sentAtUs + timeoutUs_ + 1;
            if (!deadline || candidate < *deadline) deadline = candidate;
        }
    }
    return deadline;
}

std::size_t Telemetry::InFlightCount() const {
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const auto& slot) {
        return slot && slot->state == FlightState::InFlight;
    }));
}

double Telemetry::MeanJitterMs() const {
    // J = sum |RTT_i - RTT_{i-1}| / (n - 1), n - число засчитанных RTT
    return counters_.received > 1 ? jitterAccumulatorMs_ / static_cast<double>(counters_.received - 1)
                                  : 0.0;
}

}  // namespace telemetry

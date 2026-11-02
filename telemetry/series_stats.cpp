#include "telemetry/series_stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace telemetry {

double Median(std::vector<double> values) {
    return Percentile(std::move(values), 50.0);
}

double Percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    p = std::clamp(p, 0.0, 100.0);
    const double rank = p / 100.0 * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(rank));
    const auto upper = static_cast<std::size_t>(std::ceil(rank));
    const double fraction = rank - static_cast<double>(lower);
    return values[lower] + (values[upper] - values[lower]) * fraction;
}

double MeanAbsoluteSuccessiveDifference(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 1; i < values.size(); ++i) sum += std::fabs(values[i] - values[i - 1]);
    return sum / static_cast<double>(values.size() - 1);
}

SeriesStats ComputeSeriesStats(const std::string& experimentId, const std::vector<Sample>& rows) {
    SeriesStats stats;
    stats.experimentId = experimentId;

    std::vector<std::pair<std::uint32_t, double>> received;  // (sample, rtt)
    for (const auto& row : rows) {
        if (row.experimentId != experimentId) continue;
        switch (row.status) {
            case ResponseStatus::Received:
                ++stats.sent;
                ++stats.received;
                received.emplace_back(row.sample.value_or(0), row.rttMs.value_or(0.0));
                break;
            case ResponseStatus::Timeout:
                ++stats.sent;
                ++stats.timeouts;
                break;
            case ResponseStatus::Late:      ++stats.late; break;
            case ResponseStatus::Duplicate: ++stats.duplicates; break;
            case ResponseStatus::Unknown:   ++stats.unknown; break;
        }
    }

    if (stats.sent > 0) {
        stats.lossRatePct = 100.0 * static_cast<double>(stats.timeouts) / static_cast<double>(stats.sent);
    }
    if (received.empty()) return stats;

    std::stable_sort(received.begin(), received.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<double> rtts;
    rtts.reserve(received.size());
    for (const auto& item : received) rtts.push_back(item.second);

    stats.minRttMs = *std::min_element(rtts.begin(), rtts.end());
    stats.maxRttMs = *std::max_element(rtts.begin(), rtts.end());
    stats.meanRttMs = std::accumulate(rtts.begin(), rtts.end(), 0.0) / static_cast<double>(rtts.size());
    stats.medianRttMs = Median(rtts);
    stats.p95RttMs = Percentile(rtts, 95.0);

    double squares = 0.0;
    for (double rtt : rtts) squares += (rtt - stats.meanRttMs) * (rtt - stats.meanRttMs);
    stats.stddevRttMs = rtts.size() > 1 ? std::sqrt(squares / static_cast<double>(rtts.size() - 1)) : 0.0;

    // SRTT пересчитывается по той же формуле, что и в Telemetry, в порядке измерений.
    double srtt = rtts.front();
    for (std::size_t i = 1; i < rtts.size(); ++i) srtt = (1.0 - kSrttAlpha) * srtt + kSrttAlpha * rtts[i];
    stats.finalSrttMs = srtt;

    stats.jitterMs = MeanAbsoluteSuccessiveDifference(rtts);
    return stats;
}

std::vector<SeriesStats> ComputeAllSeries(const std::vector<Sample>& rows) {
    std::vector<std::string> order;
    for (const auto& row : rows) {
        if (std::find(order.begin(), order.end(), row.experimentId) == order.end()) {
            order.push_back(row.experimentId);
        }
    }
    std::vector<SeriesStats> result;
    result.reserve(order.size());
    for (const auto& id : order) result.push_back(ComputeSeriesStats(id, rows));
    return result;
}

}  // namespace telemetry

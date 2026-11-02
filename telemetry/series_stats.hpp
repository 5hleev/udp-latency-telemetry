// telemetry/series_stats.hpp - агрегированные метрики серии измерений.
//
// Чистые функции над строками журнала: используются клиентом для итоговой сводки
// и утилитой latency_report для расчёта таблицы отчёта по docs/latency_samples.csv.
#pragma once

#include "telemetry/sample_log.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace telemetry {

struct SeriesStats {
    std::string experimentId;
    std::size_t sent = 0;        // строк received + timeout
    std::size_t received = 0;
    std::size_t timeouts = 0;
    std::size_t late = 0;        // события late_response
    std::size_t duplicates = 0;  // события duplicate_response
    std::size_t unknown = 0;     // события unknown_response

    // RTT-метрики по полученным ответам, мс (0, если received == 0)
    double minRttMs = 0;
    double maxRttMs = 0;
    double meanRttMs = 0;
    double medianRttMs = 0;
    double p95RttMs = 0;
    double stddevRttMs = 0;
    double finalSrttMs = 0;  // SRTT после последнего полученного ответа
    double jitterMs = 0;     // J = sum|RTT_i - RTT_{i-1}| / (n - 1)
    double lossRatePct = 0;  // N_timeout / N_sent * 100
};

double Median(std::vector<double> values);
double Percentile(std::vector<double> values, double p);  // p в [0, 100], линейная интерполяция
double MeanAbsoluteSuccessiveDifference(const std::vector<double>& values);

// Метрики одной серии. Строки других серий игнорируются; RTT берутся в порядке
// номеров измерений (sample), а не в порядке появления строк в журнале.
SeriesStats ComputeSeriesStats(const std::string& experimentId, const std::vector<Sample>& rows);

// Метрики всех серий в порядке их первого появления в журнале.
std::vector<SeriesStats> ComputeAllSeries(const std::vector<Sample>& rows);

}  // namespace telemetry

// tools/latency_report.cpp - расчёт метрик по журналу docs/latency_samples.csv.
//
//   latency_report [--csv docs/latency_samples.csv] [--summary docs/latency_summary.csv]
//
// Печатает Markdown-таблицу для docs/Latency_Report.md и сохраняет сводку в CSV
// (её использует tools/plot_latency.py для столбчатой диаграммы). Строки журнала,
// не прошедшие проверку формата, не учитываются и перечисляются в выводе.

#include "common/cli.hpp"
#include "common/logger.hpp"
#include "telemetry/sample_log.hpp"
#include "telemetry/series_stats.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string F(double value, int digits = 3) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

}  // namespace

int main(int argc, char** argv) {
    common::SetupConsole();
    common::ArgParser args(argc, argv);
    const std::string csvPath = args.GetString("csv", "docs/latency_samples.csv");
    const std::string summaryPath = args.GetString("summary", "docs/latency_summary.csv");
    args.RejectUnknown({"csv", "summary"});
    if (!args.Ok()) {
        for (const auto& e : args.Errors()) std::fprintf(stderr, "Ошибка: %s\n", e.c_str());
        return 2;
    }

    std::ifstream in(csvPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Не удалось открыть %s\n", csvPath.c_str());
        return 1;
    }

    std::vector<telemetry::Sample> rows;
    std::string line;
    std::size_t lineNumber = 0;
    std::size_t rejected = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line == telemetry::CsvHeader()) continue;
        if (auto sample = telemetry::ParseCsvRow(line)) {
            rows.push_back(*sample);
        } else {
            ++rejected;
            std::fprintf(stderr, "строка %zu отклонена: %s\n", lineNumber, line.c_str());
        }
    }

    const auto all = telemetry::ComputeAllSeries(rows);

    std::printf("| Серия | Отправлено | Получено | Тайм-ауты | Min RTT, мс | Avg RTT, мс | Median, мс | "
                "P95, мс | Max RTT, мс | SRTT, мс | Jitter, мс | Loss Rate, %% | late / dup / unknown |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (const auto& s : all) {
        std::printf("| %s | %zu | %zu | %zu | %s | %s | %s | %s | %s | %s | %s | %s | %zu / %zu / %zu |\n",
                    s.experimentId.c_str(), s.sent, s.received, s.timeouts, F(s.minRttMs).c_str(),
                    F(s.meanRttMs).c_str(), F(s.medianRttMs).c_str(), F(s.p95RttMs).c_str(), F(s.maxRttMs).c_str(),
                    F(s.finalSrttMs).c_str(), F(s.jitterMs).c_str(), F(s.lossRatePct, 1).c_str(), s.late,
                    s.duplicates, s.unknown);
    }
    std::printf("\nСтрок журнала: %zu, отклонено: %zu\n", rows.size(), rejected);

    std::ofstream out(summaryPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "Не удалось записать %s\n", summaryPath.c_str());
        return 1;
    }
    out << "experiment_id;sent;received;timeouts;late;duplicates;unknown;min_rtt_ms;mean_rtt_ms;median_rtt_ms;"
           "p95_rtt_ms;max_rtt_ms;stddev_rtt_ms;srtt_ms;jitter_ms;loss_rate_pct\n";
    for (const auto& s : all) {
        out << s.experimentId << ';' << s.sent << ';' << s.received << ';' << s.timeouts << ';' << s.late << ';'
            << s.duplicates << ';' << s.unknown << ';' << F(s.minRttMs) << ';' << F(s.meanRttMs) << ';'
            << F(s.medianRttMs) << ';' << F(s.p95RttMs) << ';' << F(s.maxRttMs) << ';' << F(s.stddevRttMs) << ';'
            << F(s.finalSrttMs) << ';' << F(s.jitterMs) << ';' << F(s.lossRatePct, 2) << '\n';
    }
    return 0;
}

// common/clock.hpp - монотонные часы и точность системного таймера.
#pragma once

#include <cstdint>
#include <string>

namespace common {

// Микросекунды std::chrono::steady_clock. Начало отсчёта произвольно (обычно -
// загрузка ОС), часы не переводятся и не синхронизируются, поэтому пригодны
// только для измерения интервалов внутри одного процесса.
std::uint64_t NowUs();

// Стенные часы для журнала: "2026-11-02 21:15:03.042".
std::string WallClockText();
std::string DateText();  // "2026-11-02"

// На Windows по умолчанию квант планировщика ~15.6 мс: select()/Sleep() с
// тайм-аутом 1 мс фактически спят 15 мс. Объект на время жизни включает период
// 1 мс (timeBeginPeriod), чтобы интервалы отправки и задержки эмулятора были точными.
class HighResolutionTimerScope {
public:
    HighResolutionTimerScope();
    ~HighResolutionTimerScope();
    HighResolutionTimerScope(const HighResolutionTimerScope&) = delete;
    HighResolutionTimerScope& operator=(const HighResolutionTimerScope&) = delete;

private:
    bool active_ = false;
};

}  // namespace common

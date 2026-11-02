// netem/link_emulator.hpp - модель ухудшения канала для одного направления.
//
// Чистая логика без сокетов и часов: по входной датаграмме и текущему времени
// решает, когда (и сколько раз) её выпустить и в каком виде. Используется
// UDP-прокси netem_proxy, а в тестах проверяется детерминированно.
//
// Воспроизводимость: генератор std::mt19937_64 (последовательность чисел задана
// стандартом C++), равномерное распределение строится вручную из старших 53 бит -
// в отличие от std::uniform_real_distribution результат не зависит от компилятора.
// На каждый пакет всегда тратится одинаковое число случайных чисел, поэтому при
// одном seed судьба N-го пакета одинакова при любых параметрах ветвления.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace netem {

struct LinkProfile {
    double delayMs = 0;      // базовая задержка
    double jitterMs = 0;     // равномерная добавка в [-jitter, +jitter] (итог не < 0)
    double lossPct = 0;      // вероятность потери, %
    double dupPct = 0;       // вероятность дублирования, %
    double dupGapMs = 5;     // через сколько после оригинала выходит дубль
    double spikePct = 0;     // вероятность "пика" задержки, %
    double spikeMs = 1500;   // величина пика (больше тайм-аута -> late_response)
    double corruptPct = 0;   // вероятность порчи одного байта, %
    double reseqPct = 0;     // вероятность доп. копии с изменённым sequenceNumber, %

    bool IsTransparent() const;
    std::string ToString() const;
};

// "delay=100,jitter=50,loss=5" -> LinkProfile. Пустая строка - прозрачный канал.
// Неизвестный ключ или значение вне [0, 100] % / [0, 60000] мс -> nullopt + текст ошибки.
std::optional<LinkProfile> ParseProfile(const std::string& text, std::string* error = nullptr);

enum class EmissionKind { Original, Duplicate, Resequenced };

struct Emission {
    std::uint64_t releaseAtUs = 0;
    std::vector<std::uint8_t> data;
    EmissionKind kind = EmissionKind::Original;
    bool corrupted = false;
    bool spiked = false;
};

struct EmulatorStats {
    std::size_t packets = 0;
    std::size_t dropped = 0;
    std::size_t duplicated = 0;
    std::size_t spiked = 0;
    std::size_t corrupted = 0;
    std::size_t resequenced = 0;
};

class LinkEmulator {
public:
    LinkEmulator(LinkProfile profile, std::uint64_t seed);

    // Пустой результат - пакет потерян.
    std::vector<Emission> Process(const std::vector<std::uint8_t>& datagram, std::uint64_t nowUs);

    const LinkProfile& Profile() const { return profile_; }
    const EmulatorStats& Stats() const { return stats_; }

private:
    double Uniform01();  // [0, 1)

    LinkProfile profile_;
    std::mt19937_64 rng_;
    EmulatorStats stats_;
};

}  // namespace netem

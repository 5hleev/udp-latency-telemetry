#include "netem/link_emulator.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace netem {

namespace {

constexpr std::size_t kSequenceOffset = 1;  // байты 1-2 заголовка - sequenceNumber

std::uint64_t MsToUs(double ms) {
    return static_cast<std::uint64_t>(std::llround(std::max(0.0, ms) * 1000.0));
}

void SetError(std::string* out, const std::string& text) {
    if (out != nullptr) *out = text;
}

}  // namespace

bool LinkProfile::IsTransparent() const {
    return delayMs == 0 && jitterMs == 0 && lossPct == 0 && dupPct == 0 && spikePct == 0 && corruptPct == 0 &&
           reseqPct == 0;
}

std::string LinkProfile::ToString() const {
    if (IsTransparent()) return "без искажений";
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "delay=%.1f jitter=%.1f loss=%.1f%% dup=%.1f%% spike=%.1f%%(+%.0f мс) corrupt=%.1f%% reseq=%.1f%%",
                  delayMs, jitterMs, lossPct, dupPct, spikePct, spikeMs, corruptPct, reseqPct);
    return buffer;
}

std::optional<LinkProfile> ParseProfile(const std::string& text, std::string* error) {
    LinkProfile profile;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(start, end - start);
        start = end + 1;
        if (item.empty()) continue;

        const auto eq = item.find('=');
        if (eq == std::string::npos) {
            SetError(error, "ожидается ключ=значение: '" + item + "'");
            return std::nullopt;
        }
        const std::string key = item.substr(0, eq);
        const std::string valueText = item.substr(eq + 1);
        char* parsedEnd = nullptr;
        errno = 0;
        const double value = std::strtod(valueText.c_str(), &parsedEnd);
        if (valueText.empty() || errno != 0 || parsedEnd != valueText.c_str() + valueText.size() ||
            !std::isfinite(value) || value < 0) {
            SetError(error, "некорректное значение '" + valueText + "' для " + key);
            return std::nullopt;
        }

        struct Field { const char* name; double LinkProfile::*member; double max; };
        static const Field fields[] = {
            {"delay", &LinkProfile::delayMs, 60000},   {"jitter", &LinkProfile::jitterMs, 60000},
            {"loss", &LinkProfile::lossPct, 100},      {"dup", &LinkProfile::dupPct, 100},
            {"dupgap", &LinkProfile::dupGapMs, 60000}, {"spike", &LinkProfile::spikePct, 100},
            {"spikems", &LinkProfile::spikeMs, 60000}, {"corrupt", &LinkProfile::corruptPct, 100},
            {"reseq", &LinkProfile::reseqPct, 100},
        };
        const auto field = std::find_if(std::begin(fields), std::end(fields),
                                        [&](const Field& f) { return key == f.name; });
        if (field == std::end(fields)) {
            SetError(error, "неизвестный параметр '" + key + "'");
            return std::nullopt;
        }
        if (value > field->max) {
            SetError(error, key + " вне допустимого диапазона");
            return std::nullopt;
        }
        profile.*(field->member) = value;
    }
    return profile;
}

LinkEmulator::LinkEmulator(LinkProfile profile, std::uint64_t seed) : profile_(profile), rng_(seed) {}

double LinkEmulator::Uniform01() {
    return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0);  // 2^53
}

std::vector<Emission> LinkEmulator::Process(const std::vector<std::uint8_t>& datagram, std::uint64_t nowUs) {
    ++stats_.packets;

    // Все случайные величины вытягиваются заранее и всегда в одном порядке.
    const double rLoss = Uniform01();
    const double rJitter = Uniform01();
    const double rSpike = Uniform01();
    const double rCorrupt = Uniform01();
    const double rCorruptPos = Uniform01();
    const double rCorruptMask = Uniform01();
    const double rDup = Uniform01();
    const double rReseq = Uniform01();

    std::vector<Emission> out;
    if (rLoss * 100.0 < profile_.lossPct) {
        ++stats_.dropped;
        return out;
    }

    double delayMs = profile_.delayMs + (rJitter * 2.0 - 1.0) * profile_.jitterMs;
    delayMs = std::max(0.0, delayMs);
    const bool spiked = rSpike * 100.0 < profile_.spikePct;
    if (spiked) {
        delayMs += profile_.spikeMs;
        ++stats_.spiked;
    }

    Emission original;
    original.releaseAtUs = nowUs + MsToUs(delayMs);
    original.data = datagram;
    original.spiked = spiked;
    if (!datagram.empty() && rCorrupt * 100.0 < profile_.corruptPct) {
        const auto pos = std::min(datagram.size() - 1, static_cast<std::size_t>(rCorruptPos * datagram.size()));
        const auto mask = static_cast<std::uint8_t>(1 + static_cast<int>(rCorruptMask * 255.0));  // 1..255
        original.data[pos] ^= mask;
        original.corrupted = true;
        ++stats_.corrupted;
    }
    out.push_back(original);

    if (rDup * 100.0 < profile_.dupPct) {
        Emission duplicate = original;
        duplicate.kind = EmissionKind::Duplicate;
        duplicate.releaseAtUs = original.releaseAtUs + MsToUs(profile_.dupGapMs);
        out.push_back(std::move(duplicate));
        ++stats_.duplicated;
    }

    if (datagram.size() > kSequenceOffset + 1 && rReseq * 100.0 < profile_.reseqPct) {
        // Корректный по формату пакет, но с "чужим" номером: у получателя он должен
        // стать unknown_response, а не испортить статистику.
        Emission resequenced;
        resequenced.kind = EmissionKind::Resequenced;
        resequenced.releaseAtUs = original.releaseAtUs + 1000;
        resequenced.data = datagram;
        resequenced.data[kSequenceOffset] ^= 0x80;
        out.push_back(std::move(resequenced));
        ++stats_.resequenced;
    }
    return out;
}

}  // namespace netem

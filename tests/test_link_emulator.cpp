#include <catch2/catch_test_macros.hpp>

#include "netem/link_emulator.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace netem;

namespace {

const std::vector<std::uint8_t> kPacket{0x01, 0x12, 0x34, 0x00, 0x08, 0x00, 0x01, 1, 2, 3, 4, 5, 6, 7, 8};

}  // namespace

TEST_CASE("Profile parser accepts known keys") {
    std::string error;
    const auto profile = ParseProfile("delay=100,jitter=50,loss=5", &error);
    REQUIRE(profile.has_value());
    REQUIRE(profile->delayMs == 100);
    REQUIRE(profile->jitterMs == 50);
    REQUIRE(profile->lossPct == 5);
    REQUIRE(ParseProfile("")->IsTransparent());
}

TEST_CASE("Profile parser rejects bad input") {
    REQUIRE_FALSE(ParseProfile("delay").has_value());
    REQUIRE_FALSE(ParseProfile("delay=abc").has_value());
    REQUIRE_FALSE(ParseProfile("delay=-1").has_value());
    REQUIRE_FALSE(ParseProfile("loss=101").has_value());
    REQUIRE_FALSE(ParseProfile("latency=5").has_value());
}

TEST_CASE("Transparent link forwards immediately and unchanged") {
    LinkEmulator link(LinkProfile{}, 1);
    const auto out = link.Process(kPacket, 1000);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].releaseAtUs == 1000);
    REQUIRE(out[0].data == kPacket);
}

TEST_CASE("Fixed delay is applied exactly") {
    LinkProfile profile;
    profile.delayMs = 50;
    LinkEmulator link(profile, 1);
    REQUIRE(link.Process(kPacket, 0)[0].releaseAtUs == 50'000);
}

TEST_CASE("Jitter stays within [delay - jitter, delay + jitter]") {
    LinkProfile profile;
    profile.delayMs = 100;
    profile.jitterMs = 50;
    LinkEmulator link(profile, 7);
    std::uint64_t minUs = UINT64_MAX, maxUs = 0;
    for (int i = 0; i < 2000; ++i) {
        const auto release = link.Process(kPacket, 0)[0].releaseAtUs;
        minUs = std::min(minUs, release);
        maxUs = std::max(maxUs, release);
    }
    REQUIRE(minUs >= 50'000);
    REQUIRE(maxUs <= 150'000);
    REQUIRE(maxUs - minUs > 90'000);  // диапазон действительно используется
}

TEST_CASE("Loss rate is close to configured and reproducible by seed") {
    LinkProfile profile;
    profile.lossPct = 5;
    auto countDrops = [&](std::uint64_t seed) {
        LinkEmulator link(profile, seed);
        std::vector<bool> pattern;
        for (int i = 0; i < 10000; ++i) pattern.push_back(link.Process(kPacket, 0).empty());
        return pattern;
    };
    const auto a = countDrops(42);
    const auto b = countDrops(42);
    const auto c = countDrops(43);
    REQUIRE(a == b);  // одинаковый seed - одинаковые потери
    REQUIRE(a != c);
    const auto drops = std::count(a.begin(), a.end(), true);
    REQUIRE(drops > 400);
    REQUIRE(drops < 600);
}

TEST_CASE("Fate of N-th packet does not depend on other parameters") {
    LinkProfile lossOnly;
    lossOnly.lossPct = 10;
    LinkProfile lossAndDelay = lossOnly;
    lossAndDelay.delayMs = 100;
    lossAndDelay.jitterMs = 50;
    LinkEmulator a(lossOnly, 5), b(lossAndDelay, 5);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(a.Process(kPacket, 0).empty() == b.Process(kPacket, 0).empty());
    }
}

TEST_CASE("Duplicate, spike, corruption and resequencing") {
    LinkProfile profile;
    profile.dupPct = 100;
    profile.spikePct = 100;
    profile.spikeMs = 1500;
    profile.reseqPct = 100;
    profile.corruptPct = 100;
    LinkEmulator link(profile, 3);
    const auto out = link.Process(kPacket, 0);
    REQUIRE(out.size() == 3);
    REQUIRE(out[0].kind == EmissionKind::Original);
    REQUIRE(out[0].spiked);
    REQUIRE(out[0].releaseAtUs == 1'500'000);
    REQUIRE(out[0].corrupted);
    REQUIRE(out[0].data != kPacket);
    REQUIRE(out[1].kind == EmissionKind::Duplicate);
    REQUIRE(out[1].releaseAtUs > out[0].releaseAtUs);
    REQUIRE(out[2].kind == EmissionKind::Resequenced);
    REQUIRE(out[2].data[1] == (kPacket[1] ^ 0x80));  // старший байт sequenceNumber
    REQUIRE(link.Stats().duplicated == 1);
    REQUIRE(link.Stats().corrupted == 1);
}

TEST_CASE("Empty datagram is not corrupted out of bounds") {
    LinkProfile profile;
    profile.corruptPct = 100;
    profile.reseqPct = 100;
    LinkEmulator link(profile, 3);
    const auto out = link.Process({}, 0);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].data.empty());
}

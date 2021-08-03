#pragma once

#include <cstdint>
#include <chrono>


static std::uint64_t inline getEpochTimeMs() {
    return std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
}


static std::uint64_t inline getEpochTime() {
    return getEpochTimeMs() / 1000;
}

static int inline getTimeSecondsPart() {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm utc = *gmtime(&tt);
    return utc.tm_sec;
}

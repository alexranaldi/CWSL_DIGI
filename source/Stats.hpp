#pragma once

/*
Copyright 2021 Alexander Ranaldi
W2AXR
alexranaldi@gmail.com

This file is part of CWSL_DIGI.

CWSL_DIGI is free software : you can redistribute it and /or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CWSL_DIGI is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CWSL_DIGI. If not, see < https://www.gnu.org/licenses/>.
*/

#include <cstdint>
#include <memory>
#include <vector>
#include <thread>
#include <algorithm>
#include <chrono>

#include "SafeQueue.h"
#include "ScreenPrinter.hpp"
#include "TimeUtils.hpp"
#include "Decoder.hpp"

struct StatsItem {
    StatsItem(const std::size_t id, const std::uint64_t ms) : 
        instanceId(id),
        tsMs(ms)
    {}
    std::size_t instanceId;
    std::uint64_t tsMs;
};

using DecoderStats = std::vector< std::uint64_t >;

class Stats {
public:
    Stats(
        const std::uint32_t maxIntervalSec_In, 
        const std::uint32_t numDecoders_In):
        maxIntervalSec(maxIntervalSec_In),
        maxIntervalMs(static_cast<std::uint64_t>(maxIntervalSec_In) * 1000),
        numDecoders(numDecoders_In),
        decoderStatsVec(numDecoders_In) {

        for (auto& v : decoderStatsVec) {
            v.reserve(10000);
        }

    }

    virtual ~Stats() {}

    std::uint64_t getCounts(const size_t index, const std::uint32_t intervalSec) {
        const std::uint64_t intervalMs = static_cast<std::uint64_t>( intervalSec ) * 1000;
        const std::int64_t diff = getEpochTimeMs() - intervalMs;
        std::uint64_t out = 0;
        DecoderStats& stats = decoderStatsVec[index];
        for (std::size_t k = 0; k < stats.size(); ++k) {
            if (static_cast<std::int64_t>(stats[k]) > diff) {
                out++;
            }
        }
        return out;
    }

    void handleReport(const std::size_t instanceId, const std::uint64_t tsMs) {
        StatsItem item(instanceId, tsMs);
        reports.enqueue(item);
    }

    void process() {
        while (!reports.empty()) {
            StatsItem item = reports.dequeue();
            decoderStatsVec[item.instanceId].push_back(item.tsMs);
        }
        prune();
    }

    void prune() {
        const std::int64_t dt = getEpochTimeMs() - maxIntervalMs;
        for (DecoderStats& dec : decoderStatsVec) {
            std::sort(dec.begin(), dec.end());
            while (!dec.empty()) {
                const std::int64_t dv = static_cast<std::int64_t>(dec.front());
                if (dv <= dt) {
                    dec.erase(dec.begin());
                }
                else {
                    break;
                }
            }
        }
    }

private:
    std::uint32_t maxIntervalSec;
    std::uint64_t maxIntervalMs;
    std::uint32_t numDecoders;
    std::vector< DecoderStats > decoderStatsVec;

    SafeQueue<StatsItem> reports;
};



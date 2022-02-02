#pragma once

/*
Copyright 2022 Alexander Ranaldi
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
#include <string>
#include <memory>
#include <atomic>
#include <vector>

class Instance;

using FrequencyHz = std::uint32_t;

struct JT9Output
{
    std::string output = "";
    std::string mode = "";
    uint64_t epochTime = 0;
    FrequencyHz baseFreq = 0;
    std::size_t instanceId = 0;

    JT9Output(
        const std::string& rawOutputIn,
        const std::string& modeIn,
        const uint64_t epochTimeIn,
        const FrequencyHz baseFreqIn,
        const std::size_t instanceIdIn) :
        output(rawOutputIn),
        mode(modeIn),
        epochTime(epochTimeIn),
        baseFreq(baseFreqIn),
        instanceId(instanceIdIn)
    {}
};

enum class InstanceStatus : int {
    NOT_INITIALIZED,
    RUNNING,
    STOPPED,
    FINISHED,
};


class SyncPredicate {
public:
    SyncPredicate() {
        pred.store(false);
    }
    bool load() {
        return pred.load();
    }
    void store(bool a) {
        pred.store(a);
    }
private:
    std::atomic_bool pred;
};

class SyncPredicates {
public:
    SyncPredicates() {}
    std::shared_ptr<SyncPredicate> createPredicate(std::string mode) {
        std::shared_ptr<SyncPredicate> pred = std::make_shared<SyncPredicate>();
        if (mode == "FT8") {
            ft8Preds.push_back(pred);
        }
        else if (mode == "FT4") {
            ft4Preds.push_back(pred);
        }
        else if (mode == "JT65") {
            jt65Preds.push_back(pred);
        }
        else if (mode == "Q65-30") {
            q65_30Preds.push_back(pred);
        }
        else if (mode == "WSPR") {
            wsprPreds.push_back(pred);
        }
        return pred;
    }

    std::vector<std::shared_ptr<SyncPredicate>> ft8Preds;
    std::vector<std::shared_ptr<SyncPredicate>> ft4Preds;
    std::vector<std::shared_ptr<SyncPredicate>> jt65Preds;
    std::vector<std::shared_ptr<SyncPredicate>> q65_30Preds;
    std::vector<std::shared_ptr<SyncPredicate>> wsprPreds;

};

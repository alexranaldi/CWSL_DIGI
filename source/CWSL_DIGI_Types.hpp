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

#include <stdexcept>
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
        else if (mode == "JS8") {
            ft8Preds.push_back(pred);
        }
        else if (mode == "FT4") {
            ft4Preds.push_back(pred);
        }
        else if (mode == "JT65") {
            s60sPreds.push_back(pred);
        }
        else if (mode == "Q65-30") {
            q65_30Preds.push_back(pred);
        }
        else if (mode == "FST4-60") {
            s60sPreds.push_back(pred);
        }
        else if (mode == "WSPR") {
            s120sPreds.push_back(pred);
        }
        else if (mode == "FST4-120") {
            s120sPreds.push_back(pred);
        }
        else if (mode == "FST4-300") {
            s300sPreds.push_back(pred);
        }
        else if (mode == "FST4-900") {
            s900sPreds.push_back(pred);
        }
        else if (mode == "FST4-1800") {
            s1800sPreds.push_back(pred);
        }
        else if (mode == "FST4W-120") {
            s120sPreds.push_back(pred);
        }
        else if (mode == "FST4W-300") {
            s300sPreds.push_back(pred);
        }
        else if (mode == "FST4W-900") {
            s900sPreds.push_back(pred);
        }
        else if (mode == "FST4W-1800") {
            s1800sPreds.push_back(pred);
        }
        else {
            throw std::runtime_error("Unhandled mode: " + mode);
        }
        return pred;
    }

    std::vector<std::shared_ptr<SyncPredicate>> ft8Preds;
    std::vector<std::shared_ptr<SyncPredicate>> ft4Preds;
    std::vector<std::shared_ptr<SyncPredicate>> q65_30Preds;
    std::vector<std::shared_ptr<SyncPredicate>> s60sPreds;
    std::vector<std::shared_ptr<SyncPredicate>> s120sPreds;
    std::vector<std::shared_ptr<SyncPredicate>> s300sPreds;
    std::vector<std::shared_ptr<SyncPredicate>> s900sPreds;
    std::vector<std::shared_ptr<SyncPredicate>> s1800sPreds;

};

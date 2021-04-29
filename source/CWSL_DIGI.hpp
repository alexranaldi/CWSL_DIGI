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

#include <string>

#include <random>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

#include "ThreadWatcher.hpp"
#include "ScreenPrinter.hpp"

static const std::string PROGRAM_NAME = "CWSL_DIGI";
static const std::string PROGRAM_VERSION = "0.78";

constexpr float Q65_30_PERIOD = 30.0f;
constexpr float FT8_PERIOD = 15.0f;
constexpr float FT4_PERIOD = 7.5f;
constexpr float WSPR_PERIOD = 120.0f;

constexpr size_t Wave_SR = 12000;
constexpr size_t SSB_BW = 6000;
constexpr bool USB = 1;

const float AUDIO_CLIP_VAL = std::pow(2.0f, 15.0f) - 1.0f;

using FrequencyHz = std::uint32_t;

const int MAX_SLEEP_MS = 250;
const int MIN_SLEEP_MS = 25;

constexpr int MAIN_LOOP_SLEEP_MS = 250;


struct SyncPredicates {
    std::vector<std::shared_ptr<std::atomic_bool>> preds;
    SyncPredicates(size_t n) : preds(n) {
        for (size_t k = 0; k < n; ++k) {
            preds[k] = std::make_shared<std::atomic_bool>();
            preds[k]->store(false);
        }
    }
};

std::atomic_bool syncThreadTerminateFlag = false;

std::shared_ptr<ThreadWatcher> tw;


static inline float getRXPeriod(const std::string& mode) {
    if (mode == "FT8") {
        return FT8_PERIOD;
    }
    else if (mode == "FT4") {
        return FT4_PERIOD;
    }
    else if (mode == "WSPR") {
        return WSPR_PERIOD;
    }
    else if (mode == "Q65-30") {
        return Q65_30_PERIOD;
    }
    else {
        throw std::runtime_error("Unhandled mode: " + mode);
    }
}

void waitForTimeQ65_30(std::shared_ptr<ScreenPrinter> printer, SyncPredicates& preds, const uint64_t twKey) {
    tw->threadStarted(twKey);
    int goSec = -1;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        tw->report(twKey);
        try {
            std::time_t t = std::time(nullptr);
            tm* ts = std::gmtime(&t);
            if (go && ts->tm_sec == goSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = ts->tm_sec == 0 || ts->tm_sec == 30 ;
            if (go) {
                printer->print("Signalling beginning of Q65-30 interval...", LOG_LEVEL::DEBUG);
                goSec = ts->tm_sec;
                for (size_t k = 0; k < preds.preds.size(); ++k) {
                    preds.preds[k]->store(true);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            printer->print("waitForTimeQ65_30", e);
        }
    } // while
    printer->debug("Q65-30 Synchronization thread exiting");
    tw->threadFinished(twKey);
}

void waitForTimeFT8(std::shared_ptr<ScreenPrinter> printer, SyncPredicates& preds, const uint64_t twKey) {
    tw->threadStarted(twKey);
    int goSec = -1;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        tw->report(twKey);
        try {
            std::time_t t = std::time(nullptr);
            tm* ts = std::gmtime(&t);
            if (go && ts->tm_sec == goSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = ts->tm_sec == 0 || ts->tm_sec == 15 || ts->tm_sec == 30 || ts->tm_sec == 45;
            if (go) {
                printer->print("Signalling beginning of FT8 interval...", LOG_LEVEL::DEBUG);
                goSec = ts->tm_sec;
                for (size_t k = 0; k < preds.preds.size(); ++k) {
                    preds.preds[k]->store(true);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            printer->print("waitForTimeFT8", e);
        }
    } // while
    printer->debug("FT8 Synchronization thread exiting");
    tw->threadFinished(twKey);
}

void waitForTimeWSPR(std::shared_ptr<ScreenPrinter> printer, SyncPredicates& preds, const uint64_t twKey) {
    SYSTEMTIME time;
    bool go = false;
    tw->threadStarted(twKey);
    while (!syncThreadTerminateFlag) {
        tw->report(twKey);
        try {
            GetSystemTime(&time);
            const std::uint16_t min = static_cast<std::uint16_t>(time.wMinute);
            const bool minFlag = (min & 1) == 0;
            const bool secFlag = time.wSecond == 0;
            if (minFlag & secFlag & go) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = minFlag & secFlag;
            if (go) {
                printer->print("Signalling beginning of WSPR interval...", LOG_LEVEL::DEBUG);
                for (size_t k = 0; k < preds.preds.size(); ++k) {
                    preds.preds[k]->store(true);
                }
            } //if
            else if (minFlag || (!minFlag && time.wSecond <= 55)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        } // try
        catch (const std::exception& e) {
            printer->print("waitForTimeWSPR", e);
        } // catch
    } // while
    printer->debug("WSPR Synchronization thread exiting");
    tw->threadFinished(twKey);
}

void waitForTimeFT4(std::shared_ptr<ScreenPrinter> printer, SyncPredicates& preds, const uint64_t twKey) {
    SYSTEMTIME time;
    int goSec = -1;
    bool go = false;
    tw->threadStarted(twKey);
    while (!syncThreadTerminateFlag) {
        tw->report(twKey);
        try {
            GetSystemTime(&time);
            const WORD s = time.wSecond;
            if (go && s == goSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = false;
            switch (s) {
                case 0:
                case 15:
                case 30:
                case 45:
                    go = true;
                    break;
                case 7:
                case 22:
                case 37:
                case 52:
                    while (time.wMilliseconds < 300 && s == time.wSecond) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(400 - time.wMilliseconds));
                        GetSystemTime(&time);
                    }
                    go = true;
                    break;
                default:
                    break;
            } // switch
            if (go) {
                printer->print("Beginning FT4 interval...", LOG_LEVEL::DEBUG);
                goSec = time.wSecond;
                for (size_t k = 0; k < preds.preds.size(); ++k) {
                    preds.preds[k]->store(true);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            printer->print("waitForTimeFT4", e);
        }
    } // while
    printer->debug("FT4 Synchronization thread exiting");
    tw->threadFinished(twKey);
}

#include <boost/lexical_cast.hpp>
using boost::lexical_cast;

#include <boost/uuid/uuid.hpp>
using boost::uuids::uuid;

#include <boost/uuid/uuid_generators.hpp>
using boost::uuids::random_generator;

#include <boost/uuid/uuid_io.hpp>

static inline std::string make_uuid()
{
    return lexical_cast<std::string>((random_generator())());
}

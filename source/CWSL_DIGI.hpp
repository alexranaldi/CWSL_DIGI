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

#include <string>

#include <random>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

#include "CWSL_DIGI_Types.hpp"
#include "ScreenPrinter.hpp"

static const std::string PROGRAM_NAME = "CWSL_DIGI";
static const std::string PROGRAM_VERSION = "0.84";

constexpr float Q65_30_PERIOD = 30.0f;
constexpr float FT8_PERIOD = 15.0f;
constexpr float FT4_PERIOD = 7.5f;
constexpr float WSPR_PERIOD = 120.0f;
constexpr float JT65_PERIOD = 60.0f;

constexpr size_t Wave_SR = 12000;
constexpr size_t SSB_BW = 6000;
constexpr bool USB = 1;

const float AUDIO_CLIP_VAL = std::pow(2.0f, 15.0f) - 1.0f;

using FrequencyHz = std::uint32_t;

const int MAX_SLEEP_MS = 250;
const int MIN_SLEEP_MS = 25;

constexpr int MAIN_LOOP_SLEEP_MS = 1000;

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
    else if (mode == "JT65") {
        return JT65_PERIOD;
    }
    else {
        throw std::runtime_error("Unhandled mode: " + mode);
    }
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

static inline bool doesFileExist(const std::string& fname) {
    std::ifstream file(fname, std::ifstream::in);
    if (!file.is_open()) {
        return false;
    }
    file.close();
    return true;
}

static inline std::string createTemporaryDirectory()
{
    char pathBuf[MAX_PATH] = { 0 };
    GetTempPathA(MAX_PATH, pathBuf);
    std::string pathstr(pathBuf);
    std::string fullPath = pathBuf + std::string("\\") + make_uuid();
    CreateDirectoryA((LPCSTR)fullPath.c_str(),NULL);
    return fullPath;
}

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

static const std::string PROGRAM_NAME = "CWSL_DIGI";
static const std::string PROGRAM_VERSION = "0.68-beta";

constexpr float FT8_PERIOD = 15.0f;
constexpr float FT4_PERIOD = 6.048f;
constexpr float WSPR_PERIOD = 120.0f;

const size_t Wave_SR = 12000;
const size_t SSB_BW = 6000;

using FrequencyHz = std::uint32_t;

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
    else {
        throw std::runtime_error("Unhandled mode: " + mode);
    }
}

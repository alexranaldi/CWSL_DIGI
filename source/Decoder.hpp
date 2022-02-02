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

#include <memory>
#include <string>

#include "CWSL_DIGI_Types.hpp"

class Instance;

class Decoder {
private:
    FrequencyHz freq;
    FrequencyHz freqCalibrated;
    std::string mode;
    int smNum;
    double freqCalFactor;
    std::string reporterCallsign;
    std::unique_ptr<Instance> instance;
public:
    Decoder(
        FrequencyHz freq_In,
        FrequencyHz freqCalibrated_In,
        std::string mode_In,
        int smNum_In,
        double freqCalFactor_In,
        std::string reporterCallsign_In);

    void setInstance(std::unique_ptr<Instance> inst);

    InstanceStatus getStatus();

    void terminate();

    int getsmNum() const;

    FrequencyHz getFreq() const;

    FrequencyHz getFreqCalibrated() const;

    std::string getMode() const;

    std::string getReporterCallsign() const;

    std::unique_ptr<Instance>& getInstance();

};

using DecoderVec = std::vector<Decoder>;

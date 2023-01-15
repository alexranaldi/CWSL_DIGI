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

#include "Decoder.hpp"
#include "Instance.hpp"
#include "CWSL_DIGI.hpp"

    Decoder::Decoder(
        FrequencyHz freq_In,
        FrequencyHz freqCalibrated_In,
        std::string mode_In,
        int smNum_In,
        double freqCalFactor_In,
        std::string reporterCallsign_In) :
        freq(freq_In),
        freqCalibrated(freqCalibrated_In),
        mode(mode_In),
        smNum(smNum_In),
        freqCalFactor(freqCalFactor_In),
        reporterCallsign(reporterCallsign_In),
        instance(nullptr)
    {}

    void Decoder::setInstance(std::unique_ptr<Instance> inst) {
        instance = std::move(inst);
    }

    float Decoder::getTRPeriod() {
        return getRXPeriod(mode);
    }

    InstanceStatus Decoder::getStatus() {
        if (!instance) {
            return InstanceStatus::FINISHED;
        }
        return instance->getStatus();
    }

    void Decoder::terminate() {
        if (instance) {
            instance->terminate();
        }
    }

    int Decoder::getsmNum() const {
        return smNum;
    }

    std::unique_ptr<Instance>& Decoder::getInstance() {
        return instance;
    }

    FrequencyHz Decoder::getFreq() const {
        return freq;
    }

    FrequencyHz Decoder::getFreqCalibrated() const {
        return freqCalibrated;
    }

    std::string Decoder::getMode() const {
        return mode;
    }

    std::string Decoder::getReporterCallsign() const {
        return reporterCallsign;
    }
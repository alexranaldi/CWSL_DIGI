#pragma once

#ifndef INSTANCE_HPP
#define INSTANCE_HPP

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
#include <iostream>
#include <complex>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <memory>
#include <windows.h>

class Receiver;
class ScreenPrinter;
class DecoderPool;

#include "SSBD.hpp"

#include "CWSL_DIGI_Types.hpp"
#include "CWSL_Utils.hpp"
#include "ring_buffer.h"
#include "decode_audio_buffer.h"
#include "ring_buffer_spmc.h"

class Instance {
public:
    Instance(
        std::shared_ptr<Receiver> receiverIn,
        const size_t idIn,
        std::shared_ptr<SyncPredicate> predIn,
        const FrequencyHz ssbFreqIn,
        const FrequencyHz calibratedFreqIn,
        const std::string& modeIn,
        const std::string& callsignIn,
        const uint32_t waveSampleRateIn,
        const float audioScaleFactor_ftIn,
        const float audioScaleFactor_wsprIn,
        std::shared_ptr<ScreenPrinter> sp,
        std::shared_ptr<DecoderPool> dp
        );

    virtual ~Instance();

    std::string getMode() const;

    FrequencyHz getFrequency() const;

    std::string getCallsign() const;

    void terminate();

    bool init();

    void sampleManager();

    inline bool terminateNow();

    void prepareAudio(sample_buffer_t<float>& audioBuffer, const std::string& mode);

    std::string instanceLog() const;

    InstanceStatus getStatus();

    void setReceiver(std::shared_ptr<Receiver> r);



private:

    ring_buffer_spmc_t<std::complex<float>*>* iq_buffer;

    ring_buffer_t< sample_buffer_t<float> > af_buffer;
    FrequencyHz ssbFreq;
    FrequencyHz calibratedSSBFreq;

    std::string digitalMode;
    std::uint32_t decRatio;
    std::uint32_t SSB_SR;
    std::uint32_t waveSampleRate;
    std::unique_ptr<SSBD<float>> ssbd;
    std::shared_ptr<ScreenPrinter> screenPrinter;

    std::size_t iq_reader_id;

    std::atomic_bool terminateFlag;

    std::thread sampleManagerThread;


    float audioScaleFactor_ft;
    float audioScaleFactor_wspr;

    std::shared_ptr<DecoderPool> decoderPool;

    std::shared_ptr<SyncPredicate> pred;

    std::size_t id;

    std::string smname;

    std::shared_ptr<Receiver> receiver;

    std::string cwd;

    std::string callsign;

    std::atomic<InstanceStatus> status;

}; 

#endif

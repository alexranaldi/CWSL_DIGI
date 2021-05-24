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
#include <iostream>
#include <complex>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <memory>
#include <windows.h>

#include "CWSL_DIGI.hpp"
#include "CWSL_Utils.hpp"
#include "ring_buffer.h"
#include "decode_audio_buffer.h"
#include "WaveFile.hpp"
#include "StringUtils.hpp"
#include "HamUtils.hpp"
#include "ScreenPrinter.hpp"
#include "Receiver.hpp"

#include "DecoderPool.hpp"

// SSB demod
#include "../Utils/SSBD.hpp"


class Instance {
public:
    Instance(
        std::shared_ptr<Receiver> receiverIn,
        const size_t idIn,
        std::shared_ptr<std::atomic_bool> predIn,
        const FrequencyHz ssbFreqIn,
        const FrequencyHz calibratedFreqIn,
        const std::string& modeIn,
        const uint32_t waveSampleRateIn,
        const float audioScaleFactor_ftIn,
        const float audioScaleFactor_wsprIn,
        std::shared_ptr<ScreenPrinter> sp,
        std::shared_ptr<DecoderPool> dp
        ) : 
        audioScaleFactor_ft(audioScaleFactor_ftIn),
        audioScaleFactor_wspr(audioScaleFactor_wsprIn),
        pred(predIn),
        ssbd(nullptr),
        ssbFreq(ssbFreqIn),
        calibratedSSBFreq(calibratedFreqIn),
        digitalMode(modeIn),
        decRatio(0),
        id(idIn),
        iq_buffer(nullptr),
        iq_reader_id(0),
        waveSampleRate(waveSampleRateIn),
        screenPrinter(sp),
        receiver(receiverIn),
        decoderPool(dp),
        smname(""),
        terminateFlag(false),
        threadStarted(false),
        twKey(tw->addThread("instance " + std::to_string(id)))
        {}

    virtual ~Instance(){
        terminate();
    }

    std::string getMode() const {
        return digitalMode;
    }

    FrequencyHz getFrequency() const {
        return ssbFreq;
    }

    void terminate() {
        screenPrinter->debug(instanceLog() + "Instance terminating...");
        af_buffer.terminate();
        terminateFlag = true;
        if (threadStarted) {
            while (!sampleManagerThread.joinable()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            sampleManagerThread.join();
            threadStarted = false;
        }
    }

    bool init()
    {
        terminateFlag = false;

        SSB_SR = Wave_SR;
        decRatio = receiver->getSampleRate() / Wave_SR;

        cwd = createTemporaryDirectory();
        screenPrinter->debug(instanceLog() + " temporary directory: " + cwd);

        //
        // Prepare circular buffers
        // 

        try {
            if (af_buffer.initialized) {
                af_buffer.reset();
                for (size_t k = 0; k < af_buffer.size; ++k) {
                    af_buffer.recs[k].reset();
                }
            }
            else {
                const bool initStatus = af_buffer.initialize(2);
                if (!initStatus) {
                    screenPrinter->err(instanceLog() + "Failed to initialize memory for af buffer");
                    return false;
                }

                const size_t afBufNumSa = static_cast<size_t>( static_cast<double>(SSB_SR) * static_cast<double>(getRXPeriod(digitalMode)+2) );
                screenPrinter->debug(instanceLog() + "Initializing af ring buffer, length = " + std::to_string(afBufNumSa) + " samples");

                for (size_t k = 0; k < af_buffer.size; ++k) {
                    screenPrinter->debug(instanceLog() + "Initializing af ring buffer, buffer " + std::to_string(k) + " of " + std::to_string(af_buffer.size));
                    af_buffer.recs[k].init(afBufNumSa);
                    memset(af_buffer.recs[k].buf, 0.0f, af_buffer.recs[k].byte_size());
                    af_buffer.recs[k].resetIndices();
                }
            }
        }
        catch (const std::exception& e) {
            screenPrinter->err(instanceLog() + std::string("Caught exception allocating af buffers: ") + e.what());
            return false;
        }

        iq_buffer = receiver->getIQBuffer();
        iq_reader_id = iq_buffer->addReader();

        //
        //  Start Threads
        //

        tw->threadStarted(twKey);
        screenPrinter->print(instanceLog() + "Creating samplemanager thread...", LOG_LEVEL::DEBUG);
        sampleManagerThread = std::thread(&Instance::sampleManager, this);
        threadStarted = true;

        return true;
    }

    void sampleManager() {
        screenPrinter->debug(instanceLog() + "Sample manager thread started!");
        const size_t iq_len = receiver->getIQLength();

        const std::int32_t demodFreq = calibratedSSBFreq - receiver->getLO();

        screenPrinter->print("SSBD Freq: " + std::to_string(demodFreq) + " Hz", LOG_LEVEL::DEBUG);

        ssbd = std::make_unique< SSBD<float> >(receiver->getSampleRate(), SSB_BW, static_cast<float>(demodFreq), USB);

        SSB_SR = static_cast<std::uint32_t>(ssbd->GetOutRate());
        screenPrinter->debug(instanceLog() + "SSBD SR: " + std::to_string(SSB_SR));

        decRatio = receiver->getSampleRate() / Wave_SR;
        screenPrinter->debug(instanceLog() + "SSBD Dec Ratio: " + std::to_string(decRatio));
        const size_t ssbd_in_size = ssbd->GetInSize();

        while (!terminateFlag) {
            tw->report(twKey);

            try {
                if (pred->load()) {
                    pred->store(false);
                    auto idx_next = af_buffer.get_next_write_index();

                    screenPrinter->debug(instanceLog() + "Next af buffer write index: " + std::to_string(idx_next));

                    memset(af_buffer.recs[idx_next].buf, 0.0f, af_buffer.recs[af_buffer.write_index].byte_size());
                    af_buffer.recs[idx_next].reset();
                    af_buffer.recs[idx_next].startEpochTime = std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1);

                    af_buffer.inc_write_index();
                    
                    screenPrinter->debug(instanceLog() + "Getting an af buffer, read index=" + std::to_string(af_buffer.read_index));

                    auto& current_af_buf = af_buffer.pop_ref();
                    const auto startTime = current_af_buf.startEpochTime;
                    screenPrinter->debug(instanceLog() + "Got an af buffer, size = " + std::to_string(current_af_buf.size) + " samples. start time=" + std::to_string(startTime));
                    if (0 == startTime) {
                        screenPrinter->debug(instanceLog() + "Discarding af buffer, start time is zero");
                        continue; // begin demodulating next iteration
                    }

                    try {
                        prepareAudio(current_af_buf, digitalMode);
                    }
                    catch (const std::exception& e) {
                        screenPrinter->print(instanceLog() + "Caught exception in prepareAudio", e);
                        continue;
                    }
                    //screenPrinter->debug(instanceLog() + "Audio prepared");

                    std::vector<std::int16_t> audioBuf_i16(current_af_buf.size);
                    for (std::size_t k = 0; k < current_af_buf.size; ++k) {
                        audioBuf_i16[k] = static_cast<std::int16_t>(current_af_buf.buf[k] + 0.5f);
                    }
                    //screenPrinter->debug(instanceLog() + "Audio converted from float to i16");
                    
                    ItemToDecode toDecode(audioBuf_i16, digitalMode, startTime, ssbFreq, id, cwd);
                    decoderPool->push(toDecode);

                    screenPrinter->debug(instanceLog() + "Item pushed to decode queue");

                    if (terminateFlag) { break; }

                    ssbd = std::make_unique< SSBD<float> >(receiver->getSampleRate(), SSB_BW, static_cast<float>(demodFreq), USB);

                }

                try {
                    std::complex<float>* xc = iq_buffer->pop(iq_reader_id);
                    if (af_buffer.recs[af_buffer.write_index].write_index + iq_len > af_buffer.recs[af_buffer.write_index].size - 1) {
                        screenPrinter->err(instanceLog() + "af buffer full. size=" + std::to_string(af_buffer.recs[af_buffer.write_index].size) + " write_index=" + std::to_string(af_buffer.recs[af_buffer.write_index].write_index) + " new data size=" + std::to_string(iq_len));
                        continue;
                    }
                    float* dest = af_buffer.recs[af_buffer.write_index].buf + af_buffer.recs[af_buffer.write_index].write_index;
                    for (size_t n = 0; n < iq_len; n += ssbd_in_size) {
                        ssbd->Iterate(xc + n, dest + n / decRatio);
                    }
                    af_buffer.recs[af_buffer.write_index].write_index += (iq_len / decRatio);
                }
                catch (const std::exception& e) {
                    screenPrinter->print(instanceLog() + "Caught exception in demodulation", e);
                }

            }
            catch (const std::exception& e) {
                screenPrinter->print(instanceLog() + "Caught exception in sampleManager(): ", e);
            }
        }
        screenPrinter->debug(instanceLog() + "Sample manager thread terminating");
        tw->threadFinished(twKey);

    }

    void prepareAudio(sample_buffer_t<float>& audioBuffer, const std::string& mode) {
        float maxVal = std::numeric_limits<float>::lowest();
        for (size_t k = 0; k < audioBuffer.size; ++k) {
            if (audioBuffer.buf[k] > maxVal) {
                maxVal = audioBuffer.buf[k];
            }
        }
        // parens prevent macro expansion, fix windows.h max definition collision
        float minVal = (std::numeric_limits<float>::max)();

        for (size_t k = 0; k < audioBuffer.size; ++k) {
            if (audioBuffer.buf[k] < minVal) {
                minVal = audioBuffer.buf[k];
            }
        }

        if (std::fabs(minVal) > maxVal) {
            maxVal = std::fabs(minVal);
        }

        screenPrinter->debug(instanceLog() + "Maximum audio value: " + std::to_string(maxVal));

        float factor = AUDIO_CLIP_VAL / (maxVal + 1.0f);
        //screenPrinter->debug(instanceLog() + "AUDIO_CLIP_VAL="+std::to_string(AUDIO_CLIP_VAL));
        //screenPrinter->debug(instanceLog() + "factor=" + std::to_string(factor));

        if (mode == "WSPR") {
            //screenPrinter->debug(instanceLog() + "applying WSPR factor=" + std::to_string(audioScaleFactor_wspr));

            factor *= audioScaleFactor_wspr;
        }
        else {
            //screenPrinter->debug(instanceLog() + "applying FT factor=" + std::to_string(audioScaleFactor_ft));

            factor *= audioScaleFactor_ft;
        }
        //screenPrinter->debug(instanceLog() + "new factor=" + std::to_string(factor));

        for (size_t k = 0; k < audioBuffer.size; ++k) {
            audioBuffer.buf[k] *= factor;
        }

        screenPrinter->debug(instanceLog() + "Computed audio scale factor: " + std::to_string(factor));
        screenPrinter->debug(instanceLog() + "New maximum audio value: " + std::to_string(factor * maxVal));
    }

    std::string instanceLog() const {
        const std::string s = "Instance " + std::to_string(id) + " ";
        return s;
    }

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

    std::shared_ptr<std::atomic_bool> pred;

    std::size_t id;

    std::string smname;

    std::shared_ptr<Receiver> receiver;

    bool threadStarted;
    std::uint64_t twKey;

    std::string cwd;

};

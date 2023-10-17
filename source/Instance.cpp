
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

#include "Instance.hpp"
#include "Receiver.hpp"

#include "DecoderPool.hpp"
#include "ScreenPrinter.hpp"
//#include "WaveFile.hpp"

    Instance::Instance(
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
        std::shared_ptr<DecoderPool> dp,
        const float trperiodIn
        ) : 
        audioScaleFactor_ft(audioScaleFactor_ftIn),
        audioScaleFactor_wspr(audioScaleFactor_wsprIn),
        pred(predIn),
        ssbd(nullptr),
        ssbFreq(ssbFreqIn),
        calibratedSSBFreq(calibratedFreqIn),
        digitalMode(modeIn),
        callsign(callsignIn),
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
        status(InstanceStatus::NOT_INITIALIZED),
        trperiod(trperiodIn)
        {
            receiver->addInstance(this);
        }

    Instance::~Instance(){
        terminate();
    }

    void Instance::setReceiver(std::shared_ptr<Receiver> r) {
        receiver = r;
        r->addInstance(this);
    }

    std::string Instance::getMode() const {
        return digitalMode;
    }

    FrequencyHz Instance::getFrequency() const {
        return ssbFreq;
    }

    std::string Instance::getCallsign() const {
        return callsign;
    }

    void Instance::terminate() {
        screenPrinter->debug(instanceLog() + "Instance terminating...");
        af_buffer.terminate();

        terminateFlag = true;

        screenPrinter->debug(instanceLog() + "Instance joining threads...");

        if (sampleManagerThread.joinable()) {
            screenPrinter->debug(instanceLog() + "calling join()");

            sampleManagerThread.join();
        }

        status = InstanceStatus::FINISHED;

        receiver = nullptr;
        screenPrinter->debug(instanceLog() + "Instance finished");

    }

    InstanceStatus Instance::getStatus() {
        return status.load();
    }

    /*
    ReceiverStatus Instance::getReceiverStatus() {
        return receiver->getStatus();
    }
    */

    bool Instance::init()
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

                const size_t afBufNumSa = static_cast<size_t>( static_cast<double>(SSB_SR) * static_cast<double>(getRXPeriod(digitalMode)+5) );
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

        screenPrinter->print(instanceLog() + "Creating samplemanager thread...", LOG_LEVEL::DEBUG);
        sampleManagerThread = std::thread(&Instance::sampleManager, this);

        return true;
    }

    void Instance::sampleManager() {
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

        status = InstanceStatus::RUNNING;

        while (!terminateNow()) {

            try {
           //     screenPrinter->debug(instanceLog() + "a");

                if (pred->load()) {
            //        screenPrinter->debug(instanceLog() + "b");

                    pred->store(false);
            //        screenPrinter->debug(instanceLog() + "c");

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
                    
                    ItemToDecode toDecode(audioBuf_i16, digitalMode, startTime, ssbFreq, static_cast<int>(id), cwd, trperiod);
                    decoderPool->push(toDecode);

                    screenPrinter->debug(instanceLog() + "Item pushed to decode queue");

                    if (terminateNow()) { break; }

                    ssbd = std::make_unique< SSBD<float> >(receiver->getSampleRate(), SSB_BW, static_cast<float>(demodFreq), USB);

                }
         //       screenPrinter->debug(instanceLog() + "d");

                if (terminateNow()) { break; }
          //      screenPrinter->debug(instanceLog() + "e");

                try {
                    if (!iq_buffer->wait_for_data(iq_reader_id)) {
                        break;
                    }
           //         screenPrinter->debug(instanceLog() + "f");

                    std::complex<float>* xc = iq_buffer->pop_no_wait(iq_reader_id);
           //         screenPrinter->debug(instanceLog() + "g");

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
            } // try
        } // while
        screenPrinter->debug(instanceLog() + "Sample manager thread terminating");
        status = InstanceStatus::STOPPED;
    }

    inline bool Instance::terminateNow() {
        return terminateFlag.load();
    }

    void Instance::prepareAudio(sample_buffer_t<float>& audioBuffer, const std::string& mode) {
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

    std::string Instance::instanceLog() const {
        const std::string s = "Instance " + std::to_string(id) + " ";
        return s;
    }

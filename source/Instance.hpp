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

#include "DecoderPool.hpp"

// SSB demod
#include "../Utils/SSBD.hpp"

#include <boost/lexical_cast.hpp>
using boost::lexical_cast;

#include <boost/uuid/uuid.hpp>
using boost::uuids::uuid;

#include <boost/uuid/uuid_generators.hpp>
using boost::uuids::random_generator;

#include <boost/uuid/uuid_io.hpp>

const float clipVal = std::pow(2.0f, 15.0f) - 1.0f;

static inline std::string make_uuid()
{
    return lexical_cast<std::string>((random_generator())());
}

class Instance {
public:
    Instance() : 
        audioScaleFactor_ft(0.0),
        audioScaleFactor_wspr(0.0),
        decodedepth(3),
        decRatio(0),
        digitalMode("FT8"),
        freqCal(1.0),
        iq_len(0),
        nMem(0),
        numjt9threads(3),
        radioSR(0),
        screenPrinter(nullptr),
        SHDR(nullptr),
        SM(),
        ssbd(nullptr),
        ssbBw(0),
        SSB_SR(0),
        ssbFreq(0),
        terminateFlag(false),
        waveSampleRate(0)
    {}
    virtual ~Instance(){}
    Instance(const Instance&) = delete;

    Instance(Instance&& o) noexcept 
    {}

    bool init(
        const FrequencyHz ssbFreqIn,
        const int SMNumber, 
        const std::string& mode, 
        const uint32_t bandwidth,
        const double freqCalIn,
        const uint32_t highestDecodeFreqIn,
        const std::string& wavPathIn,
        const uint32_t waveSampleRateIn,
        const int decodedepthIn,
        const int numjt9threadsIn,
        const float audioScaleFactor_ftIn,
        const float audioScaleFactor_wsprIn,
        std::shared_ptr<ScreenPrinter> sp,
        std::shared_ptr<DecoderPool> dp)
    {
        audioScaleFactor_ft = audioScaleFactor_ftIn;
        audioScaleFactor_wspr = audioScaleFactor_wsprIn;
        ssbFreq = ssbFreqIn;
        digitalMode = mode;
        ssbBw = bandwidth;
        freqCal = freqCalIn;
        highestDecodeFreq = highestDecodeFreqIn;
        wavPath = wavPathIn;
        waveSampleRate = waveSampleRateIn;
        decodedepth = decodedepthIn;
        numjt9threads = numjt9threadsIn;
        screenPrinter = sp;
        decoderPool = dp;

        nMem = findBand(static_cast<std::int64_t>(ssbFreq), SMNumber);
        if (-1 == nMem) {
            std::cerr << "Unable to open CWSL shared memory at the specified frequency. Bad frequency or sharedmem specified." << std::endl;
            return false;
        }

        // try to open shared memory
        const std::string name = createSharedMemName(nMem, SMNumber);
        if (!SM.Open(name.c_str())) {
            fprintf(stderr, "Can't open shared memory for %d receiver\n", nMem);
            return EXIT_FAILURE;
        }

        // get info about channel
        SHDR = SM.GetHeader();
        const uint32_t radioSR = static_cast<uint32_t>(SHDR->SampleRate);
        iq_len = SHDR->BlockInSamples;
        screenPrinter->print("Receiver: " + std::to_string(nMem)
            + "\tSample Rate: " + std::to_string(radioSR)
            + "\tBlock In Samples: " + std::to_string(iq_len)
            + "\tLO: " + std::to_string(SHDR->L0),
            LOG_LEVEL::INFO);

        const bool USB = true;

        screenPrinter->print("SSB Bandwidth: " + std::to_string(ssbBw) + " Hz", LOG_LEVEL::DEBUG);
        const FrequencyHz adjustedSSBFreq = static_cast<FrequencyHz>(std::round(static_cast<double>(ssbFreq) / freqCal));
        screenPrinter->print("Adjusted SSB freq: " + std::to_string(adjustedSSBFreq) + " Hz", LOG_LEVEL::DEBUG);

        // F is always Fc-LO
        const int32_t F = adjustedSSBFreq - static_cast<FrequencyHz>(SHDR->L0);
        screenPrinter->print("SSBD Freq: " + std::to_string(F) + " Hz", LOG_LEVEL::DEBUG);
        ssbd = std::make_unique< SSBD<float> >(radioSR, ssbBw, static_cast<float>(F), static_cast<bool>(USB));
        SSB_SR = static_cast<std::uint32_t>(ssbd->GetOutRate());
        screenPrinter->debug("SSBD SR: " + std::to_string(SSB_SR));
        decRatio = radioSR / SSB_SR;
        screenPrinter->print("SSBD Dec Ratio: " + std::to_string(decRatio) + " Hz", LOG_LEVEL::DEBUG);

        //
        // Prepare circular buffers
        // 

        screenPrinter->debug("Preparing IQ buffer");
        bool initStatus = iq_buffer.initialize(256);
        if (!initStatus) {
            std::cerr << "Failed to initialize memory" << std::endl;
            return EXIT_FAILURE;
        }
        for (size_t k = 0; k < iq_buffer.size; ++k) {
            iq_buffer.recs[k] = reinterpret_cast<std::complex<float>*>(malloc(sizeof(std::complex<float>) * iq_len));
        }
        screenPrinter->debug("Initializing audio ring buffer");
        initStatus = decode_audio_ring_buffer.initialize(2);
        if (!initStatus) {
            std::cerr << "Failed to initialize decode_audio_ring_buffer" << std::endl;
            return EXIT_FAILURE;
        }
        const float rxPeriod = getRXPeriod(digitalMode);
        const int NUM_SAMPLES_UPSAMPLED = static_cast<int>(rxPeriod * static_cast<float>(waveSampleRate));
        for (size_t k = 0; k < decode_audio_ring_buffer.size; ++k) {
            screenPrinter->debug("Initializing audio buffer " + std::to_string(k) + " of " + std::to_string(decode_audio_ring_buffer.size));
            decode_audio_ring_buffer.recs[k].init(NUM_SAMPLES_UPSAMPLED);
            decode_audio_ring_buffer.recs[k].clear();
        }

        //
        //  Start Threads
        //

        screenPrinter->print("Creating receiver thread...", LOG_LEVEL::DEBUG);
        iqThread = std::thread(&Instance::readIQ, this);
        iqThread.detach();
        
        screenPrinter->print("Creating SSB Demodulator thread...", LOG_LEVEL::DEBUG);
        demodThread = std::thread(&Instance::demodulate<float>, this);
        demodThread.detach();

        screenPrinter->print("Creating samplemanager thread...", LOG_LEVEL::DEBUG);
        sampleManagerThread = std::thread(&Instance::sampleManager, this);
        sampleManagerThread.detach();

        screenPrinter->print("Creating candidate finder thread...", LOG_LEVEL::DEBUG);
        wavThread = std::thread(&Instance::getCandidatesLoop, this);
        wavThread.detach();

        return true;
    }

    void getCandidatesLoop() {
        while (!terminateFlag) {
            // Wait for another audio buffer to become full.. (blocks)
            decode_audio_buffer_t<float>& audio_buffer = decode_audio_ring_buffer.pop_ref();
            if (audio_buffer.startEpochTime == 0) {
                continue;
            }

            screenPrinter->print("popped audio buffer, size: " + std::to_string(audio_buffer.size), LOG_LEVEL::DEBUG);
            if (terminateFlag) {
                break;
            }

            std::string fpart = make_uuid() + ".wav";
            const std::string fileName = wavPath + "\\" + fpart;

            float maxVal = std::numeric_limits<float>::lowest();
            for (size_t k = 0; k < audio_buffer.size; ++k) {
                if (audio_buffer.buf[k] > maxVal) {
                    maxVal = audio_buffer.buf[k];
                }
            }
            // std::cout << "largest value: " << maxVal << std::endl;


             // parens prevent macro expansion, fix windows.h max definition collision
            float minVal = (std::numeric_limits<float>::max)();
            for (size_t k = 0; k < audio_buffer.size; ++k) {
                if (audio_buffer.buf[k] < minVal) {
                    minVal = audio_buffer.buf[k];
                }
            }
            // std::cout << "smallest value: " << minVal << std::endl;

            if (std::fabs(minVal) > maxVal) {
                maxVal = std::fabs(minVal);
            }

            float factor = clipVal / (maxVal + 1.0f);
            if (digitalMode == "WSPR") {
                factor *= audioScaleFactor_wspr;
            }
            else {
                factor *= audioScaleFactor_ft;
            }

            screenPrinter->print("Computed audio scale factor: " + std::to_string(factor), LOG_LEVEL::DEBUG);
            screenPrinter->print("Maximum audio value: " + std::to_string(factor*maxVal), LOG_LEVEL::DEBUG);

            std::thread wavThread = std::thread(&Instance::waveWrite, this, std::ref(audio_buffer), fileName, factor);
            wavThread.join();
            
            FileToDecode toDecode(fileName, digitalMode, audio_buffer.startEpochTime, ssbFreq);
            toDecode.baseFreq = ssbFreq;
            toDecode.epochTime = audio_buffer.startEpochTime;
            toDecode.filename = fileName;
            toDecode.mode = digitalMode;
            decoderPool->push(toDecode);

        }
    }

    void waveWrite(decode_audio_buffer_t<float>& audioBuffer, const std::string& fileName, const float factor) {
        screenPrinter->print("Beginning wave file generation...", LOG_LEVEL::DEBUG);

        std::size_t DataLen = audioBuffer.size * sizeof(std::int16_t);
        screenPrinter->print("Audio Data Length (bytes): " + std::to_string(DataLen), LOG_LEVEL::DEBUG);

        WavHdr Hdr;

        Hdr._RIFF[0] = 'R'; Hdr._RIFF[1] = 'I'; Hdr._RIFF[2] = 'F'; Hdr._RIFF[3] = 'F';
        Hdr.FileLen = static_cast<uint32_t>((sizeof(Hdr) + DataLen) - 8);
        Hdr._WAVE[0] = 'W'; Hdr._WAVE[1] = 'A'; Hdr._WAVE[2] = 'V'; Hdr._WAVE[3] = 'E';
        Hdr._fmt[0] = 'f'; Hdr._fmt[1] = 'm'; Hdr._fmt[2] = 't'; Hdr._fmt[3] = ' ';

        Hdr.FmtLen = sizeof(WAVEFORMATEX);
        Hdr.Format.wFormatTag = WAVE_FORMAT_PCM;
        Hdr.Format.nChannels = 1;
        Hdr.Format.nSamplesPerSec = 12000;
        Hdr.Format.nBlockAlign = 2;
        Hdr.Format.nAvgBytesPerSec = Hdr.Format.nSamplesPerSec * Hdr.Format.nBlockAlign;
        Hdr.Format.wBitsPerSample = 16;
        Hdr.Format.cbSize = 0;

        Hdr._data[0] = 'd'; Hdr._data[1] = 'a'; Hdr._data[2] = 't'; Hdr._data[3] = 'a';
        Hdr.DataLen = static_cast<DWORD>(DataLen);

        std::vector<int16_t> wave16data;
        wave16data.reserve(audioBuffer.size);

        for (std::size_t k = 0; k < audioBuffer.size; ++k) {
            wave16data.push_back(static_cast<int16_t>(std::round(audioBuffer.buf[k] * factor)));
        }

        std::uint32_t bytesWritten = 0;

        HANDLE File = wavOpen(fileName, Hdr);

        void* Data = wave16data.data();

        const bool writeStatus = WriteFile(File, Data, (DWORD)DataLen, (LPDWORD)& bytesWritten, NULL);
        if (!writeStatus) {
            std::cerr << "Error writing wave file data" << std::endl;
        }
        CloseHandle(File);

        screenPrinter->print("Wave writing complete.", LOG_LEVEL::DEBUG);
    }

    template <typename T>
    void demodulate() {
        // Demodulate IQ for SSB

        const size_t ssbd_in_size = ssbd->GetInSize();

        std::vector<T> rawSamp(iq_len / decRatio, 0.0);

        while (!terminateFlag) {

            // get IQ data
            std::complex<T>* xc = iq_buffer.pop();

            for (size_t n = 0; n < iq_len; n += ssbd_in_size) {
                ssbd->Iterate(xc + n, rawSamp.data() + n / decRatio);
            }

            auto& decode_audio_buffer = decode_audio_ring_buffer.recs[decode_audio_ring_buffer.write_index];
            decode_audio_buffer.write(rawSamp);
        }
    }

    void sampleManager() {
        while (!terminateFlag) {
            waitForTime(digitalMode);
            if (terminateFlag) {
                return;
            }
            decode_audio_ring_buffer.wait_for_empty_slot();

            if (terminateFlag) {
                return;
            }
            decode_audio_ring_buffer.inc_write_index();

            decode_audio_ring_buffer.recs[decode_audio_ring_buffer.write_index].clear();

            decode_audio_ring_buffer.recs[decode_audio_ring_buffer.write_index].startEpochTime = std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1);
            // wait at least 3 seconds so we don't double decode
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }
    }

    void terminate() {
        
        decode_audio_ring_buffer.terminate();
        iq_buffer.terminate();

        terminateFlag = true;
        
        SM.Close();
    }

  
    void readIQ() {

        while (!terminateFlag) {

            // wait for new data from receiver. Blocks until data received
            SM.WaitForNewData();
            if (terminateFlag) {
                return;
            }

            // wait for a slot to be ready in the buffer. Blocks until slot available.
            if (!iq_buffer.wait_for_empty_slot()) {
                std::cout << "No slots available in IQ buffer!" << std::endl;
                continue;
            }

            std::vector<std::complex<float>> iq_raw(iq_len);

            // read block of data from receiver
            const bool readSuccess = SM.Read((PBYTE)iq_raw.data(), (DWORD)iq_len * sizeof(std::complex<float>));

            for (size_t k = 0; k < iq_len; ++k) {
                iq_buffer.recs[iq_buffer.write_index][k] = iq_raw[k];
            }

            if (readSuccess) {
                iq_buffer.inc_write_index();
            }
            else {
                std::cout << "Did not read any I/Q data from shared memory" << std::endl;
            }
            
        }
    }


    std::string getUTC(const uint8_t sec) const {
        std::time_t t = std::time(nullptr);
        std::tm* utc = std::gmtime(&t);
        std::ostringstream ss;

        ss << std::setw(2) << std::setfill('0') << utc->tm_hour;
        ss << std::setw(2) << std::setfill('0') << utc->tm_min;
        ss << std::setw(2) << std::setfill('0') << sec;

        return ss.str();
    }

    void waitForTimeWSPR() const {
        screenPrinter->print("Waiting for WSPR interval", LOG_LEVEL::DEBUG);
        SYSTEMTIME time;
        while (!terminateFlag) {
            GetSystemTime(&time);
            const std::uint16_t min = static_cast<std::uint16_t>(time.wMinute);
            if ((min & 1) == 0) { // if is even
                if (time.wSecond == 0) {
                    screenPrinter->print("Beginning WSPR interval...", LOG_LEVEL::DEBUG);
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void waitForTimeFT4() const {
        screenPrinter->print("Waiting for FT4 interval", LOG_LEVEL::DEBUG);
        SYSTEMTIME time;
        while (!terminateFlag) {
            GetSystemTime(&time);
            switch (time.wSecond) {
            case 0:
            case 15:
            case 30:
            case 45:
                screenPrinter->print("Beginning FT4 interval...", LOG_LEVEL::DEBUG);
                return;
            case 7:
            case 22:
            case 37:
            case 52:
                while (time.wMilliseconds < 475) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    GetSystemTime(&time);
                }
                screenPrinter->print("Beginning FT4 interval...", LOG_LEVEL::DEBUG);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void waitForTimeFT8() {
        screenPrinter->print("Waiting for FT8 interval", LOG_LEVEL::DEBUG);
        while (!terminateFlag) {
            std::time_t t = std::time(nullptr);
            tm* ts = std::gmtime(&t);
            const bool go = ts->tm_sec == 0 || ts->tm_sec == 15 || ts->tm_sec == 30 || ts->tm_sec == 45;
            if (go) {
                screenPrinter->print("Beginning FT8 interval...", LOG_LEVEL::DEBUG);
                //screenPrinter->print("UTC:   " + std::put_time(ts, "%c %Z"), LOG_LEVEL::DEBUG);
                return;
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }

    void waitForTime(const std::string& digitalMode) {
        if (digitalMode == "FT8") {
            waitForTimeFT8();
        }
        else if (digitalMode == "FT4") {
            waitForTimeFT4();
        }
        else if (digitalMode == "WSPR") {
            waitForTimeWSPR();
        }
    }

    ring_buffer_t<std::complex<float>*> iq_buffer;
    FrequencyHz ssbFreq;
    ring_buffer_t< decode_audio_buffer_t<float> > decode_audio_ring_buffer;
    std::thread readPipeThread;
    std::string digitalMode;
    int nMem;
    int SMNumber;
    CSharedMemory SM;
    uint32_t radioSR;
    SM_HDR* SHDR;
    int32_t ssbBw;
    double freqCal;
    uint32_t decRatio;
    uint32_t SSB_SR;
    size_t iq_len;
    uint32_t highestDecodeFreq;
    std::string wavPath;
    uint32_t waveSampleRate;
    int decodedepth;
    int numjt9threads;

    std::shared_ptr<ScreenPrinter> screenPrinter;

    std::atomic_bool terminateFlag;

    std::thread iqThread;
    std::thread demodThread;
    std::thread sampleManagerThread;
    std::thread wavThread;

    std::unique_ptr<SSBD<float>> ssbd;

    std::shared_ptr<DecoderPool> decoderPool;

    float audioScaleFactor_ft;
    float audioScaleFactor_wspr;
};

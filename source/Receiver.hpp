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

#include "ring_buffer_spmc.h"
#include "CWSL_DIGI.hpp"
#include "CWSL_Utils.hpp"
#include "ring_buffer.h"
#include "decode_audio_buffer.h"
#include "StringUtils.hpp"
#include "HamUtils.hpp"
#include "ScreenPrinter.hpp"

#include "DecoderPool.hpp"


class Receiver {
public:
    Receiver(
        const size_t idIn,
        const std::string& smnameIn,
        std::shared_ptr<ScreenPrinter> sp) : 
        id(idIn),
        iq_buffer_base_mem(nullptr),
        iq_len(0),
        smname(smnameIn),
        screenPrinter(sp),
        terminateFlag(false),
        twKey(tw->addThread("receiver " + std::to_string(id)))
    {

    }

    virtual ~Receiver(){
        terminate();
        if (iq_buffer_base_mem) {
            free(iq_buffer_base_mem);
        }
    }

    bool openSharedMemory() {
        if (!SM.Open(smname.c_str())) {
            screenPrinter->err(receiverLog()+"Can't open shared memory : " + smname);
            return false;
        }
        else {
            screenPrinter->debug(receiverLog()+"Opened shared memory OK: " + smname);
        }

        // get info about channel
        SHDR = SM.GetHeader();
        radioSR = static_cast<uint32_t>(SHDR->SampleRate);
        iq_len = SHDR->BlockInSamples;
        screenPrinter->print(receiverLog()
            + "\tSample Rate: " + std::to_string(radioSR)
            + "\tBlock In Samples: " + std::to_string(iq_len)
            + "\tLO: " + std::to_string(SHDR->L0)
            + "\tShared Memory: " + smname,
            LOG_LEVEL::INFO);
    
        return true;
    }

    std::size_t getIQLength() const {
        return iq_len;
    }

    std::uint32_t getSampleRate() const {
        return radioSR;
    }

    FrequencyHz getLO() const {
        return SHDR->L0;
    }

    bool restart() {
        screenPrinter->info(receiverLog() + "Receiver restarting");
        terminate();
        return init();
    }

    bool init()
    {
        terminateFlag = false;

        if (!openSharedMemory()) {
            return false;
        }

        //
        // Prepare circular buffers
        // 

        try {
            if (iq_buffer.initialized) {
                iq_buffer.reset();
            }
            else {
                const size_t iqBufSz = ((static_cast<size_t>(radioSR) / iq_len) + 1) * 3;
                screenPrinter->debug(receiverLog() + "Initializing IQ ring buffer, length = " + std::to_string(iqBufSz) + " blocks");
                const bool initStatus = iq_buffer.initialize(iqBufSz);
                if (!initStatus) {
                    screenPrinter->print(receiverLog() + "Failed to initialize memory for iq buffer");
                    return false;
                }

                const size_t iqBufferByLen = sizeof(std::complex<float>) * iq_len * iq_buffer.size;
                screenPrinter->debug(receiverLog() + "Allocating IQ buffer, size = " + std::to_string(iqBufferByLen) + " bytes");
                iq_buffer_base_mem = malloc(iqBufferByLen);
                memset(iq_buffer_base_mem, 0, iqBufferByLen);
                if (!iq_buffer_base_mem) {
                    return false;
                }
                std::complex<float>* mem = reinterpret_cast<std::complex<float>*>(iq_buffer_base_mem);
                for (size_t k = 0; k < iq_buffer.size; ++k) {
                    iq_buffer.recs[k] = mem;
                    mem += iq_len;
                }
            }
        }
        catch (const std::exception& e) {
            screenPrinter->err(receiverLog() + std::string("Caught exception allocating IQ buffers: ") + e.what());
            return false;
        }

        startThreads();
        
        return true;
    }

    void startThreads() {
        screenPrinter->print(receiverLog() + "Creating receiver thread...", LOG_LEVEL::DEBUG);
        iqThread = std::thread(&Receiver::readIQ, this);
        SetThreadPriority(iqThread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
    }

    ring_buffer_spmc_t<std::complex<float>*>* getIQBuffer() {
        return &iq_buffer;
    }

    void terminate() {
        screenPrinter->debug(receiverLog() + "Receiver terminating...");
        iq_buffer.terminate();
        terminateFlag = true;
        if (iqThread.joinable())
        {
            iqThread.join();
        }
        SM.Close();
    }

    void readIQ() {
        screenPrinter->debug(receiverLog() + "readIQ thread started");
        tw->threadStarted(twKey);

        const size_t readSize = (DWORD)iq_len * sizeof(std::complex<float>);

        while (!terminateFlag) {

            try {
                tw->report(twKey);

                // wait for new data from receiver. Blocks until data received
                SM.WaitForNewData();
                if (terminateFlag) {
                    break;
                }

                if (iq_buffer.full()) {
                    screenPrinter->err(receiverLog() + "I/Q buffer is full!");

                    // wait for a slot to be ready in the buffer. Blocks until slot available.
                    if (!iq_buffer.wait_for_empty_slot()) {
                        break;
                    }
                }

                // read block of data from receiver

                std::vector<std::complex<float>> rawiq(readSize);

                const bool readSuccess = SM.Read((PBYTE)rawiq.data(), (DWORD)readSize);
                if (!readSuccess) {
                    screenPrinter->warning(receiverLog() + "Did not read any I/Q data from shared memory. CPU Overload?");
                }

                memcpy(iq_buffer.recs[iq_buffer.write_index],rawiq.data(),readSize);

                iq_buffer.inc_write_index();

                /*
                float maxVal = std::numeric_limits<float>::lowest();
                for (size_t k = 0; k < readSize; ++k) {
                    if (rawiq[k].real() > maxVal) {
                        maxVal = rawiq[k].real();
                    }
                }

                ofs<<maxVal<<std::endl;
                */
                /*
                const bool readSuccess = SM.Read((PBYTE)iq_buffer.recs[iq_buffer.write_index], readSize);
                if (!readSuccess) {
                    screenPrinter->warning(receiverLog() + "Did not read any I/Q data from shared memory. CPU Overload?");
                }
                iq_buffer.inc_write_index();
            
            */
            }
            catch (const std::exception& e) {
                screenPrinter->err(receiverLog() + std::string("Caught exception in readIQ(): ") + e.what());
            }
        }
        screenPrinter->debug(receiverLog() + "readIQ thread finished");
        tw->threadFinished(twKey);
    }

    std::string receiverLog() const {
        const std::string s = "Receiver " + smname + " " + std::to_string(id) + " ";
        return s;
    }

    ring_buffer_spmc_t<std::complex<float>*> iq_buffer;
    CSharedMemory SM;
    std::uint32_t radioSR;
    SM_HDR* SHDR;
    std::size_t iq_len;
    std::shared_ptr<ScreenPrinter> screenPrinter;

    std::atomic_bool terminateFlag;

    std::thread iqThread;

    std::size_t id;

    void* iq_buffer_base_mem;

    std::string smname;

    std::uint64_t twKey;

  //  std::ofstream ofs;

};

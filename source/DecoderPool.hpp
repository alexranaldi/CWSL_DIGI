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
#include <string>
#include <memory>

#include "CWSL_DIGI.hpp"
#include "windows.h"
#include "ScreenPrinter.hpp"
#include "OutputHandler.hpp"

struct FileToDecode
{
    std::string filename = "";
    std::string mode = "";
    std::uint64_t epochTime = 0;
    FrequencyHz baseFreq = 0;
    FileToDecode(
        const std::string filenameIn,
        const std::string modeIn,
        const std::uint64_t epochTimeIn,
        const FrequencyHz baseFreqIn) :
        filename(filenameIn),
        mode(modeIn),
        epochTime(epochTimeIn),
        baseFreq(baseFreqIn)
    {}
};

class DecoderPool {
public:
    DecoderPool(
        const bool keepWavFilesIn,
        const bool printJT9OutputIn,
        const uint16_t nWork,
        const int nJT9Threads,
        const int d,
        const int wc,
        const uint32_t h,
        const std::string& binPathIn,
        const int maxDataAgeIn,
        std::shared_ptr<ScreenPrinter> sp,
        std::shared_ptr<OutputHandler> oh) : 
    binPath(binPathIn),
    decodedepth(d),
    wsprCycles(wc),
    highestDecodeFreq(h),
    keepWavFiles(keepWavFilesIn),
    maxDataAge(maxDataAgeIn),
    numjt9threads(nJT9Threads),
    numWorkers(nWork),
    outputHandler(oh),
    printJT9Output(printJT9OutputIn),
    screenPrinter(sp),
    terminateFlag(false)
    {
        for (uint16_t k = 0; k < numWorkers; ++k) {
            threads.push_back( std::thread(&DecoderPool::doWork, this) );
            threads.back().detach();
        }
    }

    virtual ~DecoderPool(){}

    const uint64_t getEpochTime() {
        uint64_t e = std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1);
        //std::cout << "Epoch time: " << e << std::endl;
        return e;
    }

    const uint64_t getEpochTimeMs() {
        uint64_t e = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
        //std::cout << "Epoch time: " << e << std::endl;
        return e;
    }

    void doWork() {
        constexpr int MAX_AGE = 200; // sec
        while (!terminateFlag) {
            auto item = toDecode.dequeue();
            const uint64_t et = getEpochTime();
            double agoSec = static_cast<double>(et) - static_cast<double>(item.epochTime);
            double rxPeriod = getRXPeriod(item.mode);
            if (item.mode == "WSPR") {
                agoSec -= WSPR_PERIOD;
            }
            double maxAgeSec = static_cast<double>(maxDataAge) * rxPeriod;
            if (maxAgeSec > 260.) {
                maxAgeSec = 260.;
            }
            if (agoSec > maxAgeSec) {
                screenPrinter->print("Data too old to decode - skipping!", LOG_LEVEL::ERR);
                continue;
            }
            else if (agoSec > maxAgeSec * 0.647217275152409) { // pi divided by three times the golden ratio
                screenPrinter->print("Data Age close to limit: " + std::to_string(agoSec) + " sec", LOG_LEVEL::WARN);
            }
            else {
                screenPrinter->print("Data Age: " + std::to_string(agoSec) + " sec", LOG_LEVEL::DEBUG);
            }
            decode(item.filename, item.epochTime, item.baseFreq, item.mode);
            screenPrinter->print("Items in decode queue: " + std::to_string(toDecode.size()), LOG_LEVEL::DEBUG);
        }
    }

    void terminate() {
        terminateFlag = true;
    }

    void decode(std::string wavFileName, uint64_t epochTime, const FrequencyHz baseFreq, const std::string& digitalMode) {
        PROCESS_INFORMATION pi;

        ZeroMemory(&pi, sizeof(pi));

        SECURITY_ATTRIBUTES saAttr;
        ZeroMemory(&saAttr, sizeof(saAttr));
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;

        saAttr.lpSecurityDescriptor = NULL;

        HANDLE m_hChildStd_OUT_Rd = NULL;
        HANDLE m_hChildStd_OUT_Wr = NULL;

        // Create a pipe for the child process's STDOUT. 

        if (!CreatePipe(&m_hChildStd_OUT_Rd, &m_hChildStd_OUT_Wr, &saAttr, 0))
        {
            return;
        }

        // Ensure the read handle to the pipe for STDOUT is not inherited.

        if (!SetHandleInformation(m_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
        {
            return;
        }

        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdError = m_hChildStd_OUT_Wr;
        si.hStdOutput = m_hChildStd_OUT_Wr;
        si.dwFlags |= STARTF_USESTDHANDLES;

        std::string appName = "";
        std::string modeOp = "";
        if (("FT8" == digitalMode) || ("FT4" == digitalMode)) {
            appName = "jt9.exe";
            if ("FT8" == digitalMode) {
                modeOp += " -8 -m " + std::to_string(numjt9threads) + " -d " + std::to_string(decodedepth) + " -w 1 -H " + std::to_string(highestDecodeFreq) + " ";
            }
            else if ("FT4" == digitalMode) {
                modeOp += " -5 -m " + std::to_string(numjt9threads) + " -d " + std::to_string(decodedepth) + " -w 1 -H " + std::to_string(highestDecodeFreq) + " ";
            }
            else {
                modeOp += "";
            }
        }
        else if ("WSPR" == digitalMode) {
            appName = "wsprd.exe";
            modeOp = " -C " + std::to_string(wsprCycles ) + " -o 5 -d ";
        }

        const std::string opts = modeOp + wavFileName;
        const std::string  cmdLine = binPath + "\\" + appName + opts;

        uint64_t startTime = getEpochTimeMs();

        screenPrinter->print("Calling: " + cmdLine, LOG_LEVEL::DEBUG);

        // Start the child process. 
        const bool procStatus = CreateProcessA(
            (binPath + "\\" + appName).c_str(),
            const_cast<char*>(opts.c_str()),
            NULL,
            NULL,
            true,
            0,
            NULL,
            binPath.c_str(),
            &si,
            &pi
        );

        if (!procStatus) {
            std::cerr << "CreateProcess failed. Error: " << GetLastError() << std::endl;
            return;
        }
        CloseHandle(m_hChildStd_OUT_Wr);

        size_t timeoutMs = 0;
        if ("FT8" == digitalMode || "FT4" == digitalMode) {
            timeoutMs = 15 * 2 * 1000;
        }
        else {
            // wspr
            timeoutMs = 120 * 2 * 1000;
        }

        // Wait until child process exits.
        WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutMs));

        uint64_t stopTime = getEpochTimeMs();

        screenPrinter->debug("External " + appName + " process completed in " + std::to_string(static_cast<float>(stopTime-startTime)/1000) + " sec");

        readDataFromExtProgram(m_hChildStd_OUT_Rd, epochTime, baseFreq, digitalMode);

        CloseHandle(m_hChildStd_OUT_Rd);

        // Close process and thread handles. 
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (!keepWavFiles) {
            // delete wav file
            const bool delStatus = DeleteFile(wavFileName.c_str());
            if (!delStatus) {
                screenPrinter->print("Failed to delete file: " + wavFileName, LOG_LEVEL::ERR);
            }
        }
        else {
            screenPrinter->print("Keeping wav file: " + wavFileName, LOG_LEVEL::DEBUG);
        }
    }

    void readDataFromExtProgram(HANDLE& pip, const uint64_t epochTime, const FrequencyHz baseFreq, const std::string& digitalMode)
    {
        constexpr size_t BUF_SIZE = 32768;
        DWORD dwRead;
        CHAR* chBuf = nullptr;
        BOOL bSuccess = FALSE;

        chBuf = (char*)malloc(BUF_SIZE);

        std::string out = "";
        for (;;)
        {
            bSuccess = ReadFile(pip, chBuf, BUF_SIZE, &dwRead, NULL);
            if (!bSuccess || dwRead == 0) break;
            std::string s(chBuf, dwRead);
            out += s;
        }

        free(chBuf);

        if (printJT9Output) {
            screenPrinter->print(out, LOG_LEVEL::INFO);
        }
        else {
            screenPrinter->print(out, LOG_LEVEL::DEBUG);
        }

        JT9Output s(out, digitalMode, epochTime, baseFreq);
        outputHandler->handle(s);
    }

    void push(const FileToDecode& s) {
        toDecode.enqueue(s);
    }

private:
    uint16_t numWorkers;
    std::shared_ptr<ScreenPrinter> screenPrinter;
    std::shared_ptr<OutputHandler> outputHandler;
    int numjt9threads;

    SafeQueue<FileToDecode> toDecode;

    std::vector<std::thread> threads;

    bool terminateFlag;

    int decodedepth;
    uint32_t highestDecodeFreq;

    std::shared_ptr<OutputHandler> oh;

    bool printJT9Output;
    std::string binPath;

    bool keepWavFiles;

    int maxDataAge;

    int wsprCycles;

};

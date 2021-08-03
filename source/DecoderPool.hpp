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
#include <utility>

#include "CWSL_DIGI.hpp"
#include "windows.h"
#include "ScreenPrinter.hpp"
#include "OutputHandler.hpp"
#include "TimeUtils.hpp"

#include "QtCore\qsharedmemory.h"


#define NSMAX 6827
#define NTMAX 30*60
#define RX_SAMPLE_RATE 12000

#ifdef __cplusplus
#include <cstdbool>
#else
#include <stdbool.h>
#endif

/*
 * This structure is shared with Fortran code, it MUST be kept in
 * sync with lib/jt9com.f90
 */
typedef struct dec_data {
    int   ipc[3];
    float ss[184 * NSMAX];
    float savg[NSMAX];
    float sred[5760];
    short int d2[NTMAX * RX_SAMPLE_RATE];
    struct
    {
        int nutc;                   //UTC as integer, HHMM
        bool ndiskdat;              //true ==> data read from *.wav file
        int ntrperiod;              //TR period (seconds)
        int nQSOProgress;           /* QSO state machine state */
        int nfqso;                  //User-selected QSO freq (kHz)
        int nftx;                   /* Transmit audio offset where
                                       replies might be expected */
        bool newdat;                //true ==> new data, must do long FFT
        int npts8;                  //npts for c0() array
        int nfa;                    //Low decode limit (Hz)
        int nfSplit;                //JT65 | JT9 split frequency
        int nfb;                    //High decode limit (Hz)
        int ntol;                   //+/- decoding range around fQSO (Hz)
        int kin;
        int nzhsym;
        int nsubmode;
        bool nagain;
        int ndepth;
        bool lft8apon;
        bool lapcqonly;
        bool ljt65apon;
        int napwid;
        int ntxmode;
        int nmode;
        int minw;
        bool nclearave;
        int minSync;
        float emedelay;
        float dttol;
        int nlist;
        int listutc[10];
        int n2pass;
        int nranera;
        int naggressive;
        bool nrobust;
        int nexp_decode;
        char datetime[20];
        char mycall[12];
        char mygrid[6];
        char hiscall[12];
        char hisgrid[6];
    } params;
} dec_data_t;


struct ItemToDecode
{
    std::string mode = "";
    std::uint64_t epochTime = 0;
    FrequencyHz baseFreq = 0;
    std::vector<std::int16_t> audio;
    int instanceId;
    std::string cwd;

    ItemToDecode() : 
        mode(""),
        epochTime(0),
        baseFreq(0),
        audio(0),
        instanceId(0),
        cwd("")
    {}

    ItemToDecode(
        std::vector<std::int16_t> audioIn,
        const std::string modeIn,
        const std::uint64_t epochTimeIn,
        const FrequencyHz baseFreqIn,
        const int instanceIdIn,
        const std::string& cwdIn) :
        audio(audioIn),
        mode(modeIn),
        epochTime(epochTimeIn),
        baseFreq(baseFreqIn),
        instanceId(instanceIdIn),
        cwd(cwdIn)
        {}
};

class DecoderPool {
public:
    DecoderPool(
        const std::string& transferMethodIn,
        const bool keepWavFilesIn,
        const bool printJT9OutputIn,
        const uint16_t nWork,
        const int maxWSPRDInstancesIn,
        const int nJT9Threads,
        const int d,
        const int wc,
        const uint32_t h,
        const std::string& binPathIn,
        const int maxDataAgeIn,
        const std::string& wavPathIn,
        std::shared_ptr<ScreenPrinter> sp,
        std::shared_ptr<OutputHandler> oh) : 
    binPath(binPathIn),
    wavPath(wavPathIn),
    decodedepth(d),
    wsprCycles(wc),
    highestDecodeFreq(h),
    keepWavFiles(keepWavFilesIn),
    maxDataAge(maxDataAgeIn),
    maxWSPRDInstances(maxWSPRDInstancesIn),
    numjt9threads(nJT9Threads),
    numWorkers(nWork),
    outputHandler(oh),
    printJT9Output(printJT9OutputIn),
    screenPrinter(sp),
    threads(0),
    transferMethod(transferMethodIn),
    terminateFlag(false) {

    }

    virtual ~DecoderPool(){}

    bool init() {
        int createdLong = 0;
        for (uint16_t k = 0; k < numWorkers; ++k) {
            screenPrinter->debug("Creating DecoderPool worker thread " + std::to_string(k) + " of " + std::to_string(numWorkers));
            uint64_t key = tw->addThread("DecoderPool worker " + std::to_string(k));
            tw->setAllowedDelta(key, 120 * 1000);
            threadKeys.push_back(key);
            const bool allowLong = createdLong < maxWSPRDInstances;
            threads.push_back(make_tuple(std::thread(&DecoderPool::doWork, this, k, allowLong), true, allowLong));
            std::get<0>(threads.back()).detach();
            if (allowLong) {
                createdLong++;
            }
        }

        //statsThread = std::thread(&DecoderPool::statsLoop,this);

        return true;
    }

    void statsLoop() {
        while (!terminateFlag) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60000));
            if (iterationTimes.empty()) { continue; }
            double totalConsumed = 0;
            double total = 0;
            for (auto p : iterationTimes) {
                total += std::get<2>(p);
                totalConsumed += std::get<1>(p);
            }
            if (0 == total) { continue; }
            const double perc = totalConsumed / total;
            screenPrinter->trace("Decoder threads consumed percentage: " + std::to_string(perc * 100));
            size_t numRunning = 0;
            size_t numTotal = threads.size();

            for (auto& pr : threads) {
                const auto& isRunning = std::get<1>(pr);
                if (isRunning) { numRunning++; }
            }
            screenPrinter->trace("Decoder threads total=" + std::to_string(numTotal) + " running=" + std::to_string(numRunning));

            if (perc < 0.4 && numRunning > 1) {
                for (auto& pr : threads) {
                    auto& isRunning = std::get<1>(pr);
                    const auto& allowsLong = std::get<2>(pr);
                    if (isRunning && !allowsLong) { isRunning = false; break; }
                }
            }
            else if (perc > 0.7 && numRunning < numTotal) {
                for (auto& pr : threads) {
                    auto& isRunning = std::get<1>(pr);
                    if (!isRunning) { isRunning = true; break; }
                }
            }
            auto et = getEpochTimeMs();
            while (!iterationTimes.empty() && std::get<0>(iterationTimes.front()) < et - (5 * 60 * 1000)) {
                iterationTimes.erase(iterationTimes.begin());
            }
        }
    }

    std::string inline decoderLog(const std::size_t workerIndex) const {
        return std::string("DecoderPool worker ") + std::to_string(workerIndex) + " ";
    }

    void doWork(const size_t workerIndex, const bool allowLong) {
        screenPrinter->debug(decoderLog(workerIndex) + " thread " + std::to_string(workerIndex) + " reporting for duty.");
        tw->threadStarted(threadKeys[workerIndex]);

        std::uint64_t iterationStartTime = getEpochTimeMs();
        std::uint64_t iterationStartDecodeTime = getEpochTimeMs();

        constexpr int MAX_AGE = 260; // sec
        while (!terminateFlag) {

            const std::uint64_t iterationEndTime = getEpochTimeMs();
            const std::uint64_t iterationConsumedTime = iterationEndTime - iterationStartDecodeTime;
            const std::uint64_t iterationTotalTime = iterationEndTime - iterationStartTime;

            if (iterationTotalTime) {
           //     iterationTimes.push_back(std::make_tuple(iterationStartTime,iterationConsumedTime,iterationTotalTime));
            }

            while (!std::get<1>(threads[workerIndex])) {
                screenPrinter->debug(decoderLog(workerIndex) + " zzzzz");
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                tw->report(threadKeys[workerIndex]);
            }

            iterationStartTime = getEpochTimeMs();
            tw->report(threadKeys[workerIndex]);
            ItemToDecode item;
            bool gotLongItem = false;
            bool gotItem = false;
            while (!gotItem && !gotLongItem) {
                if (allowLong) {
                    //screenPrinter->debug(decoderLog(workerIndex) + "DecoderPool Attempting long item dequeue");
                    gotLongItem = toDecodeLong.dequeue_timeout(item);
                }
                if (gotLongItem) {
                    screenPrinter->debug(decoderLog(workerIndex) + "DecoderPool Got long item");
                }
                else {
                    //screenPrinter->debug(decoderLog(workerIndex) + "DecoderPool Attempting item dequeue");
                    gotItem = toDecode.dequeue_timeout(item);
                }
            }
            iterationStartDecodeTime = getEpochTimeMs();
            screenPrinter->debug(decoderLog(workerIndex) + "DecoderPool Got item");
            const uint64_t et = getEpochTime();
            if (item.epochTime == 0) {
                continue;
            }
            else if (item.epochTime > et) {
                screenPrinter->err(decoderLog(workerIndex) + "data has bad epoch time: " + std::to_string(item.epochTime));
                continue;
            }
            double agoSec = static_cast<double>(et) - static_cast<double>(item.epochTime);
            const double rxPeriod = getRXPeriod(item.mode);
            if (item.mode == "WSPR" || item.mode == "JT65") {
                agoSec -= rxPeriod;
            }
            double maxAgeSec = static_cast<double>(maxDataAge) * rxPeriod;
            if (maxAgeSec > MAX_AGE) {
                maxAgeSec = MAX_AGE;
            }

            if (agoSec > maxAgeSec) {
                screenPrinter->print(decoderLog(workerIndex) + "data is " + std::to_string(agoSec) + " seconds old! Too old to decode - skipping!", LOG_LEVEL::ERR);
                continue;
            }
            else if (agoSec > maxAgeSec * 0.647217275152409) { // pi divided by three times the golden ratio
                screenPrinter->print(decoderLog(workerIndex) + "data Age close to limit: " + std::to_string(agoSec) + " sec", LOG_LEVEL::WARN);
            }
            else {
                screenPrinter->print(decoderLog(workerIndex) + "data Age: " + std::to_string(agoSec) + " sec", LOG_LEVEL::DEBUG);
            }

            if (item.mode == "WSPR" && !allowLong) {
                toDecodeLong.enqueue(item);
            }
           else  if (("shmem" == transferMethod) && (item.mode != "WSPR")) {
                screenPrinter->debug(decoderLog(workerIndex) + "item will be decoded with shmem");
                try {
                    decodeUsingShMem(item, workerIndex);
                }
                catch (const std::exception& e) {
                    screenPrinter->print(decoderLog(workerIndex) + "decodeUsingShMem call", e);
                }
            }
            else {
                screenPrinter->debug(decoderLog(workerIndex) + "Item will be decoded with wavefile");
                try {
                    decodeUsingFile(item, workerIndex);
                }
                catch (const std::exception& e) {
                    screenPrinter->print(decoderLog(workerIndex) + "decodeUsingFile call", e);
                }
            }

            screenPrinter->print(decoderLog(workerIndex) + "Items in decode queue: " + std::to_string(toDecode.size()), LOG_LEVEL::DEBUG);
        }
        screenPrinter->debug(decoderLog(workerIndex) + " thread TERMINATING!");

        tw->threadFinished(threadKeys[workerIndex]);

    }

    void terminate() {
        terminateFlag = true;
    }

    void decodeUsingShMem(const ItemToDecode& item, const size_t workerIndex) {
        std::string skey = "CWSL_DIGI_" + std::to_string(workerIndex) + "_" + std::to_string(item.instanceId) + "_" + std::to_string(getEpochTimeMs()) + "_" + make_uuid();

        QString qkey = QString::fromStdString(skey);

        QSharedMemory mem_jt9;

        mem_jt9.setKey(qkey);

        const bool cStatus = mem_jt9.create(sizeof(dec_data_t));
        if (!cStatus) {
            screenPrinter->err(decoderLog(workerIndex) + "Failed to create shared memory segment!");
            QString es = mem_jt9.errorString();
            screenPrinter->err(es.toStdString());
            return;
        }

        screenPrinter->debug(decoderLog(workerIndex) + "Created shared memory segment. Key=" + skey + " size=" + std::to_string(mem_jt9.size()) + " bytes");

        screenPrinter->debug(decoderLog(workerIndex) + "Copying data to shmem");

        if (!mem_jt9.lock())
        {
            screenPrinter->err(decoderLog(workerIndex) + "Could not acquire lock on shared memory!");
            return;
        }

        dec_data_t* dec_data = reinterpret_cast<dec_data_t*>(mem_jt9.data());
        memset(dec_data, 0, sizeof(dec_data_t));

        if (item.mode == "FT8") {
            dec_data->params.lft8apon = true;
            dec_data->params.nzhsym = 0;
            //dec_data->params.npts8 = (50 * 6912) / 16;
            dec_data->params.nmode = 8;
            dec_data->params.napwid = 50;
            dec_data->params.ntrperiod = 15; // s
        }
        else if (item.mode == "FT4") {
            dec_data->params.nmode = 5;
            dec_data->params.ntrperiod = 7.5; // s
            dec_data->params.napwid = 80;
            dec_data->params.nzhsym = 0;
        }
        else if (item.mode == "Q65-30") {
            dec_data->params.nmode = 65;
            dec_data->params.ntrperiod = 30.0; // s
            dec_data->params.nzhsym = 0;
        }
        else if (item.mode == "JT65") {
            dec_data->params.nzhsym = 174;
            dec_data->params.ntxmode = 65;
            dec_data->params.nmode = 65;
            dec_data->params.ntrperiod = 60; // s
        }

        dec_data->params.ndepth = decodedepth;
        dec_data->params.nutc = 0;
        dec_data->params.newdat = 1;
        dec_data->params.nagain = 0;
        dec_data->params.nfa = 0;
        dec_data->params.nfb = highestDecodeFreq;
        dec_data->params.emedelay = 0;
        dec_data->params.nrobust = 0;
        dec_data->params.ndiskdat = 0;
        dec_data->params.minw = 0;
        dec_data->params.minSync = 0;
        dec_data->params.dttol = 4;

        dec_data->ipc[0] = dec_data->params.nzhsym;
        dec_data->ipc[1] = 1;// istart
        dec_data->ipc[2] = -1;// idone

        memcpy(&(dec_data->d2[0]), item.audio.data(), item.audio.size() * sizeof(std::int16_t));

        mem_jt9.unlock();


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

        appName = "jt9.exe";
        if ("FT8" == item.mode) {
            modeOp += " -8 -m " + std::to_string(numjt9threads) + " ";
        }
        else if ("FT4" == item.mode) {
            modeOp += " -5 -m " + std::to_string(numjt9threads) + " ";
        }
        else if ("Q65-30" == item.mode) {

        }
        else if ("JT65" == item.mode) {
            modeOp += "-6 -m " + std::to_string(numjt9threads) + " ";
        }
        else {
            screenPrinter->err(decoderLog(workerIndex) + "Mode " + item.mode + " not handled");
        }

        const std::string opts = modeOp + " -s " + skey;
        const std::string  cmdLine = binPath + "\\" + appName + opts;

        screenPrinter->print(decoderLog(workerIndex) + "Calling: " + cmdLine, LOG_LEVEL::DEBUG);
        uint64_t startTime = getEpochTimeMs();

        // Start the child process. 
        const bool procStatus = CreateProcessA(
            (binPath + "\\" + appName).c_str(),
            const_cast<char*>(opts.c_str()),
            NULL,
            NULL,
            true,
            0,
            NULL,
            item.cwd.c_str(),
            &si,
            &pi
        );

        if (!procStatus) {
            screenPrinter->err(decoderLog(workerIndex) + "CreateProcess failed. Error: " + std::to_string(GetLastError()));
            return;
        }

        CloseHandle(m_hChildStd_OUT_Wr);

        const bool extStatus = readDataFromExtProgram(m_hChildStd_OUT_Rd, item.epochTime, item.baseFreq, item.mode, item.instanceId, workerIndex);

        if (extStatus) {

            dec_data_t* d = reinterpret_cast<dec_data_t*>(mem_jt9.data());
        
            int flag = 1;
            while (1) {
                if (!mem_jt9.lock()) {
                    screenPrinter->err(decoderLog(workerIndex) + "Could not acquire lock on shared memory!");
                    continue;
                }
                flag = d->ipc[1];
                mem_jt9.unlock();
                if (flag == 0) { break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        
            if (!mem_jt9.lock()) {
                screenPrinter->err(decoderLog(workerIndex) + "Could not acquire lock on shared memory!");
                return;
            }
            d->ipc[1] = 999;
            d->ipc[2] = 1;
        }
        mem_jt9.unlock();

        mem_jt9.detach();

        const auto timeoutMs = getRXPeriod(item.mode) * 2 * 1000;
        WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutMs));

        // Wait until child process exits.
        uint64_t stopTime = getEpochTimeMs();

        screenPrinter->debug(decoderLog(workerIndex) + "External " + appName + " process completed in " + std::to_string(static_cast<float>(stopTime - startTime) / 1000) + " sec");

        screenPrinter->debug(decoderLog(workerIndex) + "done reading output");

        CloseHandle(m_hChildStd_OUT_Rd);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

    }

    std::string writeWaveFile(const ItemToDecode& item, const std::vector<std::int16_t>& audioBuffer) {
        std::string fpart = make_uuid() + ".wav";
        const std::string fileName = wavPath + "\\" + fpart;
        screenPrinter->debug("Generated temporary file name: " + fileName);

        try {
            waveWrite(audioBuffer, fileName);
        }
        catch (const std::exception& e) {
            screenPrinter->print("waveWrite call", e);
            return "";
        }
        return fileName;
    }

    void waveWrite(const std::vector<std::int16_t>& audioBuffer, const std::string& fileName) {
        screenPrinter->print("Beginning wave file generation...", LOG_LEVEL::DEBUG);

        const uint64_t startTime = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);

        std::size_t DataLen = audioBuffer.size() * sizeof(std::int16_t);
        screenPrinter->print("Audio Data Length (bytes): " + std::to_string(DataLen), LOG_LEVEL::DEBUG);
        screenPrinter->print("Audio Data Length (samples): " + std::to_string(audioBuffer.size()), LOG_LEVEL::DEBUG);

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

        std::uint32_t bytesWritten = 0;

        HANDLE File = wavOpen(fileName, Hdr, !keepWavFiles);

        const void* Data = audioBuffer.data();

        try {
            const bool writeStatus = WriteFile(File, Data, (DWORD)DataLen, (LPDWORD)& bytesWritten, NULL);
            if (!writeStatus) {
                screenPrinter->err("Error writing wave file data");
            }
        }
        catch (const std::exception& e) {
            screenPrinter->print("WriteFile", e);
        }

        CloseHandle(File);

        const uint64_t stopTime = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
        screenPrinter->debug("Wave writing completed in " + std::to_string(static_cast<float>(stopTime - startTime) / 1000) + " sec");

    }

    void decodeUsingFile(const ItemToDecode& item, const std::size_t workerIndex) {

        const std::string wavFileName = writeWaveFile(item, item.audio);

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
        if (("FT8" == item.mode) || ("FT4" == item.mode)) {
            appName = "jt9.exe";
            if ("FT8" == item.mode) {
                modeOp += " -8 -m " + std::to_string(numjt9threads) + " -d " + std::to_string(decodedepth) + " -w 1 -H " + std::to_string(highestDecodeFreq) + " ";
            }
            else if ("FT4" == item.mode) {
                modeOp += " -5 -m " + std::to_string(numjt9threads) + " -d " + std::to_string(decodedepth) + " -w 1 -H " + std::to_string(highestDecodeFreq) + " ";
            }
            else {
                modeOp += "";
            }
        }
        else if ("Q65-30" == item.mode) {
            appName = "jt9.exe";
            modeOp += " -3 -p 30 -H " + std::to_string(highestDecodeFreq) + " ";
        }
        else if ("WSPR" == item.mode) {
            appName = "wsprd.exe";
            modeOp = " -C " + std::to_string(wsprCycles ) + " -o 5 -d ";
        }
        else if ("JT65" == item.mode) {
            appName = "jt9.exe";
            modeOp = " -6 -d " + std::to_string(decodedepth) + " ";
        }

        const std::string opts = modeOp + wavFileName;
        const std::string  cmdLine = binPath + "\\" + appName + opts;

        uint64_t startTime = getEpochTimeMs();

        screenPrinter->print(decoderLog(workerIndex) + "Calling: " + cmdLine, LOG_LEVEL::DEBUG);

        // Start the child process. 
        const bool procStatus = CreateProcessA(
            (binPath + "\\" + appName).c_str(),
            const_cast<char*>(opts.c_str()),
            NULL,
            NULL,
            true,
            0,
            NULL,
            item.cwd.c_str(),
            &si,
            &pi
        );


        if (!procStatus) {
            std::cerr << "CreateProcess failed. Error: " << GetLastError() << std::endl;
            return;
        }

        CloseHandle(m_hChildStd_OUT_Wr);

        const auto timeoutMs = getRXPeriod(item.mode) * 2 * 1000;

        // Wait until child process exits.
        WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutMs));

        uint64_t stopTime = getEpochTimeMs();

        screenPrinter->debug(decoderLog(workerIndex) + "External " + appName + " process completed in " + std::to_string(static_cast<float>(stopTime-startTime)/1000) + " sec");


        readDataFromExtProgram(m_hChildStd_OUT_Rd, item.epochTime, item.baseFreq, item.mode, item.instanceId, workerIndex);

        CloseHandle(m_hChildStd_OUT_Rd);

        // Close process and thread handles. 
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (!keepWavFiles) {
            // delete wav file
            const bool delStatus = DeleteFile(wavFileName.c_str());
            if (!delStatus) {
                screenPrinter->print(decoderLog(workerIndex) + "Failed to delete file: " + wavFileName, LOG_LEVEL::ERR);
            }
        }
        else {
            screenPrinter->print(decoderLog(workerIndex) + "Keeping wav file: " + wavFileName, LOG_LEVEL::DEBUG);
        }
    }

    bool readDataFromExtProgram(HANDLE& pip, const uint64_t epochTime, const FrequencyHz baseFreq, const std::string& digitalMode, const std::size_t instanceId, const std::size_t workerIndex)
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

            // npos means no matches were found
            if (s.find("DecodeFinished") != std::string::npos) {
                break;
            }
        }

        free(chBuf);

        if (printJT9Output) {
            screenPrinter->print(decoderLog(workerIndex) + "external process output:\n" + out, LOG_LEVEL::INFO);
        }
        else {
            screenPrinter->print(decoderLog(workerIndex) + "external process output:\n" + out, LOG_LEVEL::DEBUG);
        }

        if (out.find("SIGABRT") != std::string::npos) {
            screenPrinter->print(decoderLog(workerIndex) + "external process apparently crashed!" + out, LOG_LEVEL::ERR);
            return false;
        }

        JT9Output s(out, digitalMode, epochTime, baseFreq, instanceId);

        try {
            outputHandler->handle(s);
        }
        catch (const std::exception& e) {
            screenPrinter->print(decoderLog(workerIndex) + "In call to outputHandler->handle", e);
        }
        screenPrinter->debug(decoderLog(workerIndex) + "Decoded data passed to output handler");
        return true;
    }

    void push(const ItemToDecode& s) {
        toDecode.enqueue(s);
    }

private:
    uint16_t numWorkers;
    std::shared_ptr<ScreenPrinter> screenPrinter;
    std::shared_ptr<OutputHandler> outputHandler;
    int numjt9threads;

    SafeQueue<ItemToDecode> toDecode;
    SafeQueue<ItemToDecode> toDecodeLong;

    std::vector<std::tuple<std::thread, bool, bool>> threads;

    bool terminateFlag;

    int decodedepth;
    uint32_t highestDecodeFreq;

    std::shared_ptr<OutputHandler> oh;

    bool printJT9Output;
    std::string binPath;

    bool keepWavFiles;

    int maxDataAge;

    int wsprCycles;

    std::string transferMethod;

    std::string wavPath;

    int maxWSPRDInstances;

    std::vector<std::uint64_t> threadKeys;
    std::thread statsThread;

    std::vector<std::tuple< std::uint64_t, std::uint64_t,std::uint64_t>> iterationTimes;

};

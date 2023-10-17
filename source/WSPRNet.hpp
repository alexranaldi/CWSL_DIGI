#pragma once

/*
Copyright 2023 Alexander Ranaldi
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
#include <chrono>
#include <thread>
#include <atomic>

#include <iostream>

#include "SafeQueue.h"
#include "CWSL_DIGI.hpp"

class ScreenPrinter;

using namespace std::literals; 
using namespace std;

namespace wspr {
    struct Report
    {
        std::string callsign;
        std::int32_t snr;
        std::uint32_t freq;
        std::string locator;
        std::uint64_t epochTime;
        std::int32_t mode = -1;
        float dt;
        std::int16_t drift;
        std::uint32_t recvfreq;
        std::int16_t dbm;
        std::string reporterCallsign;
    };
}

class WSPRNet {

public:
    WSPRNet(const std::string& grid, std::shared_ptr<ScreenPrinter> sp );

    virtual ~WSPRNet();

    bool init();

    void terminate();

    void handle(const std::string& callsign, const std::string& mode, const int32_t snr, const float dt, const std::int16_t drift, const std::int16_t dbm, const uint32_t freq, const uint32_t rf, const uint64_t epochTime, const std::string& grid, const std::string& reporterCallsign);

    bool isConnected();

    bool closeSocket();

    bool connectSocket();

    void sendReportWrapper(const wspr::Report& report);

    bool sendReport(const wspr::Report& report);

    bool sendMessageWithRetry(const std::string& message);

    int sendMessage(const std::string& message);

    std::string readMessage();

    void processingLoop();

    void reportStats();

    std::shared_ptr<ScreenPrinter> screenPrinter;
    SafeQueue< wspr::Report > mReports;
    const std::string SERVER_HOSTNAME = "wsprnet.org";
    const std::string portStr = "80";
    bool terminateFlag{0};
    std::thread mSendThread;
    SOCKET mSocket;
    sockaddr_storage target;
    std::string operatorGrid;

    std::atomic_int mCountSendsOK;
    std::atomic_int mCountSendsErrored;
};

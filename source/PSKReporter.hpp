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
#include <queue>
#include <list>
#include <thread>

#include "CWSL_DIGI.hpp"

#include "SafeQueue.h"

class ScreenPrinter;

using namespace std::literals;

namespace pskreporter {

using Byte = std::uint8_t; // byte
using Packet = std::vector<Byte>;

using FrequencyHz = std::uint32_t;

struct Report
{
    std::string callsign = "";
    std::int32_t snr = 0;
    FrequencyHz freq = 0;
    std::string locator = "";
    uint64_t epochTime = 0;
    std::string mode = "";
    Report(
        const std::string callsign_in,
        const int32_t snr_in,
        const FrequencyHz freq_in,
        const std::string locator_in,
        const uint64_t epochTime_in,
        const std::string mode_in
    ) : 
    callsign(callsign_in),
    snr(snr_in),
    freq(freq_in),
    locator(locator_in),
    epochTime(epochTime_in),
    mode(mode_in)
    {
        // fix modes. wsjt-x does not report the tx/rx periods for FST4 and FST4W, so we won't either. 
        //  FST4W-XXX -> FST4W
        //  FST4-XXX -> FST4

        if (isModeFST4W(mode)) {
            mode = "FST4W";
        }
        else if (isModeFST4(mode)) {
            mode = "FST4";
        }
    }
};

enum class REPORT_TYPE {
    TYPE_HAS_LOCATOR = 1,
    TYPE_NO_LOCATOR = 2,
};

class PSKReporter {

public:
    PSKReporter();
    ~PSKReporter();

    void handle(const std::string& callsign, int32_t snr, FrequencyHz freq, uint64_t epochTime, const std::string& mode);

    void handle(const std::string& callsign, int32_t snr, uint32_t freq, const std::string& loc, uint64_t epochTime, const std::string& mode);

    bool init(const std::string& operatorCallsign, const std::string& operatorLocator, const std::string& programName, std::shared_ptr<ScreenPrinter> printer);
    
    Packet getHeader();

    Packet getReceiverInformation();

    template <typename T>
    void packetAppend(Packet& packet, T data) {
        packet.push_back(static_cast<Byte>(data));
    }

    void processingLoop();

    Packet getSenderRecord(Report& report);

    void sendPacket(Packet& packet);

    std::size_t makePackets();

    void terminate();

private:

    bool isSameBand(const FrequencyHz f1, const FrequencyHz f2);

    void addHeaderToPacket(Packet& packet);

    void addDescriptorsToPacket(Packet& packet);

    std::vector<std::uint8_t> getDescriptorReceiver();

    std::vector<std::uint8_t> getDescriptorSender(REPORT_TYPE reportType);

    std::uint32_t mID; 
    std::uint32_t mSeq;

    bool terminateFlag;

    const std::string PORT = "4739";
    const std::string PORT_TEST = "14739";

    SOCKET mSocket;

    SafeQueue< Report > mReports;
    std::list< Report > mSentReports;

    std::thread mSendThread;

    const std::int64_t MIN_SECONDS_BETWEEN_SAME_CALLSIGN_REPORTS = 181;

    // max number of "safe" bytes to put in a UDP payload (one report fewer)
    static const size_t MAX_UDP_PAYLOAD_SIZE = 1342;

    std::chrono::time_point<std::chrono::steady_clock> mTimeDescriptorsSent;

    size_t mPacketsSentWithDescriptors{0};

    std::string mReceiverCallsign;
    std::string mReceiverLocator;

    std::string mProgramName;

    std::queue< Packet > mPackets;

    std::shared_ptr<ScreenPrinter> screenPrinter;

    const std::string SERVER_HOSTNAME = "report.pskreporter.info";
};

}; // namespace

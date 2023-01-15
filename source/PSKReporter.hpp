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

#include <chrono>
#include <random>
#include <queue>
#include <list>
#include <thread>
#include <iostream>

#include "CWSL_DIGI.hpp"
#include "SafeQueue.h"
#include "TimeUtils.hpp"
#include "ScreenPrinter.hpp"

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
    {}
};

enum class REPORT_TYPE {
    TYPE_HAS_LOCATOR = 1,
    TYPE_NO_LOCATOR = 2,
};

class PSKReporter {

public:
    PSKReporter() : 
    mSocket(INVALID_SOCKET),
    terminateFlag(false)
    {

    }

    virtual ~PSKReporter() {

    }

    void handle(std::string callsign, int32_t snr, FrequencyHz freq, uint64_t epochTime, std::string mode) {
        Report rep(callsign, snr, freq, "", epochTime, mode);

        // fix modes. wsjt-x does not report the tx/rx periods for FST4 and FST4W, so we won't either. 
        //  FST4W-XXX -> FST4W
        //  FST4-XXX -> FST4
        if (isModeFST4W(rep.mode)) {
            rep.mode = "FST4W";
        }
        else if (isModeFST4(rep.mode)) {
            rep.mode = "FST4";
        }

        mReports.enqueue(rep);
    }

    void handle(std::string callsign, int32_t snr, uint32_t freq, std::string loc, uint64_t epochTime, std::string mode) {
        Report rep(callsign, snr, freq, loc, epochTime, mode);
        mReports.enqueue(rep);
    }

    bool init(const std::string& operatorCallsign, const std::string& operatorLocator, const std::string& programName, std::shared_ptr<ScreenPrinter> printer) {

        mReceiverCallsign = operatorCallsign;
        mReceiverLocator = operatorLocator;
        mProgramName = programName;
        screenPrinter = printer;

        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != NO_ERROR) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }

        mSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (mSocket == INVALID_SOCKET) {
            printf("socket failed with error %d\n", WSAGetLastError());
            return false;
        }

        Recv_addr.sin_family = AF_INET;
        Recv_addr.sin_port = htons(PORT);
        int len = sizeof(struct sockaddr_in);
        Recv_addr.sin_addr.s_addr = inet_addr(SERVER_IP.c_str());

        // init random generator
        std::random_device rd;
        std::mt19937_64 gen(rd());

        std::uniform_int_distribution<std::uint32_t> dis;
        const std::uint32_t num = dis(gen);

        mID = dis(gen);

        mSeq = 0;

        mTimeDescriptorsSent = std::chrono::steady_clock::now() - 24h;

        mSendThread = std::thread(&PSKReporter::processingLoop, this);
        SetThreadPriority(mSendThread.native_handle(), THREAD_PRIORITY_IDLE);
        mSendThread.detach();

        return true;
    }

    Packet getHeader() {

        Packet header;
        packetAppend(header, 0x00);
        packetAppend(header, 0x0A);

        // next 2 are length - populated later on
        header.push_back(0x00);
        header.push_back(0x00);

        const std::uint32_t epochTime = static_cast<std::uint32_t>(getEpochTime());
        header.push_back(static_cast<Byte>((epochTime & 0xFF000000) >> 24));
        header.push_back(static_cast<Byte>((epochTime & 0x00FF0000) >> 16));
        header.push_back(static_cast<Byte>((epochTime & 0x0000FF00) >> 8));
        header.push_back(static_cast<Byte>((epochTime & 0x000000FF) >> 0));

        // seq #
        header.push_back((mSeq & 0xFF000000) >> 24);
        header.push_back((mSeq & 0x00FF0000) >> 16);
        header.push_back((mSeq & 0x0000FF00) >> 8);
        header.push_back(mSeq & 0x000000FF);

        // random id
        header.push_back((mID & 0xFF000000) >> 24);
        header.push_back((mID & 0x00FF0000) >> 16);
        header.push_back((mID & 0x0000FF00) >> 8);
        header.push_back((mID & 0x000000FF) >> 0);

        return header;
    }

    Packet getReceiverInformation() {
        Packet payload;

        payload.push_back(static_cast<Byte>(mReceiverCallsign.size()));
        for (char c : mReceiverCallsign) {
            payload.push_back(c);
        }

        payload.push_back(static_cast<Byte>(mReceiverLocator.size()));
        for (char c : mReceiverLocator) {
            payload.push_back(c);
        }

        payload.push_back(static_cast<Byte>(mProgramName.size()));
        for (char c : mProgramName) {
            payload.push_back(c);
        }

        while (payload.size() % 4 != 0) {
            payload.push_back(0);
        }

        Packet out;
        packetAppend(out, 0x99);
        packetAppend(out, 0x92);

        uint16_t totalSize = static_cast<std::uint16_t>(payload.size() + 4);

        packetAppend(out, (totalSize & 0xFF00) >> 16);
        packetAppend(out, totalSize & 0x00FF);

        for (auto v : payload) {
            packetAppend(out, v);
        }
               
        return out;
    }

    template <typename T>
    void packetAppend(Packet& packet, T data) {
        packet.push_back(static_cast<Byte>(data));
    }

    void processingLoop() {
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_real_distribution<float> dist(18, 38);

        while (!terminateFlag) {
            const float sleepTime = dist(mt);
            std::this_thread::sleep_for(std::chrono::seconds(static_cast<std::uint32_t>(sleepTime)));
            if (terminateFlag) { return; }

            // clean up old entries in mSentReports
            auto et = getEpochTime();
            for (auto it = mSentReports.begin(); it != mSentReports.end();) {
                std::int64_t agoSec = et - it->epochTime;
                if (agoSec > MIN_SECONDS_BETWEEN_SAME_CALLSIGN_REPORTS * 5) {
                    it = mSentReports.erase(it);
                }
                else {
                    ++it;
                }
            }

            size_t reportCount = 0;
            size_t reportTotal = 0;
            do {
                reportCount = makePackets();
                reportTotal += reportCount;
            }
            while (!terminateFlag && reportCount);

            screenPrinter->print("PSKReporter - Total reports packetized: " + std::to_string(reportTotal), LOG_LEVEL::DEBUG);
            screenPrinter->print("PSKReporter - Packets waiting to be sent:" + std::to_string(mPackets.size()), LOG_LEVEL::DEBUG);
            screenPrinter->print("Sent reports size:" + std::to_string(mSentReports.size()), LOG_LEVEL::DEBUG);

            while (!terminateFlag && !mPackets.empty()) {
                auto packet = mPackets.front();
                mPackets.pop();

                send(packet);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<std::uint32_t>(333)));
            }
        }
    }

    Packet getSenderRecord(Report report) {

        Packet payload;

        const bool hasLocator = !report.locator.empty();

        if (hasLocator) {
            payload.push_back(0x64);
            payload.push_back(0xAF);
            payload.push_back(0x00);
            payload.push_back(0x00);
        }
        else {
            payload.push_back(0x62);
            payload.push_back(0xA7);
            payload.push_back(0x00);
            payload.push_back(0x00);
        }

        payload.push_back(static_cast<Byte>(report.callsign.size()));
        for (char byte : report.callsign) {
            payload.push_back(byte);
        }
        payload.push_back(static_cast<Byte>((report.freq & 0xFF000000) >> 24));
        payload.push_back(static_cast<Byte>((report.freq & 0x00FF0000) >> 16));
        payload.push_back(static_cast<Byte>((report.freq & 0x0000FF00) >> 8));
        payload.push_back(static_cast<Byte>((report.freq & 0x000000FF) >> 0));

        // snr
        payload.push_back(report.snr & 0xFF);
     
        //std::cout << report.mode << std::endl;

        payload.push_back(static_cast<Byte>(report.mode.size()));
        for (char c : report.mode) {
            payload.push_back(c);
        }

        if (hasLocator) {
            payload.push_back(static_cast<Byte>(report.locator.size()));
            for (char byte : report.locator) {
                payload.push_back(byte);
            }
        }

        // info src - always 1
        payload.push_back(0x01);

        const std::uint32_t epochTime = static_cast<std::uint32_t>(report.epochTime);
        payload.push_back(static_cast<Byte>((epochTime & 0xFF000000) >> 24));
        payload.push_back(static_cast<Byte>((epochTime & 0x00FF0000) >> 16));
        payload.push_back(static_cast<Byte>((epochTime & 0x0000FF00) >> 8));
        payload.push_back(static_cast<Byte>((epochTime & 0x000000FF) >> 0));

        while (payload.size() % 4 != 0) {
            payload.push_back(0);
        }

        const uint16_t totalSize = static_cast<std::uint16_t>(payload.size());
        payload[2] = static_cast<Byte>((totalSize & 0xFF00) >> 16);
        payload[3] = static_cast<Byte>(totalSize & 0x00FF);

        return payload;
    }

    void send(Packet packet) {
        sendto(
            mSocket, 
            (const char*)packet.data(), 
            static_cast<int>(packet.size()),
            0, 
            (sockaddr*)& Recv_addr, 
            sizeof(Recv_addr));
    }

    std::size_t makePackets() {
        Packet wholePacket;

        addHeaderToPacket(wholePacket);

        bool hasDescriptors = false;
        const std::uint32_t desc_ss = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - mTimeDescriptorsSent).count());
        if (desc_ss > 965) {
            addDescriptorsToPacket(wholePacket);
            hasDescriptors = true;
        }

        auto rx = getReceiverInformation();
        for (auto p : rx) {
            wholePacket.push_back(p);
        }

        size_t count = 0;
        while (!terminateFlag && count <= MAX_REPORTS_PER_PACKET && !mReports.empty()) {

            auto report = mReports.dequeue();

            bool skip = false;
            for (auto it = mSentReports.begin(); it != mSentReports.end(); ++it) {
                if (it->callsign == report.callsign && isSameBand(it->freq, report.freq) && it->mode == report.mode) {
                    const std::int64_t agoSec = report.epochTime - it->epochTime;
                    if (agoSec <= MIN_SECONDS_BETWEEN_SAME_CALLSIGN_REPORTS) {
                        skip = true;
                        break;
                    }
                }
            }
            if (skip) {
                continue;
            }

            auto senderRec = getSenderRecord(report);

            for (auto b : senderRec) {
                wholePacket.push_back(b);
            }

            mSentReports.push_back(report);
            count++;
        }


        // set packet size field
        uint16_t ps = static_cast<std::uint16_t>(wholePacket.size());
        wholePacket[2] = static_cast<Byte>((ps & 0xFF00) >> 8);
        wholePacket[3] = static_cast<Byte>(ps & 0x00FF);

        if (count) {
            if (hasDescriptors) {
                mTimeDescriptorsSent = std::chrono::steady_clock::now();
            }
            mPackets.push(wholePacket);
            mSeq++;
        }

        return count;
    }

    void terminate() {
        screenPrinter->debug("PSK Reporter interface terminating");
        terminateFlag = true;
    }


    private:

        bool isSameBand(const FrequencyHz f1, const FrequencyHz f2) {
            int divisor = 1000000; // 1e6
            if (f1 <= 1000000 || f2 <= 1000000) {
                divisor = 100000; // 1e5
            }
            const int c1 = f1 / divisor;
            const int c2 = f2 / divisor;
            return c1 == c2;
        }

        void addHeaderToPacket(Packet& packet) {
            auto hdr = getHeader();
            for (auto p : hdr) {
                packet.push_back(p);
            }
        }

        void addDescriptorsToPacket(Packet& packet) {
            auto yy = getDescriptorReceiver();
            for (auto p : yy) {
                packet.push_back(p);
            }
            auto xx = getDescriptorSender(REPORT_TYPE::TYPE_HAS_LOCATOR);
            for (auto p : xx) {
                packet.push_back(p);
            }
            auto qq = getDescriptorSender(REPORT_TYPE::TYPE_NO_LOCATOR);
            for (auto p : qq) {
                packet.push_back(p);
            }
        }

        std::vector<std::uint8_t> getDescriptorReceiver() {
            std::vector<std::uint8_t> out = {
                0x00, 0x03, 0x00, 0x24, 0x99, 0x92, 0x00, 0x03, 0x00, 0x00,
                0x80, 0x02, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
                0x80, 0x04, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
                0x80, 0x08, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
                0x00, 0x00,
            };
            return out;
        }

        std::vector<std::uint8_t> getDescriptorSender(REPORT_TYPE reportType) {
            if (reportType == REPORT_TYPE::TYPE_HAS_LOCATOR) {
                // 3C is size
                return {
                    0x00, 0x02, 0x00, 0x3C, 0x64, 0xAF, 0x00, 0x07,
                    0x80, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x05, 0x00, 0x04, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x0A, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x03, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F, // locator
                    0x80, 0x0B, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
                    0x00, 0x96, 0x00, 0x04, // flow start seconds
                };
            }
            else
            {
                return {
                    // 2E is size
                    0x00, 0x02, 0x00, 0x2E, 0x62, 0xA7, 0x00, 0x06,
                    0x80, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x05, 0x00, 0x04, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x0A, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
                    0x80, 0x0B, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
                    0x00, 0x96, 0x00, 0x04, // flow start seconds
                };
            }
        }

        std::uint32_t mID; 
        std::uint32_t mSeq;

        bool terminateFlag;

        struct sockaddr_in Recv_addr;
        struct sockaddr_in Sender_addr;

        const int PORT = 4739;
        const int PORT_TEST = 14739;

        SOCKET mSocket;

        SafeQueue< Report > mReports;
        std::list< Report > mSentReports;

        std::thread mSendThread;

        const std::size_t MAX_REPORTS_PER_PACKET = 64;
        const std::int64_t MIN_SECONDS_BETWEEN_SAME_CALLSIGN_REPORTS = 121;

        std::chrono::time_point<std::chrono::steady_clock> mTimeDescriptorsSent;


        std::string mReceiverCallsign;
        std::string mReceiverLocator;

        std::string mProgramName;

        std::queue< Packet > mPackets;

        std::shared_ptr<ScreenPrinter> screenPrinter;

        const std::string SERVER_IP = "74.116.41.13";
};

}; // namespace

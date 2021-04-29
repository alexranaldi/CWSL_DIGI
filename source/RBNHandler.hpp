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
#include <queue>
#include <thread>
#include <random>
#include <iostream>
#include <string>

#include "SafeQueue.h"

using namespace std::literals; 

struct RBNReport
{
    std::string callsign;
    int32_t snr;
    uint32_t freq;
    uint32_t baseFreq;
    std::string locator;
    uint64_t epochTime;
    std::string message;
    std::string mode;
    RBNReport(
        const std::string& callsign_in,
        const int32_t snr_in,
        const uint32_t freq_in,
        const uint32_t baseFreq_in,
        const std::string locator_in,
        const uint64_t epochTime_in,
        const std::string message_in,
        const std::string mode_in
    ) :
    callsign(callsign_in),
    snr(snr_in),
    freq(freq_in),
    baseFreq(baseFreq_in),
    locator(locator_in),
    epochTime(epochTime_in),
    message(message_in),
    mode(mode_in) 
    { }
};

class RBNHandler {

using Packet = std::vector<std::uint8_t>;

public:
    RBNHandler() : 
    mSocket(INVALID_SOCKET),
    lastBaseFreq(0), 
    lastMode(""),
    terminateFlag(false)
    {

    }

    virtual ~RBNHandler() {

    }

    void handle(const std::uint32_t freq, const uint32_t baseFreq, const std::int32_t snr, const std::string& message, const std::string& mode) {
        RBNReport rep("", snr, freq, baseFreq, "", 0, message, mode);
        mReports.enqueue(rep);
    }

    bool init(const std::string& operatorCallsign, const std::string& operatorLocator, const std::string& programName, const std::string& ipAddr, int port) {

        mReceiverCallsign = operatorCallsign;
        mReceiverLocator = operatorLocator;
        mProgramName = programName;
        mServerIP = ipAddr;

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
        Recv_addr.sin_port = htons(port);
        int len = sizeof(struct sockaddr_in);
        Recv_addr.sin_addr.s_addr = inet_addr(mServerIP.c_str());

        mSendThread = std::thread(&RBNHandler::processingLoop, this);
        SetThreadPriority(mSendThread.native_handle(), THREAD_PRIORITY_IDLE);
        mSendThread.detach();

        return true;
    }

    void processingLoop() {
        while (!terminateFlag) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const auto reportCount = makePackets();
            while (!mPackets.empty() && !terminateFlag) {
                auto packet = mPackets.front();
                mPackets.pop();
                send(packet);
            }
        }
    }

    void send(const Packet& packet) {
        //std::cout << "Sending packet, num bytes: " << packet.size() << std::endl;
        sendto(mSocket, (const char*)packet.data(), (int)packet.size(), 0, (sockaddr*)& Recv_addr, sizeof(Recv_addr));
    }

    std::size_t makePackets() {

        std::size_t count = 0;

        while (!mReports.empty()) {

            auto report = mReports.dequeue();

            // first make status packet

            if (lastBaseFreq != report.baseFreq || lastMode != report.mode)
            {
                Packet statusPacket;

                addHeaderToPacket(statusPacket);

                std::vector<std::uint8_t> msg1 = { 0x00, 0x00, 0x00, 0x01 };  // Message number for status datagram
                for (auto b : msg1) {
                    statusPacket.push_back(b);
                }

                addStringToPacket(statusPacket, mProgramName);

                // Base frequency as 8 byte integer
                // upper 4
                addInt32ToPacket(statusPacket, 0x0);
                // lower 4
                addInt32ToPacket(statusPacket, report.baseFreq);

                addStringToPacket(statusPacket, report.mode); // Rx Mode
                addStringToPacket(statusPacket, report.callsign); // DX call - ignored by RBNA
                addStringToPacket(statusPacket, std::to_string(report.snr)); // SNR as string - ignored by RBNA
                addStringToPacket(statusPacket, report.mode); // Tx Mode - ignored by RBNA
                statusPacket.push_back(0x0); // TX enable = false - ignored by RBNA
                statusPacket.push_back(0x0); // Transmitting = false - ignorded by RBNA
                statusPacket.push_back(0x0); // Decoding = false - ignored by RBNA

                addInt32ToPacket(statusPacket, report.freq); // rxdf - ignored by RBNA
                addInt32ToPacket(statusPacket, report.freq); // txdf - ignored by  RBNA

                addStringToPacket(statusPacket, mReceiverCallsign); // DE call - ignored by RBNA
                addStringToPacket(statusPacket, mReceiverLocator); // DE grid - ignored by RBNA
                addStringToPacket(statusPacket, "AB12"); // DX grid - ignored by RBNA

                statusPacket.push_back(0x0); // TX watchdog = false - ignored by RBNA
                addStringToPacket(statusPacket, ""); // Submode - ignored by RBNA
                statusPacket.push_back(0x0); // Fast mode = false - ignored by RBNA
                statusPacket.push_back(0x0); // Special operation mode = 0 - ignored by RBNA

                mPackets.push(statusPacket);
                count++;

            }

            // second, make decode packet
            Packet decodePacket;

            addHeaderToPacket(decodePacket);
            std::vector<std::uint8_t> msg2 = { 0x00, 0x00, 0x00, 0x02 }; // Message number for decode datagram
            for (auto b : msg2) {
                decodePacket.push_back(b);
            }

            addStringToPacket(decodePacket, mProgramName); // Software ID - ignored by RBNA
            decodePacket.push_back(0x1); // New decode = true
            addInt32ToPacket(decodePacket, 0x0); // Time = zero - ignored by RBNA
            addInt32ToPacket(decodePacket, report.snr); // Report as 4 byte integer
            addDoubleToPacket(decodePacket, 0x0); // Delta time - ignored by RBNA
            uint32_t hz = report.freq - report.baseFreq; // Delta frequency for decode datagram

            addInt32ToPacket(decodePacket, hz); // Delta frequency in hertz - ignored by RBNA
            addStringToPacket(decodePacket, report.mode); // Receive mode - ignored by RBNA
            addStringToPacket(decodePacket, report.message); // message
            decodePacket.push_back(0x0); // Low-confidence = false
            decodePacket.push_back(0x0); // Off air = false - ignored by RBNA

            mPackets.push(decodePacket);
            count++;

            lastBaseFreq = report.baseFreq;
            lastMode = report.mode;
        }

        return count;

    }

    void terminate() {
        terminateFlag = true;
    }

    private:

        std::vector<std::uint8_t> getHeader() {
            return { 0xAD, 0xBC, 0xCB, 0xDA, 0x00, 0x00, 0x00, 0x02 };
        }

        void addHeaderToPacket(Packet& packet) {
            auto hdr = getHeader();
            //std::cout << "HEADER" << std::endl;
            for (auto p : hdr) {
                packet.push_back(p);
            }
        }

        void addStringToPacket(Packet& packet, const std::string& s) {
            addInt32ToPacket(packet, static_cast<std::uint32_t>(s.size()));
            for (char c : s) {
                packet.push_back(c);
            }
        }

        void addInt32ToPacket(Packet& packet, const uint32_t num) {
            packet.push_back(static_cast<std::uint8_t>((num & 0xFF000000) >> 24));
            packet.push_back(static_cast<std::uint8_t>((num & 0x00FF0000) >> 16));
            packet.push_back(static_cast<std::uint8_t>((num & 0x0000FF00) >> 8));
            packet.push_back(static_cast<std::uint8_t>((num & 0x000000FF) >> 0));
        }

        void addDoubleToPacket(Packet& packet, double d) {
            uint64_t *num = reinterpret_cast<uint64_t*>(&d);
            packet.push_back(static_cast<std::uint8_t>((*num & 0xFF00000000000000) >> 56));
            packet.push_back(static_cast<std::uint8_t>((*num & 0x00FF000000000000) >> 48));
            packet.push_back(static_cast<std::uint8_t>((*num & 0x0000FF0000000000) >> 40));
            packet.push_back(static_cast<std::uint8_t>((*num & 0x000000FF00000000) >> 32));
            packet.push_back(static_cast<std::uint8_t>((*num & 0x00000000FF000000) >> 24));
            packet.push_back(static_cast<std::uint8_t>((*num & 0x0000000000FF0000) >> 16));
            packet.push_back(static_cast<std::uint8_t>((*num & 0x000000000000FF00) >> 8));
            packet.push_back(static_cast<std::uint8_t>((*num & 0x00000000000000FF) >> 0));
        }

        struct sockaddr_in Recv_addr;
        struct sockaddr_in Sender_addr;

        SOCKET mSocket;

        SafeQueue< RBNReport > mReports;

        std::thread mSendThread;

        std::string mReceiverCallsign;
        std::string mReceiverLocator;

        std::string mProgramName;

        std::queue< Packet > mPackets;

        std::string mMode;

        std::string mServerIP;

        uint32_t lastBaseFreq;
        std::string lastMode;

        bool terminateFlag;
};
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

#include <iomanip>
#include <ios>

#include "WSPRNet.hpp"
#include "ScreenPrinter.hpp"


    WSPRNet::WSPRNet(const std::string& grid, std::shared_ptr<ScreenPrinter> sp ) :
        operatorGrid(grid),
        screenPrinter(sp),
        mCountSendsErrored(0),
        mCountSendsOK(0),
        terminateFlag(false) {
        }

    WSPRNet::~WSPRNet() {
            WSACleanup();
        }

    bool WSPRNet::init() {

        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != NO_ERROR) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }


        mSendThread = std::thread(&WSPRNet::processingLoop, this);
        SetThreadPriority(mSendThread.native_handle(), THREAD_PRIORITY_IDLE);
        mSendThread.detach();
        return true;
    }

    void WSPRNet::terminate() {
        screenPrinter->debug("WSPRNet interface terminating");
        terminateFlag = true;
    }

    void WSPRNet::handle(const std::string& callsign, const std::string& mode, const int32_t snr, const float dt, const std::int16_t drift, const std::int16_t dbm, const uint32_t freq, const uint32_t rf, const uint64_t epochTime, const std::string& grid, const std::string& reporterCallsign) {

        wspr::Report rep;
        rep.callsign = callsign;

        // Mapping comes from:
        //  http://www.wsprnet.org/drupal/node/8983
        //  and WSJT-X source code, 2.6.0

        // WSPR-15      ->      15 (unsupported here)
        // WSPR         ->      2
        // FST4W-120    ->      3
        // FST4W-300    ->      5
        // FST4W-900    ->      15   
        // FST4W-1800   ->      30   

        if (mode == "WSPR") {
            rep.mode = 2;
        }
        else if (mode == "FST4W-120") {
            rep.mode = 3;
        }
        else if (mode == "FST4W-300") {
            rep.mode = 5;
        }
        else if (mode == "FST4W-900") {
            rep.mode = 16;
        }
        else if (mode == "FST4W-1800") {
            rep.mode = 30;
        }
        else {
            screenPrinter->err("Unsupported mode for WSPRNet: " + mode + " so not sending report to WSPRNet");
            return;
        }

        rep.snr = snr;
        rep.freq = freq;
        rep.locator = grid;
        rep.epochTime = epochTime;
        rep.dt = dt;
        rep.drift = drift;
        rep.recvfreq = rf;
        rep.dbm = dbm;
        rep.reporterCallsign = reporterCallsign;
 
        mReports.enqueue(rep);
    }

    bool WSPRNet::isConnected() {
        int error = 0;
        int len = sizeof(error);
        int retval = getsockopt(mSocket, SOL_SOCKET, SO_ERROR, (char*)&error, &len);

        if (retval != 0) {
            return false;
        }

        if (error != 0) {
            return false;
        }

        return true;
    }

    bool WSPRNet::closeSocket() {
        int closeStatus = closesocket(mSocket);
        if (0 != closeStatus) {
            screenPrinter->print("Error closing socket", LOG_LEVEL::ERR);
            return false;
        }
        return true;
    }

    bool WSPRNet::connectSocket() {

        struct addrinfo* addrs;
        struct addrinfo hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        DWORD dwRetval = getaddrinfo(SERVER_HOSTNAME.c_str(), portStr.c_str(), &hints, &addrs);
        if (dwRetval != 0) {
            screenPrinter->err("WSPRNet getaddrinfo (DNS Lookup) failed looking up host: " + SERVER_HOSTNAME + " at port: " + portStr);
            return false;
        }

        for (struct addrinfo* addr = addrs; addr != NULL; addr = addr->ai_next)
        {
            if (addr->ai_addr->sa_family == hints.ai_family && addr->ai_protocol == hints.ai_protocol && addr->ai_socktype == hints.ai_socktype) {

                mSocket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
                if (mSocket == INVALID_SOCKET) {
                    screenPrinter->err("WSPRNet socket creation failed with error: " + GetSocketError());
                    break; // fall through to return false
                }

                if (0 == connect(mSocket, addr->ai_addr, addr->ai_addrlen)) {
                    screenPrinter->debug("WSPRNet socket connection established");
                    return true;
                }
            }
        }
        // if reached, connect operation failed
        freeaddrinfo(addrs);
        screenPrinter->print("Socket connect failed with error: " + GetSocketError(), LOG_LEVEL::ERR);
        screenPrinter->print("Error connecting to WSPRNet", LOG_LEVEL::ERR);

        return false;
    }

    void WSPRNet::sendReportWrapper(const wspr::Report& report) {
        const bool status = sendReport(report);
        if (status) {
            mCountSendsOK++;
        }
        else {
            mCountSendsErrored++;
            screenPrinter->err("Failed to send WSPR report to WSPRNet");
        }
    }

    bool WSPRNet::sendReport(const wspr::Report& report) {

        std::ostringstream all;
        std::ostringstream content;
        std::ostringstream header;
        std::ostringstream request;

        content << "function=" << "wspr" << "&";
        content << "rcall=" << report.reporterCallsign << "&";
        content << "rgrid=" << operatorGrid << "&";

        // frequency in MHz
        const float rfreqf = static_cast<float>(report.recvfreq) / static_cast<float>(1000000.0);
        content << "rqrg=" << std::fixed << std::setprecision(6) << rfreqf << "&";

        // date, yymmdd utc
        content << "date=";
        std::chrono::system_clock::time_point tp{ std::chrono::seconds{report.epochTime} };
        time_t tt = std::chrono::system_clock::to_time_t(tp);
        tm utc = *gmtime(&tt);

        content << std::setfill('0') << std::setw(2) << utc.tm_year - 100;
        content << std::setfill('0') << std::setw(2) << utc.tm_mon + 1; // tm_mon is 0-based
        content << std::setfill('0') << std::setw(2) << utc.tm_mday;
        content << "&";

        // time, hhmm utc
        content << "time=";
        content << std::setfill('0') << std::setw(2) << utc.tm_hour;
        content << std::setfill('0') << std::setw(2) << utc.tm_min;
        content << "&";

        // snr
        content << "sig=" << report.snr << "&";

        // dt
        content << "dt=" << std::setprecision(2) << report.dt << "&";

        // drift
        content << "drift=" << report.drift << "&";

        content << "tcall=" << report.callsign << "&";

        content << "tgrid=" << report.locator << "&";

        const float freqf = static_cast<float>(report.freq) / static_cast<float>(1000000.0);
        content << "tqrg=" << std::fixed << std::setprecision(6) << freqf << "&";

        content << "dbm=" << report.dbm << "&";

        content << "version=" << PROGRAM_NAME << " " << PROGRAM_VERSION << "&";

        // WSPR-15      ->      15 (unsupported here)
        // WSPR         ->      2
        // FST4W-120    ->      3
        // FST4W-300    ->      5
        // FST4W-900    ->      15   
        // FST4W-1800   ->      30   

        content << "mode=" << report.mode;

        screenPrinter->debug("WSPR Message content: " + content.str());

        // http request line
        request << "POST /post? HTTP/1.1\r\n";

        // header
        header << "Connection: Keep-Alive\r\n";
        header << "Host: wsprnet.org\r\n";
        header << "Content-Type: application/x-www-form-urlencoded\r\n";
        content.seekp(0, ios::end);
        const auto contentLength = content.tellp();
        content.seekp(0, ios::beg);
        screenPrinter->debug("content length: " + std::to_string(contentLength));
        header << "Content-Length: " << std::to_string(contentLength) << "\r\n";
        header << "Accept-Language: en-US,*\r\n";
        header << "User-Agent: Mozilla/5.0\r\n";

        all << header.str();
        all << "\r\n"; // blank line between headers and body
        all << content.str();

        bool sendSuccess = sendMessageWithRetry(request.str());
        if (!sendSuccess) {
            screenPrinter->debug("Failed to send data to WSPRNet");
            return false;
        }

        sendSuccess = sendMessageWithRetry(all.str());
        if (!sendSuccess) {
            screenPrinter->debug("Failed to send data to WSPRNet");
            return false;
        }

        int numTries= 0;
        std::string response = "";
        do {
            if (numTries) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(333));
            }
            numTries++;
            screenPrinter->debug("WSPRNet attempting read, try: " + std::to_string(numTries));
            response += readMessage();
            screenPrinter->debug("WSPRNet read message of size: " + std::to_string(response.size()) + "message: " + response);
        }
        while (response.empty() && numTries <= 3);

        if (response.size()) {
            screenPrinter->debug("WSPRNet received response: " + response);
        }
        else {
            screenPrinter->debug("WSPRNet No response received, giving up!");
      //      closeSocket();
            return false;
        }

    //    closeSocket();
        return true;
    }

    bool WSPRNet::sendMessageWithRetry(const std::string& message) {

        int tries = 0;
        bool sendSuccess = false;
        while (tries <= 2) {
            ++tries;
            screenPrinter->debug("Sending message: " + message);
            const int bytesSent = sendMessage(message);
            screenPrinter->debug("sent bytes: " + std::to_string(bytesSent)  +" message size: " + std::to_string(message.size()));
            if (bytesSent == message.size()) {
                screenPrinter->debug("message send success!");
                sendSuccess = true;
                break; // success!
            }
            else if (bytesSent == SOCKET_ERROR) {
                return false;
            }
        }
        return sendSuccess;
    }

    int WSPRNet::sendMessage(const std::string& message) {
        int total = static_cast<int>(message.size());
        int sent = 0;
        do {
            int bytes = send(mSocket, message.c_str() + sent, total - sent, NULL);
            screenPrinter->debug("send() call resulted in value: " + std::to_string(bytes));
            if (bytes == SOCKET_ERROR) {
                return SOCKET_ERROR;
            }
            sent += bytes;
            if (sent < total) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } while (sent < total);

        return sent;
    }

    std::string WSPRNet::readMessage() {
        int received = 0;
        std::string message = "";
        int bytes = 0;
        char buf[8192] = {0};
        bytes = recv(mSocket, buf, 8192, NULL);
        screenPrinter->debug("recv() call yielded " + std::to_string(bytes) + " bytes");
        if (bytes > 0) {
            message.append(buf);
        }
        return message;
    }

    void WSPRNet::processingLoop() {
        while (!terminateFlag) {
            screenPrinter->debug("Reports in send queue: " + std::to_string(mReports.size()));

            while (!mReports.empty()) {
                const bool connectStatus = connectSocket();
                if (!connectStatus) 
                { 
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue; 
                }
                auto report = mReports.dequeue();
                sendReportWrapper(report);
                closeSocket();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            if (terminateFlag) { return; }

            reportStats();
        }
    }

    void WSPRNet::reportStats() {
        screenPrinter->debug("Count of successful reports to WSPRNet: " + std::to_string(mCountSendsOK));
        screenPrinter->debug("Count of failed reports to WSPRNet: " + std::to_string(mCountSendsErrored));
    }

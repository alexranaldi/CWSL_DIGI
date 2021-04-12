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

#include "PSKReporter.hpp"
#include "RBNHandler.hpp"
#include "WSPRNet.hpp"

#include "SafeQueue.h"

struct JT9Output
{
    std::string output = "";
    std::string mode = "";
    uint64_t epochTime = 0;
    FrequencyHz baseFreq = 0;
    JT9Output(
        const std::string& rawOutputIn,
        const std::string& modeIn,
        const uint64_t epochTimeIn,
        const FrequencyHz baseFreqIn) :
        output(rawOutputIn),
        mode(modeIn),
        epochTime(epochTimeIn),
        baseFreq(baseFreqIn)
        {}
};

class OutputHandler {

public:

    OutputHandler(
        const bool printHandledReportsIn, 
        const std::string& badMessageLogFileIn, 
        const std::string& decodesFileNameIn, 
        std::shared_ptr<ScreenPrinter> sp) :
        screenPrinter(sp),
        reporter(nullptr),
        wsprNet(nullptr),
        badMessageLogFile(badMessageLogFileIn),
        rbn(nullptr),
        rbnPort(0),
        decodesFileName(decodesFileNameIn),
        useDecodesFile(false),
        printHandledReports(printHandledReportsIn) {
        if (decodesFileName != "") {
            useDecodesFile = true;
            ofs.open(decodesFileName, std::ios_base::app | std::ofstream::out);
    }

    }

    virtual ~OutputHandler() {
        if (useDecodesFile) {
            ofs.close();
        }
    }

    void setRBNHandler(std::shared_ptr<RBNHandler> rep) {
        rbn = rep;
    }

    void setPSKReporter(std::shared_ptr<pskreporter::PSKReporter> rep) {
        reporter = rep;
    }

    void setWSPRNet(std::shared_ptr<WSPRNet> wn) {
        wsprNet = wn;
    }

    // TODO: make this a separate thread some day?
    void handle(const JT9Output& output) {
        if (output.mode == "FT4" || output.mode == "FT8") {
            parseOutputFT4FT8(output.output, output.epochTime, output.mode, output.baseFreq);
        }
        else if (output.mode == "WSPR") {
            parseOutputWSPR(output.output, output.epochTime, output.baseFreq);
        }
    }

    void parseOutputWSPR(const std::string& out, const uint64_t epochTime, const FrequencyHz baseFreq) {
       // const uint64_t epochTimeCorrected = epochTime + static_cast<uint64_t>(WSPR_PERIOD);
        screenPrinter->print("Handling WSPR output", LOG_LEVEL::DEBUG);

        //  01234567890123456
        //  4dab -25 -0.1
        //  9550  -0  0.3   0.001549  0  W8EDU EN91 23
        //  9550   8  0.1   0.001574  0  W9HZ FN20 33
        //  6a80 -20  0.2   0.001478  0 <G0FCA> IO83UQ 30
        std::istringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            trim(line);
            std::vector<std::string> lineVec = splitStringByDelim(line, ' ', true);

            screenPrinter->print("Number of entities in WSPR line: " + std::to_string(lineVec.size()), LOG_LEVEL::TRACE);
            if (lineVec.size() != 8) {
                continue;
            }

            // snr is 5-7
            std::string snr = lineVec[1];
            screenPrinter->print("WSPR  snr: " + snr, LOG_LEVEL::TRACE);
            trim(snr);

            // dt is 9-12
            std::string dt = lineVec[2];
            trim(dt);
            screenPrinter->print("WSPR  dt: " + dt, LOG_LEVEL::TRACE);

            // freq
            std::string freq = lineVec[3];
            trim(freq);
            screenPrinter->print("WSPR  freq: " + freq, LOG_LEVEL::TRACE);

            // drift, 25-26
            std::string drift = lineVec[4];
            trim(drift);
            screenPrinter->print("WSPR  drift: " + drift, LOG_LEVEL::TRACE);

            std::string call = lineVec[5];
            trim(call);
            screenPrinter->print("WSPR  call: " + call, LOG_LEVEL::TRACE);

            // locator 
            std::string locator = lineVec[6];
            trim(locator);
            screenPrinter->print("WSPR  locator: " + locator, LOG_LEVEL::TRACE);

            // dbm 
            std::string dbm = lineVec[7];
            trim(dbm);
            screenPrinter->print("WSPR  dbm: " + dbm, LOG_LEVEL::TRACE);

            double freqHz = static_cast<double>(baseFreq);
            double freqReported = std::stod(freq) * 1000000.0;
            freqHz += freqReported;
            uint32_t freqInt = static_cast<uint32_t>(freqHz);
            //  std::cout << "CALL: " << call << std::endl;
            //  std::cout << "SNR: " << snr << std::endl;
            //  std::cout << "FREQ: " << freqInt << std::endl;
            //  std::cout << "LOCATOR: " << locator << std::endl;

            std::ostringstream s;
            s << std::setw(4) << "WSPR" << std::setw(12) << epochTime << std::setw(12) << freqInt << std::setw(5) << snr << std::setw(12) << call << std::setw(6) << locator;
            
            if (useDecodesFile) {
                ofs << s.str() << std::endl;
            }
            if (printHandledReports) {
                screenPrinter->print(s.str(), LOG_LEVEL::INFO);
            }
            
            if (reporter) {
                reporter->handle(call, std::stoi(snr), freqInt, epochTime, "WSPR");
            }
            if (wsprNet) {
                wsprNet->handle(call, std::stoi(snr), std::stof(dt), std::stoi(drift), std::stoi(dbm), freqInt, baseFreq, epochTime, locator);
            }
        }
    }

    void parseOutputFT4FT8(std::string out, uint64_t epochTime, std::string mode, const FrequencyHz baseFreq) {

        std::istringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            trim(line);

            if (line.find("DecodeFinished") != string::npos) {
                continue;
            }

            if (line.length() <= 28) {
                screenPrinter->debug("Skipping line because less than 28 characters: " + line);
                continue;
            }

            if (line.at(6) != ' ') {
                screenPrinter->debug("Skipping line because character 6 is not a space: " + line);
                continue;
            }

            // snr is 7-9
            std::string snr = line.substr(7, 3);
            trim(snr);
            if (line.at(10) != ' ') {
                screenPrinter->debug("Skipping line because character 10 is not a space: " + line);
                continue;
            }

            // dt is 11,12,13,14
            std::string dt = line.substr(11, 4);
            trim(dt);

            // 15 is always a space
            if (line.at(15) != ' ') {
                screenPrinter->debug("Skipping line because character 15 is not a space: " + line);
                continue;
            }

            // freq 16-19
            std::string freq = line.substr(16, 4);
            trim(freq);

            // 20 is always a space
            if (line.at(20) != ' ') {
                screenPrinter->debug("Skipping line because character 20 is not a space: " + line);
                continue;
            }

            // 21 is always a ~ or a +, depending on mode
            if (line.at(21) != '~' && line.at(21) != '+') {
                screenPrinter->debug("Skipping line because character 21 is not a ~ or +: " + line);
                continue;
            }

            // 22 always space
            if (line.at(22) != ' ') {
                screenPrinter->debug("Skipping line because character 22 is not a space: " + line);
                continue;
            }

            // 23 always space
            if (line.at(23) != ' ') {
                screenPrinter->debug("Skipping line because character 23 is not a space: " + line);
                continue;
            }

            // msg is 24-end
            std::string msg = line.substr(24, line.size() - 24);
            trim(msg);

            //  double actualFreq = (std::stod(freq) + static_cast<double>(ssbFreq)) * freqCal;
            const double actualFreq = std::stod(freq) + static_cast<double>(baseFreq);
            handleMessageFT4FT8(std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, epochTime, mode, baseFreq);
        }
    }

    bool handleMessageFT4FT8(const int32_t snr, const uint32_t freq, std::string msg, const uint64_t epochTime, const std::string& mode, const FrequencyHz baseFreq) {
        std::ostringstream s;
        s << std::setw(4) << mode << std::setw(12) << epochTime << std::setw(12) << freq << std::setw(5) << snr << "  " << std::left << std::setw(52) << msg;
        const std::string msgOutput = s.str();
        if (printHandledReports) {
            screenPrinter->print(msgOutput, LOG_LEVEL::INFO);
        }
        if (useDecodesFile) {
            ofs << msgOutput << std::endl;
        }
        if (rbn) {
            rbn->handle(freq, baseFreq, snr, msg, mode);
        }
        // look for '?' and chop it, and anything after
        size_t qPos = msg.find("?");
        if (qPos) {
            msg = msg.substr(0, qPos);
            trim(msg);
        }
        // look for 'a1' and chop it, and anything after
        size_t a1Pos = msg.find("a1");
        if (a1Pos) {
            msg = msg.substr(0, a1Pos);
            trim(msg);
        }

        std::vector<size_t> spaces;
        for (int k = 0; k < msg.length(); ++k) {
            if (msg.at(k) == ' ') {
                spaces.push_back(k);
            }
        }

        const std::size_t numSpaces = spaces.size();
        const bool isCQ = (msg.at(0) == 'C') & (msg.at(1) == 'Q');
        if (isCQ && numSpaces == 1 && msg.at(2) == ' ') {
            // CQ CALL
            std::string call = msg.substr(3, msg.length() - 3);
            if (reporter) {
                reporter->handle(call, snr, freq, epochTime, mode);
            }
            return true;
        }
        if (isCQ && numSpaces == 2) {
            std::string first = msg.substr(spaces[0] + 1, spaces[1] - spaces[0]);
            std::string second = msg.substr(spaces[1] + 1, msg.length() - spaces[1] + 1);
            if (isValidLocator(second)) {
                // CQ CALL GRID
                if (reporter) {
                    reporter->handle(first, snr, freq, second, epochTime, mode);
                }
            }
            else {
                // CQ SOMETHING CALL
                if (reporter) {
                    reporter->handle(first, snr, freq, epochTime, mode);
                }
            }
            return true;
        }
        else if (isCQ && numSpaces == 3) {
            // CQ SOMETHING CALL GRID
            std::string call = msg.substr(spaces[1] + 1, spaces[2] - spaces[1]);
            std::string loc = msg.substr(spaces[2] + 1, msg.length() - spaces[2] + 1);
            if (isValidLocator(loc)) {
                if (reporter) {
                    reporter->handle(call, snr, freq, loc, epochTime, mode);
                }
            }
            else {
                logBadMessage("Full msg with bad locator: " + msg);
                if (reporter) {
                    reporter->handle(call, snr, freq, epochTime, mode);
                }
            }
            return true;
        }
        else if (!isCQ) {
            if (numSpaces == 2) {
                // CALL CALL REP
                // CALL CALL 73
                // CALL CALL GRID
                std::string tx_call = msg.substr(spaces[0] + 1, spaces[1] - spaces[0]);
                if (reporter) {
                    reporter->handle(tx_call, snr, freq, epochTime, mode);
                }
                return true;
            }
        }
        else {
            logBadMessage("Message not handled: " + msg);
            logBadMessage("isCQ=" + std::to_string(isCQ));
            logBadMessage("numSpaces=" + std::to_string(spaces.size()));

        }

        return false;
    }

    void logBadMessage(const std::string errMsg) {
        std::ofstream ofs(badMessageLogFile, std::ios_base::app | std::ofstream::out);
        ofs << errMsg << '\r\n';
        ofs.close();
    }

private:
    int rbnPort;

    std::shared_ptr<ScreenPrinter> screenPrinter;
    std::shared_ptr<RBNHandler> rbn;

    std::shared_ptr<pskreporter::PSKReporter> reporter;
    std::shared_ptr<WSPRNet> wsprNet;

    SafeQueue< JT9Output > items;

    bool useDecodesFile;
    std::string decodesFileName;

    std::string badMessageLogFile;
    bool printHandledReports;
    std::ofstream ofs;
};
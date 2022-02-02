/*
Copyright 2022 Alexander Ranaldi
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


#include "OutputHandler.hpp"

#include "PSKReporter.hpp"
#include "RBNHandler.hpp"
#include "WSPRNet.hpp"
#include "Stats.hpp"
#include "StringUtils.hpp"
#include "HamUtils.hpp"
#include "Instance.hpp"
#include "Decoder.hpp"

OutputHandler::OutputHandler(
        const bool printHandledReportsIn, 
        const std::string& badMessageLogFileIn, 
        const std::string& decodesFileNameIn, 
        std::shared_ptr<ScreenPrinter> sp,
        std::shared_ptr<Stats> statsHandler_In,
        std::vector<Decoder>& instances_In) :
        screenPrinter(sp),
        reporter(nullptr),
        wsprNet(nullptr),
        badMessageLogFile(badMessageLogFileIn),
        rbn(nullptr),
        rbnPort(0),
        decodesFileName(decodesFileNameIn),
        useDecodesFile(false),
        statsHandler(statsHandler_In),
        printHandledReports(printHandledReportsIn),
        ignoredCallsigns(),
        instances(instances_In)
       {
        if (decodesFileName != "") {
            useDecodesFile = true;
            ofs.open(decodesFileName, std::ios_base::app | std::ofstream::out);
        }
        processingThread = std::thread(&OutputHandler::processingLoop, this);
        SetThreadPriority(processingThread.native_handle(), THREAD_PRIORITY_LOWEST);
        processingThread.detach();
    }

    OutputHandler::~OutputHandler() {
        if (useDecodesFile) {
            ofs.close();
        }
    }

    void OutputHandler::setRBNHandler(std::shared_ptr<RBNHandler> rep) {
        rbn = rep;
    }

    void OutputHandler::setPSKReporter(std::shared_ptr<pskreporter::PSKReporter> rep) {
        reporter = rep;
    }

    void OutputHandler::setWSPRNet(std::shared_ptr<WSPRNet> wn) {
        wsprNet = wn;
    }

    void OutputHandler::processingLoop() {
        while (1) {
            screenPrinter->debug("OutputHandler waiting for item");
            JT9Output item = items.dequeue();
            screenPrinter->debug("OutputHandler got an item");

            if (item.mode == "FT4" || item.mode == "FT8") {
                try {
                    parseOutputFT4FT8(item.output, item.epochTime, item.mode, item.baseFreq, item.instanceId);
                }
                catch (const std::exception& e) {
                    screenPrinter->print("parseOutputFT4FT8 call", e);
                }
            }
            else if (item.mode == "WSPR") {
                try {
                    parseOutputWSPR(item.output, item.epochTime, item.baseFreq, item.instanceId);
                }
                catch (const std::exception& e) {
                    screenPrinter->print("parseOutputWSPR call", e);
                }
            }
            else if (item.mode == "Q65-30") {
                try {
                    parseOutputQ65(item);
                }
                catch (const std::exception& e) {
                    screenPrinter->print("parseOutputQ65 call", e);
                }
            }
            else if (item.mode == "JT65") {
                try {
                    parseOutputJT65(item);
                }
                catch (const std::exception& e) {
                    screenPrinter->print("parseOutputQ65 call", e);
                }
            }
            else {
                screenPrinter->err("UNKNOWN MODE: " + item.mode);
            }
        }
    }

    // TODO: make this a separate thread some day?
    void OutputHandler::handle(const JT9Output& output) {
        items.enqueue(output);
    }

    void OutputHandler::parseOutputWSPR(const std::string& out, const uint64_t epochTime, const FrequencyHz baseFreq, const std::size_t instanceId) {
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

            //screenPrinter->print("Number of entities in WSPR line: " + std::to_string(lineVec.size()), LOG_LEVEL::TRACE);
            if (lineVec.size() != 8) {
                continue;
            }

            // snr is 5-7
            std::string snr = lineVec[1];
      //      screenPrinter->print("WSPR  snr: " + snr, LOG_LEVEL::TRACE);
            trim(snr);

            // dt is 9-12
            std::string dt = lineVec[2];
            trim(dt);
       //     screenPrinter->print("WSPR  dt: " + dt, LOG_LEVEL::TRACE);

            // freq
            std::string freq = lineVec[3];
            trim(freq);
      //      screenPrinter->print("WSPR  freq: " + freq, LOG_LEVEL::TRACE);

            // drift, 25-26
            std::string drift = lineVec[4];
            trim(drift);
     //       screenPrinter->print("WSPR  drift: " + drift, LOG_LEVEL::TRACE);

            std::string call = lineVec[5];
            trim(call);
            if (!checkCall(call)) {
                screenPrinter->debug("Callsign failed validation or was ignored: " + call);
                return;
            }

      //      screenPrinter->print("WSPR  call: " + call, LOG_LEVEL::TRACE);

            // locator 
            std::string locator = lineVec[6];
            trim(locator);
      //      screenPrinter->print("WSPR  locator: " + locator, LOG_LEVEL::TRACE);

            // dbm 
            std::string dbm = lineVec[7];
            trim(dbm);
       //     screenPrinter->print("WSPR  dbm: " + dbm, LOG_LEVEL::TRACE);

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
                const std::string reporterCall = instances[instanceId].getReporterCallsign();
                wsprNet->handle(call, std::stoi(snr), std::stof(dt), std::stoi(drift), std::stoi(dbm), freqInt, baseFreq, epochTime, locator, reporterCall);
            }
            statsHandler->handleReport(instanceId, epochTime * 1000);
        }
    }

    void OutputHandler::parseOutputFT4FT8(const std::string out, const uint64_t epochTime, const std::string mode, const FrequencyHz baseFreq, const std::size_t instanceId) {

        std::istringstream ss(out);
        std::string line = "";
        while (std::getline(ss, line)) {
            trim(line);

            if (line.find("DecodeFinished") != std::string::npos) {
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
            bool success = false;
            try {
                success = handleMessageUniversal(std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, epochTime, mode, baseFreq, std::stof(dt));
            }
            catch (const std::exception& e) {
                screenPrinter->print("handleMessageUniversal exception: ", e);
                success = false;
            }
            if (success) {
                statsHandler->handleReport(instanceId, epochTime * 1000);
            }
            else {
                screenPrinter->debug("Failed to handle message: " + msg);
            }
        }
    }

    void OutputHandler::parseOutputJT65(const JT9Output& output) {

        std::istringstream ss(output.output);
        std::string line;
        while (std::getline(ss, line)) {
            trim(line);

            if (line.find("DecodeFinished") != string::npos) {
                continue;
            }

            if (line.length() <= 27) {
                screenPrinter->debug("Skipping line because less than 27 characters: " + line);
                continue;
            }

            if (line.at(4) != ' ') {
                screenPrinter->debug("Skipping line because character 4 is not a space: " + line);
                continue;
            }

            // snr is 5-7
            std::string snr = line.substr(5, 3);
            trim(snr);

            if (line.at(8) != ' ') {
                screenPrinter->debug("Skipping line because character 8 is not a space: " + line);
                continue;
            }

            // dt is 9,10,11,12
            std::string dt = line.substr(9, 4);
            trim(dt);

            // 13 is always a space
            if (line.at(13) != ' ') {
                screenPrinter->debug("Skipping line because character 13 is not a space: " + line);
                continue;
            }

            // freq 14-17
            std::string freq = line.substr(14, 4);
            trim(freq);

            // 18 is always a space
            if (line.at(20) != ' ') {
                screenPrinter->debug("Skipping line because character 20 is not a space: " + line);
                continue;
            }

            // 19 is always #
            if (line.at(19) != '#' && line.at(20) != ' ') {
                screenPrinter->debug("Skipping line because character 19 is not a # and/or 20 is not a space: " + line);
                continue;
            }

            // msg is 22-end
            std::string msg = line.substr(22, 9999999);
            trim(msg);

            const double actualFreq = std::stod(freq) + static_cast<double>(output.baseFreq);
          //  const auto success = handleMessageJT65(std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, output.epochTime, output.mode, output.baseFreq, std::stof(dt));
            const auto success = handleMessageUniversal(std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, output.epochTime, output.mode, output.baseFreq, std::stof(dt));
            if (success) {
                statsHandler->handleReport(output.instanceId, output.epochTime * 1000);
            }
            else {
                screenPrinter->debug("Failed to handle message: " + msg);
            }
        }
    }

    void OutputHandler::parseOutputQ65(const JT9Output& output) {

        std::istringstream ss(output.output);
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

            // 21 is always :
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

            const double actualFreq = std::stod(freq) + static_cast<double>(output.baseFreq);
            const auto success = handleMessageUniversal(std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, output.epochTime, output.mode, output.baseFreq, std::stof(dt));
            if (success) {
                statsHandler->handleReport(output.instanceId, output.epochTime * 1000);
            }
            else {
                screenPrinter->debug("Failed to handle message: " + msg);
            }
        }
    }

    void OutputHandler::logBadMessage(const std::string badMsg) {
        screenPrinter->print("Bad message: " + badMsg);
        std::ofstream ofs(badMessageLogFile, std::ios_base::app | std::ofstream::out);
        ofs << badMsg << std::endl;
        ofs.close();
    }

    bool OutputHandler::checkCall(const std::string& call) {
        std::string test = call;
        trim(test);

        if (test.size() < 3) {
            return false;
        }
        
        // If callsign ends in \R it is likely invalid
        if ( test.at(test.length()-1) == 'R' && test.at(test.length()-2) == '\\' ) {
            return false;
        }

        if (isCallsignIgnored(call)) {
            screenPrinter->info("Ignoring callsign: " + call);
            return false;
        }

        // Callsign OK
        return true;
    }

    void OutputHandler::ignoreCallsign(const std::string& call) {
        ignoredCallsigns.push_back(call);
    }

    bool OutputHandler::isCallsignIgnored(const std::string& test) {
        for (const auto& call : ignoredCallsigns) {
            if (call == test) {
                return true;
            }
        }
        return false;
    }

    bool OutputHandler::handleMessageUniversal(
        const int32_t snr, 
        const uint32_t freq, 
        std::string msg, 
        const uint64_t epochTime, 
        const std::string& mode, 
        const FrequencyHz baseFreq, 
        const float dt) {
        std::ostringstream s;
        s << std::setw(4) << mode << std::setw(12) << epochTime << std::setw(12) << freq << std::setw(5) << snr << "  " << std::setw(5) << dt << "  " << std::left << std::setw(52) << msg;
        const std::string msgOutput = s.str();

        if (printHandledReports) {
            screenPrinter->print(msgOutput, LOG_LEVEL::INFO);
        }
        if (useDecodesFile) {
            ofs << msgOutput << std::endl;
        }

        // Let RBN handle messages on its own - no preprocessing/checking, but only FT4/FT8
        if ((mode == "FT8" || mode == "FT4") && rbn) {
            rbn->handle(freq, baseFreq, snr, msg, mode);
        }

        //screenPrinter->debug("handling message=" + msg);

        trim(msg);

        // look for and chop any error flags and anything after
        std::vector<std::string> chop = { "?","a1","a2","q0","q1","q2","q3","q4","q5" };
        for (auto& c : chop) {
            size_t qPos = msg.find(c);
            if (qPos != std::string::npos) {
                msg = msg.substr(0, qPos);
                trim(msg);
            }
        }

        //screenPrinter->debug("handling message post chop and trim=" + msg);


        if (msg.length() < 6) {
            logBadMessage("Message not handled -- too short: " + msg);
            return false;
        }

        std::vector<size_t> spaces;
        spaces.reserve(16); // don't set initial size, we check size later
        for (size_t k = 0; k < msg.length(); ++k) {
            if (msg.at(k) == ' ') {
                spaces.push_back(k);
            }
        }

        const std::size_t numSpaces = spaces.size();
        //screenPrinter->debug("Number of spaces in message: " + std::to_string(numSpaces));

        if (0 == numSpaces) {
            logBadMessage("Message not handled -- no spaces: " + msg);
            return false;
        }

        const bool isCQ = msg.at(0) == 'C' && msg.at(1) == 'Q';

        //screenPrinter->debug("isCQ=" + std::to_string(isCQ));


        if (isCQ && numSpaces == 1 && msg.at(2) == ' ') {
            // CQ CALL
            std::string call = msg.substr(3, msg.length() - 3);
            if (checkCall(call)) {
                if (reporter) {
                    reporter->handle(call, snr, freq, epochTime, mode);
                }
                return true;
            }
            else {
                screenPrinter->debug("Callsign failed validation or was ignored: " + call);
            }
        }
        else if (isCQ && numSpaces == 2) {
            std::string first = msg.substr(spaces[0] + 1, (spaces[1] - spaces[0] - 1));
            std::string second = msg.substr(spaces[1] + 1, msg.length() - spaces[1] + 1);
            if (checkCall(first)) {
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
            else {
                screenPrinter->debug("Callsign failed validation or was ignored:" + first);
            }
        }
        else if (isCQ && numSpaces == 3) {
            // CQ SOMETHING CALL GRID
            std::string call = msg.substr(spaces[1] + 1, (spaces[2] - spaces[1]) - 1);
            std::string loc = msg.substr(spaces[2] + 1, msg.length() - spaces[2] + 1);
            if (checkCall(call) && isValidLocator(loc)) {
                if (reporter) {
                    reporter->handle(call, snr, freq, loc, epochTime, mode);
                }
                return true;
            }
            else {
                logBadMessage("Full msg with bad call or locator: " + msg);
            }
        }
        else if (!isCQ) {
            if (numSpaces == 2) {
                // CALL CALL REP
                // CALL CALL 73
                // CALL CALL GRID
                std::string tx_call = msg.substr(spaces[0] + 1, spaces[1] - spaces[0] - 1);
                if (checkCall(tx_call)) {
                    if (reporter) {
                        reporter->handle(tx_call, snr, freq, epochTime, mode);
                    }
                    return true;
                }
                else {
                    screenPrinter->debug("Callsign failed validation or was ignored: " + tx_call);
                }
            }
        }
        logBadMessage("Message not handled: " + msg);
        return false;
    }


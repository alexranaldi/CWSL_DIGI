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
#include "decodedtext.h"
#include "varicode.h"

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

            if (item.mode == "JS8") {
                screenPrinter->debug("OutputHandler processing JS8 item, instanceid=" + std::to_string(item.instanceId));
                parseOutputJS8(item.output, item.epochTime, item.mode, item.baseFreq, item.instanceId);
            }
            else if (item.mode == "FT4" || item.mode == "FT8") {
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
            else if (isModeFST4(item.mode)) {
                try {
                    parseOutputFST4(item.output, item.epochTime, item.baseFreq, item.instanceId);
                }
                catch (const std::exception& e) {
                    screenPrinter->print("parseOutputFST4 call", e);
                }
            }
            else if (isModeFST4W(item.mode)) {
                try {
                    parseOutputFST4W(item.output, item.epochTime, item.baseFreq, item.instanceId);
                }
                catch (const std::exception& e) {
                    screenPrinter->print("parseOutputFST4W call", e);
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
                    screenPrinter->print("parseOutputJT65 call", e);
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

    void OutputHandler::parseOutputFST4W(const std::string& out, const uint64_t epochTime, const FrequencyHz baseFreq, const std::size_t instanceId) {

        screenPrinter->print("Handling FST4W output", LOG_LEVEL::DEBUG);

        std::istringstream ss(out);
        std::string line;

        while (std::getline(ss, line)) {
            trim(line);
            std::vector<std::string> lineVec = splitStringByDelim(line, ' ', true);

            // snr is 5-7
            std::string snr = lineVec[1];
            screenPrinter->print("FST4W  snr: " + snr, LOG_LEVEL::TRACE);
            trim(snr);

            // dt is 9-12
            std::string dt = lineVec[2];
            trim(dt);
            screenPrinter->print("FST4W  dt: " + dt, LOG_LEVEL::TRACE);

            // freq
            std::string freq = lineVec[3];
            trim(freq);
            screenPrinter->print("FST4W  freq: " + freq, LOG_LEVEL::TRACE);

            // 18 always space
            if (line.at(18) != ' ') {
                screenPrinter->debug("Skipping line because character 18 is not a space: " + line);
                continue;
            }

            // 19 always `
            if (line.at(19) != '`') {
                screenPrinter->debug("Skipping line because character 19 is not a `: " + line);
                continue;
            }

            // 20 always space
            if (line.at(20) != ' ') {
                screenPrinter->debug("Skipping line because character 20 is not a space: " + line);
                continue;
            }

            // 21 always space
            if (line.at(21) != ' ') {
                screenPrinter->debug("Skipping line because character 21 is not a space: " + line);
                continue;
            }

            std::string call = lineVec[5];
            trim(call);
            if (!checkCall(call)) {
                screenPrinter->debug("Callsign failed validation or was ignored: " + call);
                return;
            }
            //      screenPrinter->print("FST4W  call: " + call, LOG_LEVEL::TRACE);
                  // locator 
            std::string locator = lineVec[6];
            trim(locator);
            //      screenPrinter->print("FST4W  locator: " + locator, LOG_LEVEL::TRACE);
                  // dbm 
            std::string dbm = lineVec[7];
            trim(dbm);

            double freqHz = static_cast<double>(baseFreq);
            double freqReported = std::stod(freq);
            freqHz += freqReported;
            uint32_t freqInt = static_cast<uint32_t>(freqHz);

            std::ostringstream s;
            s << std::setw(12) << instances[instanceId].getMode() << std::setw(12) << epochTime << std::setw(12) << freqInt << std::setw(5) << snr << std::setw(12) << call << std::setw(6) << locator;

            if (useDecodesFile) {
                ofs << s.str() << std::endl;
            }
            if (printHandledReports) {
                screenPrinter->print(s.str(), LOG_LEVEL::INFO);
            }
            if (reporter) {
                reporter->handle(call, std::stoi(snr), freqInt, epochTime, instances[instanceId].getMode());
            }
            if (wsprNet) {
                const std::string reporterCall = instances[instanceId].getReporterCallsign();
                wsprNet->handle(call, instances[instanceId].getMode(), std::stoi(snr), std::stof(dt), 0, std::stoi(dbm), freqInt, baseFreq, epochTime, locator, reporterCall);
            }
            statsHandler->handleReport(instanceId, epochTime * 1000);
        }
    }


    void OutputHandler::parseOutputFST4(const std::string& out, const uint64_t epochTime, const FrequencyHz baseFreq, const std::size_t instanceId) {
        screenPrinter->print("Handling FST4 output", LOG_LEVEL::DEBUG);

        // 0         1         2         3         4
        // 012345678901234567890123456789012345678901234567890
        // 0000 -13  0.4 1080 `  CQ W3TS FN10

        std::istringstream ss(out);
        std::string line;

        while (std::getline(ss, line)) {
            trim(line);
            std::vector<std::string> lineVec = splitStringByDelim(line, ' ', true);

            // snr is 5-7
            std::string snr = lineVec[1];
                  screenPrinter->print("FST4  snr: " + snr, LOG_LEVEL::TRACE);
            trim(snr);

            // dt is 9-12
            std::string dt = lineVec[2];
            trim(dt);
                 screenPrinter->print("FST4  dt: " + dt, LOG_LEVEL::TRACE);

            // freq
            std::string freq = lineVec[3];
            trim(freq);
                  screenPrinter->print("FST4  freq: " + freq, LOG_LEVEL::TRACE);

            // 18 always space
            if (line.at(18) != ' ') {
                screenPrinter->debug("Skipping line because character 18 is not a space: " + line);
                continue;
            }

            // 19 always `
            if (line.at(19) != '`') {
                screenPrinter->debug("Skipping line because character 19 is not a `: " + line);
                continue;
            }

            // 20 always space
            if (line.at(20) != ' ') {
                screenPrinter->debug("Skipping line because character 20 is not a space: " + line);
                continue;
            }

            // 21 always space
            if (line.at(21) != ' ') {
                screenPrinter->debug("Skipping line because character 21 is not a space: " + line);
                continue;
            }

            // msg is 22-end
            std::string msg = line.substr(22, line.size() - 22);
            trim(msg);

            const double actualFreq = std::stod(freq) + static_cast<double>(baseFreq);
            std::string call = "";
            std::string loc = "";
            const auto success = handleMessageUniversal(call, loc, std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, epochTime, instances[instanceId].getMode(), baseFreq, std::stof(dt));
            if (success) {
                statsHandler->handleReport(instanceId, epochTime * 1000);
            }
            else {
                screenPrinter->debug("Failed to handle FST4 message: " + msg);
            }
            
        }
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
            parseCall(call); // WSPR callsigns may be packed
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
            s << std::setw(12) << instances[instanceId].getMode() << std::setw(12) << epochTime << std::setw(12) << freqInt << std::setw(5) << snr << std::setw(12) << call << std::setw(6) << locator;
            
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
                wsprNet->handle(call, "WSPR", std::stoi(snr), std::stof(dt), std::stoi(drift), std::stoi(dbm), freqInt, baseFreq, epochTime, locator, reporterCall);
            }
            statsHandler->handleReport(instanceId, epochTime * 1000);
        }
    }

    void OutputHandler::parseOutputJS8(const std::string out, const uint64_t epochTime, const std::string mode, const FrequencyHz baseFreq, const std::size_t instanceId) {

        screenPrinter->debug("parseOutputJS8 " + out);

        std::istringstream ss(out);
        std::string line = "";
        while (std::getline(ss, line)) {
            trim(line);

            if (line.find("DecodeFinished") != std::string::npos) {
                continue;
            }
            if (line.find("DecodeStarted") != std::string::npos) {
                continue;
            }
            if (line.find("DecodeDebug") != std::string::npos) {
                continue;
            }
            if (line.find("EOF on input") != std::string::npos) {
                continue;
            }

            screenPrinter->debug("processing line: " + line);

            auto rawText = QString::fromStdString(line).remove(QRegularExpression{"\r|\n"});

            DecodedText decodedtext{ rawText, false, "FN03ng"};

            auto bits = decodedtext.bits();

            std::string call = "";
            int snr = 0;
            int freqOffset = 0;

            if (decodedtext.isDirectedMessage()) { // mainwindow.cpp line 5371
                auto parts = decodedtext.directedMessage();
                call = parts.at(0).toStdString();
                snr = decodedtext.snr();
                freqOffset = decodedtext.frequencyOffset();
            }
            else if (decodedtext.isCompound() && !decodedtext.isDirectedMessage()) { // mainwindow.cpp line 5296
                call = decodedtext.compoundCall().toStdString();
                snr = decodedtext.snr();
            }

            QString qmsg = decodedtext.message();

            std::string msg = qmsg.toStdString();

            screenPrinter->debug("message: " + decodedtext.message().toStdString());

            bool isFirst = (bits & Varicode::JS8CallFirst) == Varicode::JS8CallFirst;
            bool isHB = decodedtext.isHeartbeat();

            screenPrinter->debug("isFirst: " + std::to_string(isFirst));
            screenPrinter->debug("isHB: " + std::to_string(isHB));
            screenPrinter->debug("call: " + call);
            screenPrinter->debug("snr: " + std::to_string(snr));
            screenPrinter->debug("freqOffset: " + std::to_string(decodedtext.frequencyOffset()));
            screenPrinter->debug("isDirectedMessage: " + std::to_string(decodedtext.isDirectedMessage()));
            screenPrinter->debug("isCompound: " + std::to_string(decodedtext.isCompound()));

            if (call == "<....>" || call == "<...>") {
                screenPrinter->debug("callsign being skipped=" + call);
                continue;
            }

            else if (checkCall(call)) {
                // callsign ok
                screenPrinter->debug("callsign validation OK for call=" + call);
            }
            else {
                screenPrinter->debug("callsign validation failed for call=" + call);
                continue;
            }

            freqOffset = decodedtext.frequencyOffset();
            int freq = baseFreq + freqOffset;

            std::ostringstream s;
            s << std::setw(12) << mode << std::setw(12) << epochTime << std::setw(12) << freq << std::setw(5) << snr << "  " << std::left << std::setw(52) << msg;
            const std::string msgOutput = s.str();

            if (printHandledReports) {
                screenPrinter->print(msgOutput, LOG_LEVEL::INFO);
            }
            if (useDecodesFile) {
                ofs << msgOutput << std::endl;
            }

            if (reporter) {
                reporter->handle(call, snr, freq, epochTime, instances[instanceId].getMode());
            }

            statsHandler->handleReport(instanceId, epochTime * 1000);

        //    auto d = DecodedText("SN5-lUuJkby0", Varicode::JS8CallFirst, 1);
        //    std::cout << "KN4CRD: K0OG ===>" << d.message().toStdString() << std::endl;

        }
    }

    void OutputHandler::parseOutputFT4FT8(
        const std::string out, 
        const uint64_t epochTime, 
        const std::string mode, 
        const FrequencyHz baseFreq, 
        const std::size_t instanceId) {

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
            std::string call = "";
            std::string loc = "";

            // First, check for Fox/Hound style messages
                      //  these will contain a semicolon
            size_t semipos = msg.find(';');
            if (mode == "FT8" && semipos != std::string::npos)
            {
                screenPrinter->debug("FT8 Message is F/H: " + msg);
                // only the second part has the call of the transmitting station, so we
                //  can ignore the first part of the F/H msg
                std::string secondPart = msg.substr(semipos + 1);
                bool s2 = handleMessageUniversal(call, loc, std::stoi(snr), static_cast<uint32_t>(actualFreq), secondPart, epochTime, mode, baseFreq, std::stof(dt));
                if (s2) {
                    statsHandler->handleReport(instanceId, epochTime * 1000);
                }
                else {
                    screenPrinter->debug("Failed to handle FT8 Fox/Hound message: " + secondPart);
                }
            }
            else
            {
                try {
                    success = handleMessageUniversal(call, loc, std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, epochTime, mode, baseFreq, std::stof(dt));
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
            std::string call = "";
            std::string loc = "";
            const auto success = handleMessageUniversal(call, loc, std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, output.epochTime, output.mode, output.baseFreq, std::stof(dt));
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
            std::string call = "";
            std::string loc = "";
            const auto success = handleMessageUniversal(call, loc, std::stoi(snr), static_cast<uint32_t>(actualFreq), msg, output.epochTime, output.mode, output.baseFreq, std::stof(dt));
            if (success) {
                statsHandler->handleReport(output.instanceId, output.epochTime * 1000);
            }
            else {
                screenPrinter->debug("Failed to handle message: " + msg);
            }
        }
    }

    void OutputHandler::logBadMessage(const std::string badMsg) {
        screenPrinter->print("Message not handled: " + badMsg);
        std::ofstream ofs(badMessageLogFile, std::ios_base::app | std::ofstream::out);
        ofs << badMsg << std::endl;
        ofs.close();
    }

    void OutputHandler::parseCall(std::string& call) {
        // check for packed calls, e.g.,   <W2AXR>
if (isCallPacked(call))
{
    screenPrinter->debug("unpacking call=" + call);
    call = call.substr(1, call.length() - 2);
}
    }

    bool OutputHandler::isCallPacked(const std::string& call) {
        return call.front() == '<' && call.back() == '>' && call.length() >= 5;
    }


    bool OutputHandler::checkCall(const std::string& call) {
        if (call.size() < 3) {
            screenPrinter->debug("Callsign too short: " + call);
            return false;
        }

        // check for all letters - must have 1 number!
        // this eliminates 'QRP' and 'POTA' etc
        size_t letterCount = 0;
        for (char c : call) {
            if (std::isalpha(c)) {
                letterCount++;
            }
        }
        if (letterCount == call.size()) {
            screenPrinter->debug("Callsign contains all letters: " + call);
            return false;
        }
        else if (letterCount == 0) {
            screenPrinter->debug("Callsign contains 0 letters: " + call);
            return false;
        }

        if (call.find_first_of(' ') != std::string::npos) {
            screenPrinter->debug("Callsign contains a space: " + call);
            return false;
        }
        if (call.find_first_of('.') != std::string::npos) {
            screenPrinter->debug("Callsign contains a '.' : " + call);
            return false;
        }
        if (call.find_first_of('+') != std::string::npos) {
            screenPrinter->debug("Callsign contains a '+' : " + call);
            return false;
        }
        if (call.find_first_of('-') != std::string::npos) {
            screenPrinter->debug("Callsign contains a '-' : " + call);
            return false;
        }
        if (call.find_first_of('?') != std::string::npos) {
            screenPrinter->debug("Callsign contains a '?' : " + call);
            return false;
        }
        if (call.find_first_of(';') != std::string::npos) {
            screenPrinter->debug("Callsign contains a ';' : " + call);
            return false;
        }
        if (call.find_first_of('=') != std::string::npos) {
            screenPrinter->debug("Callsign contains a '=' : " + call);
            return false;
        }
        if (call.find_first_of('~') != std::string::npos) {
            screenPrinter->debug("Callsign contains a '~' : " + call);
            return false;
        }

        // make sure it's not a grid, or 'RR73' (same test)
        if (call.length() == 4 && std::isalpha(call[0]) && std::isalpha(call[1]) && std::isdigit(call[2]) && std::isdigit(call[3])) {
            // callsign looks like a grid or RR73
            screenPrinter->debug("Callsign is a grid: " + call);
            return false;
        }

        if (isCallsignIgnored(call)) {
            screenPrinter->info("Ignoring callsign: " + call);
            return false;
        }

        screenPrinter->debug("Callsign OK: " + call);

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

    bool OutputHandler::isSOTAMATMessage(const std::string& prefix, const std::string& call_with_suffix) {
        const size_t msgLength = prefix.length() + call_with_suffix.length() + 1;
        if (msgLength != 13) {
            screenPrinter->debug("isSOTAMATMessage - Message not correct length. len=" + std::to_string(msgLength));
            return false;
        }

        std::vector<std::string> prefixes{"S","SM","STM","STMT","SOTAM","SOTAMT","SOTAMAT"};
        if (std::find(prefixes.begin(), prefixes.end(), prefix) == prefixes.end()) {
            screenPrinter->debug("isSOTAMATMessage - Unknown prefix=" + prefix);
            return false;
        }

        const size_t pos = call_with_suffix.find_first_of('/');
        if (pos == std::string::npos) {
            screenPrinter->debug("isSOTAMATMessage - Callsign has no suffix");
            return false;
        }
        std::string suffix = call_with_suffix.substr(pos+1);
        if (suffix.length() < 2) {
            screenPrinter->debug("isSOTAMATMessage - Suffix len < 2");
            return false;
        }
        if (suffix.length() > 4) {
            screenPrinter->debug("isSOTAMATMessage - Suffix len > 4");
            return false;
        }
        std::string call_without_suffix = call_with_suffix.substr(0,pos);
        if (!checkCall(call_without_suffix)) {
            screenPrinter->debug("isSOTAMATMessage - Callsign failed validation: " + call_with_suffix);
            return false;
        }
        return true;
    }

    bool OutputHandler::handleMessageUniversal(
        std::string& o_call,
        std::string& o_loc,
        const int32_t snr, 
        const uint32_t freq, 
        std::string msg, 
        const uint64_t epochTime, 
        const std::string& mode, 
        const FrequencyHz baseFreq, 
        const float dt) {
        std::ostringstream s;
        s << std::setw(12) << mode << std::setw(12) << epochTime << std::setw(12) << freq << std::setw(5) << snr << "  " << std::setw(5) << dt << "  " << std::left << std::setw(52) << msg;
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
            o_call = msg.substr(3, msg.length() - 3);
            parseCall(o_call);
            if (checkCall(o_call)) {
                if (reporter) {
                    reporter->handle(o_call, snr, freq, epochTime, mode);
                }
                return true;
            }
            else {
                screenPrinter->debug("Callsign failed validation or was ignored: " + o_call);
            }
        }
        else if (isCQ && numSpaces == 2) {
            o_call = msg.substr(spaces[0] + 1, (spaces[1] - spaces[0] - 1));
            parseCall(o_call);
            o_loc = msg.substr(spaces[1] + 1, msg.length() - spaces[1] + 1);
            if (checkCall(o_call)) {
                if (isValidLocator(o_loc)) {
                    // CQ CALL GRID
                    if (reporter) {
                        reporter->handle(o_call, snr, freq, o_loc, epochTime, mode);
                    }
                }
                else {
                    // CQ CALL SOMETHING - not sure what SOMETHING is here?
                    if (reporter) {
                        reporter->handle(o_call, snr, freq, epochTime, mode);
                    }
                }
                return true;
            }
            else {
                std::string o_call2 = o_loc;
                parseCall(o_call2);
                if (checkCall(o_call2)) {
                    // CQ SOMETHING CALL
                    if (reporter) {
                        reporter->handle(o_call2, snr, freq, epochTime, mode);
                    }
                    return true;
                }
                else {
                    screenPrinter->debug("Callsign failed validation or was ignored:" + o_call + " and " + o_call2);
                }
            }
        }
        else if (isCQ && numSpaces == 3) {
            // CQ SOMETHING CALL GRID
            o_call = msg.substr(spaces[1] + 1, (spaces[2] - spaces[1]) - 1);
            parseCall(o_call);
            o_loc = msg.substr(spaces[2] + 1, msg.length() - spaces[2] + 1);
            if (checkCall(o_call) && isValidLocator(o_loc)) {
                if (reporter) {
                    reporter->handle(o_call, snr, freq, o_loc, epochTime, mode);
                }
                return true;
            }
            else {
                logBadMessage("Full msg with bad call or locator: " + msg);
            }
        }
        else if (!isCQ) {
            screenPrinter->debug("Message is not CQ");
            if (numSpaces == 1) {
                // look for <...> <CALL>
                // DX is packed, DE is not
                o_call = msg.substr(spaces[0] + 1); // get everything after the space
                parseCall(o_call);
                std::string dx_call = msg.substr(0,spaces[0]); // everything before the space
                bool packed_dx = isCallPacked(dx_call);
                screenPrinter->debug("DX callsign is packed: " + dx_call + " ? " + std::to_string(packed_dx));
                if (packed_dx && checkCall(o_call)) { // if the DX is packed and the DE passes checks
                    if (reporter) {
                        reporter->handle(o_call, snr, freq, epochTime, mode);
                    }
                    return true;
                }
                // Check for SOTA-style messages.  See:
                //  https://github.com/alexranaldi/CWSL_DIGI/issues/6
                else if (isSOTAMATMessage(dx_call,o_call)) {
                    screenPrinter->debug("message appears to be a SOTAmat message: " + msg + " callsign=" + o_call);
                    if (reporter) {
                        reporter->handle(o_call, snr, freq, epochTime, mode);
                    }
                    return true;
                }
                else {
                    screenPrinter->debug("Message ignored: " + msg);
                }
            }
            else if (numSpaces == 2) {
                // CALL CALL REP
                // CALL CALL 73
                // CALL CALL GRID
                o_call = msg.substr(spaces[0] + 1, spaces[1] - spaces[0] - 1);
                parseCall(o_call);
                if (checkCall(o_call)) {
                    if (reporter) {
                        reporter->handle(o_call, snr, freq, epochTime, mode);
                    }
                    return true;
                }
                else {
                    screenPrinter->debug("Callsign failed validation or was ignored: " + o_call);
                }
            }
            else if (numSpaces == 3) {
                o_call = msg.substr(spaces[0] + 1, spaces[1] - spaces[0] - 1);
                parseCall(o_call);
                if (spaces[2] - spaces[1] == 2 && msg.at(spaces[2]-1) == 'R') {
                    // CALL CALL R GRID
                    o_loc = msg.substr(spaces[2] + 1, msg.length() - spaces[2] + 1);
                    if (checkCall(o_call) && isValidLocator(o_loc)) {
                        if (reporter) {
                            reporter->handle(o_call, snr, freq, o_loc, epochTime, mode);
                        }
                        return true;
                    }
                    screenPrinter->debug("Callsign or grid failed validation or was ignored. Call: " + o_call + ", grid: " + o_loc);
                }
                else if (spaces[2] - spaces[1] == 4) {
                    // CALL CALL RST STATE - N4ZR W2AXR 599 NY
                    // CALL CALL RST SERIAL - N4ZR W2AXR 599 0244
                    if (checkCall(o_call)) {
                        if (reporter) {
                            reporter->handle(o_call, snr, freq, epochTime, mode);
                        }
                        return true;
                    }
                }
            }
        }
        logBadMessage(msg);
        return false;
    }

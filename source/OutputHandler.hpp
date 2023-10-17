#pragma once
#ifndef OUTPUT_HANDLER_HPP
#define OUTPUT_HANDLER_HPP

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

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <fstream>


class ScreenPrinter;
class Stats;
class RBNHandler;
class WSPRNet;
class Decoder;

namespace pskreporter {
    class PSKReporter;
}

#include "SafeQueue.h"
#include "CWSL_DIGI.hpp"

class OutputHandler {

public:

    OutputHandler(
        const bool printHandledReportsIn, 
        const std::string& badMessageLogFileIn, 
        const std::string& decodesFileNameIn, 
        std::shared_ptr<ScreenPrinter> sp,
        std::shared_ptr<Stats> statsHandler_In,
        std::vector<Decoder>& instances_In);

    virtual ~OutputHandler();

    void setRBNHandler(std::shared_ptr<RBNHandler> rep);

    void setPSKReporter(std::shared_ptr<pskreporter::PSKReporter> rep);

    void setWSPRNet(std::shared_ptr<WSPRNet> wn);

    void processingLoop();

    // TODO: make this a separate thread some day?
    void handle(const JT9Output& output);

    void parseOutputWSPR(const std::string& out, const uint64_t epochTime, const FrequencyHz baseFreq, const std::size_t instanceId);

    void parseOutputFST4(const std::string& out, const uint64_t epochTime, const FrequencyHz baseFreq, const std::size_t instanceId);

    void parseOutputFST4W(const std::string& out, const uint64_t epochTime, const FrequencyHz baseFreq, const std::size_t instanceId);

    void parseOutputFT4FT8(const std::string out, const uint64_t epochTime, const std::string mode, const FrequencyHz baseFreq, const std::size_t instanceId);

    void parseOutputJS8(const std::string out, const uint64_t epochTime, const std::string mode, const FrequencyHz baseFreq, const std::size_t instanceId);

    void parseOutputJT65(const JT9Output& output);

    void parseOutputQ65(const JT9Output& output);

    void logBadMessage(const std::string badMsg);

    bool checkCall(const std::string& call);

    void parseCall(std::string& call);

    bool isCallPacked(const std::string& call);

    void ignoreCallsign(const std::string& call);

    bool isCallsignIgnored(const std::string& test);

    bool isSOTAMATMessage(const std::string& s1, const std::string& s2);

    bool handleMessageUniversal(
        std::string& o_call,
        std::string& o_loc,
        const int32_t snr, 
        const uint32_t freq, 
        std::string msg, 
        const uint64_t epochTime, 
        const std::string& mode, 
        const FrequencyHz baseFreq, 
        const float dt);


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

    std::thread processingThread;

    std::shared_ptr<Stats> statsHandler;

    std::vector<std::string> ignoredCallsigns;

    std::vector<Decoder>& instances;
}; 

#endif

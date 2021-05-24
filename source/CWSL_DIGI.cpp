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
along with CWSL_DIGI.If not, see < https://www.gnu.org/licenses/>.
*/

// This work is based on: https://github.com/HrochL/CWSL

#include <windows.h>

// stdio and conio used for _kbhit() and _getch()
#include <stdio.h>
#include <conio.h>

#include <tuple>
#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <limits>
#include <memory>

#include "mmreg.h"

#include <algorithm> 
#include <cctype>
#include <locale>
#include <unordered_map>

#  pragma comment(lib, "Ws2_32.lib")

#include "../Utils/SharedMemory.h"

#include "Instance.hpp"
#include "Receiver.hpp"
#include "Stats.hpp"

#include "PSKReporter.hpp"
#include "RBNHandler.hpp"
#include "WSPRNet.hpp"

#include "boost/program_options.hpp"

#include "ScreenPrinter.hpp"
#include "OutputHandler.hpp"

std::string badMessageLogFile = "";

std::string decodesFileName;

std::shared_ptr<pskreporter::PSKReporter> reporter;
std::shared_ptr<RBNHandler> rbn;
std::shared_ptr<WSPRNet> wsprNet;

std::unordered_map<std::string, std::shared_ptr<Receiver>> receivers; 

bool usePSKReporter = false;
bool useRBN = false;
bool useWSPRNet = false;

std::string operatorCallsign = "";
std::string operatorLocator = "";

std::vector<std::unique_ptr<Instance>> instances;

std::shared_ptr<DecoderPool> decoderPool;
std::shared_ptr<OutputHandler> outputHandler;
std::shared_ptr<ScreenPrinter> printer;
std::shared_ptr<Stats> statsHandler;


void cleanup() {
    for (size_t k = 0; k < instances.size(); ++k) {
        instances[k]->terminate();
    }
    decoderPool->terminate();
    if (reporter) { reporter->terminate(); }
    if (wsprNet) { wsprNet->terminate(); }
    if (rbn) { rbn->terminate(); }
    syncThreadTerminateFlag = true;

    std::this_thread::sleep_for(std::chrono::seconds(1));
    // terminate screen printer last so it can log any useful messages
    printer->terminate();
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

void reportStats(std::shared_ptr<Stats> statsHandler, std::shared_ptr<ScreenPrinter> printer, std::vector<std::unique_ptr<Instance>>& instances, const int reportingInterval, const std::uint64_t twKey) {
    tw->threadStarted(twKey);
    while (!syncThreadTerminateFlag) {
        for (int k = 0; k < reportingInterval * 5; ++k) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            tw->report(twKey);
        }

        try {
            statsHandler->process();
        }
        catch (const std::exception& e) {
            printer->print(e);
            continue;
        }
        tw->report(twKey);

        try {
            auto v1Min = statsHandler->getCounts(60);
            tw->report(twKey);
            auto v5Min = statsHandler->getCounts(300);
            tw->report(twKey);
            auto v1Hr = statsHandler->getCounts(3600);
            tw->report(twKey);
            auto v24Hr = statsHandler->getCounts(86400);
            tw->report(twKey);
            std::stringstream hdr;
            hdr << std::setfill(' ') << std::left << std::setw(10) << "Instance" << std::setw(12) << "Frequency" << std::setw(8) << "Mode" << std::setw(8) << "24 Hour" << std::setw(8) << "1 Hour" << std::setw(8) << "5 Min" << std::setw(8) << "1 Min";
            printer->print(hdr.str());
            for (std::size_t k = 0; k < instances.size(); ++k) {
                const auto mode = instances[k]->getMode();
                std::stringstream s;
                s << std::setfill(' ') << std::left << std::setw(10) << std::to_string(k) << std::setw(12) << std::to_string(instances[k]->getFrequency()) << std::setw(8) << mode << std::setw(8) << std::to_string(v24Hr[k]) << std::setw(8) << std::to_string(v1Hr[k]) << std::setw(8) << std::to_string(v5Min[k]) << std::setw(8) << std::to_string(v1Min[k]);
                printer->print(s.str());
            }
        }
        catch (const std::exception& e) {
            printer->print(e);
            continue;
        }
        tw->report(twKey);
    }
    tw->threadFinished(twKey);

}

///////////////////////////////////////////////////////////////////////////////
// Main function
int main(int argc, char **argv)
{
    std::cout << PROGRAM_NAME + " " + PROGRAM_VERSION << " by W2AXR" << std::endl;
    std::cout << "License:  GNU GENERAL PUBLIC LICENSE, Version 3" << std::endl;
    std::cout << "Please run " + PROGRAM_NAME + " --help for syntax" << std::endl;
    std::cout << "Press CONTROL + C to terminate" << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(3));

    namespace po = boost::program_options;

    po::options_description desc("Program options");
    desc.add_options()
        ("help", "produce help message")
        ("configfile", po::value<std::string>(), "path and file name of configuration file")
        ("decoders.decoder", po::value<std::vector<std::string>>()->multitoken(), "freq, mode, shmem, freqcal")
        ("radio.freqcalibration", po::value<double>(), "frequency calibration factor in PPM, default 1.0000000000")
        ("radio.sharedmem", po::value<int>(), "CWSL shared memory interface number to use, default -1")
        ("reporting.pskreporter", po::value<bool>(), "Send spots to PSK Reporter, default false")
        ("reporting.wsprnet", po::value<bool>(), "Send WSPR spots to WSPRNet, default false")
        ("reporting.rbn", po::value<bool>(), "enables sending spots to RBN Aggregator, default false")
        ("reporting.aggregatorport", po::value<int>(), "port number for datagrams sent to RBN Aggregator")
        ("reporting.aggregatorip", po::value<std::string>(), "ip address for RBN Aggregator, default 127.0.0.1")
        ("operator.callsign", po::value<std::string>(), "operator callsign - required")
        ("operator.gridsquare", po::value<std::string>(), "operator grid square locator - required")
        ("wsjtx.decoderburden", po::value<float>(), "adds extra JT9 and WSPRD instances for decoding. default 1.0")
        ("wsjtx.keepwav", po::value<bool>(), "keep generated wav files rather than delete them, default false")
        ("wsjtx.numjt9instances", po::value<int>(), "maximum number of simultaneous JT9.exe instances, default is computed")
        ("wsjtx.maxwsprdinstances", po::value<int>(), "maximum number of simultaneous WSPRD.exe instances, default is computed")
        ("wsjtx.numjt9threads", po::value<int>(), "number of threads used by each jt9 instance. default 3")
        ("wsjtx.highestdecodefreq", po::value<int>(), "highest freq to decode, default 3000")
        ("wsjtx.decodedepth", po::value<int>(), "decode depth passed to jt9.exe. must be 1, 2 or 3. default 3")
        ("wsjtx.temppath", po::value<std::string>(), "temporary file path for writing wave files")
        ("wsjtx.binpath", po::value<std::string>(), "WSJT-X bin folder path - required")
        ("wsjtx.ftaudioscalefactor", po::value<float>(), "FT4 and FT8 audio scale factor, default 0.90")
        ("wsjtx.wspraudioscalefactor", po::value<float>(), "WSPR audio scale factor, default 0.20")
        ("wsjtx.maxdataage", po::value<int>(), "Max data age, factor of mode duration. default 10")
        ("wsjtx.wsprcycles", po::value<int>(), "WSPR decoder cycles per bit. default 3000")
        ("wsjtx.transfermethod", po::value<std::string>(), "either wavfile or shmem, default shmem")
        ("logging.statsreportinginterval", po::value<int>(), "how often to report decoder statistics in seconds, default 300")
        ("logging.decodesfile", po::value<std::string>(), "file name for decode text log")
        ("logging.logreports", po::value<bool>(), "log each handled report, default true")
        ("logging.printjt9output", po::value<bool>(), "prints output of JT9.exe, default false")
        ("logging.loglevel", po::value<int>(), "logging level, 5 is most verbose, 0 is nothing, 8 is everything, default is 3")
        ("logging.badmsglog", po::value<std::string>(), "path to log file for writing unhandled messages")
        ("logging.logimmediately", po::value<bool>(), "if true, logging is done immediately, otherwise buffered. default is false")
        ("logging.logfile", po::value<std::string>(), "log file, mirrors console output");

    po::variables_map vm;
    po::parsed_options cmdLineOpts = po::parse_command_line(argc, argv, desc);
    po::store(cmdLineOpts, vm);
    if (vm.count("help")) {
        std::cout << desc << "\n";
        return EXIT_SUCCESS;
    }
    std::string cfgFilePath;
    if (vm.count("configfile")) {
        cfgFilePath = vm["configfile"].as<std::string>();
    }
    else {
        char* appdata = getenv("appdata");
        std::string appdataStr(appdata);
        cfgFilePath = appdataStr + "\\CWSL_DIGI\\config.ini";
        std::cout << "Looking for config file: " << cfgFilePath << std::endl;
        if (!doesFileExist(cfgFilePath)) {
            char myPath[_MAX_PATH + 1];
            GetModuleFileName(NULL, myPath, _MAX_PATH);
            cfgFilePath = std::string(myPath);
            const auto idx = cfgFilePath.find_last_of('\\');
            if (idx != std::string::npos){
                cfgFilePath = cfgFilePath.substr(0,idx);
                cfgFilePath += "\\config.ini";
                std::cout << "Looking for config file: " << cfgFilePath << std::endl;
            }
        }
    }

    std::cout << "Loading config file: " << cfgFilePath << std::endl;
    std::ifstream ini_file(cfgFilePath, std::ifstream::in);
    po::parsed_options fileOpts = po::parse_config_file(ini_file, desc, true);

    po::store(fileOpts, vm);
    po::store(cmdLineOpts, vm);
    po::notify(vm);

    // Parse Logging Options

    bool logImmediately = false;
    if (vm.count("logging.logimmediately")) {
        logImmediately = vm["logging.logimmediately"].as<bool>();
    }
    if (logImmediately) {
        std::cout << "Immediate logging enabled" << std::endl;
    }

    printer = std::make_shared<ScreenPrinter>(logImmediately);

    std::string logFileName = "";

    int logLevel = static_cast<int>(LOG_LEVEL::INFO);
    if (vm.count("logging.loglevel")) {
        logLevel = vm["logging.loglevel"].as<int>();
    }

    printer->setLogLevel(logLevel);

    if (vm.count("logging.logfile")) {
        std::string logFileName = vm["logging.logfile"].as<std::string>();
        printer->enableLogFile(logFileName);
    }

    bool printHandledReports = true;
    if (vm.count("logging.logreports")) {
        printHandledReports = vm["logging.logreports"].as<bool>();
    }

    if (vm.count("logging.badmsglog")) {
        badMessageLogFile = vm["logging.badmsglog"].as<std::string>();
    }

    bool printJT9Output = false;
    if (vm.count("logging.printjt9output")) {
        printJT9Output = vm["logging.printjt9output"].as<bool>();
    }


    if (vm.count("logging.decodesfile")) {
        decodesFileName = vm["logging.decodesfile"].as<std::string>();
    }
    else {
        decodesFileName = "";
    }

    int statsReportingInterval = 300;
    if (vm.count("logging.statsreportinginterval")) {
        statsReportingInterval = vm["logging.statsreportinginterval"].as<int>();
    }
    printer->print("Statistics reporting interval: " + std::to_string(statsReportingInterval) + " sec");


    // Parse radio settings

    int SMNumber = -1;
    if (vm.count("radio.sharedmem")) {
        SMNumber = vm["radio.sharedmem"].as<int>();
        printer->print("Using the specified CWSL shared memory interface number: " + std::to_string(SMNumber));
    }

    double freqCalGlobal = 1.0;
    if (vm.count("radio.freqcalibration")) {
        freqCalGlobal = vm["radio.freqcalibration"].as<double>();
        printer->print("Frequency Calibration Factor: " + std::to_string(freqCalGlobal));
    }

    // Parse decoders

    int numFT4Decoders = 0;
    int numFT8Decoders = 0;
    int numWSPRDecoders = 0;
    int numQ65_30Decoders = 0;

    using Decoder = std::tuple<std::uint32_t, std::string, int, double>;
    using DecoderVec = std::vector<Decoder>;
    DecoderVec decoders;

    if (vm.count("decoders.decoder")) {
        std::vector<std::string> decodersRawVec = vm["decoders.decoder"].as<std::vector<std::string>>();
        printer->print("Found " + std::to_string(decodersRawVec.size()) + " decoder entries");
        for (size_t k = 0; k < decodersRawVec.size(); k++) {
            const std::string& rawLine = decodersRawVec[k];
            auto decoderVecLine = splitStringByDelim(rawLine, ' ');
            if (decoderVecLine.size() != 2 && decoderVecLine.size() != 3 && decoderVecLine.size() != 4) {
                printer->err("Error parsing decoder line: " + rawLine);
                cleanup();
                return EXIT_FAILURE;
            }
            const uint32_t freq = std::stoi(decoderVecLine[0]);
            const std::string& mode = decoderVecLine[1];
            if (mode == "FT4") {
                numFT4Decoders++;
            }
            else if (mode == "FT8") {
                numFT8Decoders++;
            }
            else if (mode == "WSPR") {
                numWSPRDecoders++;
            }
            else if (mode == "Q65-30") {
                numQ65_30Decoders++;
            }
            else {
                printer->err("Error parsing decoder line, unknown mode! Line: " + rawLine);
                cleanup();
                return EXIT_FAILURE;
            }
            int smnum = SMNumber;
            if (decoderVecLine.size() >= 3) {
                smnum = std::stoi(decoderVecLine[2]);
            }
            double decoder_freqcal = 1.0;
            if (decoderVecLine.size() >= 4) {
                decoder_freqcal = std::stod(decoderVecLine[3]);
            }
            Decoder tu = std::make_tuple(freq, mode, smnum, decoder_freqcal);
            decoders.push_back(tu);
        }
    }
    else {
        printer->err("decoders.decoder input is required but was not specified!");
        cleanup();
        return EXIT_FAILURE;
    }

    // Parse wsjtx settings

    int numJT9Instances = 0;
    if (vm.count("wsjtx.numjt9instances")) {
        numJT9Instances = vm["wsjtx.numjt9instances"].as<int>();
        if (numJT9Instances < 1) {
            printer->err("wsjtx.numjt9instances must be >= 1");
            cleanup();
            return EXIT_FAILURE;
        }
    }
    else {
        float decoderburden = 1;
        if (vm.count("wsjtx.decoderburden")) {
            decoderburden = vm["wsjtx.decoderburden"].as<float>();
            printer->print("Decoder Burden specified as: " + std::to_string(decoderburden));
        }
        const float nd1 = static_cast<float>(numFT4Decoders + numFT8Decoders + numQ65_30Decoders) * (1.0f/5.0f); // 1 per 5 ft4/ft8/Q65
        const float nd2 = static_cast<float>(numWSPRDecoders) * (1.0f/3.0f); // 1 per 3 wspr
        const float nInstf = (nd1 + nd2) * decoderburden;
        numJT9Instances = static_cast<int>(std::round(nInstf + 0.55f));
    }
    printer->print("Maximum number of simultaneous jt9.exe and wsprd.exe instances: " + std::to_string(numJT9Instances));

    int maxWSPRDInstances = 0;
    if (vm.count("wsjtx.maxwsprdinstances")) {
        maxWSPRDInstances = vm["wsjtx.maxwsprdinstances"].as<int>();
        if (maxWSPRDInstances < 1) {
            printer->err("wsjtx.maxwsprdinstances must be >= 1");
            cleanup();
            return EXIT_FAILURE;
        }
    }
    else {
        maxWSPRDInstances = static_cast<int>(std::round(static_cast<double>(numJT9Instances) * (static_cast<double>(numWSPRDecoders) / static_cast<double>(decoders.size())) ));
        if (maxWSPRDInstances < 1 && numWSPRDecoders) {
            maxWSPRDInstances = 1;
        }
    }

    printer->print("Maximum number of simultaneous wsprd.exe instances: " + std::to_string(maxWSPRDInstances));

    int highestDecodeFreq = 3000;
    if (vm.count("wsjtx.highestdecodefreq")) {
        highestDecodeFreq = vm["wsjtx.highestdecodefreq"].as<int>();
    }
    if (highestDecodeFreq > SSB_BW) {
        highestDecodeFreq = SSB_BW;
    }
    printer->print("Decoding up to " + std::to_string(highestDecodeFreq) + " Hz");

    std::string wavPath = "";
    if (vm.count("wsjtx.temppath")) {
        wavPath = vm["wsjtx.temppath"].as<std::string>();
    }
    else {
        char buf[MAX_PATH] = {0};
        GetTempPathA(MAX_PATH, buf);
        std::string s(buf);
        wavPath = buf;
    }
    printer->print("Using path for wav files: " + wavPath);

    std::string binPath = "";
    if (vm.count("wsjtx.binpath")) {
        binPath = vm["wsjtx.binpath"].as<std::string>();
    }
    else {
        printer->err("Missing wsjtx.binpath input argument!");
        cleanup();
        return EXIT_FAILURE;
    }

    bool keepWavFiles = false;
    if (vm.count("wsjtx.keepwav")) {
        keepWavFiles = vm["wsjtx.keepwav"].as<bool>();
    }

    int decodedepth = 3;
    if (vm.count("wsjtx.decodedepth")) {
        decodedepth = vm["wsjtx.decodedepth"].as<int>();
        if (decodedepth > 3) {
            printer->err("wsjtx.decodedepth is too high, setting to 3");
            decodedepth = 3;
        }
        else if (decodedepth < 1) {
            printer->err("wsjtx.decodedepth is too small, setting to 1");
            decodedepth = 1;
        }
    }

    float ftAudioScaleFactor = 0.90f;
    if (vm.count("wsjtx.ftaudioscalefactor")) {
        ftAudioScaleFactor = vm["wsjtx.ftaudioscalefactor"].as<float>();
        if (ftAudioScaleFactor > 1.0f) {
            printer->err("ftaudioscalefactor must be <= 1.0");
            cleanup();
            return EXIT_FAILURE;
        }
        else if (ftAudioScaleFactor <= 0.0f) {
            printer->err("ftaudioscalefactor must be > 0");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    float wsprAudioScaleFactor = 0.20f;
    if (vm.count("wsjtx.wspraudioscalefactor")) {
        wsprAudioScaleFactor = vm["wsjtx.wspraudioscalefactor"].as<float>();
        if (wsprAudioScaleFactor > 1.0f) {
            printer->err("wsjtx.wspraudioscalefactor must be <= 1.0");
            cleanup();
            return EXIT_FAILURE;
        }
        else if (wsprAudioScaleFactor <= 0.0f) {
            printer->err("wsjtx.wspraudioscalefactor must be > 0");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    int maxDataAge = 10;
    if (vm.count("wsjtx.maxdataage")) {
        maxDataAge = vm["wsjtx.maxdataage"].as<int>();
        if (maxDataAge > 100) {
            printer->err("wsjtx.maxdataage must be <= 100");
            cleanup();
            return EXIT_FAILURE;
        }
        else if (maxDataAge < 2) {
            printer->err("wsjtx.maxdataage must be >= 2");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    int wsprCycles = 3000;
    if (vm.count("wsjtx.wsprcycles")) {
        wsprCycles = vm["wsjtx.wsprcycles"].as<int>();
        if (wsprCycles > 10000) {
            printer->err("wsjtx.wsprcycles must be <= 10000");
            cleanup();
            return EXIT_FAILURE;
        }
        else if (wsprCycles < 100) {
            printer->err("wsjtx.wsprcycles must be >= 100");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    int numjt9threads = 3;
    if (vm.count("wsjtx.numjt9threads")) {
        numjt9threads = vm["wsjtx.numjt9threads"].as<int>();
        if (numjt9threads > 9) {
            printer->err("wsjtx.numjt9threads is too high, setting to 9");
            numjt9threads = 9;
        }
        else if (numjt9threads < 1) {
            printer->err("wsjtx.numjt9threads is too small, setting to 1");
            numjt9threads = 1;
        }
    }

    std::string transferMethod = "wavefile";
    if (vm.count("wsjtx.transfermethod")) {
        transferMethod = vm["wsjtx.transfermethod"].as<std::string>();
        if (transferMethod != "shmem" && transferMethod != "wavefile") {
            printer->err("wsjtx.transfermethod must be wavefile or shmem");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    printer->debug("Using transfer method: " + transferMethod);

    // Parse reporting options

    reporter = nullptr;
    if (vm.count("reporting.pskreporter")) {
        usePSKReporter = vm["reporting.pskreporter"].as<bool>();
    }

    wsprNet = nullptr;
    if (vm.count("reporting.wsprnet")) {
        useWSPRNet = vm["reporting.wsprnet"].as<bool>();
    }

    int rbnPort = 0;
    if (vm.count("reporting.rbn")) {
        useRBN = vm["reporting.rbn"].as<bool>();
        if (useRBN) {
            if (!vm.count("reporting.aggregatorport")) {
                printer->err("reporting.rbn option also requires reporting.aggregatorport");
                cleanup();
                return EXIT_FAILURE;
            }
            rbnPort = vm["reporting.aggregatorport"].as<int>();
        }
    }

    std::string rbnIpAddr = "127.0.0.1";
    if (vm.count("reporting.aggregatorip")) {
        rbnIpAddr = vm["reporting.aggregatorip"].as<std::string>();
    }

    if (vm.count("operator.callsign")) {
        operatorCallsign = vm["operator.callsign"].as<std::string>();
    }
    else {
        if (usePSKReporter) {
            printer->err("Missing operator.callsign input argument!");
            cleanup();
            return EXIT_FAILURE;
        }
    }
    if (vm.count("operator.gridsquare")) {
         operatorLocator = vm["operator.gridsquare"].as<std::string>();
    }
    else {
        if (usePSKReporter) {
            printer->print("Missing operator.gridsquare input argument!");
            cleanup();
            return EXIT_FAILURE;
        }
    }
    
    tw = std::make_shared<ThreadWatcher>();

    statsHandler = std::make_shared<Stats>(86400, static_cast<std::uint32_t>(decoders.size()));

    outputHandler = std::make_shared<OutputHandler>(printHandledReports, badMessageLogFile, decodesFileName, printer, statsHandler);

    decoderPool = std::make_shared<DecoderPool>(transferMethod, keepWavFiles, printJT9Output, numJT9Instances, maxWSPRDInstances, numjt9threads, decodedepth, wsprCycles, highestDecodeFreq, binPath, maxDataAge, wavPath, printer, outputHandler);
    const bool decStat = decoderPool->init();
    if (!decStat) {
        cleanup();
        return EXIT_FAILURE;
    }

    if (usePSKReporter) {
        printer->print("Initializing PSKReporter interface");
        reporter = std::make_shared<pskreporter::PSKReporter>();
        const bool res = reporter->init(operatorCallsign, operatorLocator, PROGRAM_NAME + " " + PROGRAM_VERSION, printer);
        if (!res) {
            printer->err("Failed to initialize PSKReporter!"); 
            cleanup();
            return EXIT_FAILURE;
        }
    }

    if (useWSPRNet) {
        printer->print("Initializing WSPRNet interface");
        wsprNet = std::make_shared<WSPRNet>(operatorCallsign, operatorLocator, printer);
        const bool res = wsprNet->init();
        if (!res) {
            printer->err("Failed to initialize WSPRNet!");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    if (useRBN) {
        printer->print("Initializing RBN Aggregator interface");
        rbn = std::make_shared<RBNHandler>();
        const bool res = rbn->init(operatorCallsign, operatorLocator, PROGRAM_NAME + " " + PROGRAM_VERSION, rbnIpAddr, rbnPort);
        if (!res) {
            printer->err("Failed to initialize RBN Aggregator interface!");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    if (usePSKReporter) {
        outputHandler->setPSKReporter(reporter);
    }
    if (useWSPRNet) {
        outputHandler->setWSPRNet(wsprNet);
    }
    if (useRBN) {
        outputHandler->setRBNHandler(rbn);
    }

    // create time signalling threads
    SyncPredicates ft8Preds(numFT8Decoders);
    std::thread ft8SignalThread;
    if (numFT8Decoders) {
        const auto twKey = tw->addThread("ft8SignalThread");
        ft8SignalThread = std::thread(&waitForTimeFT8, printer, std::ref(ft8Preds), twKey);
        ft8SignalThread.detach();
    }
    SyncPredicates ft4Preds(numFT4Decoders);
    std::thread ft4SignalThread;
    if (numFT4Decoders) {
        const auto twKey = tw->addThread("ft4SignalThread");
        ft4SignalThread = std::thread(&waitForTimeFT4, printer, std::ref(ft4Preds), twKey);
        ft4SignalThread.detach();
    }
    SyncPredicates wsprPreds(numWSPRDecoders);
    std::thread wsprSignalThread;
    if (numWSPRDecoders) {
        const auto twKey = tw->addThread("wsprSignalThread");
        wsprSignalThread = std::thread(&waitForTimeWSPR, printer,  std::ref(wsprPreds), twKey);
        wsprSignalThread.detach();
    }
    SyncPredicates q65_30Preds(numQ65_30Decoders);
    std::thread q65_30SignalThread;
    if (numQ65_30Decoders) {
        const auto twKey = tw->addThread("q65_30SignalThread");
        q65_30SignalThread = std::thread(&waitForTimeQ65_30, printer, std::ref(q65_30Preds), twKey);
        q65_30SignalThread.detach();
    }

    // USB/LSB.  USB = 1, LSB = 0
    constexpr int USB = 1;

    int ft8PredIndex = 0;
    int ft4PredIndex = 0;
    int wsprPredIndex = 0;
    int q65_30PredIndex = 0;

    for (size_t k = 0; k < decoders.size(); ++k) {
        const auto& decoder = decoders[k];
        const auto& f = std::get<0>(decoder);
        const auto& mode = std::get<1>(decoder);
        const auto& smnum = std::get<2>(decoder);
        const auto& d_freqcal = std::get<3>(decoder);

        if (mode != "FT8" && mode != "FT4" && mode != "WSPR" && mode != "Q65-30") {
            printer->err("Unknown mode specified: " + mode);
            cleanup();
            return EXIT_FAILURE;
        }

        const FrequencyHz instanceFreqCalibrated = static_cast<FrequencyHz>(f / (freqCalGlobal * d_freqcal));
        printer->debug("Calibrated instance frequency: " + std::to_string(instanceFreqCalibrated));

        const int nMem = findBand(static_cast<std::int64_t>(instanceFreqCalibrated), smnum);
        if (-1 == nMem) {
            printer->err("Unable to open CWSL shared memory at the specified frequency. Bad frequency or sharedmem specified.");
            printer->err("Note that frequency calibration may shift the expected frequency outside of what is expected!");
            cleanup();
            return EXIT_FAILURE;
        }
        const std::string smname = createSharedMemName(nMem, smnum);
        std::shared_ptr<Receiver> receiver = nullptr;
        auto it = receivers.find(smname);
        if (it != receivers.end()) {
            printer->debug("Using existing receiver interface");
            receiver = it->second;
        }
        else {
            printer->debug("Creating receiver interface");
            receivers.emplace(smname, std::make_shared<Receiver>(
                k,
                smname,
                printer
                ));
            receiver = receivers[smname];
            receiver->init();
        }

        printer->print("Creating Instance " + std::to_string(k) +
            " of " + std::to_string(decoders.size()-1) + " for frequency " + 
            std::to_string(f) + " mode " + mode, LOG_LEVEL::INFO);

        std::shared_ptr<std::atomic_bool> pred = nullptr;
        if (mode == "FT8") {
            pred = ft8Preds.preds[ft8PredIndex];
            ft8PredIndex++;
        }
        else if (mode == "FT4") {
            pred = ft4Preds.preds[ft4PredIndex];
            ft4PredIndex++;
        }
        else if (mode == "WSPR") {
            pred = wsprPreds.preds[wsprPredIndex];
            wsprPredIndex++;
        }
        else if (mode == "Q65-30") {
            pred = q65_30Preds.preds[q65_30PredIndex];
            q65_30PredIndex++;
        }
        else {
            printer->err("Unhandled mode: " + mode);
            cleanup();
            return EXIT_FAILURE;
        }

        std::unique_ptr<Instance> instance = std::make_unique<Instance>(
            receiver,
            k,
            pred,
            f,
            instanceFreqCalibrated,
            mode,
            Wave_SR,
            ftAudioScaleFactor, 
            wsprAudioScaleFactor,
            printer,
            decoderPool
        );

        instances.push_back(std::move(instance));

        printer->print("Initializing instance " + std::to_string(k + 1) + " of " + std::to_string(decoders.size()), LOG_LEVEL::DEBUG);
        try {
            const bool status = instances.back()->init();
            if (!status) {
                printer->err("Failed to initialize decoder instance");
                cleanup();
                return EXIT_FAILURE;
            }
        }
        catch (const std::exception& e) {
            printer->print(e);
            cleanup();
            return EXIT_FAILURE;
        }
    }

    const std::uint64_t statstwKey = tw->addThread("reportStats");
    std::thread statsThread = std::thread(&reportStats, std::ref(statsHandler), printer, std::ref(instances), statsReportingInterval, statstwKey);
    SetThreadPriority(statsThread.native_handle(), THREAD_PRIORITY_IDLE);
    tw->setAllowedDelta(statstwKey, 5000);
    statsThread.detach();
    tw->threadStarted(statstwKey); // this thread can be slow to start, so report it as started here as well as in the thread itself

    //
    //  Main Loop
    //

    printer->print("Main loop started");

    constexpr float MAIN_LOOP_TICKS_S = 1000 / MAIN_LOOP_SLEEP_MS;
    // latch prevents log spam
    std::vector<uint64_t> latch(tw->numThreads());
    while (1) {
        std::fill(latch.begin(), latch.end(), 0);
      //   printer->debug("Checking on threads. Thread count=" + std::to_string(tw->numThreads()));
        for (size_t k = 0; k < tw->numThreads(); ++k) {
            const std::pair<bool, std::int64_t> p = tw->check(k);
            if (!p.first && !latch[k]) {
                printer->err("Thread failed to report on time! name=" + tw->getName(k) + " index=" + std::to_string(k) + " time delta=" + std::to_string(p.second) + "ms");
                auto status = tw->getStatus(k);
                if (ThreadStatus::Finished == status) {
                    printer->err("Thread has finished status! index=" + std::to_string(k));
                }
                latch[k]++;
            }
            else if (p.first) {
                if (latch[k]) {
                    printer->info("Thread is reporting again. index=" + std::to_string(k));
                }
                latch[k] = 0;
       //         printer->trace("Thread reported on time. name=" + tw->getName(k) + " index=" + std::to_string(k) + " time delta=" + std::to_string(p.second) + "ms");
            }
            else if (!p.first) {
                if (latch[k] > MAIN_LOOP_TICKS_S * 5) {
                    latch[k] = 0;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(MAIN_LOOP_SLEEP_MS));
    }

    std::cout << "Exiting" << std::endl;
    return EXIT_SUCCESS;
}    

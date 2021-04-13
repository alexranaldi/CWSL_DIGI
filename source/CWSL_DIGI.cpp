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

#  pragma comment(lib, "Ws2_32.lib")

#include "../Utils/SharedMemory.h"

#include "Instance.hpp"

#include "PSKReporter.hpp"
#include "RBNHandler.hpp"
#include "WSPRNet.hpp"

#include "boost/program_options.hpp"

#include "ScreenPrinter.hpp"

std::atomic_bool terminateFlag;

std::string badMessageLogFile = "";


std::string decodesFileName;

std::shared_ptr<pskreporter::PSKReporter> reporter;
std::shared_ptr<RBNHandler> rbn;
std::shared_ptr<WSPRNet> wsprNet;

bool usePSKReporter = false;
bool useRBN = false;
bool useWSPRNet = false;

std::string operatorCallsign = "";
std::string operatorLocator = "";


std::vector<std::unique_ptr<Instance>> instances;

std::shared_ptr<DecoderPool> decoderPool;
std::shared_ptr<OutputHandler> outputHandler;

///////////////////////////////////////////////////////////////////////////////
// Main function
int main(int argc, char **argv)
{
    std::cout << PROGRAM_NAME + " " + PROGRAM_VERSION << " by W2AXR" << std::endl;
    std::cout << "License:  GNU GENERAL PUBLIC LICENSE, Version 3" << std::endl;
    std::cout << "Please run " + PROGRAM_NAME + " --help for syntax" << std::endl;

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
        ("wsjtx.keepwav", "keep generated wav files rather than delete them, default false")
        ("wsjtx.numjt9instances", po::value<int>(), "maximum number of simultaneous JT9.exe instances, default is 0.3*number of decoders")
        ("wsjtx.numjt9threads", po::value<int>(), "number of threads used by each jt9 instance. default 3")
        ("wsjtx.highestdecodefreq", po::value<int>(), "highest freq to decode, default 3000")
        ("wsjtx.decodedepth", po::value<int>(), "decode depth passed to jt9.exe. must be 1, 2 or 3. default 3")
        ("wsjtx.temppath", po::value<std::string>(), "temporary file path for writing wave files - required")
        ("wsjtx.binpath", po::value<std::string>(), "WSJT-X bin folder path - required")
        ("wsjtx.ftaudioscalefactor", po::value<float>(), "FT4 and FT8 audio scale factor, default 0.90")
        ("wsjtx.wspraudioscalefactor", po::value<float>(), "WSPR audio scale factor, default 0.20")
        ("wsjtx.maxdataage", po::value<int>(), "Max data age, factor of mode duration. default 10")
        ("wsjtx.wsprcycles", po::value<int>(), "WSPR decoder cycles per bit. default 3000")
        ("logging.decodesfile", po::value<std::string>(), "file name for decode text log")
        ("logging.printreports", po::value<bool>(), "prints each handled report, default true")
        ("logging.printjt9output", po::value<bool>(), "prints output of JT9.exe, default false")
        ("logging.loglevel", po::value<int>(), "logging level, 5 is most verbose, 0 is nothing, 8 is everything, default is 3")
        ("logging.badmsglog", po::value<std::string>(), "path to log file for writing unhandled messages")
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
    }
       
    std::cout << "Loading config file: " << cfgFilePath << std::endl;
    std::ifstream ini_file(cfgFilePath, std::ifstream::in);
    po::parsed_options fileOpts = po::parse_config_file(ini_file, desc, true);

    po::store(fileOpts, vm);
    po::store(cmdLineOpts, vm);
    po::notify(vm);

    // Parse Logging Options

    std::string logFileName = "";

    int logLevel = static_cast<int>(LOG_LEVEL::INFO);
    if (vm.count("logging.loglevel")) {
        logLevel = vm["logging.loglevel"].as<int>();
    }

    std::shared_ptr<ScreenPrinter> printer = std::make_shared<ScreenPrinter>();
    printer->setLogLevel(logLevel);

    if (vm.count("logging.logfile")) {
        std::string logFileName = vm["logging.logfile"].as<std::string>();
        printer->enableLogFile(logFileName);
    }

    bool printHandledReports = true;
    if (vm.count("logging.printreports")) {
        printHandledReports = vm["logging.printreports"].as<bool>();
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

    // Parse radio settings

    int SMNumber = -1;
    if (vm.count("radio.sharedmem")) {
        SMNumber = vm["radio.sharedmem"].as<int>();
        std::cout << "Using the specified CWSL shared memory interface number: " << SMNumber << std::endl;
    }

    double freqCal = 1.0;
    if (vm.count("radio.freqcalibration")) {
        freqCal = vm["radio.freqcalibration"].as<double>();
        std::cout.precision(std::numeric_limits<double>::max_digits10);
        std::cout << "Frequency Calibration Factor: " << freqCal << std::endl;
    }

    // Parse decoders

    int numFT4Decoders = 0;
    int numFT8Decoders = 0;
    int numWSPRDecoders = 0;

    using Decoder = std::tuple<std::uint32_t, std::string, int, double>;
    using DecoderVec = std::vector<Decoder>;
    DecoderVec decoders;

    if (vm.count("decoders.decoder")) {
        std::vector<std::string> decodersRawVec = vm["decoders.decoder"].as<std::vector<std::string>>();
        std::cout << "Found " << decodersRawVec.size() << " decoder entries" << std::endl;
        for (size_t k = 0; k < decodersRawVec.size(); k++) {
            const std::string& rawLine = decodersRawVec[k];
            auto decoderVecLine = splitStringByDelim(rawLine, ' ');
            if (decoderVecLine.size() != 2 && decoderVecLine.size() != 3 && decoderVecLine.size() != 4) {
                std::cerr << "Error parsing decoder line: " << rawLine << std::endl;
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
            else {
                std::cerr << "Error parsing decoder line, unknown mode! Line: " << rawLine << std::endl;
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
        std::cerr << "decoders.decoder input is required but was not specified!" << std::endl;
        return EXIT_FAILURE;
    }

    // Parse wsjtx settings

    int numJT9Instances = 0;
    if (vm.count("wsjtx.numjt9instances")) {
        numJT9Instances = vm["wsjtx.numjt9instances"].as<int>();
        if (numJT9Instances < 1) {
            std::cout << "wsjtx.numjt9instances must be >= 1" << std::endl;
            return EXIT_FAILURE;
        }
    }
    else {
        float decoderburden = 1;
        if (vm.count("wsjtx.decoderburden")) {
            decoderburden = vm["wsjtx.decoderburden"].as<float>();
            std::cout << "Decoder Burden specified as: " << decoderburden << std::endl;
        }
        const float nd1 = static_cast<float>(numFT4Decoders + numFT8Decoders) * 0.20f;
        const float nd2 = static_cast<float>(numWSPRDecoders) * 0.35f;
        const float nInstf = (nd1 + nd2) * decoderburden;
        numJT9Instances = static_cast<int>(std::round(nInstf + 0.55f));
    }

    std::cout << "Maximum number of simultaneous jt9.exe/wsprd.exe instances: " << numJT9Instances << std::endl;

    int highestDecodeFreq = 3000;
    if (vm.count("wsjtx.highestdecodefreq")) {
        highestDecodeFreq = vm["wsjtx.highestdecodefreq"].as<int>();
    }
    if (highestDecodeFreq > SSB_BW) {
        highestDecodeFreq = SSB_BW;
    }
    std::cout << "Decoding up to " << highestDecodeFreq << " Hz" << std::endl;

    std::string wavPath = "";
    if (vm.count("wsjtx.temppath")) {
        wavPath = vm["wsjtx.temppath"].as<std::string>();
    }
    else {
        std::cerr << "Missing wsjtx.temppath input argument!" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "Using temporary path for wav files: " << wavPath << std::endl;

    std::string binPath = "";
    if (vm.count("wsjtx.binpath")) {
        binPath = vm["wsjtx.binpath"].as<std::string>();
    }
    else {
        std::cerr << "Missing wsjtx.binpath input argument!" << std::endl;
        return EXIT_FAILURE;
    }

    bool keepWavFiles = false;
    if (vm.count("wsjtx.keepwav")) {
        keepWavFiles = true;
    }

    int decodedepth = 3;
    if (vm.count("wsjtx.decodedepth")) {
        decodedepth = vm["wsjtx.decodedepth"].as<int>();
        if (decodedepth > 3) {
            std::cerr << "wsjtx.decodedepth is too high, setting to 3" << std::endl;
            decodedepth = 3;
        }
        else if (decodedepth < 1) {
            std::cerr << "wsjtx.decodedepth is too small, setting to 1" << std::endl;
            decodedepth = 1;
        }
    }
    float ftAudioScaleFactor = 0.90f;
    if (vm.count("wsjtx.ftaudioscalefactor")) {
        ftAudioScaleFactor = vm["wsjtx.ftaudioscalefactor"].as<float>();
        if (ftAudioScaleFactor > 1.0f) {
            std::cerr << "ftaudioscalefactor must be <= 1.0" << std::endl;
            return EXIT_FAILURE;
        }
        else if (ftAudioScaleFactor < 0.0f) {
            std::cerr << "ftaudioscalefactor must be > 0" << std::endl;
            return EXIT_FAILURE;
        }
    }

    float wsprAudioScaleFactor = 0.20f;
    if (vm.count("wsjtx.wspraudioscalefactor")) {
        wsprAudioScaleFactor = vm["wsjtx.wspraudioscalefactor"].as<float>();
        if (wsprAudioScaleFactor > 1.0f) {
            std::cerr << "wsjtx.wspraudioscalefactor must be <= 1.0" << std::endl;
            return EXIT_FAILURE;
        }
        else if (wsprAudioScaleFactor < 0.0f) {
            std::cerr << "wsjtx.wspraudioscalefactor must be > 0" << std::endl;
            return EXIT_FAILURE;
        }
    }

    int maxDataAge = 10;
    if (vm.count("wsjtx.maxdataage")) {
        maxDataAge = vm["wsjtx.maxdataage"].as<int>();
        if (maxDataAge > 100) {
            std::cerr << "wsjtx.maxdataage must be <= 100" << std::endl;
            return EXIT_FAILURE;
        }
        else if (maxDataAge < 2) {
            std::cerr << "wsjtx.maxdataage must be >= 2" << std::endl;
            return EXIT_FAILURE;
        }
    }

    int wsprCycles = 3000;
    if (vm.count("wsjtx.wsprcycles")) {
        wsprCycles = vm["wsjtx.wsprcycles"].as<int>();
        if (wsprCycles > 10000) {
            std::cerr << "wsjtx.wsprcycles must be <= 10000" << std::endl;
            return EXIT_FAILURE;
        }
        else if (wsprCycles < 100) {
            std::cerr << "wsjtx.wsprcycles must be >= 100" << std::endl;
            return EXIT_FAILURE;
        }
    }

    int numjt9threads = 3;
    if (vm.count("wsjtx.numjt9threads")) {
        numjt9threads = vm["wsjtx.numjt9threads"].as<int>();
        if (numjt9threads > 9) {
            std::cerr << "wsjtx.numjt9threads is too high, setting to 9" << std::endl;
            numjt9threads = 9;
        }
        else if (numjt9threads < 1) {
            std::cerr << "wsjtx.numjt9threads is too small, setting to 1" << std::endl;
            numjt9threads = 1;
        }
    }

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
                std::cerr << "reporting.rbn option also requires reporting.aggregatorport" << std::endl;
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
            std::cout << "Missing operator.callsign input argument!" << std::endl;
            return EXIT_FAILURE;
        }
    }
    if (vm.count("operator.gridsquare")) {
         operatorLocator = vm["operator.gridsquare"].as<std::string>();
    }
    else {
        if (usePSKReporter) {
            std::cout << "Missing operator.gridsquare input argument!" << std::endl;
            return EXIT_FAILURE;
        }
    }

    //std::cout << "initializing output handler" << std::endl;

    outputHandler = std::make_shared<OutputHandler>(printHandledReports, badMessageLogFile, decodesFileName, printer);
    //std::cout << "initializing decoder pool" << std::endl;

    decoderPool = std::make_shared<DecoderPool>(keepWavFiles, printJT9Output, numJT9Instances, numjt9threads, decodedepth, wsprCycles, highestDecodeFreq, binPath, maxDataAge, printer, outputHandler);

    if (usePSKReporter) {
        std::cout << "Initializing PSKReporter interface" << std::endl;
        reporter = std::make_shared<pskreporter::PSKReporter>();
        const bool res = reporter->init(operatorCallsign, operatorLocator, PROGRAM_NAME + " " + PROGRAM_VERSION, printer);
        if (!res) {
            std::cerr << "Failed to initialize PSKReporter!" << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (useWSPRNet) {
        std::cout << "Initializing WSPRNet interface" << std::endl;
        wsprNet = std::make_shared<WSPRNet>(operatorCallsign, operatorLocator, printer);
        const bool res = wsprNet->init();
        if (!res) {
            std::cerr << "Failed to initialize WSPRNet!" << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (useRBN) {
        std::cout << "Initializing RBN Aggregator interface" << std::endl;
        rbn = std::make_shared<RBNHandler>();
        const bool res = rbn->init(operatorCallsign, operatorLocator, PROGRAM_NAME + " " + PROGRAM_VERSION, rbnIpAddr, rbnPort);
        if (!res) {
            std::cerr << "Failed to initialize RBN Aggregator interface!" << std::endl;
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

    // USB/LSB.  USB = 1, LSB = 0
    constexpr int USB = 1;

    for (size_t k = 0; k < decoders.size(); ++k) {
        auto decoder = decoders[k];
        auto f = std::get<0>(decoder);
        auto mode = std::get<1>(decoder);
        auto smnum = std::get<2>(decoder);
        auto d_freqcal = std::get<3>(decoder);

        if (mode != "FT8" && mode != "FT4" && mode != "WSPR") {
            std::cerr << "Unknown mode specified: " << mode << std::endl;
            return EXIT_FAILURE;
        }

        printer->print("Creating Instance " + std::to_string(k + 1) +
            " of " + std::to_string(decoders.size()) + " for frequency " + 
            std::to_string(f) + " mode " + mode, LOG_LEVEL::INFO);

        std::unique_ptr<Instance> instance = std::make_unique<Instance>();
        instances.push_back(std::move(instance));
        printer->print("Initializing instance " + std::to_string(k + 1) + " of " + std::to_string(decoders.size()), LOG_LEVEL::DEBUG);
        const bool status = instances.back()->init(
            f, 
            smnum, 
            mode, 
            SSB_BW, 
            freqCal * d_freqcal, // global freq cal * decoder freq cal 
            highestDecodeFreq, 
            wavPath, 
            Wave_SR, 
            decodedepth, 
            numjt9threads, 
            ftAudioScaleFactor, 
            wsprAudioScaleFactor, 
            printer, 
            decoderPool);
        if (!status) {
            std::cerr << "Failed to initialize decoder instance!" << std::endl;
            return EXIT_FAILURE;
        }
    }

    //
    //  Main Loop
    //

    std::cout << std::endl << "Main loop started! Press Q to terminate." << std::endl;
    terminateFlag = false;
    while (!terminateFlag) {
        // Was Exit requested?
        if (_kbhit()) {
            const char ch = _getch();
            if (ch == 'Q' || ch == 'q') {
                std::cout << "Q pressed, so terminating" << std::endl;
                terminateFlag = true;
                for (size_t k = 0; k < instances.size(); ++k) {
                    std::cout << "terminating instance " << k+1 << " of " << instances.size() << std::endl;
                    instances[k]->terminate();
                }
                std::cout << "terminating screen printer" << std::endl;
                printer->terminate();
                std::cout << "terminating decoder pool" << std::endl;
                decoderPool->terminate();
                std::cout << "terminating PSKReporter interface" << std::endl;
                if (reporter) { reporter->terminate(); }
                std::cout << "terminating WSPRNet interface" << std::endl;
                if (wsprNet) { wsprNet->terminate(); }
                std::cout << "terminating RBN Aggregator interface" << std::endl;
                if (rbn) { rbn->terminate(); }
            }
        }
        // 88 ms == 88mph == 142km/h == 1.21 gigawatts
        std::this_thread::sleep_for(std::chrono::milliseconds(88));
    }
    std::cout << "Exiting" << std::endl;
    return EXIT_SUCCESS;
}    

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

#include "SharedMemory.h"

std::atomic_bool syncThreadTerminateFlag = false;

#include "CWSL_DIGI.hpp"

#include "OutputHandler.hpp"
#include "Instance.hpp"
#include "Receiver.hpp"
#include "Stats.hpp"
#include "DecoderPool.hpp"

#include "PSKReporter.hpp"
#include "RBNHandler.hpp"
#include "WSPRNet.hpp"

#include "boost/program_options.hpp"

#include "ScreenPrinter.hpp"
#include "Decoder.hpp"

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

//std::vector<std::unique_ptr<Instance>> instances;

std::shared_ptr<DecoderPool> decoderPool;
std::shared_ptr<OutputHandler> outputHandler;
std::shared_ptr<ScreenPrinter> printer;
std::shared_ptr<Stats> statsHandler;

DecoderVec decoders;

SyncPredicates preds;

float ftAudioScaleFactor = 0.90f;
float wsprAudioScaleFactor = 0.20f;

static inline bool setupDecoder(Decoder& decoder, size_t instanceId) {

    printer->debug("Calibrated instance frequency: " + std::to_string(decoder.getFreqCalibrated()));

    const int nMem = findBand(static_cast<std::int64_t>(decoder.getFreqCalibrated()), decoder.getsmNum());
    std::shared_ptr<Receiver> receiver = nullptr;
    if (-1 == nMem) {
        printer->debug("Unable to open CWSL shared memory at the specified frequency. Bad frequency or sharedmem specified?");
        printer->debug("Note that frequency calibration may shift the expected frequency outside of what is expected!");
        return true; // false only returned on catastrophic failure
    }
    else {
        const std::string smname = createSharedMemName(nMem, decoder.getsmNum());
        auto it = receivers.find(smname);
        if (it != receivers.end()) {
            printer->debug("Using existing receiver interface");
            receiver = it->second;
        }
        else {
            printer->debug("Creating receiver interface");
            receivers.emplace(smname, std::make_shared<Receiver>(
                smname,
                printer
                ));
            receiver = receivers[smname];
            receiver->init();
        }
        printer->print("Creating Instance for frequency " +
            std::to_string(decoder.getFreq()) + " mode " + decoder.getMode(), LOG_LEVEL::INFO);

        if (!decoder.getInstance()) {
            std::shared_ptr<SyncPredicate> pred = preds.createPredicate(decoder.getMode());

            std::unique_ptr<Instance> inst = std::make_unique<Instance>(
                receiver,
                instanceId,
                pred,
                decoder.getFreq(),
                decoder.getFreqCalibrated(),
                decoder.getMode(),
                decoder.getReporterCallsign(),
                Wave_SR,
                ftAudioScaleFactor,
                wsprAudioScaleFactor,
                printer,
                decoderPool,
                decoder.getTRPeriod()
                );

            decoder.setInstance(std::move(inst));
        }
        else {
            decoder.getInstance()->setReceiver(receiver);
        }
        
        printer->print("Initializing instance ", LOG_LEVEL::DEBUG);
        try {
            const bool status = decoder.getInstance()->init();
            if (!status) {
                printer->err("Failed to initialize decoder instance");
                return false;
            }
        }
        catch (const std::exception& e) {
            printer->print(e);
            return false;
        }
    }
    return true;
}

static inline void waitForTimeQ65_30(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    int goSec = -1;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            std::time_t t = std::time(nullptr);
            tm* ts = std::gmtime(&t);
            if (go && ts->tm_sec == goSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = ts->tm_sec == 0 || ts->tm_sec == 30;
            if (go) {
                printer->print("Signalling beginning of Q65-30 interval...", LOG_LEVEL::DEBUG);
                goSec = ts->tm_sec;
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            printer->print("waitForTimeQ65_30", e);
        }
    } // while
    printer->debug("Q65-30 Synchronization thread exiting");
}

static inline void waitForTime60(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    int goSec = -1;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            std::time_t t = std::time(nullptr);
            tm* ts = std::gmtime(&t);
            if (go && ts->tm_sec == goSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = ts->tm_sec == 0;
            if (go) {
                printer->print("Signalling beginning of 60s interval...", LOG_LEVEL::DEBUG);
                goSec = ts->tm_sec;
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            printer->print("waitForTime60", e);
        }
    } // while
    printer->debug("60s Synchronization thread exiting");
}

static inline void waitForTimeFT8(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    int goSec = -1;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            std::time_t t = std::time(nullptr);
            tm* ts = std::gmtime(&t);
            if (go && ts->tm_sec == goSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = ts->tm_sec == 0 || ts->tm_sec == 15 || ts->tm_sec == 30 || ts->tm_sec == 45;
            if (go) {
                printer->print("Signalling beginning of FT8 interval...", LOG_LEVEL::DEBUG);
                goSec = ts->tm_sec;
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            printer->print("waitForTimeFT8", e);
        }
    } // while
    printer->debug("FT8 Synchronization thread exiting");
}

static inline void waitForTime1800(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    SYSTEMTIME time;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            GetSystemTime(&time);
            const std::uint16_t min = static_cast<std::uint16_t>(time.wMinute);
            const bool minFlag = min % 30 == 0;
            const bool secFlag = time.wSecond == 0;
            if (minFlag && secFlag && go) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = minFlag && secFlag;
            if (go) {
                printer->print("Signalling beginning of 1800s interval...", LOG_LEVEL::DEBUG);
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            } //if
            else if (minFlag || (!minFlag && time.wSecond <= 55)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        } // try
        catch (const std::exception& e) {
            printer->print("waitForTime1800", e);
        } // catch
    } // while
    printer->debug("1800s Synchronization thread exiting");
}

static inline void waitForTime900(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    SYSTEMTIME time;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            GetSystemTime(&time);
            const std::uint16_t min = static_cast<std::uint16_t>(time.wMinute);
            const bool minFlag = min % 15 == 0;
            const bool secFlag = time.wSecond == 0;
            if (minFlag && secFlag && go) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = minFlag && secFlag;
            if (go) {
                printer->print("Signalling beginning of 900s interval...", LOG_LEVEL::DEBUG);
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            } //if
            else if (minFlag || (!minFlag && time.wSecond <= 55)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        } // try
        catch (const std::exception& e) {
            printer->print("waitForTime900", e);
        } // catch
    } // while
    printer->debug("900s Synchronization thread exiting");
}

static inline void waitForTime300(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    SYSTEMTIME time;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            GetSystemTime(&time);
            const std::uint16_t min = static_cast<std::uint16_t>(time.wMinute);
            const bool minFlag = min % 5 == 0;
            const bool secFlag = time.wSecond == 0;
            if (minFlag && secFlag && go) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = minFlag && secFlag;
            if (go) {
                printer->print("Signalling beginning of 300s interval...", LOG_LEVEL::DEBUG);
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            } //if
            else if (minFlag || (!minFlag && time.wSecond <= 55)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        } // try
        catch (const std::exception& e) {
            printer->print("waitForTime300", e);
        } // catch
    } // while
    printer->debug("300s Synchronization thread exiting");
}


static inline void waitForTime120(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    SYSTEMTIME time;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            GetSystemTime(&time);
            const std::uint16_t min = static_cast<std::uint16_t>(time.wMinute);
            const bool minFlag = (min & 1) == 0;
            const bool secFlag = time.wSecond == 0;
            if (minFlag && secFlag && go) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = minFlag && secFlag;
            if (go) {
                printer->print("Signalling beginning of 120s interval...", LOG_LEVEL::DEBUG);
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            } //if
            else if (minFlag || (!minFlag && time.wSecond <= 55)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        } // try
        catch (const std::exception& e) {
            printer->print("waitForTime120", e);
        } // catch
    } // while
    printer->debug("120s Synchronization thread exiting");
}


static inline void waitForTimeFT4(std::shared_ptr<ScreenPrinter> printer, std::vector<std::shared_ptr<SyncPredicate>>& preds) {
    SYSTEMTIME time;
    int goSec = -1;
    bool go = false;
    while (!syncThreadTerminateFlag) {
        try {
            GetSystemTime(&time);
            const WORD s = time.wSecond;
            if (go && s == goSec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MAX_SLEEP_MS));
                continue;
            }
            go = false;
            switch (s) {
            case 0:
            case 15:
            case 30:
            case 45:
                go = true;
                break;
            case 7:
            case 22:
            case 37:
            case 52:
                while (time.wMilliseconds < 300 && s == time.wSecond) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(400 - time.wMilliseconds));
                    GetSystemTime(&time);
                }
                go = true;
                break;
            default:
                break;
            } // switch
            if (go) {
                printer->print("Beginning FT4 interval...", LOG_LEVEL::DEBUG);
                goSec = time.wSecond;
                for (size_t k = 0; k < preds.size(); ++k) {
                    preds[k]->store(true);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            printer->print("waitForTimeFT4", e);
        }
    } // while
    printer->debug("FT4 Synchronization thread exiting");
}


void cleanup() {
    for (size_t k = 0; k < decoders.size(); ++k) {
        decoders[k].terminate();
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

void reportStats(std::shared_ptr<Stats> statsHandler, std::shared_ptr<ScreenPrinter> printer, std::vector<Decoder>& instances, const int reportingInterval) {
    while (!syncThreadTerminateFlag) {
        for (int k = 0; k < reportingInterval * 5; ++k) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        try {
            statsHandler->process();
        }
        catch (const std::exception& e) {
            printer->print(e);
            continue;
        }

        try {
            std::stringstream hdr;
            hdr << std::setfill(' ') << std::left << std::setw(10) << "Instance" << std::setw(16) << "Status" << std::setw(12) << "Frequency" << std::setw(12) << "Mode" << std::setw(8) << "24 Hour" << std::setw(8) << "1 Hour" << std::setw(8) << "5 Min" << std::setw(8) << "1 Min";
            printer->print(hdr.str());
            for (std::size_t k = 0; k < instances.size(); ++k) {
                auto v1Min = statsHandler->getCounts(k, 60);
                auto v5Min = statsHandler->getCounts(k, 300);
                auto v1Hr = statsHandler->getCounts(k, 3600);
                auto v24Hr = statsHandler->getCounts(k, 86400);
                const auto mode = instances[k].getMode();
                std::string status;

                if (instances[k].getStatus() == InstanceStatus::RUNNING) {
                    status = "Running";
                }
                else if (instances[k].getStatus() == InstanceStatus::FINISHED) {
                    status = "Inactive";
                }
                else if (instances[k].getStatus() == InstanceStatus::STOPPED) {
                    status = "Stopped";
                }
                else if (instances[k].getStatus() == InstanceStatus::NOT_INITIALIZED) {
                    status = "Uninitialized";
                }
                std::stringstream s;
                s << std::setfill(' ') << std::left << std::setw(10) << std::to_string(k) << std::setw(16) << status << std::setw(12) << std::to_string(instances[k].getFreq()) << std::setw(12) << mode << std::setw(8) << std::to_string(v24Hr) << std::setw(8) << std::to_string(v1Hr) << std::setw(8) << std::to_string(v5Min) << std::setw(8) << std::to_string(v1Min);
                printer->print(s.str());
            }
        }
        catch (const std::exception& e) {
            printer->print(e);
            continue;
        }
    }

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
        ("decoders.decoder", po::value<std::vector<std::string>>()->multitoken(), "freq, mode, shmem, freqcal, callsign")
//        ("radio.extiodll", po::value<std::string>(), "path and file name of your SDRs extio.dll, or blank if using CWSL")
        ("radio.freqcalibration", po::value<double>(), "frequency calibration factor in PPM, default 1.0000000000")
        ("radio.sharedmem", po::value<int>(), "CWSL shared memory interface number to use, default -1")
        ("reporting.pskreporter", po::value<bool>(), "Send spots to PSK Reporter, default false")
        ("reporting.wsprnet", po::value<bool>(), "Send WSPR spots to WSPRNet, default false")
        ("reporting.rbn", po::value<bool>(), "enables sending spots to RBN Aggregator, default false")
        ("reporting.aggregatorport", po::value<int>(), "port number for datagrams sent to RBN Aggregator")
        ("reporting.aggregatorip", po::value<std::string>(), "ip address for RBN Aggregator, default 127.0.0.1")
        ("reporting.ignoredcalls", po::value<std::vector<std::string>>()->multitoken(), "list of callsigns to ignore")
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
        ("wsjtx.ftaudioscalefactor", po::value<float>(), "Audio scale factor for all modes but WSPR, default 0.90")
        ("wsjtx.wspraudioscalefactor", po::value<float>(), "WSPR audio scale factor, default 0.20")
        ("wsjtx.maxdataage", po::value<int>(), "Max data age to decode, in seconds, default 300")
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
    bool bUseExtioDLL = false;
    std::string extioPath;
    if (vm.count("radio.extiodll")) {
        extioPath = vm["radio.extiodll"].as<std::string>();
        printer->print("ExtIO DLL path: " + extioPath);
        bUseExtioDLL = true;
    }

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

    // parse operator info

    if (vm.count("operator.callsign")) {
        operatorCallsign = vm["operator.callsign"].as<std::string>();
    }
    else {
        printer->err("Missing operator.callsign input argument!");
        cleanup();
        return EXIT_FAILURE;
    }
    if (vm.count("operator.gridsquare")) {
        operatorLocator = vm["operator.gridsquare"].as<std::string>();
    }
    else {
        printer->print("Missing operator.gridsquare input argument!");
        cleanup();
        return EXIT_FAILURE;
    }

    // Parse decoders

    int numFT4Decoders = 0;
    int numFT8Decoders = 0;
    int numWSPRDecoders = 0;
    int numQ65_30Decoders = 0;
    int numJT65Decoders = 0;

    int numFST4Decoders = 0;
    int numFST4WDecoders = 0;

    // period-specific decoders, excluding WSPR
    int num60sDecoders = 0;
    int num120sDecoders = 0;
    int num300sDecoders = 0;
    int num900sDecoders = 0;
    int num1800sDecoders = 0;

    if (vm.count("decoders.decoder")) {
        std::vector<std::string> decodersRawVec = vm["decoders.decoder"].as<std::vector<std::string>>();
        printer->print("Found " + std::to_string(decodersRawVec.size()) + " decoder entries");
        for (size_t k = 0; k < decodersRawVec.size(); k++) {
            const std::string& rawLine = decodersRawVec[k];
            auto decoderVecLine = splitStringByDelim(rawLine, ' ');
            if (decoderVecLine.size() != 2 && decoderVecLine.size() != 3 && decoderVecLine.size() != 4 && decoderVecLine.size() != 5) {
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
            else if (mode == "JT65") {
                numJT65Decoders++;
                num60sDecoders++;
            }
            else if (mode == "FST4-60") {
                numFST4Decoders++;
                num60sDecoders++;
            }
            else if (mode == "FST4-120") {
                numFST4Decoders++;
                num120sDecoders++;
            }
            else if (mode == "FST4-300") {
                numFST4Decoders++;
                num300sDecoders++;
            }
            else if (mode == "FST4-900") {
                numFST4Decoders++;
                num900sDecoders++;
            }
            else if (mode == "FST4-1800") {
                numFST4Decoders++;
                num1800sDecoders++;
            }
            else if (mode == "FST4W-120") {
                numFST4WDecoders++;
                num120sDecoders++;
            }
            else if (mode == "FST4W-300") {
                numFST4WDecoders++;
                num300sDecoders++;
            }
            else if (mode == "FST4W-900") {
                numFST4WDecoders++;
                num900sDecoders++;
            }
            else if (mode == "FST4W-1800") {
                numFST4WDecoders++;
                num1800sDecoders++;
            }
            else {
                printer->err("Error parsing decoder line, unknown mode: " + mode + "Full Line: " + rawLine);
                cleanup();
                return EXIT_FAILURE;
            }
            int smnum = SMNumber;
            if (bUseExtioDLL) {
                printer->info("Loading Extio DLL library: " + extioPath);
                HMODULE extioModH = LoadLibraryA(extioPath.c_str());
                if (!extioModH) {
                    printer->err("Error loading Extio DLL");
                    return EXIT_FAILURE;
                }
                printer->info("Loaded Extio DLL library");
            }
            else {
                if (decoderVecLine.size() >= 3) {
                    smnum = std::stoi(decoderVecLine[2]);
                }
            }
            double decoder_freqcal = 1.0;
            if (decoderVecLine.size() >= 4) {
                decoder_freqcal = std::stod(decoderVecLine[3]);
            }
            std::string decoder_callsign = operatorCallsign;
            if (decoderVecLine.size() >= 5) {
                if (mode != "WSPR") {
                    printer->err("Callsigns are only supported per-decoder for WSPR decoders");
                    return EXIT_FAILURE;
                }
                decoder_callsign = decoderVecLine[4];
            }
            const FrequencyHz decoderFreqCalibrated = static_cast<FrequencyHz>(freq / (freqCalGlobal * decoder_freqcal));
            decoders.emplace_back(freq, decoderFreqCalibrated, mode, smnum, decoder_freqcal, decoder_callsign);
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
        const float nd3 = static_cast<float>(numJT65Decoders) * (1.0f / 3.0f); // 1 per 3 JT-65
        const float nd4 = static_cast<float>(numFST4WDecoders) * (1.0f / 3.0f); // 1 per 3 FST4W
        const float nd5 = static_cast<float>(numFST4Decoders) * (1.0f / 3.0f); // 1 per 3 FST4
        const float nInstf = (nd1 + nd2 + nd3 + nd4 + nd5) * decoderburden;
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
    printer->print("Maximum SSB audio frequency for decode: " + std::to_string(highestDecodeFreq) + " Hz");

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

    int maxDataAge = 300;
    if (vm.count("wsjtx.maxdataage")) {
        maxDataAge = vm["wsjtx.maxdataage"].as<int>();
        if (maxDataAge > 600) {
            printer->err("wsjtx.maxdataage must be <= 600");
            cleanup();
            return EXIT_FAILURE;
        }
        else if (maxDataAge < 30) {
            printer->err("wsjtx.maxdataage must be >= 30");
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

    statsHandler = std::make_shared<Stats>(86400, static_cast<std::uint32_t>(decoders.size()));

    outputHandler = std::make_shared<OutputHandler>(printHandledReports, badMessageLogFile, decodesFileName, printer, statsHandler, decoders);

    if (vm.count("reporting.ignoredcalls")) {
        std::vector<std::string> ignoredRawVec = vm["reporting.ignoredcalls"].as<std::vector<std::string>>();
        for (size_t k = 0; k < ignoredRawVec.size(); k++) {
            const std::string& rawLine = ignoredRawVec[k];
            auto vecLine = splitStringByDelim(rawLine, ' ');
            for (const auto& call : vecLine) {
                printer->info("Will ignore callsign: " + call);
                outputHandler->ignoreCallsign(call);
            }
        }
    }

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
        wsprNet = std::make_shared<WSPRNet>(operatorLocator, printer);
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

    std::thread ft8SignalThread;
    if (numFT8Decoders) {
        ft8SignalThread = std::thread(&waitForTimeFT8, printer, std::ref(preds.ft8Preds));
        ft8SignalThread.detach();
    }

    std::thread ft4SignalThread;
    if (numFT4Decoders) {
        ft4SignalThread = std::thread(&waitForTimeFT4, printer, std::ref(preds.ft4Preds));
        ft4SignalThread.detach();
    }

    std::thread signal60sThread;
    if (num60sDecoders) {
        signal60sThread = std::thread(&waitForTime60, printer, std::ref(preds.s60sPreds));
        signal60sThread.detach();
    }
    std::thread signal120sThread;
    if (numWSPRDecoders || num120sDecoders) {
        signal120sThread = std::thread(&waitForTime120, printer, std::ref(preds.s120sPreds));
        signal120sThread.detach();
    }
    std::thread signal300sThread;
    if (num300sDecoders) {
        signal300sThread = std::thread(&waitForTime300, printer, std::ref(preds.s300sPreds));
        signal300sThread.detach();
    }
    std::thread signal900sThread;
    if (num900sDecoders) {
        signal900sThread = std::thread(&waitForTime900, printer, std::ref(preds.s900sPreds));
        signal900sThread.detach();
    }
    std::thread signal1800sThread;
    if (num1800sDecoders) {
        signal1800sThread = std::thread(&waitForTime1800, printer, std::ref(preds.s1800sPreds));
        signal1800sThread.detach();
    }
    std::thread q65_30SignalThread;
    if (numQ65_30Decoders) {
        q65_30SignalThread = std::thread(&waitForTimeQ65_30, printer, std::ref(preds.q65_30Preds));
        q65_30SignalThread.detach();
    }


    // USB/LSB.  USB = 1, LSB = 0
    constexpr int USB = 1;

    for (size_t k = 0; k < decoders.size(); ++k) {
        auto& decoder = decoders[k];
        const bool s = setupDecoder(decoder, k);
        if (!s) {
            printer->err("Decoder failed to setup!");
            return EXIT_FAILURE;
        }
    }

    std::thread statsThread = std::thread(&reportStats, std::ref(statsHandler), printer, std::ref(decoders), statsReportingInterval);
    SetThreadPriority(statsThread.native_handle(), THREAD_PRIORITY_IDLE);
    statsThread.detach();

    //
    //  Main Loop
    //

    printer->print("Main loop starting");

    int counter = 0;

    std::chrono::time_point<std::chrono::steady_clock> lastRBNStatusMessageTime = std::chrono::steady_clock::now() - 1h;

    while (1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(MAIN_LOOP_SLEEP_MS));
        std::vector< std::string > receiversToErase;
        for (auto it : receivers) {
            auto& r = it.second;
            if (r->getStatus() == ReceiverStatus::STOPPED) {
                r->terminate();
                receiversToErase.push_back(it.first);
            }
        }
        for (auto&& key : receiversToErase) {
            receivers.erase(key);
        }
        if (counter == 10) {
            for (size_t k = 0; k < decoders.size(); ++k) {
                auto& d = decoders[k];
                auto s = d.getStatus();
                if (s == InstanceStatus::FINISHED) {
                    setupDecoder(d, k);
                }
            }
            counter = 0;
        }
        else {
            counter++;
        }
        if (useRBN) {
            if (std::chrono::steady_clock::now() - lastRBNStatusMessageTime > 60s) {

                RBNStatus status;
                status.highestDecodeFreq = highestDecodeFreq;

                for (size_t k = 0; k < decoders.size(); ++k) {
                    auto& d = decoders[k];
                    auto s = d.getStatus();
                    if (s == InstanceStatus::RUNNING) {
                        RBNDecoder rbnDecoder;
                        rbnDecoder.mode = d.getMode();
                        rbnDecoder.freq = d.getFreq();
                        status.decoders.push_back(rbnDecoder);
                    }
                }

                rbn->handleStatus(status);

                lastRBNStatusMessageTime = std::chrono::steady_clock::now();

            }
        }
    }

    std::cout << "Exiting" << std::endl;
    return EXIT_SUCCESS;
}    

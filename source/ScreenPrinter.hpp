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

#include <string>
#include <chrono>
#include <thread>
#include <iostream>

#include "SafeQueue.h"

enum LOG_LEVEL : int {
    MAX_VERBOSE = 8,
    TRACE = 5,
    DEBUG = 4,
    INFO = 3,
    WARN = 2,
    ERR = 1,
    NONE = 0,
};

struct LogMessage {
    LogMessage(const std::string& s, const LOG_LEVEL l) : 
        str(s),
        lvl(l)
        {}
    std::string str;
    LOG_LEVEL lvl;
};

class ScreenPrinter {
public:
    ScreenPrinter() : 
    terminateFlag(false),
    logLevel(LOG_LEVEL::INFO),
    logFileEnabled(false),
    logFileName("")
    {
        printThread = std::thread(&ScreenPrinter::flush, this);
        printThread.detach();
    }
    
    virtual ~ScreenPrinter() 
    {
        if (logFileEnabled) {
            ofs.close();
        }
    }

    void enableLogFile(const std::string& fn) {
        logFileEnabled = true;
        logFileName = fn;
        ofs.open(logFileName, std::ios_base::app | std::ofstream::out);
    }

    void setLogLevel(LOG_LEVEL lvl) {
        logLevel = lvl;
    }

    void setLogLevel(int lvl) {
        setLogLevel(static_cast<LOG_LEVEL>(lvl));
    }

    void info(const std::string& message) {
        strs.enqueue(LogMessage(message, LOG_LEVEL::INFO));
    }

    void print(const std::string& message) {
        info(message);
    }

    void print(const std::string& message, LOG_LEVEL lvl) {
        strs.enqueue(LogMessage(message, lvl));
    }

    void debug(const std::string& message) {
        strs.enqueue(LogMessage(message, LOG_LEVEL::DEBUG));
    }

    void err(const std::string& message) {
        strs.enqueue(LogMessage(message, LOG_LEVEL::ERR));
    }

    void flush() {
        while (!terminateFlag) {
            auto message = strs.dequeue();
            if (LOG_LEVEL::NONE == logLevel) {
                continue; // Never print!
            }

            if (message.lvl <= logLevel) {
                std::cout << message.str << std::endl;
                if (logFileEnabled) {
                    ofs << message.str << std::endl;
                }
            }
        }
    }

    void terminate() {
        terminateFlag = true;
    }

private:

    SafeQueue<LogMessage> strs;
    std::thread printThread;
    LOG_LEVEL logLevel;
    bool terminateFlag;

    bool logFileEnabled;
    std::string logFileName;
    std::ofstream ofs;

};

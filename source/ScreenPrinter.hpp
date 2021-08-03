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
#include <fstream>
#include <iomanip>
#include <sstream>
#include "SafeQueue.h"

#include <windows.h> // GetSystemTime

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
        lvl(l) {
            GetSystemTime(&time);
        }
    std::string str;
    LOG_LEVEL lvl;
    SYSTEMTIME time;
};

class ScreenPrinter {
public:
    ScreenPrinter(const bool logImmediately) : 
    terminateFlag(false),
    logLevel(LOG_LEVEL::INFO),
    logFileEnabled(false),
    logFileName(""),
    immediate(logImmediately)
    {
        if (!immediate) {
            printThread = std::thread(&ScreenPrinter::printLoop, this);
            SetThreadPriority(printThread.native_handle(), THREAD_PRIORITY_IDLE);
            printThread.detach();
        }
    }
    
    virtual ~ScreenPrinter() 
    {
        flush();
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
        log(message, LOG_LEVEL::INFO);
    }

    void print(const std::string& message) {
        info(message);
    }

    void print(const std::string& message, LOG_LEVEL lvl) {
        log(message, lvl);
    }

    void warning(const std::string& message) {
        log(message, LOG_LEVEL::WARN);
    }

    void print(const std::exception& e) {
        err(std::string("Caught Exception: ") + e.what());
    }

    void print(const std::string& msg, const std::exception& e) {
        err(msg);
        err(std::string("Caught Exception: ") + e.what());
    }

    void debug(const std::string& message) {
        log(message, LOG_LEVEL::DEBUG);
    }

    void trace(const std::string& message) {
        log(message, LOG_LEVEL::TRACE);
    }

    void err(const std::string& message) {
        log(message, LOG_LEVEL::ERR);
    }

    void log(const LOG_LEVEL lvl, const std::string& message) {
        log(message, lvl);
    }

    void log(const std::string& message, const LOG_LEVEL lvl) {
        if (lvl <= logLevel) {
            if (immediate) {
                LogMessage msg = LogMessage(message, lvl);
                processMessage(msg);
            }
            else {
                strs.enqueue(LogMessage(message, lvl));
            }
        }
    }

    void printLoop() {
        while (1) {
            flush();
            if (terminateFlag) { return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (terminateFlag) { return; }
        }
    }

    void processMessage(LogMessage& message) {
        std::string str = "";
        if (LOG_LEVEL::ERR == message.lvl) {
            str = "### ERROR :: " + message.str;
        }
        else if (LOG_LEVEL::WARN == message.lvl) {
            str = "@@@ WARNING ::" + message.str;
        }
        else if (LOG_LEVEL::TRACE == message.lvl) {
            str = "%%% TRACE :: " + message.str;
        }
        else {
            str = message.str;
        }
        std::stringstream ss;
        ss << std::setw(2) << std::setfill('0') << message.time.wYear
            << "-"
            << std::setw(2) << std::setfill('0') << message.time.wMonth
            << "-"
            << std::setw(2) << std::setfill('0') << message.time.wDay
            << " "
            << std::setw(2) << std::setfill('0') << message.time.wHour
            << ":"
            << std::setw(2) << std::setfill('0') << message.time.wMinute
            << ":"
            << std::setw(2) << std::setfill('0') << message.time.wSecond
            << "."
            << std::setw(3) << std::setfill('0') << message.time.wMilliseconds
            ;

        str = ss.str() + " " + str;

        // Write to console
        std::cout << str << std::endl;

        // Write to log file
        if (logFileEnabled) {
            ofs << str << std::endl;
        }
    }

    void flush() {
        do {
            auto message = strs.dequeue();
            processMessage(message);
        } while (!strs.empty());
    }

    void terminate() {
        debug("printer received terminate flag");
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
    bool immediate;
};

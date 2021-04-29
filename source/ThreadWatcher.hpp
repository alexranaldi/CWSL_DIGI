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

#include <atomic>
#include <cstdint>
#include <chrono>
#include <utility>
#include <iostream>

enum ThreadStatus {
    Running,
    Finished
};

constexpr std::uint64_t DEFAULT_THRESH_MS = 1000;

class WatchedThread {
public:
    WatchedThread() :
        allowedDelta(DEFAULT_THRESH_MS),
        name(""),
        time(0),
        status(ThreadStatus::Finished)
    {
    }

    WatchedThread(const std::string n) :
        allowedDelta(DEFAULT_THRESH_MS),
        name(n),
        time(0),
        status(ThreadStatus::Finished)
    {
    }

    virtual ~WatchedThread()
    {}

    std::string name;
    std::atomic<std::uint64_t> time;
    std::atomic<ThreadStatus> status;
    std::uint64_t allowedDelta;
};


class ThreadWatcher {
    public:
    ThreadWatcher() : threads(0)
    {
    }

    virtual ~ThreadWatcher() {

    }

    std::uint64_t getEpochTimeMs() const {
        return std::chrono::steady_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    }

    ThreadStatus getStatus(const size_t index) {
        return threads[index]->status.load();
    }

    std::size_t numThreads() const {
        return threads.size();
    }

    std::pair<bool, std::int64_t> check(const size_t index) {
        std::pair<bool, std::uint64_t> p = std::make_pair(false, 0);
        if (getStatus(index) != ThreadStatus::Running) {
            return p;
        }
        const std::uint64_t last = getTime(index);
        const std::uint64_t curr = getEpochTimeMs();
        const std::int64_t delta = curr - last;
        p.second = delta;
        // check for > 0 in case we end up with -0
        const bool failed = delta > 0 && delta > threads[index]->allowedDelta;
        p.first = !failed;
        return p;
    }

    void setAllowedDelta(const size_t index, const uint64_t d) {
        threads[index]->allowedDelta = d;
    }

    void threadFinished(const size_t index) {
        threads[index]->status.store(ThreadStatus::Finished);
    }

    void threadStarted(const size_t index) {
        report(index);
        threads[index]->status.store(ThreadStatus::Running);
    }

    void report(const size_t index) {
        setTime(index, getEpochTimeMs());
    }

    std::uint64_t getTime(const size_t index) const {
        return threads[index]->time.load();
    }

    std::string getName(const size_t index) const {
        return threads[index]->name;
    }

    void inline setTime(const size_t index, const std::uint64_t newVal) {
        threads[index]->time.store(newVal);
    }

    size_t addThread(const std::string& name) {

        std::unique_ptr<WatchedThread> w = std::make_unique<WatchedThread>(name);
        threads.push_back(std::move(w));
        return threads.size() - 1;
    }

private:
    std::vector< std::unique_ptr< WatchedThread > > threads;

};

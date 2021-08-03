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
#include <thread>
#include <chrono>

// Used internally as a single producer single consumer queue (ring buffer)
template <typename T>
struct ring_buffer_t {
    T* recs;
    // Use atomics for indices to prevent instruction reordering
    std::atomic<std::uint64_t> read_index;
    std::atomic<std::uint64_t> write_index;
    // Number of items the ring buffer can hold.
    std::size_t size;
    std::atomic_bool terminateFlag = false;
    bool initialized = false;

    ring_buffer_t() :
        terminateFlag(false),
        recs(nullptr),
        initialized(false),
        size(0)
    {
    }

    bool initialize(const size_t sizeIn)
    {
        size = sizeIn;
        recs = reinterpret_cast<T*>(malloc(sizeof(T) * size));
        initialized = recs != nullptr;
        reset();
        return initialized;
    }

    void reset() {
        read_index = 0;
        write_index = 0;
        terminateFlag = false;
    }

    void terminate()
    {
        terminateFlag = true;
    }

    ~ring_buffer_t()
    {
        if (nullptr != recs)
        {
            free(recs);
        }
    }

    bool full() const {
        return ((read_index == write_index + 1) || (read_index == 0 && static_cast<int64_t>(write_index) == static_cast<int64_t>(size) - 1));
    }

    bool wait_for_empty_slot() const
    {
        while (full()) {
            std::this_thread::yield();
            if (terminateFlag) {
                return false;
            }
        }
        return true;
    }

    void inc_write_index()
    {
        // cast to signed so we don't break subtraction
        if (static_cast<int64_t>(write_index) == static_cast<int64_t>(size) - 1) {
            write_index = 0;
        }
        else {
            write_index++;
        }
    }

    std::uint64_t get_next_write_index()
    {
        // cast to signed so we don't break subtraction
        if (static_cast<int64_t>(write_index) == static_cast<int64_t>(size) - 1) {
            return 0;
        }
        else {
            return write_index + 1;
        }
    }

    T& current() {
        return recs[read_index];
    }

    T& pop_ref()
    {
        T& curr = recs[read_index];
        if (read_index == size - 1) {
            read_index = 0;
        }
        else {
            read_index++;
        }
        return curr;
    }

    T pop()
    {
        T curr = recs[read_index];
        if (read_index == size - 1) {
            read_index = 0;
        }
        else {
            read_index++;
        }
        return curr;
    }

    bool wait_for_data() const
    {
        while (empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (terminateFlag) {
                return false;
            }
        }
        return true;
    }

    bool empty() const {
        return read_index == write_index;
    }

};

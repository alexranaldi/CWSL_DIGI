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
struct ring_buffer_spmc_t {
    T* recs;
    // Use atomics for indices to prevent instruction reordering
    std::vector<std::unique_ptr<std::atomic<std::uint64_t>>> read_index;
    std::atomic<std::uint64_t> write_index;
    // Number of items the ring buffer can hold.
    std::size_t size;
    std::atomic_bool terminateFlag = false;
    bool initialized = false;

    ring_buffer_spmc_t() :
        terminateFlag(false),
        recs(nullptr),
        initialized(false),
        size(0)
    {
    }

    std::uint64_t inline getReadIndex(const size_t readerIndex) const {
        return read_index[readerIndex]->load();
    }

    void inline setReadIndex(const size_t readerIndex, const std::uint64_t newVal) {
        read_index[readerIndex]->store(newVal);
    }

    bool initialize(const size_t sizeIn) {
        size = sizeIn;
        recs = reinterpret_cast<T*>(malloc(sizeof(T) * size));
        initialized = recs != nullptr;
        reset();
        return initialized;
    }

    size_t addReader() {
        read_index.emplace_back( std::make_unique< std::atomic<std::uint64_t> >() );
        return read_index.size() - 1;
    }

    void reset() {
        for (size_t k = 0; k < read_index.size(); ++k) {
            setReadIndex(k, 0);
         }

        write_index = 0;
        terminateFlag = false;
    }

    void terminate()
    {
        terminateFlag = true;
    }

    ~ring_buffer_spmc_t()
    {
        if (nullptr != recs)
        {
            free(recs);
        }
    }

    bool full() const {
        for (std::size_t k = 0; k < read_index.size(); ++k) {
            if ((getReadIndex(k) == write_index + 1) || (getReadIndex(k) == 0 && static_cast<int64_t>(write_index) == static_cast<int64_t>(size) - 1))
            {
                return true;
            }
        }
        return false;
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

    T& pop_ref(const std::size_t readerIndex)
    {
        wait_for_data();
        T& curr = recs[getReadIndex(readerIndex)];
        if (getReadIndex(readerIndex) == size - 1) {
            setReadIndex(readerIndex, 0);
        }
        else {
            setReadIndex(readerIndex, getReadIndex(readerIndex) + 1);
        }
        return curr;
    }

    T pop_no_wait(const std::size_t readerIndex)
    {
        T curr = recs[getReadIndex(readerIndex)];
        if (getReadIndex(readerIndex) == size - 1) {
            setReadIndex(readerIndex, 0);
        }
        else {
            setReadIndex(readerIndex, getReadIndex(readerIndex) + 1);
        }
        return curr;
    }

    T pop(const std::size_t readerIndex)
    {
        wait_for_data(readerIndex);
        T curr = recs[getReadIndex(readerIndex)];
        if (getReadIndex(readerIndex) == size - 1) {
            setReadIndex(readerIndex, 0);
        }
        else {
            setReadIndex(readerIndex, getReadIndex(readerIndex) + 1);
        }
        return curr;
    }

    bool wait_for_data(const std::size_t readerIndex) const
    {
        while (empty(readerIndex)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (terminateFlag) {
                return false;
            }
        }
        return true;
    }

    bool empty(const std::size_t readerIndex) const {
        if (getReadIndex(readerIndex) != write_index) {
            return false;
        }
        return true;
    }
};

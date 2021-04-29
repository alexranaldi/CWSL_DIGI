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
#include <array>

template <typename T>
struct sample_buffer_t {
public:
    std::uint64_t write_index;
    T* buf;
    std::uint64_t startEpochTime;
    std::size_t size;

    sample_buffer_t() :
        write_index(0),
        size(0),
        buf(nullptr),
        startEpochTime(0)
    {
    }

    std::size_t byte_size() const {
        return sizeof(T) * size;
    }

    sample_buffer_t(const sample_buffer_t& obj) {
        startEpochTime = obj.startEpochTime;
        write_index = 0;
        size = obj.size;
        buf = reinterpret_cast<T*>(malloc(byte_size()));
        memcpy(buf, obj.buf, byte_size());
    }

    virtual ~sample_buffer_t() {
        deallocate();
    }

    void deallocate() {
        if (buf) {
            free(buf);
            buf = nullptr;
        }
    }

    void initialize(const std::size_t new_size) {
        init(new_size);
    }

    void init(const std::size_t new_size)
    {
        size = new_size;
        buf = reinterpret_cast<T*>(malloc(new_size * sizeof(T)));
        write_index = 0;
        startEpochTime = 0;
    }

    bool full() const {
        return size == write_index - 1;
    }

    bool write(const std::vector<T>& samples)
    {
        if ((samples.size() + write_index) > size)
        {
            size_t k = 0;
            while (write_index < size) {
                buf[write_index] = samples[k];
                write_index++;
                k++;
            }
            return false;
        }
        else {
            for (const auto& sample : samples) {
                buf[write_index] = sample;
                write_index++;
            }
            return true;
        }
    }

    void resetIndices() {
        write_index = 0;
    }

    void reset() {
        resetIndices();
    }

    void scale(const T factor) {
        for (size_t k = 0; k < size; ++k) {
            buf[k] *= factor;
        }
    }

    void clear()
    {
        for (size_t k = 0; k < size; ++k) {
            buf[k] = 0;
        }
        resetIndices();
    }
    

};

template <typename T>
struct decode_audio_buffer_t : public sample_buffer_t<T> {
    
};

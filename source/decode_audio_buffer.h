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
struct decode_audio_buffer_t {
	std::size_t write_index;
	std::size_t read_index;
	T* buf;
    std::atomic<bool> paused;
    std::uint64_t startEpochTime;
    std::size_t size;

	decode_audio_buffer_t() : 
		write_index(0),
		read_index(0),
        paused(false),
        size(0),
        buf(nullptr),
        startEpochTime(0)
	{
	}

    void init(const std::size_t new_size)
    {
        size = new_size;
        buf = reinterpret_cast<T*>(malloc(new_size * sizeof(T)));
        paused = false;
        write_index = 0;
        read_index = 0;
        startEpochTime = 0;
    }

    void pause()
    {
        paused = true;
    }

    void resume()
    {
        paused = false;
    }

	bool write(const std::vector<T>& samples) 
	{
        if (paused)
        {
            return false;
        }
		else if ( (samples.size() + write_index) >= size )
		{
            size_t k = 0;
            while (write_index + 1 < size) {
                buf[write_index] = samples[k];
                write_index++;
                k++;
            }
            return false;

		}
		for (const auto& sample : samples) {
			buf[write_index] = sample;
			write_index++;
		}
		return true;
	}

	void clear() 
	{
        for (size_t k = 0; k < size; ++k) {
		    buf[k] = 0;
        }
		write_index = 0;
		read_index = 0;
	}
};



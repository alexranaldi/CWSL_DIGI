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

#pragma once
#ifndef SSBD_HPP
#   define SSBD_HPP

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <stdexcept>

#include "LowPass.hpp"

// An efficient single sideband demodulator.
//  - Operates on blocks based on the demication rate: Fs/B
//  - Consumes 2*Fs/B complex samples at a time, outputting 4 real samples.
//      - The output sample rate is 2*B.
//  - The output samples are delayed by several cycles (GetDelay)
//      - This latency is a consequence of better filtering.
//      - Lowering this value will result lower quality output.
// All frequencies are in Hz 
template<class PRECISION = double> 
class SSBD { 

public:

    // Constructor: default tunes to Upper Sideband @ 0 Hz
    SSBD(const size_t Fs, const size_t B, const double F = 0.0,
         const bool isUSB = true, const size_t latency_log2 = 3) :
        Fs(Fs), B(B), latency(1 << latency_log2)
    {

        // Check inputs
        if (0 == B || (Fs/B/2)*2*B != Fs || Fs < 4*B)
            throw std::invalid_argument("Fs/B must be an even integer >= 4");
        if (latency_log2 < 1)
            throw std::invalid_argument("log2(Latency) must >= 1");
        if (16 < latency_log2)
            throw std::invalid_argument("log2(Latency) must be <= 16");

        // Create the lowpass filter
        FiltOrder = latency*2*Fs/B;
        filter = BuildLowPass<PRECISION>(FiltOrder, B/(double)Fs);

        // Normalize the filter
        PRECISION sum = 0.0;
        for (size_t n = 0; n < FiltOrder; sum += filter[n++]);
        for (size_t n = 0; n < FiltOrder; filter[n++] /= sum);

        // Create the down-conversion tone
        BlockSize = Fs/B/2;
        tone = new std::complex<PRECISION>[BlockSize];

        // Create the workspace
        NumWS = FiltOrder/BlockSize;
        workspace = new std::complex<PRECISION>[NumWS];
        index_max = NumWS-1;
        index = 0;

        // Perform the initial tune
        Tune(F, isUSB);

    }

    // Destructor
    ~SSBD(void)
    {

        // Free allocated memory
        delete[] filter;
        delete[] tone;
        delete[] workspace;

    }

    // Retunes the demodulator
    void Tune(const double F, const bool isUSB, const bool reset = true)
    {
        // Check the parameters
        if (fabs(F) > Fs/2)
            throw std::invalid_argument("Signal outside of band (low)");
        if (fabs(F+B*(isUSB ? 1.0 : -1.0)) > Fs/2)
            throw std::invalid_argument("Signal outside of band (high)");

        // Store demodulation parameters
        Fc = F;
        USB = isUSB;

        // Update the down-conversion tone
        sign = static_cast<PRECISION>(isUSB ? 1.0 : -1.0);
        PRECISION phase_delta = static_cast<PRECISION>(-2.0*PI*(F+sign*B/2.0)/static_cast<double>(Fs));
        for (size_t n = 0; n < BlockSize; ++n)
            tone[n] = std::exp(std::complex<PRECISION>(0.0, phase_delta*n));
        phase_inc = std::exp(std::complex<PRECISION>(0.0, phase_delta*BlockSize));

        // Reset the workspace
        if (reset) {
            std::fill(workspace, workspace+NumWS, std::complex<PRECISION>(0.0, 0.0));
            index = 0;
            phase = std::complex<PRECISION>(1.0, 0.0);
        }

    }

    // Consumes the next 2*Fs/B complex input samples from the provided pointer
    // and generates 4 real output samples at the provided pointer.
    template<class TYPE = double>
    void Iterate(const std::complex<TYPE> *in, TYPE *out)
    {

        // Process the four blocks
        out[0] = static_cast<TYPE>(+ProcessBlock(in+0*BlockSize).real());
        out[1] = static_cast<TYPE>(-ProcessBlock(in+1*BlockSize).imag()*sign);
        out[2] = static_cast<TYPE>(-ProcessBlock(in+2*BlockSize).real());
        out[3] = static_cast<TYPE>(+ProcessBlock(in+3*BlockSize).imag()*sign);

    }

    // Returns the input sample rate (complex samples per second)
    inline size_t GetInRate(void) {return Fs;}
    // Returns the output sample rate (real samples per second)
    inline size_t GetOutRate(void) {return 2*B;}
    // Returns the number of input complex samples consumed each iteration.
    inline size_t GetInSize(void) {return 2*Fs/B;}
    // Returns the number of output real samples generated each iteration.
    inline size_t GetOutSize(void) {return 4;}
    // Returns the demodulator bandwidth
    inline size_t GetBandwidth(void) { return B;}
    // Returns the currently tuned frequency
    inline double GetCarrier(void) { return Fc;}
    // Returns true if the demodulator is tuned for upper sideband
    inline bool IsUSB(void) { return USB;}
    // Returns the output delay of this filter (at the output rate)
    inline size_t GetDelay(void) const {return latency;}

private:

    // Processes a single block of samples
    template<class TYPE = double>
    std::complex<PRECISION> ProcessBlock(const std::complex<TYPE> *in)
    {

        // Update the workspace
        for (size_t n = 0; n < NumWS; ++n) {
            std::complex<PRECISION> sum(0.0, 0.0);
            for (size_t m = 0; m < BlockSize; ++m) {
                sum += static_cast<std::complex<PRECISION>>(in[m]) *
                       tone[m] * filter[m+n*BlockSize];
            }
            workspace[(NumWS-n-1+index)&index_max] += sum*phase;
        }

        // Update the initial phase for the next block
        phase *= phase_inc;

        // Retrieve the output sample and reset it in the workspace
        std::complex<PRECISION> out = workspace[index];
        workspace[index] = std::complex<PRECISION>(0.0, 0.0);
        index = (index+1) & index_max;

        return out;

    }

    // Input complex sample rate
    const size_t Fs;
    // Demodulator bandwidth
    const size_t B;
    // Currently tuned frequency
    double Fc;
    // True when upper sideband
    bool USB;
    // +/- 1 for USB/LSB
    PRECISION sign;

    // Weighted low-pass filter
    PRECISION *filter;
    // Low-pass filter order
    size_t FiltOrder;
    // Output sample latency of filter/demodulator
    const size_t latency;

    // Down-conversion mixing tone for a block
    std::complex<PRECISION> *tone;
    // Current initial phase for next block
    std::complex<PRECISION> phase;
    // Phase increment for a full block of samples
    std::complex<PRECISION> phase_inc;
    // Input samples per block
    size_t BlockSize;

    // Convolution workspace
    std::complex<PRECISION> *workspace;
    // Index of next output sample
    size_t index;
    // Used to bit-mask instead of modular arithmetic: CPU savings
    size_t index_max;
    // Size of the workspace
    size_t NumWS;

}; // class SSBD


#endif // SSBD_HPP

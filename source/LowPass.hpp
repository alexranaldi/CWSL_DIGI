#pragma once
#ifndef LOWPASS_HPP
#define LOWPASS_HPP

// David Mittiga
// Alex Ranaldi  W2AXR   alexranaldi@gmail.com

// LICENSE: GNU General Public License v3.0
// THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.


#include <cmath>
#define PI 3.14159265358979323846

// Builds a lowpass filter with the given order and fractional bandwidth.
template<class PRECISION = double>
static PRECISION* BuildLowPass(const size_t order, const double bandwidth)
{
    // Uses a weighted sinc for the taps
    //  Note that the number of taps in the filter is order+1 but since
    //  the last coeff would be 0 so we don't need the +1.
    //  The first tap is 0, the middle tap is 1, and the taps are symmetrical.
    PRECISION *filter = new PRECISION[order];
    filter[0] = static_cast<PRECISION>(0.0);
    filter[order/2] = static_cast<PRECISION>(1.0);
    const double x0 = -1.0*order/2;
    for (size_t n = 1; n < order/2; ++n) { // filter[0] = 0 so skip
        const double xPi = (x0+n)*PI*bandwidth;
        const double y = sin(xPi)/xPi * (0.54-0.46*cos(2.0*PI*n/(double)order));
        filter[n] = static_cast<PRECISION>(y);
        filter[order-n] = static_cast<PRECISION>(y);
    }
    return filter;

}

#endif // LOWPASS_HPP

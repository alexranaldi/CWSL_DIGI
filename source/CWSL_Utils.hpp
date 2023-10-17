#pragma once

#include <cstdint>
#include <string>

#include "SharedMemory.h"

// Maximum of CWSL bands
#define MAX_CWSL   32


// Prefix and suffix for the shared memories names
static std::string gPreSM = "CWSL";
static std::string gPostSM = "Band";

static inline std::string createSharedMemName(const int bandIndex, const int SMNumber) {
    // create name of shared memory
    std::string Name = gPreSM + std::to_string(bandIndex) + gPostSM;
    if (SMNumber != -1) {
        Name += std::to_string(SMNumber);
    }
    return Name;
}

//////////////////////////////////////////////////////////////////////////////
// Find the right band
static inline int findBand(const int64_t f, const int SMNumber) {
    CSharedMemory SM;
    SM_HDR h;

    // try to find right band - for all possible bands ...
    for (int bandIndex = 0; bandIndex < MAX_CWSL; ++bandIndex) {

        // create name of shared memory
		const std::string Name = createSharedMemName(bandIndex, SMNumber);

        // try to open shared memory
        if (SM.Open(Name.c_str())) {
            // save data from header of this band
            memcpy(&h, SM.GetHeader(), sizeof(SM_HDR));

            // close shared memory
            SM.Close();

            // is frequency in this band ?
            if ((h.SampleRate > 0) && (f >= h.L0 - h.SampleRate / 2) && (f <= h.L0 + h.SampleRate / 2)) {
                // yes -> assign it and break the finding loop
                return bandIndex;
            }
        }
    }
    return -1;
}

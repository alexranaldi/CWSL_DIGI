#pragma once


#include <string>
#include <vector>

#include "windows.h"


#ifdef __GNUC__
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

// Complete WAV file header
PACK(struct WavHdr
{
    char  _RIFF[4]; // "RIFF"
    std::uint32_t FileLen;  // length of all data after this (FileLength - 8)

    char _WAVE[4];  // "WAVE"

    char _fmt[4];        // "fmt "
    DWORD FmtLen;        // length of the next item (sizeof(WAVEFORMATEX))
   // WAVEFORMATEXTENSIBLE Format; // wave format description   

    WAVEFORMATEX Format; // wave format description   

    char  _data[4];  // "data"
    DWORD DataLen;   // length of the next data (FileLength - sizeof(struct WavHdr))

});


//////////////////////////////////////////////////////////////////////////////////////////////
// Open wav file
inline HANDLE wavOpen(const std::string& filename, WavHdr& Hdr, const bool isTemp)
{
    LPCVOID h;
    DWORD hl, l;

    HANDLE File = INVALID_HANDLE_VALUE;

    if (isTemp) {
        File = ::CreateFile(filename.c_str(), GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ , NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
            NULL
        );
    }
    else {
        File = ::CreateFile(filename.c_str(), GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            NULL
        );
    }

    if (File == INVALID_HANDLE_VALUE) {
        return File;
    }

    // use Hdr
    h = &Hdr;
    hl = sizeof(Hdr);

    // std::cout << "Wave header FILE SIZE FIELD IS set to " << Hdr.FileLen << " Bytes" << std::endl;

     // std::cout << "Writing wave header, " << hl << " Bytes" << std::endl;

     // write header to file
    if ((!::WriteFile(File, h, hl, &l, NULL)) || (l != hl))
    {
        ::CloseHandle(File);
        File = INVALID_HANDLE_VALUE;
        std::cerr << "Header write failure" << std::endl;
        return(FALSE);
    }

    // success
    return File;
}

inline void waveWrite(const std::vector<std::int16_t>& audioBuffer, const std::string& fileName) {
   // screenPrinter->print("Beginning wave file generation...", LOG_LEVEL::DEBUG);

    const uint64_t startTime = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);

    std::size_t DataLen = audioBuffer.size() * sizeof(std::int16_t);
   // screenPrinter->print("Audio Data Length (bytes): " + std::to_string(DataLen), LOG_LEVEL::DEBUG);
   // screenPrinter->print("Audio Data Length (samples): " + std::to_string(audioBuffer.size()), LOG_LEVEL::DEBUG);

    WavHdr Hdr;

    Hdr._RIFF[0] = 'R'; Hdr._RIFF[1] = 'I'; Hdr._RIFF[2] = 'F'; Hdr._RIFF[3] = 'F';
    Hdr.FileLen = static_cast<uint32_t>((sizeof(Hdr) + DataLen) - 8);
    Hdr._WAVE[0] = 'W'; Hdr._WAVE[1] = 'A'; Hdr._WAVE[2] = 'V'; Hdr._WAVE[3] = 'E';
    Hdr._fmt[0] = 'f'; Hdr._fmt[1] = 'm'; Hdr._fmt[2] = 't'; Hdr._fmt[3] = ' ';

    Hdr.FmtLen = sizeof(WAVEFORMATEX);
    Hdr.Format.wFormatTag = WAVE_FORMAT_PCM;
    Hdr.Format.nChannels = 1;
    Hdr.Format.nSamplesPerSec = 12000;
    Hdr.Format.nBlockAlign = 2;
    Hdr.Format.nAvgBytesPerSec = Hdr.Format.nSamplesPerSec * Hdr.Format.nBlockAlign;
    Hdr.Format.wBitsPerSample = 16;
    Hdr.Format.cbSize = 0;

    Hdr._data[0] = 'd'; Hdr._data[1] = 'a'; Hdr._data[2] = 't'; Hdr._data[3] = 'a';
    Hdr.DataLen = static_cast<DWORD>(DataLen);

    std::uint32_t bytesWritten = 0;

    HANDLE File = wavOpen(fileName, Hdr, false);

    const void* Data = audioBuffer.data();

    try {
        const bool writeStatus = WriteFile(File, Data, (DWORD)DataLen, (LPDWORD)&bytesWritten, NULL);
        if (!writeStatus) {
    //        screenPrinter->err("Error writing wave file data");
        }
    }
    catch (const std::exception&) {
    }

    CloseHandle(File);

    const uint64_t stopTime = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
  //  screenPrinter->debug("Wave writing completed in " + std::to_string(static_cast<float>(stopTime - startTime) / 1000) + " sec");

}
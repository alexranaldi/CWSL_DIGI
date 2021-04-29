#pragma once

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
HANDLE wavOpen(const std::string& filename, WavHdr& Hdr, const bool isTemp)
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

#pragma once


// Complete WAV file header
struct WavHdr
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

};


//////////////////////////////////////////////////////////////////////////////////////////////
// Open wav file
HANDLE wavOpen(const std::string& filename, WavHdr& Hdr)
{
    LPCVOID h;
    DWORD hl, l;

    HANDLE File = INVALID_HANDLE_VALUE;

    // std::cout << "Creating wav file: " << filename << std::endl;
    // open the file
    File = ::CreateFile(filename.c_str(), GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        NULL
    );

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


//////////////////////////////////////////////////////////////////////////////////////////////
// Close wav file
void wavClose(HANDLE& File)
{
    // close the file
    ::CloseHandle(File);

    // invalidate handle
    File = INVALID_HANDLE_VALUE;
}
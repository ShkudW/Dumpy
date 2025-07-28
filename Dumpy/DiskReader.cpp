#include "DiskReader.h"
#include <iostream>

DiskReader::DiskReader(const std::wstring& drivePath) {
    hDrive = CreateFileW(
        drivePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING,
        NULL
    );

    if (hDrive == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open drive. Error: " + std::to_string(GetLastError()));
    }
}

DiskReader::~DiskReader() {
    if (hDrive != INVALID_HANDLE_VALUE) {
        CloseHandle(hDrive);
    }
}

std::vector<BYTE> DiskReader::read(LARGE_INTEGER offset, DWORD size) const {

    const DWORD SECTOR_SIZE = 512;


    ULARGE_INTEGER alignedOffset;
    alignedOffset.QuadPart = (offset.QuadPart / SECTOR_SIZE) * SECTOR_SIZE;

    DWORD totalReadSize = size + (offset.QuadPart - alignedOffset.QuadPart);
    totalReadSize = ((totalReadSize + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;

    std::vector<BYTE> buffer(totalReadSize);
    DWORD bytesRead = 0;

    // Set file pointer to the aligned offset
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = static_cast<LONGLONG>(alignedOffset.QuadPart);
    if (SetFilePointerEx(hDrive, liOffset, NULL, FILE_BEGIN) == 0) {
        throw std::runtime_error("Failed to set file pointer. Error: " + std::to_string(GetLastError()));
    }

    // Read data from the drive
    if (!ReadFile(hDrive, buffer.data(), totalReadSize, &bytesRead, NULL)) {
        throw std::runtime_error("ReadFile failed. Error: " + std::to_string(GetLastError()));
    }

    if (bytesRead < totalReadSize) {
        throw std::runtime_error("Could not read the requested amount of data.");
    }

    // Extract the requested portion from the buffer
    std::vector<BYTE> result(size);
    memcpy(result.data(), buffer.data() + (offset.QuadPart - alignedOffset.QuadPart), size);

    return result;
}


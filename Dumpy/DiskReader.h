#ifndef DISKREADER_H
#define DISKREADER_H

#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>

class DiskReader {
public:
    DiskReader(const std::wstring& drivePath);
    ~DiskReader();
    std::vector<BYTE> read(LARGE_INTEGER offset, DWORD size) const;

private:
    HANDLE hDrive;
};

#endif 


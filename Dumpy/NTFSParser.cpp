#include "NTFSParser.h"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <vector>
#include <cmath>

bool iequals(const std::wstring& a, const std::wstring& b) {
    if (a.length() != b.length()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](wchar_t char_a, wchar_t char_b) {
            return std::tolower(char_a) == std::tolower(char_b);
        });
}


NTFSParser::NTFSParser(const DiskReader& reader, uint64_t partitionOffset)
    : diskReader(reader), ntfsOffset(partitionOffset), mftRecordSize(1024) {
    analyzeNTFSHeader();
}

// Analyzes the NTFS boot sector
void NTFSParser::analyzeNTFSHeader() {
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(ntfsOffset);
    std::vector<BYTE> bootSector = diskReader.read(offset, 512);
    NTFS_BOOT_SECTOR* ntfsHeader = reinterpret_cast<NTFS_BOOT_SECTOR*>(bootSector.data());

    if (memcmp(ntfsHeader->OEMID, "NTFS    ", 8) != 0) {
        throw std::runtime_error("The selected partition is not a valid NTFS partition.");
    }
    std::cout << "NTFS Partition signature verified." << std::endl;

    clusterSize = ntfsHeader->BytesPerSector * ntfsHeader->SectorsPerCluster;
    std::cout << "Cluster Size: " << clusterSize << " bytes" << std::endl;

    if (ntfsHeader->ClustersPerMFTRecord < 0) {
        mftRecordSize = 1 << std::abs(static_cast<int>(ntfsHeader->ClustersPerMFTRecord));
    }
    else {
        mftRecordSize = ntfsHeader->ClustersPerMFTRecord * clusterSize;
    }
    std::cout << "MFT Record Size: " << mftRecordSize << " bytes" << std::endl;

    mftLocation = ntfsOffset + (ntfsHeader->MFTClusterNumber * clusterSize);
    std::wcout << L"MFT Absolute Location: 0x" << std::hex << mftLocation << std::dec << std::endl;
}

// Reads a single MFT record
std::vector<BYTE> NTFSParser::getMFTRecord(uint64_t recordNumber) {
    uint64_t recordOffset = mftLocation + (recordNumber * mftRecordSize);
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(recordOffset);
    return diskReader.read(offset, mftRecordSize);
}

bool NTFSParser::applyFixup(std::vector<BYTE>& recordBytes) {
    if (recordBytes.size() < sizeof(MFT_RECORD_HEADER)) return false;
    MFT_RECORD_HEADER* header = reinterpret_cast<MFT_RECORD_HEADER*>(recordBytes.data());
    if (header->signature != 0x454C4946) return false;
    if (header->fixup_offset == 0 || header->fixup_size == 0) return true;
    if (header->fixup_offset >= recordBytes.size() || (size_t)header->fixup_offset + (header->fixup_size * 2) > recordBytes.size()) return false;

    uint16_t* usa = reinterpret_cast<uint16_t*>(&recordBytes[header->fixup_offset]);
    uint16_t usn = usa[0];
    for (int i = 1; i < header->fixup_size; ++i) {
        size_t sectorEndOffset = (size_t)i * 512 - 2;
        if (sectorEndOffset + 1 >= recordBytes.size()) return false;
        uint16_t* sectorEnd = reinterpret_cast<uint16_t*>(&recordBytes[sectorEndOffset]);
        if (*sectorEnd != usn) return false;
        *sectorEnd = usa[i];
    }
    return true;
}


void NTFSParser::buildDirectoryMap() {
    std::cout << "[*] Pass 1: Building directory map..." << std::endl;
    const uint64_t maxRecordsToScan = 200000;
    for (uint64_t i = 0; i < maxRecordsToScan; ++i) {
        std::vector<BYTE> recordBytes;
        try {
            recordBytes = getMFTRecord(i);
        }
        catch (...) { continue; }

        if (!applyFixup(recordBytes)) continue;

        MFT_RECORD_HEADER* header = reinterpret_cast<MFT_RECORD_HEADER*>(recordBytes.data());
        if (!(header->flags & 0x01) || !(header->flags & 0x02)) {
            continue;
        }

        BYTE* p = recordBytes.data() + header->attribute_offset;
        BYTE* end = recordBytes.data() + header->used_size;

        while (p < end && p > recordBytes.data() && (p + sizeof(ATTRIBUTE_HEADER_NON_RESIDENT)) <= end) {
            ATTRIBUTE_HEADER_NON_RESIDENT* attr = reinterpret_cast<ATTRIBUTE_HEADER_NON_RESIDENT*>(p);
            if (attr->type == 0xFFFFFFFF || attr->length == 0) break;

            if (attr->type == 0x30 && !attr->non_resident) {
                if ((BYTE*)attr + sizeof(ATTRIBUTE_HEADER_NON_RESIDENT) + sizeof(FILE_NAME_ATTRIBUTE) <= end) {
                    FILE_NAME_ATTRIBUTE* fnAttr = (FILE_NAME_ATTRIBUTE*)((char*)attr + 24);
                    if (fnAttr->file_name_type != 2) {
                        directoryMap[i] = {
                            std::wstring(fnAttr->file_name, fnAttr->file_name_length),
                            (uint64_t)(fnAttr->parent_directory_record_number & 0x0000FFFFFFFFFFFF)
                        };
                        break;
                    }
                }
            }
            if (attr->length > 0) p += attr->length; else break;
        }
    }
    std::cout << "[*] Pass 1 finished. Found " << directoryMap.size() << " directories." << std::endl;
}

// Reconstructs a path using the pre-built map
std::wstring NTFSParser::getPathForRecord(uint64_t recordId) {
    if (recordId == 5) return L"\\";
    if (pathCache.count(recordId)) return pathCache[recordId];
    if (!directoryMap.count(recordId)) return L"\\_ORPHANED_\\";

    const auto& dirInfo = directoryMap[recordId];
    std::wstring parentPath = getPathForRecord(dirInfo.parentId);
    std::wstring fullPath = parentPath + dirInfo.name + L"\\";

    pathCache[recordId] = fullPath;
    return fullPath;
}

std::vector<BYTE> NTFSParser::readNonResidentData(ATTRIBUTE_HEADER_NON_RESIDENT* attr) {
    std::vector<BYTE> fileData;
    BYTE* p = (BYTE*)attr + attr->data_runs_offset;
    BYTE* end = (BYTE*)attr + attr->length;
    int64_t currentCluster = 0;

    while (p < end && *p != 0x00) {
        BYTE header = *p++;
        int offsetBytes = (header >> 4) & 0x0F;
        int lengthBytes = header & 0x0F;

        if (p + lengthBytes + offsetBytes > end) break;

        uint64_t runLength = 0;
        memcpy(&runLength, p, lengthBytes);
        p += lengthBytes;

        int64_t runOffset = 0;
        memcpy(&runOffset, p, offsetBytes);
        p += offsetBytes;

        // Handle signed offset
        if (offsetBytes > 0 && (runOffset >> (offsetBytes * 8 - 1))) {
            runOffset |= (-1LL) << (offsetBytes * 8);
        }

        currentCluster += runOffset;

        LARGE_INTEGER readOffset;
        readOffset.QuadPart = ntfsOffset + (currentCluster * clusterSize);
        DWORD bytesToRead = (DWORD)(runLength * clusterSize);

        try {
            std::vector<BYTE> runData = diskReader.read(readOffset, bytesToRead);
            fileData.insert(fileData.end(), runData.begin(), runData.end());
        }
        catch (const std::exception& e) {
            std::cerr << "Error reading data run: " << e.what() << std::endl;
            break;
        }
    }
    return fileData;
}


void NTFSParser::findAndExtractFiles(const std::vector<std::wstring>& filesToFind) {
    buildDirectoryMap();

    if (directoryMap.empty()) {
        std::cerr << "[ERROR] Directory map is empty. Cannot proceed." << std::endl;
        return;
    }

    std::cout << "[*] Pass 2: Scanning for target files..." << std::endl;
    unsigned int filesFound = 0;
    const uint64_t maxRecordsToScan = 200000;

    for (uint64_t i = 0; i < maxRecordsToScan && filesFound < filesToFind.size(); ++i) {
        std::vector<BYTE> recordBytes;
        try {
            recordBytes = getMFTRecord(i);
        }
        catch (...) { continue; }

        if (!applyFixup(recordBytes)) continue;

        MFT_RECORD_HEADER* header = reinterpret_cast<MFT_RECORD_HEADER*>(recordBytes.data());
        if (!(header->flags & 0x01) || (header->flags & 0x02)) {
            continue;
        }

        BYTE* p = recordBytes.data() + header->attribute_offset;
        BYTE* end = recordBytes.data() + header->used_size;

        std::wstring fileName;
        uint64_t parentRecordId = 0;

        while (p < end && p > recordBytes.data() && (p + sizeof(ATTRIBUTE_HEADER_NON_RESIDENT)) <= end) {
            ATTRIBUTE_HEADER_NON_RESIDENT* attr = reinterpret_cast<ATTRIBUTE_HEADER_NON_RESIDENT*>(p);
            if (attr->type == 0xFFFFFFFF || attr->length == 0) break;

            if (attr->type == 0x30 && !attr->non_resident) {
                if ((BYTE*)attr + sizeof(ATTRIBUTE_HEADER_NON_RESIDENT) + sizeof(FILE_NAME_ATTRIBUTE) <= end) {
                    FILE_NAME_ATTRIBUTE* fnAttr = (FILE_NAME_ATTRIBUTE*)((char*)attr + 24);
                    if (fnAttr->file_name_type != 2) {
                        fileName = std::wstring(fnAttr->file_name, fnAttr->file_name_length);
                        parentRecordId = (uint64_t)(fnAttr->parent_directory_record_number & 0x0000FFFFFFFFFFFF);
                        break;
                    }
                }
            }
            if (attr->length > 0) p += attr->length; else break;
        }

        if (fileName.empty() || parentRecordId == 0) continue;

        std::wstring parentPath = getPathForRecord(parentRecordId);
        if (parentPath.find(L"_ORPHANED_") != std::wstring::npos) continue;

        std::wstring fullPath = parentPath + fileName;

        for (const auto& targetFile : filesToFind) {
            if (iequals(fullPath, targetFile)) {
                std::wcout << L"[*] Found target file: " << fullPath << std::endl;
                BYTE* data_p = recordBytes.data() + header->attribute_offset;
                while (data_p < end && data_p > recordBytes.data() && (data_p + sizeof(ATTRIBUTE_HEADER_NON_RESIDENT)) <= end) {
                    ATTRIBUTE_HEADER_NON_RESIDENT* data_attr = reinterpret_cast<ATTRIBUTE_HEADER_NON_RESIDENT*>(data_p);
                    if (data_attr->type == 0xFFFFFFFF || data_attr->length == 0) break;
                    if (data_attr->type == 0x80) {
                        FoundFileInfo info;
                        info.name = fullPath;
                        if (!data_attr->non_resident) {

                            DWORD dataSize = *(DWORD*)((char*)data_attr + 16);
                            WORD dataOffset = *(WORD*)((char*)data_attr + 20);
                            BYTE* dataStart = (BYTE*)data_attr + dataOffset;
                            info.data.assign(dataStart, dataStart + dataSize);
                        }
                        else {

                            info.data = readNonResidentData(data_attr);
                        }

                        if (!info.data.empty()) {
                            extractFile(info);
                            filesFound++;
                        }
                        else {
                            std::wcerr << L"[ERROR] Failed to extract data for " << fullPath << std::endl;
                        }
                        break;
                    }
                    if (data_attr->length > 0) data_p += data_attr->length; else break;
                }
                break;
            }
        }
    }
    std::cout << "\nScan finished." << std::endl;
}

void NTFSParser::extractFile(const FoundFileInfo& fileInfo) {
    std::wstring safeFilename = fileInfo.name;
    std::replace(safeFilename.begin(), safeFilename.end(), L'\\', L'_');
    std::replace(safeFilename.begin(), safeFilename.end(), L':', L'_');

    std::string narrowFilename;
    int requiredSize = WideCharToMultiByte(CP_UTF8, 0, safeFilename.c_str(), -1, NULL, 0, NULL, NULL);
    if (requiredSize > 0) {
        std::vector<char> buffer(requiredSize);
        WideCharToMultiByte(CP_UTF8, 0, safeFilename.c_str(), -1, buffer.data(), requiredSize, NULL, NULL);
        narrowFilename = buffer.data();
    }
    else {
        std::wcerr << L"Failed to convert filename: " << safeFilename << std::endl;
        return;
    }

    std::ofstream outFile(narrowFilename, std::ios::binary);
    if (!outFile) {
        std::wcerr << L"Failed to create output file: " << safeFilename << std::endl;
        return;
    }

    outFile.write(reinterpret_cast<const char*>(fileInfo.data.data()), fileInfo.data.size());
    outFile.close();
    std::wcout << L"[SUCCESS] Extracted " << fileInfo.name << L" (" << fileInfo.data.size() << L" bytes) to file " << safeFilename << std::endl;
}

void NTFSParser::debugPrintRecord(uint64_t recordNumber) {
    (void)recordNumber;
}
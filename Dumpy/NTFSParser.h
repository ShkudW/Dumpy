#ifndef NTFSPARSER_H
#define NTFSPARSER_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include "DiskReader.h"

class DiskReader;


#pragma pack(push, 1)
typedef struct {
    BYTE  Jump[3];
    BYTE  OEMID[8];
    WORD  BytesPerSector;
    BYTE  SectorsPerCluster;
    WORD  ReservedSectors;
    BYTE  Fats;
    WORD  RootEntries;
    WORD  TotalSectors16;
    BYTE  MediaType;
    WORD  SectorsPerFat16;
    WORD  SectorsPerTrack;
    WORD  Heads;
    DWORD HiddenSectors;
    DWORD TotalSectors32;
    BYTE  _unused1[4];
    ULONGLONG TotalSectors64;
    ULONGLONG MFTClusterNumber;
    ULONGLONG MFTMirrorClusterNumber;
    CHAR  ClustersPerMFTRecord;
    BYTE  _unused2[3];
    CHAR  ClustersPerIndexBuffer;
    BYTE  _unused3[3];
    ULONGLONG VolumeSerialNumber;
    DWORD Checksum;
} NTFS_BOOT_SECTOR;

typedef struct {
    DWORD signature;
    WORD fixup_offset;
    WORD fixup_size;
    ULONGLONG lsn;
    WORD sequence_number;
    WORD hard_link_count;
    WORD attribute_offset;
    WORD flags;
    DWORD used_size;
    DWORD allocated_size;
    ULONGLONG base_file_record;
    WORD next_attribute_id;
    WORD _padding;
    DWORD mft_record_number;
} MFT_RECORD_HEADER;


typedef struct {
    DWORD type;
    DWORD length;
    BYTE non_resident;
    BYTE name_length;
    WORD name_offset;
    WORD flags;
    WORD attribute_id;
} ATTRIBUTE_HEADER;


typedef struct {
    DWORD type;
    DWORD length;
    BYTE non_resident;
    BYTE name_length;
    WORD name_offset;
    WORD flags;
    WORD attribute_id;
    ULONGLONG start_vcn;
    ULONGLONG end_vcn;
    WORD data_runs_offset;
    WORD compression_unit;
    BYTE _padding[4];
    ULONGLONG allocated_size;
    ULONGLONG real_size;
    ULONGLONG initialized_size;
} ATTRIBUTE_HEADER_NON_RESIDENT;


typedef struct {
    ULONGLONG parent_directory_record_number;
    ULONGLONG creation_time;
    ULONGLONG last_modification_time;
    ULONGLONG last_mft_change_time;
    ULONGLONG last_access_time;
    ULONGLONG allocated_size;
    ULONGLONG real_size;
    DWORD flags;
    DWORD _reparse;
    BYTE file_name_length;
    BYTE file_name_type;
    WCHAR file_name[1];
} FILE_NAME_ATTRIBUTE;

#pragma pack(pop)

struct FoundFileInfo {
    std::wstring name;
    std::vector<BYTE> data;
};

struct DirectoryInfo {
    std::wstring name;
    uint64_t parentId;
};

class NTFSParser {
public:
    NTFSParser(const DiskReader& reader, uint64_t partitionOffset);
    void findAndExtractFiles(const std::vector<std::wstring>& filesToFind);
    void debugPrintRecord(uint64_t recordNumber);

private:
    const DiskReader& diskReader;
    uint64_t ntfsOffset;
    uint64_t mftLocation;
    uint32_t clusterSize;
    uint32_t mftRecordSize;

    std::map<uint64_t, DirectoryInfo> directoryMap;
    std::map<uint64_t, std::wstring> pathCache;

    void analyzeNTFSHeader();
    void buildDirectoryMap();
    std::wstring getPathForRecord(uint64_t recordId);


    std::vector<BYTE> readNonResidentData(ATTRIBUTE_HEADER_NON_RESIDENT* attr);

    std::vector<BYTE> getMFTRecord(uint64_t recordNumber);
    void extractFile(const FoundFileInfo& fileInfo);
    bool applyFixup(std::vector<BYTE>& recordBytes);
};

#endif 
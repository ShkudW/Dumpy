# Dumpy
NTDS,SAM,SYSTEM dumper

The tool establishes direct access to the physical disk using RAW Disk Access.
It utilizes the Windows API function CreateFileW to open the handle to "\\.\PhysicalDrive0", which represents the entire physical disk.

This approach completely bypasses the NTFS file system driver (ntfs.sys), allowing the tool to access raw disk sectors directly without relying on the OS’s file I/O mechanisms.

The tool reads the GPT header located at offset 512 and parses the partition table entries.

This allows it to locate partitions of type Basic Data Partition, which typically contain the Windows operating system
Once the target partition is located, the tool reads the Volume Boot Record (VBR) of that NTFS partition.

From this structure, it extracts:

- The cluster size (allocation unit)
- The location of the Master File Table (MFT)

These values provide the necessary context to navigate the NTFS file system layout and interpret the file system’s internal structures.
The tool scans all MFT entries that represent directories and builds a complete in-memory folder tree (parent-child relationships).

This structure is later used to reconstruct the full path of each file:
- C:\Windows\System32\config\SAM
- C:\Windows\System32\config\SYSTEM
- C:\Windows\System32\config\SECURITY
- C:\Windows\NTDS\ntds.dit

Large files in NTFS are typically non-resident, meaning their actual content is not stored within the MFT entry but is spread across the disk.

NTFS uses Data Runs, which are sequences of clusters pointing to the actual file content.

It decodes the Data Runs, directly seeks to the physical disk locations, reads the raw data, and reconstructs the full file content in memory — without interacting with ntfs.sys or the Windows file system.

Once the data is reconstructed in memory, the tool writes it to a new file on disk.

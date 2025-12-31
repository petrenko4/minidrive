# Issues with the Program

## Argument Parsing Issue
The program currently has an issue with parsing the connection string argument. Specifically, the format `ip:port` does not work as intended due to a parsing error. To log in as a public user, the user must use the format `@ip:port` or `public@ip:port` instead. This workaround is necessary until the parsing logic is fixed.

## SYNC Command Issue
The SYNC command is designed to synchronize the local and server file systems by comparing their structures. The intended workflow is as follows:

1. Build two maps representing the file system structures of the local and server environments.
2. For each path in the local file system map, look it up in the server map.
3. If the same file exists in both maps, compare their hashes. If the hashes match, remove the corresponding file entries from both maps.
4. After all paths are compared:
   - The local file system map contains only files that need to be uploaded to the server.
   - The server file system map contains only files that need to be deleted from the server.

### Problem
The current implementation does not work as intended because the same files on the local and server sides have different hashes. This causes the program to fail to recognize synchronized files, leading to the following issues:
- All files on the server are deleted.
- All files from the local file system are uploaded to the server.

This behavior is highly inefficient and negates the purpose of the SYNC command. A solution is needed to ensure that identical files are correctly identified and skipped during synchronization.
/*
Simple and Light Protocol - SLP

This implementation is based on POSIX threads:
https://stackoverflow.com/questions/40177613/c-linux-pthreads-sending-data-from-one-thread-to-another- ...
http://www.yolinux.com/TUTORIALS/LinuxTutorialPosixThreads.html

Other sources:
https://www.geeksforgeeks.org/search-insert-and-delete-in-a-sorted-array/
https://barrgroup.com/Embedded-Systems/How-To/CRC-Calculation-C-Code

This can easily be ported to other Operating System environments, also into embedded SW having some OS.
*/

/*
 * The width of the CRC calculation and result.
 * Modify the typedef for a 16 or 32-bit CRC standard.
 */
typedef uint32_t crc;

int binarySearch(uint64_t arr[], int low, int high, uint64_t key);
void crcInit(void);
crc crcFast(uint8_t const message[], int nBytes);

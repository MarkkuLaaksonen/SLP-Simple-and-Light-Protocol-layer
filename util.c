/*
Simple and Light Protocol - SLP

This implementation is based on POSIX threads:

https://stackoverflow.com/questions/40177613/c-linux-pthreads-sending-data-from-one-thread-to-another- ...

http://www.yolinux.com/TUTORIALS/LinuxTutorialPosixThreads.html

https://www.geeksforgeeks.org/search-insert-and-delete-in-a-sorted-array/

https://barrgroup.com/Embedded-Systems/How-To/CRC-Calculation-C-Code

This can be ported easily to other Operating System environments, also into embedded SW having some OS.
*/

#include "common.h"
#include "util.h"

int binarySearch(uint64_t arr[], int low, int high, uint64_t key)
{
    if (high < low)
         return -1;
    int mid = (low + high)/2;  /*low + (high - low)/2;*/
    if (key == arr[mid])
         return mid;
    if (key > arr[mid])
         return binarySearch(arr, (mid + 1), high, key);
    return binarySearch(arr, low, (mid -1), key);
}

/* For CRC */
#define POLYNOMIAL 0xD8  /* 11011 followed by 0's */

#define WIDTH  (8 * sizeof(crc))
#define TOPBIT (1 << (WIDTH - 1))

static crc crcTable[256];

void crcInit(void)
{
    crc  remainder;


    /*
     * Compute the remainder of each possible dividend.
     */
    for (int dividend = 0; dividend < 256; ++dividend)
    {
        /*
         * Start with the dividend followed by zeros.
         */
        remainder = dividend << (WIDTH - 8);

        /*
         * Perform modulo-2 division, a bit at a time.
         */
        for (uint8_t bit = 8; bit > 0; --bit)
        {
            /*
             * Try to divide the current data bit.
             */
            if (remainder & TOPBIT)
            {
                remainder = (remainder << 1) ^ POLYNOMIAL;
            }
            else
            {
                remainder = (remainder << 1);
            }
        }

        /*
         * Store the result into the table.
         */
        crcTable[dividend] = remainder;
    }

}   /* crcInit() */

crc crcFast(uint8_t const message[], int nBytes)
{
    uint8_t data;
    crc remainder = 0;

    /*
     * Divide the message by the polynomial, a byte at a time.
     */
    for (int byte = 0; byte < nBytes; ++byte)
    {
        data = message[byte] ^ (remainder >> (WIDTH - 8));
        remainder = crcTable[data] ^ (remainder << 8);
    }

    /*
     * The final remainder is the CRC.
     */
    return (remainder);

}   /* crcFast() */

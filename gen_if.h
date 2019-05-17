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

#define GEN_MEM_SIZE                    (8*1024)
#define GEN_ID_INVALID                  0xffffffffffffffff

#define GEN_SMALL_THREAD_DELAY_US       1
#define GEN_THREAD_DELAY_US             10

typedef long mtype_t;

extern int gGenDebugPrint;
extern pthread_mutex_t gGenPrintLock;
extern pthread_mutex_t gAppLock;
extern pthread_mutex_t gSlpTxLock;
extern pthread_mutex_t gSlpRxLock;

int GenCertainSyncRelatedMsgQueuesEmpty(void);

//conditional test features
#define GEN_APP_TEST_KEEP_RANDOM_BREAKS
#define GEN_SLP_TEST_LOST_APP_DATA
#define GEN_SLP_TEST_LOST_ACKS
#define GEN_SLP_TEST_RAND_LOST

//conditional debug statistics
#define GEN_APP_DEBUG_STATISTICS
#define GEN_SLP_TX_DEBUG_STATISTICS
#define GEN_SLP_RX_DEBUG_STATISTICS

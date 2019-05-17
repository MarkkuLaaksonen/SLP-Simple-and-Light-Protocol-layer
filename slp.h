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

#define SLP_SIMULATED_TRANSFER_DELAY_US         100000
#define SLP_SIM_CTRL_MSG_TRANS_DELAY_US         10000
#define SLP_SIM_SMALL_CTRL_MSG_TRANS_DELAY_US   1000

#define SLP_WAIT_FOR_POLL_SENDING_US            100

#define SLP_MAX_NR_OF_BLOCKS                    (4*GEN_MEM_SIZE)
#define SLP_FILL_TOLERANCE                      1024
#define SLP_APP_WAIT_LIMIT                      (SLP_MAX_NR_OF_BLOCKS - SLP_FILL_TOLERANCE)
#define SLP_APP_RESTART_LIMIT                   0

//SLP message structures: slp_tx.c <=> slp_rx.c
typedef struct SlpSubHeader_t {
    uint32_t    appDataLen;
    uint32_t    fill;
    uint64_t    seqNum;
} SlpSubHeader_t;

typedef struct SlpHeader_t {
    uint32_t            crc;
    uint32_t            fill;
    SlpSubHeader_t      subHeader;
} SlpHeader_t;

typedef struct SlpData_t {
    SlpHeader_t slpHeader;
    uint8_t     appData[SLP_APP_DATA_SIZE];
} SlpData_t;

//SLP data messages: slp_tx.c => slp_rx.c
typedef struct SlpInnerMsg_t {
    mtype_t     mtype;
    SlpData_t   data;
} SlpInnerMsg_t;

//SLP poll message: slp_tx.c => slp_rx.c
//SLP ack and nack messages: slp_rx.c => slp_tx.c
typedef struct SlpShortMsg_t {
    mtype_t             mtype;
    SlpHeader_t         slpHeader;
//subHeader.appDataLen is in flag use
#define SLP_FLAGS_RECEIVER_RESET    1
} SlpShortMsg_t;

int SlpTestRandOfThisSeqNum(uint64_t seqNum, int testCase);

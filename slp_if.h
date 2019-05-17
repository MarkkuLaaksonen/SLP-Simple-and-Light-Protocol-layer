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

//Application data size
#define SLP_APP_DATA_SIZE (GEN_MEM_SIZE - 84)

//Data structure for APP data messages: APP <=> SLP
typedef struct SlpAppData_t {
    uint64_t    genId; //appId or slpId depending on direction
    uint32_t    len;
    uint8_t     appData[SLP_APP_DATA_SIZE];
} SlpAppData_t;

typedef struct SlpAppMsg_t {
    mtype_t             mtype;
    SlpAppData_t        data;
} SlpAppMsg_t;

//Data structure for INFO message: SLP => APP
typedef struct SlpInfoData_t {
    uint8_t infoType;
#define SLP_INFO_TYPE_APP_DATA_RECEIVED 1
#define SLP_INFO_TYPE_DONE              2
#define SLP_INFO_TYPE_DONE_AND_RX_RESET 3
#define SLP_INFO_TYPE_RX_RESET          4
    uint64_t appId;
    uint64_t slpId; //SLP seqNum
} SlpInfoData_t;

typedef struct SlpInfoMsg_t {
    mtype_t                     mtype;
    SlpInfoData_t               data;
} SlpInfoMsg_t;

//Data structure for WAIT_STATE message: SLP => APP
typedef struct SlpStateData_t {
    uint8_t                     state;
#define SLP_ASKS_APP_TO_GO_ON 0
#define SLP_ASKS_APP_TO_WAIT  1
} SlpStateData_t;

typedef struct SlpStateMsg_t {
    mtype_t                     mtype;
    SlpStateData_t              data;
} SlpStateMsg_t;

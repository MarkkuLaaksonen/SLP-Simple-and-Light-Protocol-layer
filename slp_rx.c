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
#include "gen_if.h"
#include "util_if.h"
#include "msg.h"
#include "slp_if.h"
#include "slp.h"

pthread_mutex_t gSlpRxLock;

#define SLP_NACK_CHECK_DELAY_US         1000
#define SLP_NACK_CHECK_LIMIT            10
#define SLP_NACK_RETRANS_LIMIT          10

typedef struct SlpRxBlockData_t {
    void*       pAppDataPtr;
    uint32_t    appLen;
}  SlpRxBlockData_t;


typedef struct SlpRxState_t {
    uint64_t            wrongOrderSeqNums[SLP_MAX_NR_OF_BLOCKS];
    SlpRxBlockData_t    wrongOrderBlockData[SLP_MAX_NR_OF_BLOCKS];
    int                 nrOfWrongOrderReceivedDataBlocks;
    uint64_t            waitSeqNum;
    uint64_t            lastSentNackSeqNum;
    int                 lastSentNackSeqNumClearCount;
} SlpRxState_t;

static SlpRxState_t sSlpRxState;

typedef struct SlpDataToSendAck_t
{
    uint64_t seqNum;
} SlpDataToSendAck_t;

static SlpDataToSendAck_t sSlpDataToSendAck[SLP_MAX_NR_OF_BLOCKS];
static int sSlpSendAckReadIndex;
static int sSlpSendAckWriteIndex;

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
typedef struct SlpRxDebug_t {
    uint32_t nrOfReceivedDataBlocks;
    uint32_t nrOfAcceptedDataBlocks;
    uint32_t nrOfSentAcks;
    uint32_t nrOfSentNacks;
    uint32_t nrOfReceivedRetransmittedDataBlocks;
    uint32_t nrOfReceivedRetransmittedPolls;
    uint32_t nrOfAcceptedRetransmittedDataBlocks;
    uint32_t nrOfAcceptedRetransmittedPolls;
    uint32_t nrOfReceivedPolls;
    uint32_t nrOfDroppedSuccessiveDataBlocks;
    uint32_t nrOfDroppedRandDataBlocks;
    uint32_t nrOfDroppedRetransmittedDataBlocks;
    uint32_t nrOfDroppedRetransmittedPolls;
    uint32_t nrOfDroppedPolls;
    uint32_t nrOfDataBlocksForwardedToApp;
} SlpRxDebug_t;

static SlpRxDebug_t sSlpRxDebug;
#endif

static int sSlpRxDebugPrint;

void* slp_rx_send_ack()
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpShortMsg_t sbuf;

    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while (sSlpSendAckReadIndex != sSlpSendAckWriteIndex) {
            usleep(GEN_THREAD_DELAY_US);

            //get the message queue id for the key with value SLP_ACK_MSG_QUEUE_KEY_ID
            key = SLP_ACK_MSG_QUEUE_KEY_ID;

            if ((msqid = msgget(key, msgflg)) < 0) {
                perror("msgget");
                exit(1);
            }

            //send message type SLP_ACK_MSG 
            sbuf.mtype = SLP_ACK_MSG;

            //set ACK data, appDataLen is in flag use
            if (0 == sSlpRxState.waitSeqNum) {
                sbuf.slpHeader.subHeader.appDataLen = SLP_FLAGS_RECEIVER_RESET;
            } else {
                sbuf.slpHeader.subHeader.appDataLen = 0;
            }
            sbuf.slpHeader.subHeader.fill = 0; //not used

            sSlpSendAckReadIndex++;
            sSlpSendAckReadIndex &= (SLP_MAX_NR_OF_BLOCKS - 1);
            sbuf.slpHeader.subHeader.seqNum = sSlpDataToSendAck[sSlpSendAckReadIndex].seqNum;

            sbuf.slpHeader.crc =  crcFast(((const uint8_t*) &sbuf.slpHeader.subHeader),
                sizeof(sbuf.slpHeader.subHeader));
            sbuf.slpHeader.fill = 0; //not used

            if (gGenDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_rx_send_ack: seqNum %lu, write index %d, read index %d\n",
                    sbuf.slpHeader.subHeader.seqNum, sSlpSendAckWriteIndex, sSlpSendAckReadIndex);
                pthread_mutex_unlock(&gGenPrintLock);
            }

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            sSlpRxDebug.nrOfSentAcks++;
#endif

            //send 
            if (msgsnd(msqid, &sbuf, sizeof(sbuf.slpHeader), 0) < 0) {
                perror("msgsnd");
                exit(1);
            }
        }
    }
}

static void SlpRemoveInWrongOrderReceivedDataBlock(int pos)
{
    int i;

    //Free dynamically allocated APP memorywrongOrderBlockData
    if (NULL != sSlpRxState.wrongOrderBlockData[pos].pAppDataPtr) {
        free(sSlpRxState.wrongOrderBlockData[pos].pAppDataPtr);
    }

    // Deleting element
    for (i = pos; i < sSlpRxState.nrOfWrongOrderReceivedDataBlocks; i++) {
        if (i < (sSlpRxState.nrOfWrongOrderReceivedDataBlocks - 1)) {
            sSlpRxState.wrongOrderBlockData[i] = sSlpRxState.wrongOrderBlockData[i+1];
            sSlpRxState.wrongOrderSeqNums[i] = sSlpRxState.wrongOrderSeqNums[i+1];
        } else {
            sSlpRxState.wrongOrderSeqNums[i] =  0;
            sSlpRxState.wrongOrderBlockData[i].pAppDataPtr = NULL;
            sSlpRxState.wrongOrderBlockData[i].appLen = 0;
        }
    }

    //Decrement nr in wrong order received blocks
    sSlpRxState.nrOfWrongOrderReceivedDataBlocks--;
}

static void SlpWrongOrderCleanup(void)
{
    int     i = 0;
    int     j;

    while ((i < sSlpRxState.nrOfWrongOrderReceivedDataBlocks) && (sSlpRxState.waitSeqNum >= sSlpRxState.wrongOrderSeqNums[i])) {
        i++;
    }

    //remove in wrong order received data block(s)
    i--;
    for (j = i; j >= 0; j--) {
        SlpRemoveInWrongOrderReceivedDataBlock(j);
    }
}

static int SlpIsNackToBeSent(uint64_t* pSeqNum)
{
    int nr;

    pthread_mutex_lock(&gSlpRxLock);
    SlpWrongOrderCleanup();
    nr = sSlpRxState.nrOfWrongOrderReceivedDataBlocks;
    *pSeqNum = sSlpRxState.waitSeqNum;
    pthread_mutex_unlock(&gSlpRxLock);
    if (0 < nr) return 1;
    return 0;
}

int SlpNackShouldBeSent(uint64_t* pSeqNum)
{
    int count;

    for (count = 1; count <= SLP_NACK_CHECK_LIMIT; count++) {
        usleep(SLP_NACK_CHECK_DELAY_US);
        if (!SlpIsNackToBeSent(pSeqNum)) return 0;
    }
    return 1;
}

void* slp_rx_send_nack()
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpShortMsg_t sbuf;

    for (;;) {
        uint64_t seqNum;

        if (SlpNackShouldBeSent(&seqNum)) {
            sSlpRxState.lastSentNackSeqNumClearCount++;
            if (SLP_NACK_RETRANS_LIMIT <= sSlpRxState.lastSentNackSeqNumClearCount) {
                sSlpRxState.lastSentNackSeqNum = GEN_ID_INVALID;
                sSlpRxState.lastSentNackSeqNumClearCount = 0;
            }
            if (sSlpRxState.lastSentNackSeqNum == seqNum) continue;
            sSlpRxState.lastSentNackSeqNum = seqNum;

            //get the message queue id for the key with value SLP_NACK_MSG_QUEUE_KEY_ID
            key = SLP_NACK_MSG_QUEUE_KEY_ID;

            if ((msqid = msgget(key, msgflg)) < 0) {
                perror("msgget");
                exit(1);
            }

            //send message type SLP_NACK_MSG
            sbuf.mtype = SLP_NACK_MSG;

            //set ACK data, appDataLen is in flag use
            if (0 == sSlpRxState.waitSeqNum) {
                sbuf.slpHeader.subHeader.appDataLen = SLP_FLAGS_RECEIVER_RESET;
            } else {
                sbuf.slpHeader.subHeader.appDataLen = 0;
            }
            sbuf.slpHeader.subHeader.fill = 0; //not used
            sbuf.slpHeader.subHeader.seqNum = seqNum;
            sbuf.slpHeader.crc =  crcFast(((const uint8_t*) &sbuf.slpHeader.subHeader),
                sizeof(sbuf.slpHeader.subHeader));
            sbuf.slpHeader.fill = 0; //not used

            if (gGenDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_send_nack: for getting message having seqNum %lu\n",
                    sbuf.slpHeader.subHeader.seqNum);
                pthread_mutex_unlock(&gGenPrintLock);
            }

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            sSlpRxDebug.nrOfSentNacks++;
#endif

            //send
            if (msgsnd(msqid, &sbuf, sizeof(sbuf.slpHeader), 0) < 0) {
                perror("msgsnd");
                exit(1);
            }
        }
    }
}

static void SlpSendAck(uint64_t seqNum)
{
    sSlpSendAckWriteIndex++;
    sSlpSendAckWriteIndex &= (SLP_MAX_NR_OF_BLOCKS - 1);
    assert(sSlpSendAckWriteIndex != sSlpSendAckReadIndex);
    sSlpDataToSendAck[sSlpSendAckWriteIndex].seqNum = seqNum;
    if (sSlpRxDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("SlpSendAck: seqNum %lu, write index %d, read index %d\n",
            sSlpDataToSendAck[sSlpSendAckWriteIndex].seqNum, sSlpSendAckWriteIndex, sSlpSendAckReadIndex);
        pthread_mutex_unlock(&gGenPrintLock);
    }
}

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
static void SlpRxDebugPrintStatistics(void)
{
    pthread_mutex_lock(&gGenPrintLock);
    printf(
        "SLP-rx statistics:\n"
        " nr of received data blocks %u\n"
        " nr of accepted data blocks %u\n"
        " nr of sent acks %u\n"
        " nr of sent nacks %u\n"
        " nr of received retransmitted data blocks %u\n"
        " nr of received retransmitted polls %u\n"
        " nr of accepted retransmitted data blocks %u\n"
        " nr of accepted retransmitted polls %u\n"
        " nr of received polls %u\n"
        " nr of dropped data blocks by test method a) %u\n"
        " nr of dropped data blocks by test method b) %u\n"
        " nr of dropped retransmitted data blocks by test %u\n"
        " nr of dropped retransmitted polls by test %u\n"
        " nr of dropped polls by test %u\n"
        " nr of to APP forwarded data blocks %u\n",
        sSlpRxDebug.nrOfReceivedDataBlocks, sSlpRxDebug.nrOfAcceptedDataBlocks,
        sSlpRxDebug.nrOfSentAcks, sSlpRxDebug.nrOfSentNacks,
        sSlpRxDebug.nrOfReceivedRetransmittedDataBlocks, sSlpRxDebug.nrOfReceivedRetransmittedPolls,
        sSlpRxDebug.nrOfAcceptedRetransmittedDataBlocks, sSlpRxDebug.nrOfAcceptedRetransmittedPolls,
        sSlpRxDebug.nrOfReceivedPolls, sSlpRxDebug.nrOfDroppedSuccessiveDataBlocks, sSlpRxDebug.nrOfDroppedRandDataBlocks,
        sSlpRxDebug.nrOfDroppedRetransmittedDataBlocks, sSlpRxDebug.nrOfDroppedRetransmittedPolls, sSlpRxDebug.nrOfDroppedPolls,
        sSlpRxDebug.nrOfDataBlocksForwardedToApp);
    pthread_mutex_unlock(&gGenPrintLock);
}
#endif

static void SlpForwardReceivedDataToApp(SlpInnerMsg_t* pRbuf)
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpAppMsg_t sbuf;

    key = SLP_APP_DATA_RECEIVE_MSG_QUEUE_KEY_ID;
    if ((msqid = msgget(key, msgflg)) < 0) {
        perror("msgget");
        exit(1);
    }
    sbuf.mtype = SLP_APP_DATA_RECEIVE_MSG;

    sbuf.data.genId = pRbuf->data.slpHeader.subHeader.seqNum;
    sbuf.data.len =  pRbuf->data.slpHeader.subHeader.appDataLen;
    memcpy(sbuf.data.appData, pRbuf->data.appData, pRbuf->data.slpHeader.subHeader.appDataLen);

    if (gGenDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("SlpForwardReceivedDataToApp: forwarded seqNum(slpId) %lu to APP\n", sbuf.data.genId);
        pthread_mutex_unlock(&gGenPrintLock);
    }

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
    sSlpRxDebug.nrOfDataBlocksForwardedToApp++;
    SlpRxDebugPrintStatistics();
#endif

    //send
    if (msgsnd(msqid, &sbuf, sizeof(sbuf.data), 0) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

static void SlpForwardInWrongOrderReceivedDataToApp(int pos)
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpAppMsg_t sbuf;

    key = SLP_APP_DATA_RECEIVE_MSG_QUEUE_KEY_ID;
    if ((msqid = msgget(key, msgflg)) < 0) {
        perror("msgget");
        exit(1);
    }
    sbuf.mtype = SLP_APP_DATA_RECEIVE_MSG;

    sbuf.data.genId = sSlpRxState.wrongOrderSeqNums[pos];
    sbuf.data.len = sSlpRxState.wrongOrderBlockData[pos].appLen;
    memcpy(sbuf.data.appData, sSlpRxState.wrongOrderBlockData[pos].pAppDataPtr, sSlpRxState.wrongOrderBlockData[pos].appLen);

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
    sSlpRxDebug.nrOfDataBlocksForwardedToApp++;
    SlpRxDebugPrintStatistics();
#endif

    //send
    if (msgsnd(msqid, &sbuf, sizeof(sbuf.data), 0) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

static void SlpSaveInWrongOrderReceivedDataBlock(SlpInnerMsg_t* pRbuf)
{
    void* pAppData;

    assert(SLP_MAX_NR_OF_BLOCKS > sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
    pAppData = malloc(pRbuf->data.slpHeader.subHeader.appDataLen);
    assert(NULL != pAppData);
    memcpy(pAppData, pRbuf->data.appData, pRbuf->data.slpHeader.subHeader.appDataLen);
    sSlpRxState.wrongOrderBlockData[sSlpRxState.nrOfWrongOrderReceivedDataBlocks].pAppDataPtr = pAppData;
    sSlpRxState.wrongOrderBlockData[sSlpRxState.nrOfWrongOrderReceivedDataBlocks].appLen = pRbuf->data.slpHeader.subHeader.appDataLen;
    sSlpRxState.wrongOrderSeqNums[sSlpRxState.nrOfWrongOrderReceivedDataBlocks] = pRbuf->data.slpHeader.subHeader.seqNum;
    sSlpRxState.nrOfWrongOrderReceivedDataBlocks++;
}
static void SlpSaveInWrongOrderReceivedPoll(uint64_t seqNum)
{
    assert(SLP_MAX_NR_OF_BLOCKS > sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
    sSlpRxState.wrongOrderBlockData[sSlpRxState.nrOfWrongOrderReceivedDataBlocks].pAppDataPtr = NULL;
    sSlpRxState.wrongOrderBlockData[sSlpRxState.nrOfWrongOrderReceivedDataBlocks].appLen = 0;
    sSlpRxState.wrongOrderSeqNums[sSlpRxState.nrOfWrongOrderReceivedDataBlocks] = seqNum;
    sSlpRxState.nrOfWrongOrderReceivedDataBlocks++;
}

static void SlpHandleInWrongOrderReceivedDataBlocks(uint64_t seqNum)
{
    int     i = 0;
    int     j;

    while ((i < sSlpRxState.nrOfWrongOrderReceivedDataBlocks) && (seqNum == sSlpRxState.wrongOrderSeqNums[i])) {
        if (0 < sSlpRxState.wrongOrderBlockData[i].appLen) {
            SlpForwardInWrongOrderReceivedDataToApp(i);
        }
        SlpSendAck(seqNum);

        if (sSlpRxDebugPrint) {
            pthread_mutex_lock(&gGenPrintLock);
            if (0 < sSlpRxState.wrongOrderBlockData[i].appLen) {
                printf("SlpHandleInWrongOrderReceivedDataBlocks: data block message having seqNum %lu, pos %d, nr in wrong order received Blocks %d\n",
                    seqNum, i, sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
            } else {
                printf("SlpHandleInWrongOrderReceivedDataBlocks: poll message having seqNum %lu, pos %d, nr in wrong order received Blocks %d\n",
                    seqNum, i, sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
            }
            pthread_mutex_unlock(&gGenPrintLock);
        }
        sSlpRxState.waitSeqNum++;
        seqNum++;
        i++;
    }

    //remove in wrong order received data block(s)
    i--;
    for (j = i; j >= 0; j--) {
        SlpRemoveInWrongOrderReceivedDataBlock(j);
    }
}

void* slp_rx_receive_app_data()
{
    int msqid;
    key_t key;
    SlpInnerMsg_t rbuf;
    int retVal;

    //get the message queue id
    key = SLP_INNER_APP_DATA_MSG_QUEUE_KEY_ID;

    //receive continuously
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.data), SLP_INNER_APP_DATA_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        usleep(SLP_SIMULATED_TRANSFER_DELAY_US);

#ifdef GEN_SLP_TEST_LOST_APP_DATA
        //10 successive APP data blocks per 256 are lost
        if ((100 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100)) || (101 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100))  ||
            (102 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100)) || (103 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100))  ||
            (104 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100)) || (105 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100))  ||
            (106 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100)) || (107 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100))  ||
            (108 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100)) || (109 == (rbuf.data.slpHeader.subHeader.seqNum % 0x100)))  {
            pthread_mutex_lock(&gGenPrintLock);
            printf("slp_rx_receive_app_data/test executed: APP data lost having seqNum %lu, nr in wrong order received blocks %d\n",
                rbuf.data.slpHeader.subHeader.seqNum, sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
            pthread_mutex_unlock(&gGenPrintLock);
            usleep(10000);
#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            sSlpRxDebug.nrOfDroppedSuccessiveDataBlocks++;
#endif
            continue;
        }
#endif
#ifdef GEN_SLP_TEST_RAND_LOST
        if (SlpTestRandOfThisSeqNum(rbuf.data.slpHeader.subHeader.seqNum, 3)) {
#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            sSlpRxDebug.nrOfDroppedRandDataBlocks++;
#endif
            continue;
        }
#endif
        if (rbuf.data.slpHeader.crc == crcFast(((const uint8_t*) &rbuf.data.slpHeader.subHeader),
            sizeof(rbuf.data.slpHeader.subHeader) + rbuf.data.slpHeader.subHeader.appDataLen)) {

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            sSlpRxDebug.nrOfReceivedDataBlocks++;
#endif

            pthread_mutex_lock(&gSlpRxLock);
            if (gGenDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_rx_receive_app_data: receiving APP data of seqNum %lu, waiting for seqNum %lu, nr in wrong order received blocks %d\n",
                     rbuf.data.slpHeader.subHeader.seqNum, sSlpRxState.waitSeqNum, sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
                pthread_mutex_unlock(&gGenPrintLock);
            }

            //zero seqNums are always accepted due to possible device resets
            if (!sSlpRxState.waitSeqNum || !rbuf.data.slpHeader.subHeader.seqNum || (sSlpRxState.waitSeqNum == rbuf.data.slpHeader.subHeader.seqNum)) {
                SlpForwardReceivedDataToApp(&rbuf);
                SlpSendAck(rbuf.data.slpHeader.subHeader.seqNum);
                sSlpRxState.waitSeqNum++;
                SlpHandleInWrongOrderReceivedDataBlocks(sSlpRxState.waitSeqNum);
#ifdef GEN_SLP_RX_DEBUG_STATISTICS
                sSlpRxDebug.nrOfAcceptedDataBlocks++;
#endif
            } else if (sSlpRxState.waitSeqNum < rbuf.data.slpHeader.subHeader.seqNum) {
                //at least one data block lost
                SlpSaveInWrongOrderReceivedDataBlock(&rbuf);
#ifdef GEN_SLP_RX_DEBUG_STATISTICS
                sSlpRxDebug.nrOfAcceptedDataBlocks++;
#endif
            }
            pthread_mutex_unlock(&gSlpRxLock);

            //print received last byte of received APP data
            if (sSlpRxDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_rx_receive_app_data: received last byte of APP data %u\n",
                    rbuf.data.appData[rbuf.data.slpHeader.subHeader.appDataLen - 1]);
                pthread_mutex_unlock(&gGenPrintLock);
            }
            if (gGenDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_rx_receive_app_data: seqNum %lu received\n", rbuf.data.slpHeader.subHeader.seqNum);
                pthread_mutex_unlock(&gGenPrintLock);
            }
        }
    }
}

void* slp_rx_receive_retrans()
{
    int msqid;
    key_t key;
    SlpInnerMsg_t rbuf;
    int retVal;

    //get the message queue id
    key = SLP_RETRANS_MSG_QUEUE_KEY_ID;

    //receive continuously
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.data), SLP_RETRANS_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        usleep(SLP_SIM_CTRL_MSG_TRANS_DELAY_US);

#ifdef GEN_SLP_TEST_RAND_LOST
        {
            int callId;

            if (0 < rbuf.data.slpHeader.subHeader.appDataLen) {
                callId = 4;
            } else {
                callId = 5;
            }

            if (SlpTestRandOfThisSeqNum(rbuf.data.slpHeader.subHeader.seqNum, callId)) {
#ifdef GEN_SLP_RX_DEBUG_STATISTICS
                if (0 < rbuf.data.slpHeader.subHeader.appDataLen) {
                    sSlpRxDebug.nrOfDroppedRetransmittedDataBlocks++;
                } else {
                    sSlpRxDebug.nrOfDroppedRetransmittedPolls++;
                }
#endif
                continue;
            }
        }
#endif

        //crc must match
        if (rbuf.data.slpHeader.crc == crcFast(((const uint8_t*) &rbuf.data.slpHeader.subHeader),
            sizeof(rbuf.data.slpHeader.subHeader) + rbuf.data.slpHeader.subHeader.appDataLen)) {

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            if (0 < rbuf.data.slpHeader.subHeader.appDataLen) {
                sSlpRxDebug.nrOfReceivedRetransmittedDataBlocks++;
            } else {
                sSlpRxDebug.nrOfReceivedRetransmittedPolls++;
            }
#endif

            //nothing to do if no wrong order received data blocks or retransmitted data block is not the oldest one
            pthread_mutex_lock(&gSlpRxLock);
            if ((0 == sSlpRxState.nrOfWrongOrderReceivedDataBlocks) ||
                (sSlpRxState.waitSeqNum != rbuf.data.slpHeader.subHeader.seqNum))
            {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_of_rec_dev_receive_retransmit: received seqNum %lu isn´t waiting for seqNum %lu or not in wrong order received data blocks %d\n",
                    rbuf.data.slpHeader.subHeader.seqNum, sSlpRxState.waitSeqNum, sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
                pthread_mutex_unlock(&gGenPrintLock);
                pthread_mutex_unlock(&gSlpRxLock);
                continue;
            }
            if (0 < rbuf.data.slpHeader.subHeader.appDataLen) {
                SlpForwardReceivedDataToApp(&rbuf);
            }
            SlpSendAck(rbuf.data.slpHeader.subHeader.seqNum);
            sSlpRxState.waitSeqNum++;
            SlpHandleInWrongOrderReceivedDataBlocks(sSlpRxState.waitSeqNum);
            pthread_mutex_unlock(&gSlpRxLock);
#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            if (0 < rbuf.data.slpHeader.subHeader.appDataLen) {
                sSlpRxDebug.nrOfAcceptedRetransmittedDataBlocks++;
            } else {
                sSlpRxDebug.nrOfAcceptedRetransmittedPolls++;
            }
#endif
        }
    }
}

void* slp_rx_receive_poll()
{
    int msqid;
    key_t key;
    SlpShortMsg_t rbuf;
    int retVal;
    int     pos;

    //get the message queue id for the key with value SLP_POLL_MSG_QUEUE_KEY_ID
    key = SLP_POLL_MSG_QUEUE_KEY_ID;

    //receive continuously message type SLP_POLL_MSG
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.slpHeader), SLP_POLL_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        usleep(SLP_SIM_CTRL_MSG_TRANS_DELAY_US);

#ifdef GEN_SLP_TEST_RAND_LOST
        if (SlpTestRandOfThisSeqNum(rbuf.slpHeader.subHeader.seqNum, 6)) {
#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            sSlpRxDebug.nrOfDroppedPolls++;
#endif
            continue;
        }
#endif

        //crc must match
        if (rbuf.slpHeader.crc == crcFast(((const uint8_t*) &rbuf.slpHeader.subHeader),
            sizeof(rbuf.slpHeader.subHeader))) {

#ifdef GEN_SLP_RX_DEBUG_STATISTICS
            sSlpRxDebug.nrOfReceivedPolls++;
#endif

            pthread_mutex_lock(&gSlpRxLock);
            if (gGenDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_of_rec_dev_receive_poll: receiving poll of seqNum %lu, waiting for seqNum %lu, nr in wrong order received data blocks %d\n",
                     rbuf.slpHeader.subHeader.seqNum, sSlpRxState.waitSeqNum, sSlpRxState.nrOfWrongOrderReceivedDataBlocks);
                pthread_mutex_unlock(&gGenPrintLock);
            }

            //zero seqNums are always accepted due to possible device resets
            if (!sSlpRxState.waitSeqNum || !rbuf.slpHeader.subHeader.seqNum || (sSlpRxState.waitSeqNum == rbuf.slpHeader.subHeader.seqNum)) {
                SlpSendAck(rbuf.slpHeader.subHeader.seqNum);
                sSlpRxState.waitSeqNum++;
                if (gGenDebugPrint) {
                     pthread_mutex_lock(&gGenPrintLock);
                     printf("slp_of_rec_dev_receive_poll: poll with seqNum %lu successfully received\n",
                         rbuf.slpHeader.subHeader.seqNum);
                     pthread_mutex_unlock(&gGenPrintLock);
                }
                SlpHandleInWrongOrderReceivedDataBlocks(sSlpRxState.waitSeqNum);
            } else if (sSlpRxState.waitSeqNum < rbuf.slpHeader.subHeader.seqNum) {
                //at least one data block lost
                SlpSaveInWrongOrderReceivedPoll(rbuf.slpHeader.subHeader.seqNum);
            }
            pthread_mutex_unlock(&gSlpRxLock);
        }
    }
}

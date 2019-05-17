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

pthread_mutex_t gSlpTxLock;

typedef struct SlpTxBlockData_t {
    void*	pAppDataPtr;
    uint32_t    appLen;
}  SlpTxBlockData_t;

typedef struct SlpTxState_t {
    uint64_t            seqNums[SLP_MAX_NR_OF_BLOCKS];
    SlpTxBlockData_t    blockData[SLP_MAX_NR_OF_BLOCKS];
    int                 nrOfDataBlocks;
    uint64_t            seqNumCount;
    int                 primaryAppWait;
    int                 secondaryAppWait;
    int                 dataBlockSendingDecided;
    int                 pollSendingDecided;
} SlpTxState_t;

static SlpTxState_t sSlpTxState;

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
typedef struct SlpTxDebug_t {
    pthread_mutex_t timeLock;
    struct tm startTimeInfo;
    int    startTimeSet;
    struct tm timeInfo;
    uint32_t nrOfReceivedDataBlocksFromApp;
    uint32_t nrOfSentDataBlocks;
    uint32_t nrOfRetransmittedDataBlocks;
    uint32_t nrOfRetransmittedPolls;
    uint32_t nrOfReceivedAcks;
    uint32_t nrOfReceivedNacks;
    uint32_t nrOfSentPolls;
    uint32_t nrOfDroppedSuccessiveAcks;
    uint32_t nrOfDroppedRandAcks;
    uint32_t nrOfDroppedNacks;
} SlpTxDebug_t;

static SlpTxDebug_t sSlpTxDebug;
#endif

static int sSlpTxDebugPrint;

static void SlpPollAckReceived(uint64_t seqNum);

static void SlpSendState(uint8_t state)
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpStateMsg_t sbuf;

    //get the message queue id
    key = SLP_APP_STATE_MSG_QUEUE_KEY_ID;

    if ((msqid = msgget(key, msgflg)) < 0) {
        perror("msgget");
        exit(1);
    }

    //send message
    sbuf.mtype = SLP_APP_STATE_MSG;

    //set speed control data parameter
    sbuf.data.state = state;

    if (sSlpTxDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("SlpSendState: state %u sent\n", sbuf.data.state);
        pthread_mutex_unlock(&gGenPrintLock);
    }

    //send 
    if (msgsnd(msqid, &sbuf, sizeof(sbuf.data), 0) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

static void SlpSendInfo(uint8_t infoType, uint64_t slpId, uint64_t appId)
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpInfoMsg_t sbuf;

    //get the message queue id
    key = SLP_APP_INFO_MSG_QUEUE_KEY_ID;

    if ((msqid = msgget(key, msgflg)) < 0) {
        perror("msgget");
        exit(1);
    }

    //set message type
    sbuf.mtype = SLP_APP_INFO_MSG;

    //set msg parameters
    sbuf.data.infoType = infoType;
    sbuf.data.appId = appId;
    sbuf.data.slpId = slpId;

    if (sSlpTxDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        if (0 < sSlpTxState.nrOfDataBlocks) {
            printf("SlpSendInfo: nr of data blocks %d, seqNums %lu..%lu..%lu\n",
                sSlpTxState.nrOfDataBlocks,
                sSlpTxState.seqNums[0], slpId, sSlpTxState.seqNums[sSlpTxState.nrOfDataBlocks - 1]);
        } else {
            printf("SlpSendInfo: nr of data blocks %d\n", sSlpTxState.nrOfDataBlocks);
        }
        pthread_mutex_unlock(&gGenPrintLock);
    }

    //send
    if (msgsnd(msqid, &sbuf, sizeof(sbuf.data), 0) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

static void SlpRemoveDataBlock(int pos)
{
    int i;

    // Deleting element
    for (i = pos; i < sSlpTxState.nrOfDataBlocks; i++) {
        if (i < (sSlpTxState.nrOfDataBlocks - 1)) {
            sSlpTxState.blockData[i] = sSlpTxState.blockData[i+1];
            sSlpTxState.seqNums[i] = sSlpTxState.seqNums[i+1];
        } else {
            sSlpTxState.seqNums[i] = 0;
            sSlpTxState.blockData[i].pAppDataPtr = NULL;
            sSlpTxState.blockData[i].appLen = 0;
        }
    }

    //Decrement nr of data blocks
    sSlpTxState.nrOfDataBlocks--;
}

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
void* slp_tx_debug_get_time()
{
    time_t rawTime;
    struct tm* pTimeInfo;

    if (pthread_mutex_init(&sSlpTxDebug.timeLock, NULL) != 0)
    {
        printf("\n slp tx debug mutex init failed\n");
        exit(1);
    }
    for (;;) {
        time(&rawTime);
        pTimeInfo = localtime(&rawTime);
        pthread_mutex_lock(&sSlpTxDebug.timeLock);
        if (!sSlpTxDebug.startTimeSet) {
            sSlpTxDebug.startTimeSet = 1;
            sSlpTxDebug.startTimeInfo = *pTimeInfo;
        }
        sSlpTxDebug.timeInfo = *pTimeInfo;
        pthread_mutex_unlock(&sSlpTxDebug.timeLock);
        sleep(1);
    }
}

static void SlpTxDebugPrintStatistics(void)
{
    pthread_mutex_lock(&gGenPrintLock);
    pthread_mutex_lock(&sSlpTxDebug.timeLock);
    printf("%s", asctime(&sSlpTxDebug.startTimeInfo));
    printf("%s", asctime(&sSlpTxDebug.timeInfo));
    pthread_mutex_unlock(&sSlpTxDebug.timeLock);
    printf(
        "SLP-tx statistics:\n"
        " nr of data blocks received from APP %u\n"
        " nr of sent data blocks %u\n"
        " nr of received acks %u\n"
        " nr of received nacks %u\n"
        " nr of retransmitted data blocks %u\n"
        " nr of retransmitted polls %u\n"
        " nr of sent polls %u\n"
        " nr of dropped acks by test method a) %u\n"
        " nr of dropped acks by test method b) %u\n"
        " nr of dropped nacks by test %u\n",
        sSlpTxDebug.nrOfReceivedDataBlocksFromApp, sSlpTxDebug.nrOfSentDataBlocks,
        sSlpTxDebug.nrOfReceivedAcks, sSlpTxDebug.nrOfReceivedNacks,
        sSlpTxDebug.nrOfRetransmittedDataBlocks, sSlpTxDebug.nrOfRetransmittedPolls, sSlpTxDebug.nrOfSentPolls,
        sSlpTxDebug.nrOfDroppedSuccessiveAcks, sSlpTxDebug.nrOfDroppedRandAcks, sSlpTxDebug.nrOfDroppedNacks);
    pthread_mutex_unlock(&gGenPrintLock);
}
#endif

static void SlpEndDataBlock(int pos, uint32_t flags)
{
    if (NULL != sSlpTxState.blockData[pos].pAppDataPtr) {
        //ordinary APP data ack received: send DONE msg to APP
        if (0 != (SLP_FLAGS_RECEIVER_RESET & flags)) {
            SlpSendInfo(SLP_INFO_TYPE_DONE_AND_RX_RESET, sSlpTxState.seqNums[pos], 0);
        } else {
            SlpSendInfo(SLP_INFO_TYPE_DONE, sSlpTxState.seqNums[pos], 0);
        }

        //Free dynamically allocated APP memory
        free(sSlpTxState.blockData[pos].pAppDataPtr);
    } else {
        //poll ack received: send possible receiver reset to APP
        if (0 != (SLP_FLAGS_RECEIVER_RESET & flags)) {
            SlpSendInfo(SLP_INFO_TYPE_RX_RESET, sSlpTxState.seqNums[pos], 0);
        }
        //ack info to poll sending
        SlpPollAckReceived(sSlpTxState.seqNums[pos]);
    }

    //remove data block from SLP bookkeeping
    SlpRemoveDataBlock(pos);
#ifdef GEN_SLP_TX_DEBUG_STATISTICS
    SlpTxDebugPrintStatistics();
#endif
}

static void SlpSave(SlpAppMsg_t* pRbuf, uint64_t seqNum, int nr)
{
    void* pAppData;

    pAppData = malloc(pRbuf->data.len);
    assert(NULL != pAppData);
    memcpy(pAppData, pRbuf->data.appData, pRbuf->data.len);
    assert(SLP_MAX_NR_OF_BLOCKS > nr);
    sSlpTxState.blockData[nr].pAppDataPtr = pAppData;
    sSlpTxState.blockData[nr].appLen = pRbuf->data.len;
    sSlpTxState.seqNums[nr] = seqNum;
}

static void SlpSendInnerMsg(SlpAppMsg_t* pRbuf, uint64_t seqNum)
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpInnerMsg_t sbuf;

    //get the message queue id for the key with value SLP_APP_DATA_MSG_QUEUE_KEY_ID
    key = SLP_INNER_APP_DATA_MSG_QUEUE_KEY_ID;

    if ((msqid = msgget(key, msgflg)) < 0) {
        perror("msgget");
        exit(1);
    }

    //send message type SLP_APP_DATA_MSG
    sbuf.mtype = SLP_INNER_APP_DATA_MSG;

    //set SLP header and APP data
    sbuf.data.slpHeader.subHeader.appDataLen =  pRbuf->data.len;
    sbuf.data.slpHeader.subHeader.fill = 0; //not used

    sbuf.data.slpHeader.subHeader.seqNum =  seqNum;
    memcpy(sbuf.data.appData, pRbuf->data.appData, pRbuf->data.len);
    if (SLP_APP_DATA_SIZE > pRbuf->data.len) {
        memset(sbuf.data.appData + pRbuf->data.len, 0, SLP_APP_DATA_SIZE - pRbuf->data.len);
    }
    sbuf.data.slpHeader.crc =  crcFast(((const uint8_t*) &sbuf.data.slpHeader.subHeader),
         sizeof(sbuf.data.slpHeader.subHeader) + sbuf.data.slpHeader.subHeader.appDataLen);
    sbuf.data.slpHeader.fill = 0; //not used

    if (sSlpTxDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("SlpSendInnerMsg: sent seqNum %lu and %u bytes with last byte %u\n",
            sbuf.data.slpHeader.subHeader.seqNum, sbuf.data.slpHeader.subHeader.appDataLen, sbuf.data.appData[sbuf.data.slpHeader.subHeader.appDataLen - 1]);
        pthread_mutex_unlock(&gGenPrintLock);
    }
    if (gGenDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("SlpSendInnerMsg: seqNum %lu and len %u sent\n", sbuf.data.slpHeader.subHeader.seqNum, sbuf.data.slpHeader.subHeader.appDataLen);
        pthread_mutex_unlock(&gGenPrintLock);
    }

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
    sSlpTxDebug.nrOfSentDataBlocks++;
#endif

    //send
    if (msgsnd(msqid, &sbuf, sizeof(sbuf.data), 0) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

void* slp_tx_receive_app_data()
{
    int msqid;
    key_t key;
    SlpAppMsg_t rbuf;
    int retVal;
    uint64_t seqNum;

    //get the message queue id for the key with value APP_DATA_MSG_QUEUE_KEY_ID
    key = SLP_APP_DATA_SEND_MSG_QUEUE_KEY_ID;

    //receive continuously message type APP_DATA_MSG
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);

        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.data), SLP_APP_DATA_SEND_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        if ((0 == rbuf.data.len) || (SLP_APP_DATA_SIZE < rbuf.data.len)) {
            pthread_mutex_lock(&gGenPrintLock);
            printf("slp_tx_receive_app_data: incorrect rbuf.appData.len %u",
                rbuf.data.len);
            pthread_mutex_unlock(&gGenPrintLock);
            exit(1);
        }

        //wait for poll sending
        for (;;) {
            pthread_mutex_lock(&gSlpTxLock);
            if (sSlpTxState.pollSendingDecided) {
                pthread_mutex_unlock(&gSlpTxLock);
                usleep(SLP_WAIT_FOR_POLL_SENDING_US);
            } else {
                break;
            }
        }
        sSlpTxState.dataBlockSendingDecided = 1;


#ifdef GEN_SLP_TX_DEBUG_STATISTICS
        sSlpTxDebug.nrOfReceivedDataBlocksFromApp++;
#endif

        //Read seqNumCount and nr of data blocks, save data block and increment counters during mutex is locked
        seqNum = sSlpTxState.seqNumCount;

        //save data block for possible retransmission
        SlpSave(&rbuf, seqNum, sSlpTxState.nrOfDataBlocks);

        //increment and release mutex
        sSlpTxState.nrOfDataBlocks++;
        sSlpTxState.seqNumCount++;
        sSlpTxState.dataBlockSendingDecided = 0;
        pthread_mutex_unlock(&gSlpTxLock);

        if (!sSlpTxState.primaryAppWait && (SLP_APP_WAIT_LIMIT <= sSlpTxState.nrOfDataBlocks)) {
            sSlpTxState.primaryAppWait = 1;
            if (!sSlpTxState.secondaryAppWait) {
                SlpSendState(SLP_ASKS_APP_TO_WAIT);
            }
        }

        //send info to APP
        SlpSendInfo(SLP_INFO_TYPE_APP_DATA_RECEIVED, seqNum, rbuf.data.genId);

        //send APP data block to SLP-rx
        SlpSendInnerMsg(&rbuf, seqNum);

        if (gGenDebugPrint) {
            pthread_mutex_lock(&gGenPrintLock);
            printf("slp_tx_receive_app_data: seqNum %lu, nr of data blocks %u, asked to wait %d\n",
                seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait);
            pthread_mutex_unlock(&gGenPrintLock);
        }
    }
}

#ifdef GEN_SLP_TEST_RAND_LOST
int SlpTestRandOfThisSeqNum(uint64_t seqNum, int testCase)
{
    long r = rand();
    long chance;

    //control chance to lower in case of high frequence messages: SLP_APP_DATA_MSG and SLP_ACK_MSG
    if ((1 == testCase) || (3 == testCase)) {
        chance = 0x100;
    } else {
        chance = 0x10;
    }

    if ((r % chance) == (seqNum % chance)) {
        pthread_mutex_lock(&gGenPrintLock);
        if (1 == testCase) {
            printf("SlpTestRandOfThisSeqNum/test executed: ack lost having seqNum %lu, nr of data blocks %d, asked to wait %d, used random number %ld\n",
                seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait, r);
        } else if (2 == testCase) {
            printf("SlpTestRandOfThisSeqNum/test executed: nack lost having seqNum %lu, nr of data blocks %d, asked to wait %d, used random number %ld\n",
                seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait, r);
        } else if (3 == testCase) {
            printf("SlpTestRandOfThisSeqNum/test executed: data block lost having seqNum %lu, nr of data blocks %d, asked to wait %d, used random number %ld\n",
                seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait, r);
        } else if (4 == testCase) {
            printf("SlpTestRandOfThisSeqNum/test executed: retransmitted data block lost having seqNum %lu, nr of data blocks %d, asked to wait %d, used random number %ld\n",
                seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait, r);
        } else if (5 == testCase) {
            printf("SlpTestRandOfThisSeqNum/test executed: retransmitted poll lost having seqNum %lu, nr of data blocks %d, asked to wait %d, used random number %ld\n",
               seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait, r);
        } else if (6 == testCase) {
            printf("SlpTestRandOfThisSeqNum/test executed: poll lost having seqNum %lu, nr of data blocks %d, asked to wait %d, used random number %ld\n",
                seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait, r);
        } else {
            printf("SlpTestRandOfThisSeqNum/test executed: item lost having seqNum %lu, nr of data blocks %d, asked to wait %d, testCase %d, used random number %ld\n",
                seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait, testCase, r);
        }
        pthread_mutex_unlock(&gGenPrintLock);
        return 1;
    }
    return 0;
}
#endif

void* slp_tx_receive_ack()
{
    int msqid;
    key_t key;
    SlpShortMsg_t rbuf;
    int retVal;
    int     pos;

    //get the message queue id for the key with value SLP_ACK_MSG_QUEUE_KEY_ID
    key = SLP_ACK_MSG_QUEUE_KEY_ID;

    //receive continuously message type SLP_ACK_MSG
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.slpHeader), SLP_ACK_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        usleep(SLP_SIM_CTRL_MSG_TRANS_DELAY_US);

#ifdef GEN_SLP_TEST_LOST_ACKS
        //10 successive ACKs per 256 are lost
        if ((100 == (rbuf.slpHeader.subHeader.seqNum % 0x100)) || (101 == (rbuf.slpHeader.subHeader.seqNum % 0x100))  ||
           (102 == (rbuf.slpHeader.subHeader.seqNum % 0x100)) || (103 == (rbuf.slpHeader.subHeader.seqNum % 0x100))  ||
           (104 == (rbuf.slpHeader.subHeader.seqNum % 0x100)) || (105 == (rbuf.slpHeader.subHeader.seqNum % 0x100))  ||
           (106 == (rbuf.slpHeader.subHeader.seqNum % 0x100)) || (107 == (rbuf.slpHeader.subHeader.seqNum % 0x100))  ||
           (108 == (rbuf.slpHeader.subHeader.seqNum % 0x100)) || (109 == (rbuf.slpHeader.subHeader.seqNum % 0x100)))  {
            pthread_mutex_lock(&gGenPrintLock);
            printf("slp_tx_receive_ack/test executed: ACK lost having seqNum %lu, nr of data blocks %d, asked to wait %d\n",
                rbuf.slpHeader.subHeader.seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait);
            pthread_mutex_unlock(&gGenPrintLock);
            usleep(10000);
#ifdef GEN_SLP_TX_DEBUG_STATISTICS
            sSlpTxDebug.nrOfDroppedSuccessiveAcks++;
#endif
            continue;
        }
#endif
#ifdef GEN_SLP_TEST_RAND_LOST
        if (SlpTestRandOfThisSeqNum(rbuf.slpHeader.subHeader.seqNum, 1)) {
#ifdef GEN_SLP_TX_DEBUG_STATISTICS
            sSlpTxDebug.nrOfDroppedRandAcks++;
#endif
            continue;
        }
#endif
        //crc must match
        if (rbuf.slpHeader.crc == crcFast(((const uint8_t*) &rbuf.slpHeader.subHeader),
            sizeof(rbuf.slpHeader.subHeader))) {
            int     pos;
            int     i;

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
            sSlpTxDebug.nrOfReceivedAcks++;
#endif

            pthread_mutex_lock(&gSlpTxLock);

            //find saved data block having this seqNum
            pos = binarySearch(sSlpTxState.seqNums, 0, sSlpTxState.nrOfDataBlocks - 1, rbuf.slpHeader.subHeader.seqNum);
            if (0 > pos)
            {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_tx_receive_ack: seqNum %lu not found!!!, nr of data blocks %d, oldest seqNum %lu\n",
                    rbuf.slpHeader.subHeader.seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.seqNums[0]);
                pthread_mutex_unlock(&gGenPrintLock);
                pthread_mutex_unlock(&gSlpTxLock);
                continue;
            }

            //print all data blocks from beginning to seqNum of this ACK
            if (gGenDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                for (i = 0; i <= pos; i++) {
                    printf("slp_tx_receive_ack: seqNums %lu/%lu, pos %d, nr of data blocks %d, asked to wait %d\n",
                        rbuf.slpHeader.subHeader.seqNum,  sSlpTxState.seqNums[i], i, sSlpTxState.nrOfDataBlocks, sSlpTxState.primaryAppWait);
                }
                pthread_mutex_unlock(&gGenPrintLock);
            }

            //end all blocks from pos down to beginning
            for (i = pos; i >= 0; i--) {
                //subHeader.appDataLen is in flag use: SLP_FLAGS_RECEIVER_RESET
                SlpEndDataBlock(i, rbuf.slpHeader.subHeader.appDataLen);
            }

            pthread_mutex_unlock(&gSlpTxLock);

            if (sSlpTxState.primaryAppWait && (SLP_APP_RESTART_LIMIT >= sSlpTxState.nrOfDataBlocks)) {
                sSlpTxState.primaryAppWait = 0;
                if (!sSlpTxState.secondaryAppWait) {
                    SlpSendState(SLP_ASKS_APP_TO_GO_ON);
                }
            }
#ifdef SLP_SECONDARY_APP_WAIT
            if (sSlpTxState.secondaryAppWait) {
                sSlpTxState.secondaryAppWait = 0;
                if (!sSlpTxState.primaryAppWait) {
                    SlpSendState(SLP_ASKS_APP_TO_GO_ON);
                }
            }
#endif
        }
    }
}

static void SlpRetransmit(int pos)
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpInnerMsg_t sbuf;

    //get the message queue id for the key with value SLP_RETRANS_MSG_QUEUE_KEY_ID
    key = SLP_RETRANS_MSG_QUEUE_KEY_ID;

    if ((msqid = msgget(key, msgflg)) < 0) {
        perror("msgget");
        exit(1);
    }

    //send message type SLP_RETRANS_MSG
    sbuf.mtype = SLP_RETRANS_MSG;

    //set SLP header and APP data
    sbuf.data.slpHeader.subHeader.appDataLen =  sSlpTxState.blockData[pos].appLen;
    sbuf.data.slpHeader.subHeader.fill = 0; //not used

    sbuf.data.slpHeader.subHeader.seqNum =  sSlpTxState.seqNums[pos];

    //sanity check
    if (NULL != sSlpTxState.blockData[pos].pAppDataPtr) {
        assert(0 < sSlpTxState.blockData[pos].appLen);
    } else {
        assert(0 == sSlpTxState.blockData[pos].appLen);
    }

    //poll sending saves pure seqNum without any APP data when pAppDataPtr is set NULL
    if (NULL != sSlpTxState.blockData[pos].pAppDataPtr) {
        memcpy(sbuf.data.appData, sSlpTxState.blockData[pos].pAppDataPtr, sSlpTxState.blockData[pos].appLen);
        if (SLP_APP_DATA_SIZE > sSlpTxState.blockData[pos].appLen) {
            memset(sbuf.data.appData + sSlpTxState.blockData[pos].appLen, 0, SLP_APP_DATA_SIZE - sSlpTxState.blockData[pos].appLen);
        }
    } else {
        memset(sbuf.data.appData, 0, SLP_APP_DATA_SIZE);
    }

    sbuf.data.slpHeader.crc =  crcFast(((const uint8_t*) &sbuf.data.slpHeader.subHeader),
         sizeof(sbuf.data.slpHeader.subHeader) + sbuf.data.slpHeader.subHeader.appDataLen);
    sbuf.data.slpHeader.fill = 0; //not used

    if (gGenDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        if (0 < sSlpTxState.blockData[pos].appLen) {
            printf("SlpRetransmit: retransmit APP data block message having seqNum %lu of %u bytes with last byte %u\n",
                sbuf.data.slpHeader.subHeader.seqNum, sbuf.data.slpHeader.subHeader.appDataLen, sbuf.data.appData[sbuf.data.slpHeader.subHeader.appDataLen - 1]);
        } else {
            printf("SlpRetransmit: retransmit poll message having seqNum %lu\n",
                sbuf.data.slpHeader.subHeader.seqNum);
        }
        pthread_mutex_unlock(&gGenPrintLock);
    }

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
    if (0 < sSlpTxState.blockData[pos].appLen) {
        sSlpTxDebug.nrOfRetransmittedDataBlocks++;
    } else {
        sSlpTxDebug.nrOfRetransmittedPolls++;
    }
#endif

    //send
    if (msgsnd(msqid, &sbuf, sizeof(sbuf.data), 0) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

void* slp_tx_receive_nack()
{
    int msqid;
    key_t key;
    SlpShortMsg_t rbuf;
    int retVal;
    int     pos;

    //get the message queue id for the key with value SLP_NACK_MSG_QUEUE_KEY_ID
    key = SLP_NACK_MSG_QUEUE_KEY_ID;

    //receive continuously message type SLP_NACK_MSG
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.slpHeader), SLP_NACK_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        usleep(SLP_SIM_SMALL_CTRL_MSG_TRANS_DELAY_US);

#ifdef GEN_SLP_TEST_RAND_LOST
        if (SlpTestRandOfThisSeqNum(rbuf.slpHeader.subHeader.seqNum, 2)) {
#ifdef GEN_SLP_TX_DEBUG_STATISTICS
            sSlpTxDebug.nrOfDroppedNacks++;
#endif
            continue;
        }
#endif

        //crc must match
        if (rbuf.slpHeader.crc == crcFast(((const uint8_t*) &rbuf.slpHeader.subHeader),
            sizeof(rbuf.slpHeader.subHeader))) {

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
           sSlpTxDebug.nrOfReceivedNacks++;
#endif

            //find saved data block having this seqNum
            pthread_mutex_lock(&gSlpTxLock);
            pos = binarySearch(sSlpTxState.seqNums, 0, sSlpTxState.nrOfDataBlocks - 1, rbuf.slpHeader.subHeader.seqNum);
            if (0 > pos)
            {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_receive_nack: seqNum %lu not found!!!, nr of data blocks %d, oldest seqNum %lu\n",
                    rbuf.slpHeader.subHeader.seqNum, sSlpTxState.nrOfDataBlocks, sSlpTxState.seqNums[0]);
                pthread_mutex_unlock(&gGenPrintLock);
                pthread_mutex_unlock(&gSlpTxLock);
                continue;
            }
            assert(rbuf.slpHeader.subHeader.seqNum == sSlpTxState.seqNums[pos]);

            if (gGenDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_receive_nack: received nack message having seqNum %lu\n",
                    rbuf.slpHeader.subHeader.seqNum);
                pthread_mutex_unlock(&gGenPrintLock);
            }

            //retransmit
            SlpRetransmit(pos);
            pthread_mutex_unlock(&gSlpTxLock);

            if (0 != (SLP_FLAGS_RECEIVER_RESET & rbuf.slpHeader.subHeader.appDataLen)) {
                SlpSendInfo(SLP_INFO_TYPE_RX_RESET, sSlpTxState.seqNums[pos], 0);
            }
#ifdef SLP_SECONDARY_APP_WAIT
            if (!sSlpTxState.secondaryAppWait) {
                sSlpTxState.secondaryAppWait = 1;
                if (!sSlpTxState.primaryAppWait) {
                    SlpSendState(SLP_ASKS_APP_TO_WAIT);
                }
            }
#endif
        }
    }
}

#define SLP_POLL_PTHREAD_PERIOD_US     1000
#define SLP_POLL_CHECK_TIME_US         (3*SLP_SIMULATED_TRANSFER_DELAY_US)
#define SLP_POLL_ACK_TIMEOUT_MS        (SLP_POLL_CHECK_TIME_US/SLP_POLL_PTHREAD_PERIOD_US)

typedef struct SlpPollState_t {
    int      waitForPollAck;
    uint64_t pollAckWaitSeqNum;
    int      pollTimeoutCount;
} SlpPollState_t;

SlpPollState_t sSlpPrevPollState;


static void SlpPollAckReceived(uint64_t seqNum)
{
    if (seqNum == sSlpPrevPollState.pollAckWaitSeqNum) {
        sSlpPrevPollState.waitForPollAck = 0;
    }
}

static int SlpShouldPollBeSent(uint64_t* pSeqNum, int* pNr)
{
    int retVal = 0;
    int checkThis = 0;
    int nr;
    uint64_t seqNum;
    uint64_t prevSeqNum;

    if (sSlpPrevPollState.waitForPollAck) {
        if (SLP_POLL_ACK_TIMEOUT_MS > sSlpPrevPollState.pollTimeoutCount) {
            sSlpPrevPollState.pollTimeoutCount++;
            return 0;
        }
    }
    sSlpPrevPollState.waitForPollAck = 0;

    //read essential values
    pthread_mutex_lock(&gSlpTxLock);
    nr = sSlpTxState.nrOfDataBlocks;
    seqNum = sSlpTxState.seqNums[0];
    pthread_mutex_unlock(&gSlpTxLock);

    if (0 < nr) {
        prevSeqNum = seqNum;
        checkThis = 1;
    }

    //wait
    usleep(SLP_POLL_CHECK_TIME_US);

    if (checkThis) {
        //read essential values again
        pthread_mutex_lock(&gSlpTxLock);
        nr = sSlpTxState.nrOfDataBlocks;
        seqNum = sSlpTxState.seqNums[0];
        pthread_mutex_unlock(&gSlpTxLock);

        if (0 < nr) {
            //send poll if still same oldest seqNum
            //because probably communication is stuck
            if (prevSeqNum == seqNum) {
                retVal = 1;
            }
        }
    }
    if (!retVal) return 0;
    sSlpPrevPollState.pollTimeoutCount = 0;
    sSlpPrevPollState.waitForPollAck = 1;
    *pSeqNum = seqNum;
    *pNr = nr;
    if (gGenDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("SlpShouldPollBeSent: is returning 1, read seqNum %lu and nr %d\n",
            seqNum, nr);
        pthread_mutex_unlock(&gGenPrintLock);
    }
    return 1;
}

void* slp_tx_send_poll()
{
    uint64_t seqNum;
    int nr;

    for (;;) {
        usleep(SLP_POLL_PTHREAD_PERIOD_US);
        if (SlpShouldPollBeSent(&seqNum, &nr)) {
            struct msqid_ds qbuf;
            int msqid;
            int msgflg = IPC_CREAT | MSG_FLAG;
            key_t key;
            SlpShortMsg_t sbuf;

            key = SLP_POLL_MSG_QUEUE_KEY_ID;
            if ((msqid = msgget(key, msgflg)) < 0) {
                perror("msgget");
                exit(1);
            }

            //Compare originally read: seqNum and nr to real values and cancel sending if changed,
            //save data block and increment counters during mutex is locked
            if ((seqNum != sSlpTxState.seqNums[0]) || (nr != sSlpTxState.nrOfDataBlocks)) {
                if (gGenDebugPrint) {
                    pthread_mutex_lock(&gGenPrintLock);
                    printf("slp_send_possible_poll: sending cancelled due to changed seqNum or nr, seqNums %lu/%lu and nrs %d/%d\n",
                        seqNum, sSlpTxState.seqNums[0], nr, sSlpTxState.nrOfDataBlocks);
                    pthread_mutex_unlock(&gGenPrintLock);
                }
                pthread_mutex_unlock(&gSlpTxLock);
                continue;
            }

            //cancel sending if data block sending decided
            pthread_mutex_lock(&gSlpTxLock);
            if (sSlpTxState.dataBlockSendingDecided) {
                pthread_mutex_unlock(&gSlpTxLock);
                continue;
            }
            sSlpTxState.pollSendingDecided = 1;

            //save data block for ack
            assert(SLP_MAX_NR_OF_BLOCKS > sSlpTxState.nrOfDataBlocks);
            sSlpTxState.blockData[sSlpTxState.nrOfDataBlocks].pAppDataPtr = NULL;
            sSlpTxState.blockData[sSlpTxState.nrOfDataBlocks].appLen = 0;
            seqNum = sSlpTxState.seqNumCount;
            sSlpTxState.seqNums[sSlpTxState.nrOfDataBlocks] = seqNum;

            //increment and release mutex
            sSlpTxState.nrOfDataBlocks++;
            sSlpTxState.seqNumCount++;
            sSlpTxState.pollSendingDecided = 0;
            pthread_mutex_unlock(&gSlpTxLock);

            sSlpPrevPollState.pollAckWaitSeqNum = seqNum;

            if (!sSlpTxState.primaryAppWait && (SLP_APP_WAIT_LIMIT <= sSlpTxState.nrOfDataBlocks)) {
                sSlpTxState.primaryAppWait = 1;
                if (!sSlpTxState.secondaryAppWait) {
                    SlpSendState(SLP_ASKS_APP_TO_WAIT);
                }
            }

            //set data to buffer to be sent
            sbuf.mtype = SLP_POLL_MSG;
            sbuf.slpHeader.subHeader.fill = 0; //not used
            sbuf.slpHeader.subHeader.appDataLen = 0;
            sbuf.slpHeader.subHeader.seqNum = seqNum;
            sbuf.slpHeader.crc =  crcFast(((const uint8_t*) &sbuf.slpHeader.subHeader),
                sizeof(sbuf.slpHeader.subHeader));
            sbuf.slpHeader.fill = 0; //not used

            if (sSlpTxDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("slp_tx_send_poll: seqNum %lu, nr %d\n", seqNum, nr);
                pthread_mutex_unlock(&gGenPrintLock);
            }

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
            sSlpTxDebug.nrOfSentPolls++;
#endif

            //send
            if (msgsnd(msqid, &sbuf, sizeof(sbuf.slpHeader), 0) < 0) {
                perror("msgsnd");
                exit(1);
            }
        }
    }
}

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

#include "common.h"
#include "gen_if.h"
#include "util_if.h"
#include "slp_if.h"
#include "msg.h"

#define APP_MAX_NR_OF_NON_COMPLETED_DATA_BLOCKS (8*GEN_MEM_SIZE)
#define APP_DELAY_US                            10000

pthread_mutex_t gAppLock;

typedef struct AppNonCompletedData_t {
    void*       pAppDataPtr;
    uint32_t    appLen;
}  AppNonCompletedData_t;

typedef struct AppState_t {
    uint64_t                    appId[APP_MAX_NR_OF_NON_COMPLETED_DATA_BLOCKS];
    uint64_t                    slpId[APP_MAX_NR_OF_NON_COMPLETED_DATA_BLOCKS];
    AppNonCompletedData_t       nonCompletedData[APP_MAX_NR_OF_NON_COMPLETED_DATA_BLOCKS];
    int                         nrOfNonCompletedDataBlocks;
    uint64_t                    appIdCount;
    uint64_t                    waitAppId;
    int                         waitState;
    int                         nrOfRandBreaks;
    uint64_t                    randBreakTime;
} AppState_t;

static AppState_t sAppState;

static int sAppDebugPrint;

static uint64_t AppSave(SlpAppMsg_t* pSbuf)
{
    void* pAppData;
    uint64_t appId = sAppState.appIdCount;
    int pos = sAppState.nrOfNonCompletedDataBlocks;

    sAppState.appIdCount++;
    pAppData = malloc(pSbuf->data.len);
    assert(NULL != pAppData);
    memcpy(pAppData, pSbuf->data.appData, pSbuf->data.len);
    assert(APP_MAX_NR_OF_NON_COMPLETED_DATA_BLOCKS > sAppState.nrOfNonCompletedDataBlocks);
    sAppState.nonCompletedData[pos].pAppDataPtr = pAppData;
    sAppState.nonCompletedData[pos].appLen = pSbuf->data.len;
    sAppState.appId[pos] = appId;
    sAppState.slpId[pos] = GEN_ID_INVALID;
    sAppState.nrOfNonCompletedDataBlocks++;
    if (sAppDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("AppSave: appId %lu and len %u saved into pos %d\n",
            appId, pSbuf->data.len, pos);
        pthread_mutex_unlock(&gGenPrintLock);
    }
    return appId;
}

static void AppFree(int pos)
{
    int i;

    if (sAppDebugPrint) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("AppFree: freeing pos %d having appId %lu and slpId %lu\n",
            pos, sAppState.appId[pos], sAppState.slpId[pos]);
        pthread_mutex_unlock(&gGenPrintLock);
    }

    //Free dynamically allocated APP memory
    free(sAppState.nonCompletedData[pos].pAppDataPtr);

    //Deleting element
    for (i = pos; i < sAppState.nrOfNonCompletedDataBlocks; i++) {
        if (pos < (sAppState.nrOfNonCompletedDataBlocks - 1)) {
            sAppState.appId[i] = sAppState.appId[i + 1];
            sAppState.slpId[i] = sAppState.slpId[i + 1];
            sAppState.nonCompletedData[i] = sAppState.nonCompletedData[i + 1];
        } else {
            sAppState.appId[pos] =  0;
            sAppState.slpId[pos] =  0;
            sAppState.nonCompletedData[pos].pAppDataPtr = NULL;
            sAppState.nonCompletedData[pos].appLen = 0;
        }
    }

    //Decrement nr of sent data blocks
    sAppState.nrOfNonCompletedDataBlocks--;
}

void* app_tx_send_data()
{
    int msqid;
    int msgflg = IPC_CREAT | MSG_FLAG;
    key_t key;
    SlpAppMsg_t sbuf;
    uint8_t appCount = 0;
    int i;

    //send APP data continuously
    for (;;) {
        usleep(APP_DELAY_US);

        //get the message queue id for the key with value APP_DATA_MSG_QUEUE_KEY_ID
        key = SLP_APP_DATA_SEND_MSG_QUEUE_KEY_ID;

        if ((msqid = msgget(key, msgflg)) < 0) {
            perror("msgget");
            exit(1);
        }

        //send message type APP_DATA_MSG
        sbuf.mtype = SLP_APP_DATA_SEND_MSG;

        //set APP data
        sbuf.data.len = SLP_APP_DATA_SIZE - appCount;
        for(i = 0; i < sbuf.data.len; i++) {
            sbuf.data.appData[i] = i + appCount;
        }
        sbuf.data.appData[sbuf.data.len - 1] = appCount++;
        pthread_mutex_lock(&gAppLock);
        sbuf.data.genId = AppSave(&sbuf);
        pthread_mutex_unlock(&gAppLock);

        if (gGenDebugPrint) {
            pthread_mutex_lock(&gGenPrintLock);
            printf("app_tx_send_data: sending %u bytes with last byte %u\n", sbuf.data.len, sbuf.data.appData[sbuf.data.len - 1]);
            pthread_mutex_unlock(&gGenPrintLock);
        }

        if (msgsnd(msqid, &sbuf, sizeof(sbuf.data), 0) < 0) { //last parameter IPC_NOWAIT replaced with 0
            perror("msgsnd");
            exit(1);
        }

        while (sAppState.waitState) {
            usleep(APP_DELAY_US);
        }

#ifdef GEN_APP_TEST_KEEP_RANDOM_BREAKS
        {
            long r = rand();
            if ((r % 100) == (sbuf.data.genId % 100)) {
                uint8_t appBreak = r % 10;

                if (0 < appBreak) {
                    sAppState.nrOfRandBreaks++;
                    sAppState.randBreakTime += appBreak;
                    pthread_mutex_lock(&gGenPrintLock);
                    printf("app_tx_send_data: random break of %u s started\n", appBreak);
                    pthread_mutex_unlock(&gGenPrintLock);
                    sleep(appBreak);
                }
            }
        }
#endif
    }
}

void* app_tx_receive_info()
{
    int msqid;
    key_t key;
    SlpInfoMsg_t rbuf;
    int retVal;
    int pos;

    //get the message queue id
    key = SLP_APP_INFO_MSG_QUEUE_KEY_ID;

    //receive continuously
    for (;;) {
        usleep(GEN_SMALL_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_SMALL_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.data), SLP_APP_INFO_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        //find saved appId for saving corresponding slpId
        if (SLP_INFO_TYPE_APP_DATA_RECEIVED == rbuf.data.infoType) {
            pthread_mutex_lock(&gAppLock);
            pos = binarySearch(sAppState.appId, 0, sAppState.nrOfNonCompletedDataBlocks - 1, rbuf.data.appId);
            if (0 > pos)
            {
                pthread_mutex_lock(&gGenPrintLock);
                printf("app_tx_receive_info: appId %lu not found!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!, nr of non-completed data blocks %d, oldest appId %lu\n",
                    rbuf.data.appId, sAppState.nrOfNonCompletedDataBlocks, sAppState.appId[0]);
                pthread_mutex_unlock(&gGenPrintLock);
                pthread_mutex_unlock(&gAppLock);
                continue;
            }
            sAppState.slpId[pos] = rbuf.data.slpId;
            pthread_mutex_unlock(&gAppLock);
            if (sAppDebugPrint) {
                pthread_mutex_lock(&gGenPrintLock);
                printf("app_tx_receive_info: received appId %lu found from pos %d where saved received slpId %lu, len %u\n",
                    rbuf.data.appId, pos, rbuf.data.slpId, sAppState.nonCompletedData[pos].appLen);
                pthread_mutex_unlock(&gGenPrintLock);
            }
        }

        //print received msg
        if (gGenDebugPrint) {
            pthread_mutex_lock(&gGenPrintLock);
            if (SLP_INFO_TYPE_APP_DATA_RECEIVED == rbuf.data.infoType) {
                printf("app_tx_receive_info: infoType: APP_DATA_RECEIVED, slpId %lu, waiting %u, nr of non-completed data blocks %d\n",
                    rbuf.data.slpId, sAppState.waitState, sAppState.nrOfNonCompletedDataBlocks);
            } else if (SLP_INFO_TYPE_DONE == rbuf.data.infoType) {
                printf("app_tx_receive_info: infoType: DONE, slpId %lu, waiting %u, nr of non-completed data blocks %d\n",
                    rbuf.data.slpId, sAppState.waitState, sAppState.nrOfNonCompletedDataBlocks);
            } else if (SLP_INFO_TYPE_DONE_AND_RX_RESET == rbuf.data.infoType) {
                printf("app_tx_receive_info: infoType: DONE_AND_RX_RESET, slpId %lu, waiting %u, nr of non-completed data blocks %d\n",
                    rbuf.data.slpId, sAppState.waitState, sAppState.nrOfNonCompletedDataBlocks);
            } else if (SLP_INFO_TYPE_RX_RESET == rbuf.data.infoType) {
                printf("app_tx_receive_info: infoType: RX_RESET, slpId %lu, waiting %u, nr of non-completed data blocks %d\n",
                    rbuf.data.slpId, sAppState.waitState, sAppState.nrOfNonCompletedDataBlocks);
            } else {
                printf("app_tx_receive_info: unknown infoType %u, slpId %lu, waiting %u, nr of non-completed data blocks %d\n",
                    rbuf.data.infoType, rbuf.data.slpId, sAppState.waitState, sAppState.nrOfNonCompletedDataBlocks);
            }
            pthread_mutex_unlock(&gGenPrintLock);
        }
    }
}

void* app_tx_receive_state()
{
    int msqid;
    key_t key;
    SlpStateMsg_t rbuf;
    int retVal;

    //get the message queue id for the key with value APP_STATE_FROM_SLP_MSG_QUEUE_KEY_ID
    key = SLP_APP_STATE_MSG_QUEUE_KEY_ID;

    //receive continuously message type APP_STATE_FROM_SLP_MSG
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.data), SLP_APP_STATE_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        //set APP state
        sAppState.waitState = rbuf.data.state;

        if (gGenDebugPrint) {
            pthread_mutex_lock(&gGenPrintLock);
            printf("app_tx_receive_state: received state %u, nr of non-completed data blocks %d\n",
                sAppState.waitState, sAppState.nrOfNonCompletedDataBlocks);
            pthread_mutex_unlock(&gGenPrintLock);
        }
    }
}

void* app_rx_receive_data()
{
    int msqid;
    key_t key;
    SlpAppMsg_t rbuf;
    int retVal;
    int pos;
    uint64_t appId;

    //get the message queue id for the key with value APP_RECEIVE_DATA_FROM_SLP_MSG_QUEUE_KEY_ID
    key = SLP_APP_DATA_RECEIVE_MSG_QUEUE_KEY_ID;

    //receive continuously message type APP_RECEIVE_DATA_FROM_SLP_MSG
    for (;;) {
        usleep(GEN_THREAD_DELAY_US);
        while ((msqid = msgget(key, MSG_FLAG)) < 0) {
            usleep(GEN_THREAD_DELAY_US);
        }
        retVal = msgrcv(msqid, &rbuf, sizeof(rbuf.data), SLP_APP_DATA_RECEIVE_MSG, 0);
        if (0 > retVal) {
            perror("msgrcv");
            exit(1);
        }

        //find saved slpId for comparing received data block to slpId corresponding sent data block
        pthread_mutex_lock(&gAppLock);
        pos = binarySearch(sAppState.slpId, 0, sAppState.nrOfNonCompletedDataBlocks - 1, rbuf.data.genId);
        if (0 > pos) {
            pthread_mutex_lock(&gGenPrintLock);
            printf("app_rx_receive_data: slpId %lu not found!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n",
                rbuf.data.genId);
            pthread_mutex_unlock(&gGenPrintLock);
            pthread_mutex_unlock(&gAppLock);
            continue;
        }

        //failed assert if differences found
        assert(sAppState.nonCompletedData[pos].appLen == rbuf.data.len);
        assert(0 == memcmp(sAppState.nonCompletedData[pos].pAppDataPtr, rbuf.data.appData, rbuf.data.len));

        //ensure proper order
        appId = sAppState.appId[pos];
        assert(appId == sAppState.waitAppId);
        sAppState.waitAppId++;

        //saved APP data is not needed anymore
        AppFree(pos);

#ifdef GEN_APP_DEBUG_STATISTICS
        pthread_mutex_lock(&gGenPrintLock);
        printf(
            "APP result statistics:\n"
            " nr of data blocks sent and successfully received %lu\n"
            " nr of APP random breaks %d\n"
            " random break total time %lu s\n"
            "==========================================================\n",
            appId + 1,
            sAppState.nrOfRandBreaks, sAppState.randBreakTime);
        pthread_mutex_unlock(&gGenPrintLock);
#endif
        pthread_mutex_unlock(&gAppLock);
    }
}

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
#include "msg.h"
#include "gen.h"

int gGenDebugPrint;
pthread_mutex_t gGenPrintLock;

static int GenIsThisMsgQueueEmpty(key_t key)
{
    struct msqid_ds qbuf;
    int msqid;

    //wait until input queue is empty
    if ((msqid = msgget(key, MSG_FLAG)) < 0) {
        return 1;
    }
    if (msgctl(msqid, IPC_STAT, &qbuf) < 0) {
        pthread_mutex_lock(&gGenPrintLock);
        printf("SlpIsThisMsgQueueEmpty: msgctl() failed\n");
        pthread_mutex_unlock(&gGenPrintLock);
        return 0;
    }
    if (0 == qbuf.msg_qnum) return 1;
    return 0;
}

int GenCertainSyncRelatedMsgQueuesEmpty(void)
{
    int count;

    for (count = 1; count <= 3; count++) {
        if (!GenIsThisMsgQueueEmpty(SLP_APP_DATA_MSG_QUEUE_KEY_ID)) {
            return 0;
        }
        if (!GenIsThisMsgQueueEmpty(SLP_POLL_MSG_QUEUE_KEY_ID)) {
            return 0;
        } 
        usleep(1);
    }
    return 1;
}

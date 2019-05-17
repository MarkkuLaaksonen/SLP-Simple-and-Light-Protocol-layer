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
#include "msg.h"
#include "gen_if.h"
#include "util_if.h"

//Main function which starts all necessary threads
int main(void)
{
    pthread_t thread_app1;
    pthread_t thread_app2;
    pthread_t thread_app3;
    pthread_t thread_app4;
    pthread_t thread_slp1;
    pthread_t thread_slp2;
    pthread_t thread_slp3;
    pthread_t thread_slp4;
    pthread_t thread_slp5;
    pthread_t thread_slp6;
    pthread_t thread_slp7;
    pthread_t thread_slp8;
    pthread_t thread_slp9;
#ifdef GEN_SLP_TX_DEBUG_STATISTICS
    pthread_t thread_slp_d1;
#endif
    int retVal;

    crcInit();

    if (pthread_mutex_init(&gGenPrintLock, NULL) != 0)
    {
        printf("\n print mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&gAppLock, NULL) != 0)
    {
        printf("\n App mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&gSlpTxLock, NULL) != 0)
    {
        printf("\n slp tx mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&gSlpRxLock, NULL) != 0)
    {
        printf("\n slp rx mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    //create thread_app1
    retVal = pthread_create(&thread_app1, NULL, app_tx_send_data, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread1, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    //create thread_app2
    retVal = pthread_create(&thread_app2, NULL, app_tx_receive_info, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_app2, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    //create thread_app3
    retVal = pthread_create(&thread_app3, NULL, app_tx_receive_state, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_app3, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    //create thread_app4
    retVal = pthread_create(&thread_app4, NULL, app_rx_receive_data, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_app4, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp1
    retVal = pthread_create(&thread_slp1, NULL, slp_tx_receive_app_data, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp1, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp2
    retVal = pthread_create(&thread_slp2, NULL, slp_tx_receive_ack, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp2, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp3
    retVal = pthread_create(&thread_slp3, NULL, slp_tx_receive_nack, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp3, ..) returned value: %d\n",retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp4
    retVal = pthread_create(&thread_slp4, NULL, slp_tx_send_poll, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp4, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp5
    retVal = pthread_create(&thread_slp5, NULL, slp_rx_receive_app_data, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp5, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp6
    retVal = pthread_create(&thread_slp6, NULL, slp_rx_send_ack, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp6, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp7
    retVal = pthread_create(&thread_slp7, NULL, slp_rx_send_nack, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp7, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }


    // create thread_slp8
    retVal = pthread_create(&thread_slp8, NULL, slp_rx_receive_retrans, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp8, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

    // create thread_slp9
    retVal = pthread_create(&thread_slp9, NULL, slp_rx_receive_poll, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp9, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }

#ifdef GEN_SLP_TX_DEBUG_STATISTICS
    // create thread_slp_d1
    retVal = pthread_create(&thread_slp_d1, NULL, slp_tx_debug_get_time, NULL);
    if(retVal)
    {
        fprintf(stderr,"Error - pthread_create(&thread_slp_d1, ..) returned value: %d\n", retVal);
        exit(EXIT_FAILURE);
    }
#endif

    //wait untill threads are done with their routines before continuing with main thread
    pthread_join(thread_app1, NULL);
    pthread_join(thread_app2, NULL);
    pthread_join(thread_app3, NULL);
    pthread_join(thread_app4, NULL); 
    pthread_join(thread_slp1, NULL);
    pthread_join(thread_slp2, NULL);
    pthread_join(thread_slp3, NULL);
    pthread_join(thread_slp4, NULL);
    pthread_join(thread_slp5, NULL);
    pthread_join(thread_slp6, NULL);
    pthread_join(thread_slp7, NULL);
    pthread_join(thread_slp8, NULL);
    pthread_join(thread_slp9, NULL);
    exit(EXIT_SUCCESS);
}

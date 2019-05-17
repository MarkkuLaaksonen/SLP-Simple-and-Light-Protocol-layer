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

//Message types from APP to SLP
#define SLP_APP_DATA_SEND_MSG           1 //actual APP data message to SLP to be sent to another device

//Message types from SLP to APP
#define SLP_APP_INFO_MSG                2 //4 kind of types
#define SLP_APP_STATE_MSG               3 //SLP stops/restarts APP sending with this
#define SLP_APP_DATA_RECEIVE_MSG        4 //APP of receiving device receives APP data with this

//Message types from SLP sender to SLP of another device
#define SLP_INNER_APP_DATA_MSG          5 //SLP inner message for sending/receiving APP data
#define SLP_RETRANS_MSG                 6 //same as above but not same thread because retransmitting case
#define SLP_POLL_MSG                    7 //for recovery e.g. when last ACK was lost

//Message types from SLP of another device to SLP sender
#define SLP_ACK_MSG                     8 //SLP receiver acknowledges with this SLP_INNER_APP_DATA_MSG
#define SLP_NACK_MSG                    9 //SLP receiver sends this when it observes that previous message(s) is/are missing,
                                          //causes retramsmission when received by sender of original SLP_INNER_APP_DATA_MSG or SLP_POLL_MSG

//Keys of message queue ids
#define SLP_APP_DATA_SEND_MSG_QUEUE_KEY_ID           1001

#define SLP_APP_INFO_MSG_QUEUE_KEY_ID                1002
#define SLP_APP_STATE_MSG_QUEUE_KEY_ID               1003

#define SLP_APP_DATA_RECEIVE_MSG_QUEUE_KEY_ID        1004

#define SLP_INNER_APP_DATA_MSG_QUEUE_KEY_ID          1005
#define SLP_RETRANS_MSG_QUEUE_KEY_ID                 1006
#define SLP_POLL_MSG_QUEUE_KEY_ID                    1007

#define SLP_ACK_MSG_QUEUE_KEY_ID                     1008
#define SLP_NACK_MSG_QUEUE_KEY_ID                    1009

//Message flag for msgget
#define MSG_FLAG                                     0666

//Function prototypes of APP pthreads
void* app_tx_send_data();
void* app_tx_receive_info();
void* app_tx_receive_state();
void* app_rx_receive_data();

//Function prototypes of SLP sending device pthreads
void* slp_tx_receive_app_data();
void* slp_tx_receive_ack();
void* slp_tx_receive_nack();
void* slp_tx_send_poll();
void* slp_tx_debug_get_time();

//Function prototypes of SLP receiving device pthreads
void* slp_rx_receive_app_data();
void* slp_rx_send_ack();
void* slp_rx_send_nack();
void* slp_rx_receive_retrans();
void* slp_rx_receive_poll();

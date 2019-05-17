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


#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>

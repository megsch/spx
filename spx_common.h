#define _POSIX_C_SOURCE 199309L
#define _POSIX_SOURCE
#ifndef SPX_COMMON_H
#define SPX_COMMON_H


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#define FIFO_EXCHANGE "/tmp/spx_exchange_%d"
#define FIFO_TRADER "/tmp/spx_trader_%d"
#define FEE_PERCENTAGE 1

#define FIFO_PERMISSION 0777
#define FIFO_NAME_MAX 30

#define NOT_PIPE_MAX 9

#define INTEGER_MAX 999999

#define MAX_ORDER_SIZE 43

#endif

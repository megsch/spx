#ifndef SPX_TRADER_H
#define SPX_TRADER_H

#include "spx_common.h"

typedef struct pipes pipes;

struct pipes {
    char* ex_pipe;
    char* tr_pipe;
    int tr_fd;
    int ep_fd;
    struct epoll_event* ev;
    struct epoll_event* events;
};

void sigusr1_handler(int, siginfo_t*, void*);
int setup_sighandler(struct sigaction*);
int setup_pipes(char*, pipes*);
void exchange_cycle(pipes*);
char* read_message();
int send_message(char*, int*, pipes*);
int is_digit(char*);
int is_accepted(char*);

void cleanup(pipes*);


#endif

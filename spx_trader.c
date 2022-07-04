#include "spx_trader.h"

volatile int ex_fd;

void sigusr1_handler(int signum, siginfo_t* info, void* context) {
    /* 
     * Message from the exchange.
     */

}

int setup_sighandler(struct sigaction* sig) {
    /* 
     * Set up the sigaction.
     * 
     * sig: sigaction structure to setup
     * 
     * returns: -1 if setting up fails. Otherwise 0.
     */

    memset(sig, 0, sizeof(struct sigaction));
    sig->sa_sigaction = sigusr1_handler;
    sig->sa_flags = SA_SIGINFO;
    if (sigaction(SIGUSR1, sig, NULL) == -1) {
        perror("Trader b: sigaction usr1 failed");
        return -1;
    }
    return 0;
}

int setup_pipes(char* argv, pipes* pipes_record) {
    /* 
     * Setup trader and exchange pipes and epoll.
     * 
     * argv: Trader's id string
     * pipes_record: Contains all pipe names and file descriptors and 
     *              epoll_events.
     * 
     * return: -1 if setting up the pipes fail. Otherwise, 0.
     */

    int trader_id = (int) strtol(argv, NULL, 10);

    /* Open exchange pipe */
    sprintf(pipes_record->ex_pipe, FIFO_EXCHANGE, trader_id);
    printf("Autotrader: exchange name %s\n", pipes_record->ex_pipe);
    ex_fd = open(pipes_record->ex_pipe, O_RDONLY);
    if (ex_fd == -1) {
        perror("Trader: open ex_pipe failed");
        free(pipes_record->ex_pipe);
        return -1;
    }

    /* Setup epoll for the exchange pipe */
    pipes_record->ep_fd = epoll_create1(0);
    pipes_record->ev->events = EPOLLIN;
    pipes_record->ev->data.fd = ex_fd;
    epoll_ctl(pipes_record->ep_fd, EPOLL_CTL_ADD, ex_fd, pipes_record->ev);
    
    // Open trader pipe
    sprintf(pipes_record->tr_pipe, FIFO_TRADER, trader_id);
    printf("Autotrader: pipe name %s\n", pipes_record->tr_pipe);
    pipes_record->tr_fd = open(pipes_record->tr_pipe, O_WRONLY);
    if (pipes_record->tr_fd == -1) {
        perror("Trader: open tr_pipe failed");
        close(ex_fd);
        free(pipes_record->ex_pipe);
        free(pipes_record->tr_pipe);
        return -1;
    }

    return 0;
}

void exchange_cycle(pipes* pipes_record) {
    /* 
     * While there is no order >= 1000 qty, wait for exchange to send a SELL 
     * message and send back a matching BUY order.
     * 
     * pipes_record: Contains all pipe names and file descriptors and 
     *              epoll_events.
     */

    int current_order_id = 0;
    while (1) {
        /* Wait for SELL message */
        int fd_ready_num;
        do {
			fd_ready_num = epoll_wait(pipes_record->ep_fd, pipes_record->events, 
            1, -1);
		} while (fd_ready_num < 0 && errno==EINTR);

        /* Go through messages from the exchange */
        if (fd_ready_num > 0) {
            char* message = read_message();
            if (message == NULL) {
                free(message);
                continue;
            }
            /* Search for SELL orders */
            int message_type = send_message(message, &current_order_id, 
            pipes_record);
            if (message_type == 0) {
                /* Wait for ACCEPTED message */
                while (1) {
                    /* Keep signalling to the exchange until it accepts */
                    if (epoll_wait(pipes_record->ep_fd, pipes_record->events, 1, 
                    300) == 1) {
                        /* Check if the exchange sent the ACCEPTED message */
                        char* return_message = read_message();
                        if ((return_message == NULL) || 
                        (!is_accepted(return_message))) {
                            /* Resend message */
                            free(return_message);
                            kill(getppid(), SIGUSR1);
                        } else {
                            free(return_message);
                            break;
                        }
                    } else {
                        /* Resend message */
                        kill(getppid(), SIGUSR1);
                    }
                    
                }
                free(message);
            } if (message_type == 1) {
                free(message);
                return;
            }
        }
    }
}

char* read_message() {
    /* 
     * Reads message sent by the exchange. 
     * 
     * returns: The message string. Null if reading the message fails.
     */

    char* message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
    for (int i=0; i<MAX_ORDER_SIZE; i++) {
        if (read(ex_fd, message+i, 1) > 0) {
            if (*(message+i) == ';') {
                *(message+i) = '\0';
                return message;
            }
        }
    }
    return NULL;
}

int send_message(char* message, int* current_order_id, pipes* pipes_record) {
    /* 
     * Sends a message if it is a SELL order.
     * 
     * message: message sent by exchange.
     * current_order_id: The id of the BUY order.
     * pipes_record: contains pipes and file descriptions
     * 
     * return: 0 if it sent a BUY order. 1 if the BUY order was >= 1000 qty. 
     *         -1 if the order was something else.
     */

    char* return_message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
    int count = 0;
    sprintf(return_message, "BUY %d ", *current_order_id);
    count = (strlen(return_message));

    char* token;
    /* First word should be MARKET */
    token = strtok(message, " ");
    if (strcmp(token, "MARKET") != 0) {
        free(return_message);
        return -1;
    }

    /* Second word should be SELL */
    token = strtok(NULL, " ");
    if (strcmp(token, "SELL") != 0) {
        free(return_message);
        return -1;
    }

    /* Third word is the product */
    token = strtok(NULL, " ");
    sprintf(return_message+count, "%s ", token);
    count = strlen(return_message);


    /* Fourth word is the qty */
    token = strtok(NULL, " ");
    if (is_digit(token)) {
        int qty = atoi(token);
        if (qty >= 1000) {
            /* End the autotrader */
            free(return_message);
            return 1;
        }
    }
    sprintf(return_message+count, "%s ", token);
    count = strlen(return_message);


    /* Fifth word is the price */
    token = strtok(NULL, " ");
    sprintf(return_message+count, "%s;", token);

    /* Send message */
    write(pipes_record->tr_fd, return_message, strlen(return_message));
    free(return_message);

    *current_order_id += 1;

    return 0;
}

int is_digit(char* message) {
    /* 
     * verifies if the message is a digit
     * 
     * message: the digit message
     * 
     * returns: 1 if message is a digit. Otherwise 0.
     */

    for (int i = 0; i < strlen(message); i++) {
		if (!isdigit(message[i])) {
			return 0;
		}
	}
	return 1;
}

int is_accepted(char* message) {
    /* 
     * Checks if the message has "ACCEPTED" as the first word.
     * 
     * message: the message to check.
     * 
     * returns: 1 if the message is ACCEPTED. Otherwise 0.
     */

    char* token = strtok(message, " ");
    if (strcmp(token, "ACCEPTED") == 0) {
        return 1;
    } else {
        return 0;
    }
}

void cleanup(pipes* pipes_record) {
    /* 
     * Free allocated memory and close pipes.
     * 
     * pipes_record: Contains all pipe names and file descriptors and 
     *              epoll_events.
     */

    close(pipes_record->tr_fd);
    close(ex_fd);
    close(pipes_record->ep_fd);
    free(pipes_record->ex_pipe);
    free(pipes_record->tr_pipe);
    free(pipes_record->ev);
    free(pipes_record->events);
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    /* register signal handler */
    struct sigaction sig;
    if (setup_sighandler(&sig) == -1) {
        exit(-1);
    }

    pipes* pipes_record = (pipes*) calloc(1, sizeof(pipes));

    /* Open exchange pipes */
    pipes_record->ex_pipe = (char*) calloc(FIFO_NAME_MAX, sizeof(char));
    pipes_record->tr_pipe = (char*) calloc(FIFO_NAME_MAX, sizeof(char));
    /* Setup epoll */
    pipes_record->ev = (struct epoll_event*) calloc(1, 
    sizeof(struct epoll_event));
    pipes_record->events = (struct epoll_event*) calloc(1, 
    sizeof(struct epoll_event));
    if (setup_pipes(argv[1], pipes_record) == -1) {
        exit(-1);
    }

    /* Wait for exchange and read pipe */
    exchange_cycle(pipes_record);

    cleanup(pipes_record);
    return 0;
    
    // event loop:

    // wait for exchange update (MARKET message)
    // send order
    // wait for exchange confirmation (ACCEPTED message)
    
}

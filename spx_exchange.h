#ifndef SPX_EXCHANGE_H
#define SPX_EXCHANGE_H

#include "spx_common.h"

#define LOG_PREFIX "[SPX]"
#define MAX_PRODUCT_NAME 18

typedef struct product product;
typedef struct trader trader;
typedef struct order order;
typedef struct product_history product_history;


enum order_type {
    BUY=0,
    SELL,
    AMEND,
    CANCEL,
};

struct product {
    char* name;
    order* ordhead; // Linked list of orders for this product
    // Order of: Highest to lowest price. Earliest to latest time.
    int buy_level; // Buy levels
    int sell_level;

};

struct trader {
    int tid; // trader id
    pid_t pid; 

    product_history** history; // Array of products bought and sold
    int hsize; // History size

    int next_oid; // Next order id.

    order* ordhead; // Linked list of current orders in the logbook. Latest first.

    int ex_fd; // exchange file description
    char* ex_pipe; // exchange pipe name
    int tr_fd; // trader file description
    char* tr_pipe; // trader pipe name

    struct epoll_event* ev; // epoll_event for tr_fd
};

struct order {
    enum order_type type;
    product* product_type;
    trader* tra;
    int oid; // order id
    int amount;
    int price;

    order* tranext; // Next for trader list
    order* traprev; // prev for trader list

    order* pronext; // Next for product list
    order* proprev; // Prev for product list
};

struct product_history {
    product* prod;
    int amount;
    long total;
};

int is_integer(char*);
int is_alphanum(char*);
void sigchld_handler(int, siginfo_t*, void*);
void sigusr1_handler(int, siginfo_t*, void*);
trader* create_trader(int, pid_t, product_history**, int, int, char*, int, char*, 
struct epoll_event*);
product_history** create_product_history(int, product**);

trader* lookup_pid(trader**, trader*, int);
int trader_pid_comp(const void*, const void*);
trader* lookup_trfd(trader**, trader*, int);
int trader_trfd_comp(const void*, const void*);

char* read_pipe(int);
order* convert_order(char*, trader*, product**, int);
int order_int(char*);
void send_invalid_notif(trader*);
void send_accepted_notif(trader*, int);
void send_amended_notif(trader*, int);
void send_cancelled_notif(trader*, int);
void notify_others(trader**, trader*, order*, order*, int);
char* order_type_string(order*);

order* find_order_oid(order*);
product_history* find_product_history(order*);
long round_num(double);

void match_order(order*, long*);
void add_to_orderbook(order*);
void remove_from_orderbook(order*);
void remove_from_trader_list(order*);
order* amend_order(order*, order*);
void delete_order(order*);

void print_logbook(product**, trader**, int, int); //TODO

int clean_up(trader**, product**, int, int, int, struct epoll_event*, trader**);
int close_coms(trader**, int);
int close_epoll(int, trader**, int, struct epoll_event*);
void free_traders(trader**, int, int);
void free_products(product**, int);
void kill_all_traders(trader**, int, product**, int, int, struct epoll_event*, 
product_history**);

#endif

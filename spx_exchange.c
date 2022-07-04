/**
 * comp2017 - assignment 3
 * Megan Gock Kwai
 * mgoc2312
 */

#include "spx_exchange.h"

volatile int not_fd;

void sigchld_handler(int signum, siginfo_t* info, void* context) {
	/* 
	 * Handles child process ending. Waitpid on child process.
	 *
	 * signum: Signal type
	 */
	waitpid(info->si_pid, NULL, 0 || WNOHANG);
	
}

void sigusr1_handler(int signum, siginfo_t* info, void* context) {
	/* 
	 * Handles SIGUSR1 signals from traders. Writes "<pid>" into not_pipe.
	 */

	/* Write pid as an integer into not_pipe */
	if (write(not_fd, (void*) &(info->si_pid), sizeof(int)) == -1) {
		perror("SIGUSR1 handler write() failed");
	}
}

int is_integer(char* str) {
	/* 
	 * Checks if the given string is an integer.
	 * 
	 * str: the given string to check
	 * 
	 * return: 0 if str isn't an integer. Otherwise, 1.
	 */

	for (int i = 0; i < strlen(str); i++) {
		if (str[i] == '\n' && i == 0) {
			return 0;
		} if (!isdigit(str[i])) {
			return 0;
		}
	}
	return 1;
}

int is_alphanum(char* str) {
	/* 
	 * Checks if the given string only has alphanumeric chars
	 * 
	 * str: the given string
	 * 
	 * return: 0 if the str isn't alphanumeric. Otherwise, 1.
	 */

	for (int i=0; i < strlen(str); i++) {
		if (str[i] == '\n' && i == 0) {
			printf("is_alphanum new line\n");
			return 0;
		} else if (str[i] == '\n') {
			str[i] = '\0';
			return 1;
		} else if (!isalnum(str[i])) {
			printf("%c is not alunum\n", str[i]);
			return 0;
		}
	}
	return 1;
}

product_history** create_product_history(int pnum, product** plist) {
	/* 
	 * Creates a default list of product_history.
	 * 
	 * pnum: number of existing products.
	 * plist: List of existing product.
	 * 
	 * return: Pointer to the list of product_history.
	 */

	product_history** tmp = (product_history**) 
	calloc(pnum, sizeof(product_history*));
	for (int i=0; i < pnum; i++) {
		// Create a product_history for each product in plist
		product_history* tph = (product_history*) 
		calloc(1, sizeof(product_history));

		tph->prod = plist[i];
		tph->amount = 0;
		tph->total = 0;

		tmp[i] = tph;

	}
	return tmp;
}

trader* create_trader(int tid, pid_t pid, product_history** ph, int hsize, 
int ex_fd, char* ex_pipe, int tr_fd, char* tr_pipe, struct epoll_event* ev) {
	/* 
	 * Create a trader and initialise attributes.
	 * 
	 * tid: trader id.
	 * pid: trader's pid
	 * ph: Pointer to a default product history array for the trader.
	 * hsize: Size of the product history list. No. of products.
	 * ex_fd: exchange file descriptor.
	 * ex_pipe: name of the exchange pipe
	 * tr_fd: trader file descriptor.
	 * tr_pipe: name of the trader pipe
	 * ev: epoll_event for the tr_fd
	 * 
	 * return: pointer to the created trader.
	 */

	trader* tra = (trader*) calloc(1, sizeof(trader));

	tra->tid = tid;
	tra->pid = pid;
	tra->history = ph;
	tra->hsize = hsize;
	tra->ex_fd = ex_fd;
	tra->ex_pipe = ex_pipe;
	tra->tr_fd = tr_fd;
	tra->tr_pipe = tr_pipe;
	tra->ev = ev;

	return tra;
}

void free_products(product** products, int product_num) {
	/* 
	 * Frees malloc'ed products from products list, string name, orders, and 
	 * list.
	 * 
	 * products: pointer to the products list
	 * product_num: number of products in products list
	 */

	for (int i=product_num-1; i>= 0; i--) {
		free(products[i]->name);
		/* Free orders */
		for (order* ord=products[i]->ordhead; ord!=NULL;) {
			order* tmp = ord->pronext;
			free(ord);
			ord = tmp;
		}
		free(products[i]);
	}
	free(products);

}

char* read_pipe(int fd) {
	/* 
	 * Reads a string from the file descriptor until a ';'. If the string is 
	 * invalid, it reads until it finds a ; or until EOF
	 * 
	 * fd: file descriptor to read from.
	 * 
	 * returns: The address to the string read from fd. Or NULL if the string 
	 * doesn't end with ';' or has more than MAX_ORDER_SIZE (43) characters.
	 */

	char* str_order = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));

	int counter = 0;
	/* Read in one char at a time */
	while (counter <= MAX_ORDER_SIZE) {
		int ret;
		do {
			ret = read(fd, str_order + counter, sizeof(char));
		} while ((ret == -1) && (errno == EINTR));
		if (ret == -1) {
			perror("read_pipe read() failed");
			free(str_order);
			return NULL;
		} else if (ret == 0) {
			/* End of file with no ';'. Invalid order */
			free(str_order);
			return NULL;
		} else {
			/* Check if last char is ';' */
			if (*(str_order+counter) == ';') {
				/* End of order */
				return str_order;
			}
		}
		counter++;
	}

	/* If we reach MAX_ORDER_SIZE without finding a ';' continue reading until 
	we find one or EOF */
	while (1) {
		int ret = read(fd, str_order, sizeof(char));
		if (ret == -1) {
			perror("read_pipe read() failed");
			break;
		} else if (ret == 0) {
			break;
		} else {
			if (*str_order == ';') {
				break;
			}
		}
	}

	free(str_order);
	return NULL;

}

order* convert_order(char* str, trader* tra, product** products, int pronum) {
	/* 
	 * Convert string into struct order*
	 * 
	 * str: order string
	 * tra: address of trader
	 * products: array of products
	 * 
	 * returns: order address of the converted string. Null if the string has 
	 * an invalid order type, format, data type, or product.
	 */

	order* ord = (order*) calloc(1, sizeof(order));
	char* token;

	/* First word is an order type */
	token = strtok(str, " ");
	if (strcmp(token, "BUY") == 0) {
		ord->type = BUY;
	} else if(strcmp(token, "SELL") == 0) {
		ord->type = SELL;
	} else if (strcmp(token, "AMEND") == 0) {
		ord->type = AMEND;
	} else if (strcmp(token, "CANCEL") == 0) {
		ord->type = CANCEL;
	} else {
		/* Invalid order type */
		free(ord);
		return NULL;
	}

	/* Second word: order id */
	token = strtok(NULL, " ");
	if ((ord->type == CANCEL) && (token[strlen(token)-1] != ';')) {
		/* Invalid order */
		free(ord);
		return NULL;
	} else if (ord->type == CANCEL) {
		token[strlen(token)-1] = '\0';
	}
	ord->oid = order_int(token);
	if ((ord->oid < 0) || ((ord->type == BUY || ord->type == SELL) && 
	(ord->oid != tra->next_oid)) || ((ord->type == AMEND || ord->type == CANCEL)
	&& (ord->oid >= tra->next_oid))) {
		/* Invalid order id */
		free(ord);
		return NULL;
	} 
	
	/* BUY or SELL order */
	if (ord->type == BUY || ord->type == SELL) {
		/* Word: product */
		token = strtok(NULL, " ");
		/* Check if the product exists */
		int valid_pro = 0;
		for (int i=0; i<pronum; i++) {
			if (strcmp(products[i]->name, token) == 0) {
				/* Product exists */
				valid_pro = 1;
				ord->product_type = products[i];
				break;
			}
		}
		if (!valid_pro) {
			/* Product doesn't exist */
			free(ord);
			return NULL;
		}

	}

	if (ord->type != CANCEL) {
		/* Word qty */
		token = strtok(NULL, " ");
		ord->amount = order_int(token);
		if (ord->amount < 1) {
			/* Invalid qty */
			free(ord);
			return NULL;
		}

		/* Word: price */
		token = strtok(NULL, " ");
		if (token[strlen(token)-1] != ';') {
			/* Invalid order */
			free(ord);
			return NULL;
		} else {
			token[strlen(token)-1] = '\0';
		}
		ord->price = order_int(token);
		if (ord->price < 1) {
			/* Invalid price */
			free(ord);
			return NULL;
		}

	}

	ord->tra = tra;

	if (ord->type == BUY || ord->type == SELL) {
		tra->next_oid += 1;
	}

	
	return ord;
}

int order_int(char* token) {
	/* 
	 * Turns an string word from an order into an integer. 
	 * 
	 * token: word to turn into an int.
	 * 
	 * return: the integer if valid. -1 if the word is not an integer, or 
	 * > INTEGER MAX. 
	 */

	if ((token != NULL) && (is_integer(token))) {
		int tmp = atoi(token);
		if (tmp > INTEGER_MAX) {
			/* Invalid quantity */
			return -1;
		} else {
			return tmp;
		}
	} else {
		/* Invalid qty */
		return -1;
	}
}

void send_invalid_notif(trader* tra) {
	/* 
	 * Writes "INVALID;" message to trader's ex_fd. Then sends SIGUSR1 to
	 * the trader.
	 * 
	 * tra: The trader to send the message to.
	 */

	/* Write message to pipe */
	char* message = "INVALID;";
	if (write(tra->ex_fd, message, strlen(message)) < 0) {
		// perror("Exchange: send invalid notif write() failed");
	}

	/* Send SIGUSR1 to trader */
	if (kill(tra->pid, 0) != -1) {
		if (kill(tra->pid, SIGUSR1) == -1) {
			// perror("Exchange: send invalid notif kill() failed");
		}
	}
}

void send_accepted_notif(trader* tra, int oid) {
	/* 
	 * Writes "ACCEPTED <OID>;" message to trader's ex_fd. Then sends SIGUSR1 to
	 * the trader.
	 * 
	 * tra: The trader to send the message to.
	 * oid: order id
	 */

	/* Write message to pipe */
	char* message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
	sprintf(message, "ACCEPTED %d;", oid);
	if (write(tra->ex_fd, message, strlen(message)) < 0) {
		// perror("Exchange: send accepted notif write() failed");
	}
	free(message);

	/* Send SIGUSR1 to trader */
	if (kill(tra->pid, 0) != -1) {
		if (kill(tra->pid, SIGUSR1) == -1) {
			// perror("Exchange: send accepted notif kill() failed");
		}
	}
}

void send_amended_notif(trader* tra, int oid) {
	/* 
	 * Writes "AMENDED <oid>;" to trader's ex_fd. Sends SIGUSR1.
	 * 
	 * tra: trader
	 * oid: order id
	 */

	/* Write message to pipe */
	char* message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
	sprintf(message, "AMENDED %d;", oid);
	if (write(tra->ex_fd, message, strlen(message)) < 0) {
		// perror("Exchange: send amended notif write() failed");
	}
	free(message);

	/* Send SIGUSR1 to trader */
	if (kill(tra->pid, 0) != -1) {
		if (kill(tra->pid, SIGUSR1) == -1) {
			// perror("Exchange: send amended notif kill() failed");
		}
	}
}

void send_cancelled_notif(trader* tra, int oid) {
	/* 
	 * Writes "CANCLLED <oid>;" to trader's ex_fd. Sends SIGUSR1.
	 * 
	 * tra: trader
	 * oid: order id
	 */

	/* Write message to pipe */
	char* message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
	sprintf(message, "CANCELLED %d;", oid);
	if (write(tra->ex_fd, message, strlen(message)) < 0) {
		// perror("Exchange: send cancelled notif write() failed");
	}
	free(message);

	/* Send SIGUSR1 to trader */
	if (kill(tra->pid, 0) != -1) {
		if (kill(tra->pid, SIGUSR1) == -1) {
			// perror("Exchange: send cancelled notif kill() failed");
		}
	}
}

void notify_others(trader** traders, trader* exclude_tra, order* ord, 
order* original, int tranum) {
	/* 
	 * Notifies all other traders of the order made.
	 * 
	 * traders: Array of traders.
	 * exclude_tra: trader to exclude from notifying.
	 * ord: order to talk about.
	 * original: If ord is type AMEND or CANCEL, this is the order being 
	 * 			changed. Otherwise this is passed in NULL.
	 * tranum: number of traders
	 */

	char* message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
	if (ord->type == SELL) {
		sprintf(message, "MARKET %s %s %d %d;", "SELL", ord->product_type->name,
		ord->amount, ord->price);
	} else if (ord->type == BUY) {
		sprintf(message, "MARKET %s %s %d %d;", "BUY", ord->product_type->name,
		ord->amount, ord->price);
	} else if (ord->type == AMEND) {
		sprintf(message, "MARKET %s %s %d %d;", order_type_string(original), 
		original->product_type->name, ord->amount, ord->price);
	} else {
		sprintf(message, "MARKET %s %s %d %d;", order_type_string(original), 
		original->product_type->name, 0, 0);
	}
	for (int i=0; i<tranum; i++) {
		if (traders[i] != exclude_tra) {
			write(traders[i]->ex_fd, message, strlen(message));
		}
	}
	free(message);

	/* Send signals */
	for (int i=0; i<tranum; i++) {
		if (traders[i] != exclude_tra) {
			if (kill(traders[i]->pid, 0) == 0) {
				if (kill(traders[i]->pid, SIGUSR1) == -1) {
					// perror("Exchange: notify others kill() failed");
				}
			}
		}
	}
}

char* order_type_string(order* ord) {
	/* 
	 * Returns the string equivalent of the enum type for an order.
	 * 
	 * ord: String equivalent for this order.
	 * 
	 * returns: string equivalent.
	 */

	if (ord->type == BUY) {
		return "BUY";
	} else if (ord->type == SELL) {
		return "SELL";
	}
	return NULL;
}

order* find_order_oid(order* ord) {
	/* 
	 * Find the order with the order id as ord.
	 * 
	 * ord: order
	 * 
	 * returns: order with same id as ord. Null if the order doesn't exist.
	 */

	for (order* pointer = ord->tra->ordhead; pointer!=NULL; 
	pointer = pointer->tranext) {
		if ((ord != pointer) && (pointer->oid == ord->oid)) {
			return pointer;
		}
	}
	return NULL;
}

product_history* find_product_history(order* ord) {
	/* 
	 * Find the product history similar to the product the order is changing.
	 * 
	 * ord: order
	 * 
	 * returns: the product history
	 */

	for (int i=0; i<ord->tra->hsize; i++) {
		if (ord->tra->history[i]->prod == ord->product_type) {
			return ord->tra->history[i];
		}
	}
	return NULL;	
}

long round_num(double number) {
	/* 
	 * rounds number to the nearest whole number.
	 * 
	 * number: number to round
	 * 
	 * returns: rounded number as a long
	 */

	return (long)(number+0.5);
}


void match_order(order* ord, long* ex_total) {
	/* 
	 * Matches new order to orders for the same product. Adds total to traders 
	 * and exchange. Notifies the matched traders. Adds the new order to the 
	 * product list.
	 * 
	 * ord: The newest order
	 * ex_total: The total amount of fees taken by the trader.
	 */

	
	product_history* ph_ord = find_product_history(ord);

	if (ord->type == SELL) {
		/* Find the highest priced BUY */
		for (order* cursor = ord->product_type->ordhead; (cursor != NULL);) {
			if ((cursor->type == BUY) && (cursor->price >= ord->price)) {
				int qty = 0;

				/* Find quantity sold */
				if (cursor->amount > ord->amount) {
					/* Completely fill new order */
					qty = ord->amount;
				} else {
					/* Partially fill new order */
					qty = cursor->amount;
				}
				cursor->amount -= qty;
				ord->amount -= qty;

				/* Change product history */
				long total = ((long)qty * (long)cursor->price);
				long fee = round_num(total*0.01);
				product_history* ph_cursor = find_product_history(cursor);
				ph_cursor->amount = ph_cursor->amount + qty;
				ph_cursor->total = ph_cursor->total - total;
				ph_ord->amount = ph_ord->amount - qty;
				ph_ord->total = ph_ord->total + total - fee;
				*ex_total += fee;

				/* Notify Traders */
				char* message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
				sprintf(message, "FILL %d %d;", cursor->oid, qty);
				write(cursor->tra->ex_fd, message, strlen(message));
				if (kill(cursor->tra->pid, 0) == 0) {
					if (kill(cursor->tra->pid, SIGUSR1) == -1) {
						// perror("Exchange: match order kill() failed");
					}
				}
				sprintf(message, "FILL %d %d;", ord->oid, qty);
				write(ord->tra->ex_fd, message, strlen(message));
				if (kill(ord->tra->pid, 0) == 0) {
					if (kill(ord->tra->pid, SIGUSR1) == -1) {
						// perror("Exchange: match order kill() failed");
					}
				}
				free(message);

				printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%ld.\n", 
				LOG_PREFIX, cursor->oid, cursor->tra->tid, ord->oid, ord->tra->tid, 
				total, fee);

				if (cursor->amount <= 0) {
					/* Delete cursor order */
					order* tmp = cursor->pronext;
					delete_order(cursor);
					cursor = tmp;
					if (ord->amount < 1) {
						break;
					} else {
						continue;
					}
				} else if (ord->amount < 1) {
					break;
				}
			}
			cursor = cursor->pronext;
		}

		if (ord->amount < 1) {
			/* Delete new order */
			remove_from_trader_list(ord);
			#ifndef TESTING
			free(ord);
			#endif
			return;
		} else {
			/* Add remaining new order to product list */
			add_to_orderbook(ord);
		}
		
	} else {
		/* Find the lowest priced SELL */
		order* cursor = ord->product_type->ordhead;
		for (; ((cursor != NULL) && (cursor->pronext != NULL)); 
		cursor = cursor->pronext) {
		}
		/* Traverse the product order list reverse */
		for (; cursor != NULL;) {
			/* Find the lowest price sell */
			if ((cursor->type == SELL) && (cursor->price <= ord->price)) {
				/* Find the earliest order */
				order* earliest = cursor;
				for (; (earliest->proprev != NULL) && 
				(earliest->proprev->type == SELL) && 
				(earliest->proprev->price == earliest->price); earliest = earliest->proprev) {

				}
				/* for (order* earl = cursor->proprev; earl != NULL;) {
					if (earl->price != cursor->price) {
				 */		/* earl.next is the earliest order of that level */
				/* 		earliest = earl->pronext;
						break;
					} else {
						earliest = earl;
					}
				} */

				/* Match order earliest to ord */
				int qty = 0;
				if (earliest->amount > ord->amount) {
					/* Ord is fufilled */
					qty = ord->amount;
				} else {
					/* Ord is partially filled. */
					qty = earliest->amount;
				}

				/* Deduct quantity from each order */
				earliest->amount -= qty;
				ord->amount -= qty;

				/* Change product history */
				long total = ((long)qty * (long)earliest->price);
				long fee = round_num(total*0.01);
				product_history* ph_earliest = find_product_history(earliest);
				ph_earliest->amount -= qty;
				ph_earliest->total += total;
				ph_ord->amount += qty;
				ph_ord->total -= (total + fee);
				*ex_total += fee;

				/* Notify traders */
				char* message = (char*) calloc(MAX_ORDER_SIZE, sizeof(char));
				sprintf(message, "FILL %d %d;", ord->oid, qty);
				write(ord->tra->ex_fd, message, strlen(message));
				if (kill(ord->tra->pid, 0) == 0) {
					if (kill(ord->tra->pid, SIGUSR1) == -1) {
						// perror("Exchange: match order kill() failed");
					}
				}
				sprintf(message, "FILL %d %d;", earliest->oid, qty);
				write(earliest->tra->ex_fd, message, strlen(message));
				if (kill(earliest->tra->pid, 0) == 0) {
					if (kill(earliest->tra->pid, SIGUSR1) == -1) {
						// perror("Exchange: match order kill() failed");
					}
				}
				free(message);

				printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%ld.\n", 
				LOG_PREFIX, earliest->oid, earliest->tra->tid, ord->oid, ord->tra->tid, 
				total, fee);

				/* Delete orders */
				if (earliest->amount <= 0) {
					/* Delete earliest order */
					if (cursor == earliest) {
						order* tmp = earliest->proprev;
						delete_order(earliest);
						cursor = tmp;
					} else {
						delete_order(earliest);
					}
					if (ord->amount < 1) {
						break;
					} else {
						continue;
					}
				} else if (ord->amount < 1) {
					break;
				}
			}
			cursor = cursor->proprev;
		}

		if (ord->amount < 1) {
			/* Delete new order */
			remove_from_trader_list(ord);
			#ifndef TESTING
			free(ord);
			#endif
			return;
		} else {
			/* Add remaining new order to product list */
			add_to_orderbook(ord);
		}
	}


}

void add_to_orderbook(order* ord) {
	/* 
	 * Add the new order to the correct product order list according to highest 
	 * to lowest price order, and for each level, earliest to latest time.
	 * 
	 * ord: new order to add
	 */

	product* prod = ord->product_type;

	if (prod->ordhead == NULL) {
		/* ord is the only order */
		prod->ordhead = ord;
		
		/* Add level */
		if (ord->type == BUY) {
			prod->buy_level += 1;
		} else {
			prod->sell_level += 1;
		}
	} else {
		for (order* cursor = prod->ordhead; cursor != NULL;) {
			if (cursor->price == ord->price) {
				/* Add to the end of the level */
				if ((ord->type == cursor->type) && (cursor->pronext != NULL) &&
				(cursor->pronext->type == cursor->type) && 
				(cursor->pronext->price == cursor->price)) {
					/* Continue down the list */
					cursor = cursor->pronext;
				} else if (ord->type == cursor->type) {
					/* Add ord to end of cursor */
					ord->proprev = cursor;
					ord->pronext = cursor->pronext;
					cursor->pronext = ord;
					if (ord->pronext != NULL) {
						ord->pronext->proprev = ord;
					}
					return;
				}
			} else if ((cursor->price < ord->price)) {
				/* ord level doesn't exist */
				/* Insert order to the right*/
				ord->proprev = cursor->proprev;
				ord->pronext = cursor;
				cursor->proprev = ord;
				if (ord->proprev != NULL) {
					ord->proprev->pronext = ord;
				} else {
					prod->ordhead = ord;
				}

				/* Add new level */
				if (ord->type == BUY) {
					prod->buy_level += 1;
				} else {
					prod->sell_level += 1;
				}
				return;

			} else if ((cursor->price > ord->price) && (cursor->pronext == NULL)) {
				/* Order level doesn't exist */
				/* Insert order to the left */
				ord->proprev = cursor;
				ord->pronext = cursor->pronext;
				cursor->pronext = ord;
				if (ord->pronext != NULL) {
					ord->pronext->proprev = ord;
				}
				/* Add a new level */
				if (ord->type == BUY) {
					prod->buy_level += 1;
				} else {
					prod->sell_level += 1;
				}
				return;
			}
			else {
				/* Continue down the list */
				cursor = cursor->pronext;
			}
		}

	}
}

void remove_from_orderbook(order* ord) {
	/* 
	 * Remove order from orderbook
	 * 
	 * ord: order to remove
	 */

	product* prod = ord->product_type;

	/* Check if ord has the same level to its left */
	int has_left = 0;
	if ((ord->proprev != NULL) && (ord->proprev->type == ord->type) && 
	(ord->proprev->price == ord->price)) {
		has_left = 1;
	}
	
	/* Check if ord has the same level to its right */
	int has_right = 0;
	if ((ord->pronext != NULL) && (ord->pronext->type == ord->type) &&
	(ord->pronext->price == ord->price)) {
		has_right = 1;
	}

	if ((!has_left) && (!has_right)) {
		/* Decrement level */
		if (ord->type == BUY) {
			prod->buy_level -= 1;
		} else {
			prod->sell_level -= 1;
		}
	}

	/* Remove order */
	if (ord->pronext != NULL) {
		ord->pronext->proprev = ord->proprev;
	}
	if (ord->proprev != NULL) {
		ord->proprev->pronext = ord->pronext;
	} else {
		prod->ordhead = ord->pronext;
	}
	

} 

void remove_from_trader_list(order* ord) {
	/* 
	 * Remove order from the trader order list
	 * 
	 * ord: order to remove
	 */

	if (ord->traprev == NULL) {
		ord->tra->ordhead = ord->tranext;
	} else {
		ord->traprev->tranext = ord->tranext;
	}
	if (ord->tranext != NULL) {
		ord->tranext->traprev = ord->traprev;
	}
}

order* amend_order(order* original, order* new) {
	/* 
	 * Amend original order with new order information.
	 * 
	 * original: the prexisting order
	 * new: the new information
	 * 
	 * return: the amended order
	 */

	/* Remove original from the orderbook */
	remove_from_orderbook(original);
	/* Change order information */
	original->amount = new->amount;
	original->price = new->price;
	/* Delete new order */
	remove_from_trader_list(new);
	#ifndef TESTING
	free(new);
	#endif
	return original;
}

void delete_order(order* ord) {
	/* 
	 * Deletes order. Deletes from product list, trader list, free order.
	 * 
	 * ord: order to delete
	 */

	remove_from_orderbook(ord);
	remove_from_trader_list(ord);
	#ifndef TESTING
	free(ord);
	#endif

}

void print_logbook(product** products, trader** traders, int pronum, int tranum) {
	/* 
	 * Prints the products orders --PRODUCTS-- and traders
	 * 
	 * products: products to print
	 * traders: traders to print
	 * pronum: number of products
	 * tranum: number of traders
	 */

	printf("%s\t--ORDERBOOK--\n", LOG_PREFIX);
	/* Parse through each product */
	for (int i=0; i<pronum; i++) {
		printf("%s\tProduct: %s; Buy levels: %d; Sell levels: %d\n", LOG_PREFIX, 
		products[i]->name, products[i]->buy_level, products[i]->sell_level);
		/* Parse through each order */
		int qty = 0;
		int orders = 0;
		for (order* cursor = products[i]->ordhead; cursor != NULL; 
		cursor = cursor->pronext) {
			/* Increment qty and orders */
			qty += cursor->amount;
			orders += 1;
			/* Start of new level */
			if ((cursor->pronext == NULL) || 
			(cursor->pronext->type != cursor->type) ||
			(cursor->pronext->price != cursor->price)) {
				/* Print out current order level */
				if (cursor->type == BUY) {
					if (orders > 1) {
						printf("%s\t\tBUY %d @ $%d (%d orders)\n", LOG_PREFIX, qty, 
						cursor->price, orders);
					} else {
						printf("%s\t\tBUY %d @ $%d (%d order)\n", LOG_PREFIX, qty, 
						cursor->price, orders);
					}
				} else {
					if (orders > 1) {
						printf("%s\t\tSELL %d @ $%d (%d orders)\n", LOG_PREFIX, qty, 
						cursor->price, orders);
					} else {
						printf("%s\t\tSELL %d @ $%d (%d order)\n", LOG_PREFIX, qty, 
						cursor->price, orders);
					}
				}
				qty = 0;
				orders = 0;
			}
		}
	}

	/* Parse through each trader */
	printf("%s\t--POSITIONS--\n", LOG_PREFIX);
	for (int i=0; i<tranum; i++) {
		printf("%s\tTrader %d:", LOG_PREFIX, traders[i]->tid);
		/* Parse through each product history */
		for (int j=0; j<traders[i]->hsize; j++) {
			product_history* ph = traders[i]->history[j];
			if (j != traders[i]->hsize -1) {
				printf(" %s %d ($%ld),", ph->prod->name, ph->amount, ph->total);
			} else {
				printf(" %s %d ($%ld)\n", ph->prod->name, ph->amount, ph->total);
			}
		}
	}

}

int close_coms(trader** traders, int num) {
	/* 
	 * Close all file descriptions and unlink pipes. Free pipe names.
	 * 
	 * traders: an array of all traders to access fds and pipes
	 * num: number of traders in array
	 * 
	 * returns: 0 if any close or unlink operation fails. Otherwise 1.
	 */

	for (int i=0; i<num; i++) {
		if (close(traders[i]->tr_fd) == -1) {
			// perror("trader unable to close tr_fd");
			return 0;
		}
		/* if (close(traders[i]->ex_fd) == -1) {
			perror("trader unable to close ex_fd");
			return 0;
		} */
		if (unlink(traders[i]->ex_pipe) == -1) {
			// perror("trader unable to unlink ex_pipe");
			return 0;
		}
		if (unlink(traders[i]->tr_pipe) == -1) {
			// perror("trader unable to unlink tr_pipe");
			return 0;

		}
		free(traders[i]->ex_pipe);
		free(traders[i]->tr_pipe);
	}
	return 1;
}

int close_epoll(int epfd, trader** traders, int num, 
struct epoll_event* events) {
	/* 
	 * Free epoll_events registered to epoll. Close epoll file description.
	 * 
	 * epfd: Epoll file descriptor
	 * traders: Array of traders
	 * num: number of traders in traders
	 * events: epoll_events*
	 * 
	 * return: 0 if any close or free operations fail. Otherwise 1.
	 */

	for (int i=0; i<num; i++) {
		free(traders[i]->ev);
	}
	if (close(epfd) == -1) {
		perror("close_epoll close() failed for epfd");
		return 0;
	}
	free(events);

	return 1;
}

void free_traders(trader** traders, int tranum, int pronum) {
	/* 
	 * Free history. Free trader.
	 * 
	 * Traders: Array of traders.
	 * tranum: number of traders.
	 * pronum: number of products.
	 */

	for (int i=0; i<tranum; i++) {
		for (int j=0; j<pronum; j++) {
			/* Free each trader history */
			free(traders[i]->history[j]);
		}
		/* Free trader product history array */
		free(traders[i]->history);

		free(traders[i]);
	}
	free(traders);
}

void free_product_history(product_history** ph, int pronum) {
	/* 
	 * Frees one array of product history
	 * 
	 * ph: array of product history
	 * pronum: number of products
	 */

	for (int i=0; i<pronum; i++) {
		free(ph[i]);
	}
	free(ph);
}

int clean_up(trader** traders, product** products, int tranum, int pronum, 
int epfd, struct epoll_event* events, trader** lookup_ar) {
	/* 
	 * Closes, frees and unlinks everything.
	 * 
	 * traders: array of traders.
	 * products: array of products.
	 * tranum: number of traders.
	 * pronum: number of products
	 * epfd: epoll file descriptor
	 * events: epoll_event events
	 * lookup_ar: array of trader addresses
	 * 
	 * returns: 0 if there any cleanup fails. Otherwise 1.
	 */

	if (close_coms(traders, tranum) == 0) {
		return 0;
	}
	if (close_epoll(epfd, traders, tranum, events) == 0) {
		return 0;
	}

	free_traders(traders, tranum, pronum);
	free_products(products, pronum);
	free(lookup_ar);

	return 1;
}

int trader_pid_comp(const void* a, const void* b) {
	/* 
	 * Compares pid's of trader* a to trader* b.
	 * 
	 * a: trader** a.
	 * b: trader** b.
	 * 
	 * returns: <0 if b>a. 0 if b==a. >0 if a>b.
	 */

	const trader* atmp = *(trader**)a;
	const trader* btmp = *(trader**)b;

	int apid = (int) atmp->pid;
	int bpid = (int) btmp->pid;

	return (apid - bpid);
}

trader* lookup_pid(trader** traders, trader* dummy, int tranum) {
	/* 
	 * Searches for trader by pid.
	 * 
	 * traders: array of traders.
	 * pid: pid of the target trader.
	 * tranum: number of trader in traders
	 * 
	 * returns: address of the trader.
	 */

	/* Sort traders by pid */
	qsort(traders, tranum, sizeof(trader*), trader_pid_comp);

	/* Search for trader by pid */
	trader** tra = (trader**) bsearch(&dummy, traders, tranum, sizeof(trader*), 
	trader_pid_comp);

	return *tra;
}

int trader_trfd_comp(const void* a, const void* b) {
	/* 
	 * Compares trader file descriptor of trader a to trader b
	 * 
	 * a: trader** a
	 * b: trader** b
	 * 
	 * returns: <0 if b>a. 0 if b==a. >0 if a>b.
	 */

	const trader* atmp = *(trader**)a;
	const trader* btmp = *(trader**)b;

	int afd = atmp->tr_fd;
	int bfd = btmp->tr_fd;

	return (afd - bfd);
}

trader* lookup_trfd(trader** traders, trader* dummy, int tranum) {
	/* 
	 * Searches for trader by trader file descriptor
	 * 
	 * traders: array of traders.
	 * dummy: trader with only trader file descriptor initialised
	 * tranum: number of trader in traders
	 * 
	 * returns: address of the trader.
	 */

	/* Sort traders by trfd */
	qsort(traders, tranum, sizeof(trader*), trader_trfd_comp);

	/* Search for trader by trfd */
	trader** tra = (trader**) bsearch(&dummy, traders, tranum, 
	sizeof(trader*), trader_trfd_comp);

	return *tra;
}

void kill_all_traders(trader** traders, int current_num, product** products, 
int pronum, int epfd, struct epoll_event* events, product_history** ph_tmp) {
	/* 
	 * Kills all traders when an open() or exec() has failed
	 * 
	 * traders: array of existing traders
	 * current_num: current number of traders successfully created
	 * products: array of products
	 * pronum: number of products
	 * epfd: Epoll file descriptor
	 * events: epoll_events*
	 * ph_tmp: array of product history
	 */

	for (int i=0; i<current_num; i++) {
		if (kill(traders[i]->pid, 0) == 0) {
			kill(traders[i]->pid, SIGKILL);
			waitpid(traders[i]->pid, NULL, 0);
		}
	}
	close_coms(traders, current_num);
	close_epoll(epfd, traders, current_num, events);
	free_traders(traders, current_num, pronum);
	free_product_history(ph_tmp, pronum);
	free_products(products, pronum);
}

#ifndef TESTING
int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Not enough arguments.\n");
		return -1;
	}

	/* Print Starting Exchange Message */
	printf("%s Starting\n", LOG_PREFIX);
	
	/* Go through product file */

	FILE* productfp;
	productfp = fopen(argv[1], "r");
	if (productfp == NULL) {
		perror("fopen");
		exit(-1);
	}

	char buf[MAX_PRODUCT_NAME];

	if (fgets(buf, MAX_PRODUCT_NAME, productfp) == NULL) {
		printf("Product file not in right format\n");
		exit(-1);
	}
	/* Remove \n at the end of string */
	if (buf[strlen(buf)-1] == '\n') {
		buf[strlen(buf)-1] = '\0';
	}
	if (!is_integer(buf)) {
		printf("First line of product file isn't a number\n");
		exit(-1);	
	} 

	int product_num = atoi(buf);
	if (product_num < 1) {
		printf("Product file must carry products");
		exit(-1);
	}

	product** products = (product**) calloc(product_num, sizeof(product*));

	for (int i=0; i < product_num; i++) {
		// Go through the product file
		if (fgets(buf, MAX_PRODUCT_NAME, productfp) == NULL) {
			printf("Not enough products in file\n");
			exit(-1);
		} else {
			// Check the product is less than 17 characters
			if (buf[strlen(buf)-1] == '\n') {
				buf[strlen(buf)-1] = '\0';
			}
			if (strlen(buf) > 16) {
				printf("Product name invalid\n");
				exit(-1);
			}
			// Check the product is only alphanumeric
			if (!is_alphanum(buf)) {
				printf("Product name isn't alphanumeric: %s\n", buf);
				exit(-1);
			}
			// Add product to products array
			product* temp = (product*) calloc(product_num, sizeof(product));
			temp->name = (char*) calloc(MAX_PRODUCT_NAME, sizeof(char));
			temp->name = (char*) memcpy(temp->name, buf, strlen(buf));
			products[i] = temp;
		}

	}
	
	fclose(productfp);

	/* Print out products */
	printf("%s Trading %d products:", LOG_PREFIX, product_num);
	for (int i=0; i < product_num; i++) {
		/* Print out each product */
		printf(" %s", products[i]->name);
	}
	printf("\n");

	// Set up SIGCHLD handler
	struct sigaction chld_sig;
	memset(&chld_sig, 0, sizeof(struct sigaction));
	chld_sig.sa_sigaction = sigchld_handler;
	chld_sig.sa_flags = SA_SIGINFO;

	if (sigaction(SIGCHLD, &chld_sig, NULL) == -1) {
		perror("sigaction chld failed");
		return 1;
	}

	// Set up SIGUSR1 handler
	struct sigaction usr_sig;
	memset(&usr_sig, 0, sizeof(struct sigaction));
	usr_sig.sa_sigaction = sigusr1_handler;
	usr_sig.sa_flags = SA_SIGINFO;

	if (sigaction(SIGUSR1, &usr_sig, NULL) == -1) {
		perror("sigaction sigusr1 failed");
		return 1;
	}

	/* Set up SIGPIPE handler */
	struct sigaction pipe_sig;
	memset(&usr_sig, 0, sizeof(struct sigaction));
	pipe_sig.sa_handler = SIG_IGN;
	pipe_sig.sa_flags = 0;

	if (sigaction(SIGPIPE, &pipe_sig, NULL) == -1) {
		perror("sigaction sigpipe failed");
		return 1;
	}
	

	/* Go through each trader binary */

	// Current trader id
	int tranum = 0;

	// Array of trader addresses from binary files
	trader** traders = (trader**) calloc(argc - 2, sizeof(trader*));

	// Start an epoll
	int epfd = epoll_create1(EPOLL_CLOEXEC);
	struct epoll_event *events = (struct epoll_event*) calloc(argc-1, 
	sizeof(struct epoll_event));

	// Go through each binary
	for (int i=2; i < argc; i++) {
		// Create product history for each trader
		product_history** ph_tmp = create_product_history(product_num, products);

		// Exchange pipe
		char* ex_pipe = (char*) calloc(FIFO_NAME_MAX, sizeof(char));
		
		// Trader pipe
		char* tr_pipe = (char*) calloc(FIFO_NAME_MAX, sizeof(char));

		/* Create exchange_pipe */
		sprintf(ex_pipe, FIFO_EXCHANGE, tranum);
		unlink(ex_pipe);
		if (mkfifo(ex_pipe, FIFO_PERMISSION) == -1) {
			perror("Exchange: mkfifo ex_pipe failed");
			free(ex_pipe);
			free(tr_pipe);
			kill_all_traders(traders, i-2, products, product_num, epfd, events, 
			ph_tmp);
			
			return 1;
		}
		printf("%s Created FIFO %s\n", LOG_PREFIX, ex_pipe);

		/* Create trader_pipe */
		sprintf(tr_pipe, FIFO_TRADER, tranum);
		unlink(tr_pipe);
		if (mkfifo(tr_pipe, FIFO_PERMISSION) == -1) {
			perror("mkfifo tr_pipe failed");
			unlink(tr_pipe);
			unlink(ex_pipe);
			free(tr_pipe);
			free(ex_pipe);
			kill_all_traders(traders, i-2, products, product_num, epfd, events, 
			ph_tmp);

			return 1;
		}
		printf("%s Created FIFO %s\n", LOG_PREFIX, tr_pipe);

		/* Start Trader */
		printf("%s Starting trader %d (%s)\n", LOG_PREFIX, tranum, argv[i]);
		pid_t pid = fork();

		if (pid == -1) {
			perror("Forking failed");
			unlink(tr_pipe);
			unlink(ex_pipe);
			free(tr_pipe);
			free(ex_pipe);
			kill_all_traders(traders, i-2, products, product_num, epfd, events, 
			ph_tmp);
			return 1;
		} else if (pid == 0) {
			char strid[MAX_PRODUCT_NAME];
			sprintf(strid, "%d", tranum);
			if (execlp(argv[i], argv[i], strid, (char*) NULL) == -1) {
				perror("Child execlp() failed");
				unlink(tr_pipe);
				unlink(ex_pipe);
				free(tr_pipe);
				free(ex_pipe);
				kill_all_traders(traders, i-2, products, product_num, epfd, events, 
				ph_tmp);
				return 1;
			}
			
		} else {
			/* Open exchange pipe */
			
			int ex_fd = open(ex_pipe, O_WRONLY);
			if (ex_fd == -1) {
				perror("Exchange: open ex_pipe failed");
				unlink(tr_pipe);
				unlink(ex_pipe);
				free(tr_pipe);
				free(ex_pipe);
				kill_all_traders(traders, i-2, products, product_num, epfd, events, 
				ph_tmp);
				return 1;
			}
			printf("%s Connected to %s\n", LOG_PREFIX, ex_pipe);
			
			/* Open trader pipe */
			int tr_fd = open(tr_pipe, O_RDONLY);
			if (tr_fd == -1) {
				perror("Exchange: open tr_pipe failed");
				unlink(tr_pipe);
				unlink(ex_pipe);
				free(tr_pipe);
				free(ex_pipe);
				kill_all_traders(traders, i-2, products, product_num, epfd, events, 
				ph_tmp);
				return 1;
			}
			printf("%s Connected to %s\n", LOG_PREFIX, tr_pipe);

			/* Monitor tr_fd only using epoll */
			struct epoll_event* ev = (struct epoll_event*) calloc(1, 
			sizeof(struct epoll_event));
			ev->events = EPOLLHUP;
			ev->data.fd = tr_fd;
			epoll_ctl(epfd, EPOLL_CTL_ADD, tr_fd, ev);

			/* Create trader and add to trader array */
			traders[i-2] = create_trader(i-2, pid, ph_tmp, product_num, ex_fd, 
			ex_pipe, tr_fd, tr_pipe, ev);
		}

		tranum++;

	}

	/* Create a lookup array with all the traders */
	trader** lookup_ar = (trader**) calloc(argc-2, sizeof(trader*));
	memcpy(lookup_ar, traders, sizeof(trader*)*(argc-2));

	/* Create notification pipe that the handlers will write to 
	format: <type of message> <pid>;
	where type of message: 'k' or 'o' for kill or order 
	and pid: pid of trader of message */
	char* not_pipe = "/tmp/not_pipe";
	unlink(not_pipe);
	if (mkfifo(not_pipe, FIFO_PERMISSION) == -1) {
		perror("Exchange: notification pipe mkfifo() failed");
		if (clean_up(traders, products, argc-2, product_num, epfd, events, 
		lookup_ar) == -1) {
			return 1;
		}
		return 1;
	}
	
	not_fd = open(not_pipe, O_RDWR);
	if (not_fd == -1) {
		perror("Exchange: open not_pipe failed");
		unlink(not_pipe);
		if (clean_up(traders, products, argc-2, product_num, epfd, events, 
		lookup_ar) == -1) {
			return 1;
		}
		return 1;
	}
	
	/* Epoll will monitor notification pipe for any messages from traders */
	struct epoll_event not_ev;
	not_ev.events = EPOLLIN;
	not_ev.data.fd = not_fd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, not_fd, &not_ev);
	


	/* Go through each trader binary */

	char* open_mess = "MARKET OPEN;";
	for (int i=0; i<(argc-2); i++) {
		if (write(traders[i]->ex_fd, open_mess, strlen(open_mess)) == -1) {
			// perror("opening message write() failed");
			clean_up(traders, products, argc-2, product_num, epfd, events, 
			lookup_ar);
			unlink(not_pipe);
			return 1;
		}
		kill(traders[i]->pid, SIGUSR1);
	}

	/* Total exchange fees collected */
	long ex_fees = 0;
	
	/* While there are still traders active, wait for orders */
	while (tranum > 0) {
		/* Check the not_pipe for orders or if there are closed pipes */
		int fd_ready_num;
		do {
			fd_ready_num = epoll_wait(epfd, events, argc-1, -1);
		} while (fd_ready_num < 0 && errno==EINTR);
		if (fd_ready_num == -1) {
			perror("epoll_wait failed");
			clean_up(traders, products, argc-2, product_num, epfd, events, 
			lookup_ar);
			unlink(not_pipe);
			return 1;
		}

		/* Parse through all pipes that set off epoll */
		for (int i=0; i<fd_ready_num; i++) {
			if (events[i].data.fd == not_fd) {
				int pid;
				if (read(events[i].data.fd, (void*) &pid, sizeof(int)) == -1) {
					perror("parse through events read() failed");
					clean_up(traders, products, argc-2, product_num, epfd, events, 
					lookup_ar);
					unlink(not_pipe);
					return 1;
				}

				if (pid >= 0) {
					/* Trader has made an order */
					// printf("PARSING ORDER\n\n");
					
					/* Find trader tr_pipe specified in not_fd */
					trader* dummy = (trader*) calloc(1, sizeof(trader));
					dummy->pid = pid;
					trader* tra = lookup_pid(lookup_ar, dummy, argc-2);
					free(dummy);

					/* Get string from pipe */
					char* str_order = read_pipe(tra->tr_fd);
					if (str_order == NULL) {
						/* Order was invalid. Send message to trader */
						send_invalid_notif(tra);
						continue;
					}

					char* str = (char*) calloc(strlen(str_order), sizeof(char));
					str = (char*) memcpy(str, str_order, strlen(str_order)-1);
					printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, 
					tra->tid, str);
					free(str);

					/* Turn string into order */
					order* ord = convert_order(str_order, tra, products, product_num);
					free(str_order);
					if (ord == NULL) {
						/* Invalid order. Send message to trader */
						send_invalid_notif(tra);
						continue;
					}
					
					/* Attach order to trader list */
					ord->tranext = tra->ordhead;
					if (tra->ordhead != NULL) {
						tra->ordhead->traprev = ord;
					}
					tra->ordhead = ord;

					/* Attach order to product list in price-time order */
					if (ord->type == BUY || ord->type == SELL) {
						/* Send Accepted message to the trader */
						send_accepted_notif(tra, ord->oid);
						/* Notify other traders */
						notify_others(traders, tra, ord, NULL, argc-2);

						/* Match order */
						match_order(ord, &ex_fees);


					} else if (ord->type == AMEND) {
						/* Amend order */
						// If order doesn't exist. Send invalid message
						order* original = find_order_oid(ord);
						if (original == NULL) {
							/* Invalid order */
							send_invalid_notif(tra);
							remove_from_trader_list(ord);
							free(ord);
							continue;
						} else {
							/* Send Amended message to the trader */
							send_amended_notif(tra, ord->oid);
							/* Notify other traders */
							notify_others(traders, tra, ord, original, argc-2);
							/* Amend order */
							order* neword = amend_order(original, ord);
							/* Match order */
							match_order(neword, &ex_fees);
						}

					} else {
						order* original = find_order_oid(ord);
						if (original == NULL) {
							/* Invalid order. Order doesn't exist */
							send_invalid_notif(tra);
							remove_from_trader_list(ord);
							free(ord);
							continue;
						} else {
							/* Send Cancelled messsage to the trader */
							send_cancelled_notif(tra, ord->oid);
							/* Notify other traders */
							notify_others(traders, tra, ord, original, argc-2);
							/* Delete order */
							delete_order(original);
							remove_from_trader_list(ord);
							free(ord);
						}

					}

					print_logbook(products, traders, product_num, argc-2);
					

				} else {
					/* Trader has been killed */
					/* Find trader by pid */
					trader* dummy = (trader*) calloc(1, sizeof(trader));
					dummy->pid = -1*pid;
					trader* tra = lookup_pid(lookup_ar, dummy, argc-2);
					free(dummy);
					printf("%s Trader %d disconnected\n", LOG_PREFIX, tra->tid);
					tranum--;
				}
				

			} else {
				/* Trader pipe is closed. Kill and notify in not_pipe */
				trader* dummy = (trader*) calloc(1, sizeof(trader));
				dummy->tr_fd = events[i].data.fd;
				trader* tra = lookup_trfd(lookup_ar, dummy, argc-2);
				free(dummy);
				
				/* Close exchange file descriptor */
				if (close(tra->ex_fd) == -1) {
					perror("Exchange: trader pipe closed close exfd failed");
					clean_up(traders, products, argc-2, product_num, epfd, events, 
					lookup_ar);
					unlink(not_pipe);
					return 1;
				}

				/* Remove event from epoll */
				if (epoll_ctl(epfd, EPOLL_CTL_DEL, tra->tr_fd, tra->ev) < 0) {
					perror("Exchange epoll del failed");
					clean_up(traders, products, argc-2, product_num, epfd, events, 
					lookup_ar);
					unlink(not_pipe);
					return 1;
				}

				/* Write -<pid> into not_pipe */
				int tmppid = -1*((int)tra->pid);
				if (write(not_fd, &tmppid, sizeof(int)) == -1) {
					perror("Exchange: trader pipe closed write() failed");
					clean_up(traders, products, argc-2, product_num, epfd, events, 
					lookup_ar);
					unlink(not_pipe);
					return 1;
				}
				/* Send SIGKILL to trader */
				if (kill(tra->pid, 0) != -1) {
					/* Trader still exists */
					if (kill(tra->pid, SIGKILL) == -1) {
						perror("Exchange: trader pipe closed kill() failed");
						clean_up(traders, products, argc-2, product_num, epfd, events, 
						lookup_ar);
						unlink(not_pipe);
						return 1;
					}
				}

				
			}
		}
	}

	/* Close all fd, unlink all pipes, free structures 
	All traders are already dead and reaped*/

	printf("%s Trading completed\n", LOG_PREFIX);
	printf("%s Exchange fees collected: $%ld\n", LOG_PREFIX, ex_fees);

	if (clean_up(traders, products, argc-2, product_num, epfd, events, 
	lookup_ar) == -1) {
		return 1;
	}
	unlink(not_pipe);

	return 0;
}
#endif

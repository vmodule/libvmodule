/*
 * conn_wrap.h
 *
 *  Created on: May 1, 2017
 *      Author: jeffrey
 */
/*
 * Copyright (c) <2017>, Memcached
 * All rights reserved.
 * This source code copy from Memcached Open Source
 * format for Network bu Jeffrey..
 */
#ifndef _CONN_WRAP_H_
#define _CONN_WRAP_H_
#include <network/core/conn_base.h>
#define NEED_ALIGN 1

enum try_read_result {
	READ_DATA_RECEIVED, READ_NO_DATA_RECEIVED, READ_ERROR, /** an error occurred (on the socket) (or client closed connection) */
	READ_MEMORY_ERROR /** failed to allocate more memory */
};

enum transmit_result {
	TRANSMIT_COMPLETE, /** All done writing. */
	TRANSMIT_INCOMPLETE, /** More data remaining to write. */
	TRANSMIT_SOFT_ERROR, /** Can't write any more right now. */
	TRANSMIT_HARD_ERROR /** Can't write (c->state is set to conn_closing) */
};

enum transmit_result transmit(conn *c);

int ensure_iov_space(conn *c);

int add_iov(conn *c, const void *buf, int len);

/*
 * Adds a message header to a connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
int add_msghdr(conn *c);

/* set up a connection to write a buffer then free it, used for stats */
void write_and_free(conn *c, char *buf, int bytes);
/*
 * Constructs a set of UDP headers and attaches them to the outgoing messages.
 */
int build_udp_headers(conn *c);

void out_string(conn *c, const char *str);

void out_of_memory(conn *c, char *ascii_error);

int try_read_command(conn *c);

enum try_read_result try_read_network(conn *c);

enum try_read_result try_read_udp(conn *c);

void maximize_sndbuf(const int sfd);

int new_socket_unix(void);

int server_socket_unix(const char *path, int access_mask);

int new_socket(struct addrinfo *ai);

int server_socket(const char *interface, int port,
		enum network_transport transport, FILE *portnumber_file);

int server_sockets(int port, enum network_transport transport,
		FILE *portnumber_file);

#endif /* CONN_WRAP_H_ */

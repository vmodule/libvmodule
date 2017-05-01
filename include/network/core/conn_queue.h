/*
 * conn_queue.h
 *
 *  Created on: Apr 21, 2017
 *      Author: jeffrey
 */
/*
 * Copyright (c) <2017>, Memcached
 * All rights reserved.
 * This source code copy from Memcached Open Source
 * format for Network bu Jeffrey..
 */
#ifndef CONN_QUEUE_H_
#define CONN_QUEUE_H_
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <network/core/conn_base.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ITEMS_PER_ALLOC 64

/* An item in the connection queue. */
typedef struct conn_queue_item CQ_ITEM;
struct conn_queue_item {
	int sfd;
	enum conn_states init_state;
	int event_flags;
	int read_buffer_size;
	enum network_transport transport;
	conn *c;
	CQ_ITEM *next;
};

/* A connection queue. */
typedef struct conn_queue CQ;
struct conn_queue {
	CQ_ITEM *head;
	CQ_ITEM *tail;
	pthread_mutex_t lock;
};

void cq_freelist_init();

/*
 * Initializes a connection queue.
 */
void cq_init(CQ *cq);

/*
 * Looks for an item on a connection queue, but doesn't block if there isn't
 * one.
 * Returns the item, or NULL if no item is available
 */
CQ_ITEM *cq_pop(CQ *cq);

/*
 * Adds an item to a connection queue.
 */
void cq_push(CQ *cq, CQ_ITEM *item);

/*
 * Returns a fresh connection queue item.
 */
CQ_ITEM *cqi_new(void);

/*
 * Frees a connection queue item (adds it to the freelist.)
 */
void cqi_free(CQ_ITEM *item);

#ifdef __cplusplus
}
#endif
#endif /* CONNECTQUEUE_H_ */

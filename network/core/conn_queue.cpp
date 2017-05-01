/*
 * connectqueue.cpp
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

#include <network/core/conn_queue.h>
#include <network/core/conn_base.h>
#include <network/core/conn_thread.h>
#include <vutils/Logger.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "conn_queue"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif

/* Free list of CQ_ITEM structs */
static CQ_ITEM *cqi_freelist;
static pthread_mutex_t cqi_freelist_lock;

/*
 * Initializes a connection queue.
 */
void cq_freelist_init() {
	pthread_mutex_init(&cqi_freelist_lock, NULL);
	cqi_freelist = NULL;
}

/*
 * Initializes a connection queue.
 */
void cq_init(CQ *cq) {
	pthread_mutex_init(&cq->lock, NULL);
	cq->head = NULL;
	cq->tail = NULL;
}

/*
 * Looks for an item on a connection queue, but doesn't block if there isn't
 * one.
 * Returns the item, or NULL if no item is available
 */
CQ_ITEM *cq_pop(CQ *cq) {
	CQ_ITEM *item;

	pthread_mutex_lock(&cq->lock);
	item = cq->head;
	if (NULL != item) {
		cq->head = item->next;
		if (NULL == cq->head)
			cq->tail = NULL;
	}
	pthread_mutex_unlock(&cq->lock);

	return item;
}

/*
 * Adds an item to a connection queue.
 */
void cq_push(CQ *cq, CQ_ITEM *item) {
	item->next = NULL;

	pthread_mutex_lock(&cq->lock);
	if (NULL == cq->tail)
		cq->head = item;
	else
		cq->tail->next = item;
	cq->tail = item;
	pthread_mutex_unlock(&cq->lock);
}

/*
 * Returns a fresh connection queue item.
 */
CQ_ITEM *cqi_new(void) {
	CQ_ITEM *item = NULL;
	pthread_mutex_lock(&cqi_freelist_lock);
	if (cqi_freelist) {
		item = cqi_freelist;
		cqi_freelist = item->next;
	}
	pthread_mutex_unlock(&cqi_freelist_lock);

	if (NULL == item) {
		int i;

		/* Allocate a bunch of items at once to reduce fragmentation */
		item = (CQ_ITEM *) malloc(sizeof(CQ_ITEM) * ITEMS_PER_ALLOC);
		if (NULL == item) {
			MY_LOGE("%s malloc CQ_ITEM Error", __func__);
			STATS_LOCK();
			stats.malloc_fails++;
			STATS_UNLOCK();
			return NULL;
		}

		/*
		 * Link together all the new items except the first one
		 * (which we'll return to the caller) for placement on
		 * the freelist.
		 */
		for (i = 2; i < ITEMS_PER_ALLOC; i++)
			item[i - 1].next = &item[i];

		pthread_mutex_lock(&cqi_freelist_lock);
		item[ITEMS_PER_ALLOC - 1].next = cqi_freelist;
		cqi_freelist = &item[1];
		pthread_mutex_unlock(&cqi_freelist_lock);
	}

	return item;
}

/*
 * Frees a connection queue item (adds it to the freelist.)
 */
void cqi_free(CQ_ITEM *item) {
	pthread_mutex_lock(&cqi_freelist_lock);
	item->next = cqi_freelist;
	cqi_freelist = item;
	pthread_mutex_unlock(&cqi_freelist_lock);
}

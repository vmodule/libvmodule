/*
 * conn_thread.cpp
 *
 *  Created on: Apr 28, 2017
 *      Author: jeffrey
 */
/*
 * Copyright (c) <2017>, Memcached
 * All rights reserved.
 * This source code copy from Memcached Open Source
 * format for Network bu Jeffrey..
 */
#include <network/core/conn_thread.h>
#include <network/core/conn_queue.h>
#include <vutils/Logger.h>
#include <pthread.h>
#include <event.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "conn_thread"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif

/*
 * Each libevent instance has a wakeup pipe, which other threads
 * can use to signal that they've put a new connection on its queue.
 */
static LIBEVENT_THREAD *threads;

/*
 * Number of worker threads that have finished setting themselves up.
 */
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;

static void thread_libevent_process(int fd, short which, void *arg);

static void wait_for_thread_registration(int nthreads) {
	while (init_count < nthreads) {
		pthread_cond_wait(&init_cond, &init_lock);
	}
}

static void register_thread_initialized(void) {
	pthread_mutex_lock(&init_lock);
	init_count++;
	pthread_cond_signal(&init_cond);
	pthread_mutex_unlock(&init_lock);
#if 0
	/* Force worker threads to pile up if someone wants us to */
	pthread_mutex_lock (&worker_hang_lock);
	pthread_mutex_unlock(&worker_hang_lock);
#endif
}

/****************************** LIBEVENT THREADS *****************************/

/*
 * Creates a worker thread.
 */
static void create_worker(void *(*func)(void *), void *arg) {
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);

	if ((ret = pthread_create(&((LIBEVENT_THREAD*) arg)->thread_id, &attr, func,
			arg)) != 0) {
		MY_LOGE( "Can't create thread: %s\n", strerror(ret));
		exit(1);
	}
}

/*
 * Set up a thread's information.
 */
static void setup_thread(LIBEVENT_THREAD *me) {
	me->base = event_init();
	if (!me->base) {
		MY_LOGE( "Can't allocate event base\n");
		exit(1);
	}

	/* Listen for notifications from other threads */
	event_set(&me->notify_event, me->notify_receive_fd, EV_READ | EV_PERSIST,
			thread_libevent_process, me);
	event_base_set(me->base, &me->notify_event);

	if (event_add(&me->notify_event, 0) == -1) {
		MY_LOGE( "Can't monitor libevent notify pipe\n");
		exit(1);
	}

	me->new_conn_queue = (struct conn_queue *)malloc(sizeof(struct conn_queue));
	if (me->new_conn_queue == NULL) {
		MY_LOGE("Failed to allocate memory for connection queue");
		exit(EXIT_FAILURE);
	}
	cq_init(me->new_conn_queue);

	if (pthread_mutex_init(&me->stats.mutex, NULL) != 0) {
		MY_LOGE("Failed to initialize mutex");
		exit(EXIT_FAILURE);
	}
#if 0
	me->suffix_cache = cache_create("suffix", SUFFIX_SIZE, sizeof(char*), NULL,
			NULL);
	if (me->suffix_cache == NULL) {
		MY_LOGE( "Failed to create suffix cache\n");
		exit(EXIT_FAILURE);
	}
#endif
}

/*
 * Worker thread: main event loop
 */
static void *worker_libevent(void *arg) {
	LIBEVENT_THREAD *me = (LIBEVENT_THREAD *)arg;
#if 0
	/* Any per-thread setup can happen here; memcached_thread_init() will block until
	 * all threads have finished initializing.
	 */
	me->l = logger_create();
	me->lru_bump_buf = item_lru_bump_buf_create();
	if (me->l == NULL || me->lru_bump_buf == NULL) {
		abort();
	}
#endif
	register_thread_initialized();

	event_base_loop(me->base, 0);
	return NULL;
}

/*
 * Processes an incoming "handle a new connection" item. This is called when
 * input arrives on the libevent wakeup pipe.
 */
static void thread_libevent_process(int fd, short which, void *arg) {
	LIBEVENT_THREAD *me = (LIBEVENT_THREAD *)arg;
	CQ_ITEM *item;
	char buf[1];
	unsigned int timeout_fd;

	if (read(fd, buf, 1) != 1) {
		if (settings.verbose > 0)
			MY_LOGE( "Can't read from libevent pipe\n");
		return;
	}

	switch (buf[0]) {
	case 'c':
		item = cq_pop(me->new_conn_queue);

		if (NULL != item) {
			conn *c = conn_new(item->sfd, item->init_state, item->event_flags,
					item->read_buffer_size, item->transport, me->base);
			if (c == NULL) {
				if (IS_UDP(item->transport)) {
					MY_LOGE( "Can't listen for events on UDP socket\n");
					exit(1);
				} else {
					if (settings.verbose > 0) {
						MY_LOGE( "Can't listen for events on fd %d\n",
								item->sfd);
					}
					close(item->sfd);
				}
			} else {
				c->thread = me;
			}
			cqi_free(item);
		}
		break;
	case 'r':
		item = cq_pop(me->new_conn_queue);

		if (NULL != item) {
			conn_worker_readd(item->c);
			cqi_free(item);
		}
		break;
		/* we were told to pause and report in */
	case 'p':
		register_thread_initialized();
		break;
		/* a client socket timed out */
	case 't':
		if (read(fd, &timeout_fd, sizeof(timeout_fd)) != sizeof(timeout_fd)) {
			if (settings.verbose > 0)
				MY_LOGE( "Can't read timeout fd from libevent pipe\n");
			return;
		}
		conn_close_idle(conns[timeout_fd]);
		break;
	}
}

/* Which thread we assigned a connection to most recently. */
static int last_thread = -1;

/*
 * Dispatches a new connection to another thread. This is only ever called
 * from the main thread, either during initialization (for UDP) or because
 * of an incoming connection.
 */
void dispatch_conn_new(int sfd, enum conn_states init_state, int event_flags,
		int read_buffer_size, enum network_transport transport) {
	CQ_ITEM *item = cqi_new();
	char buf[1];
	if (item == NULL) {
		close(sfd);
		/* given that malloc failed this may also fail, but let's try */
		fprintf(stderr, "Failed to allocate memory for connection object\n");
		return;
	}

	int tid = (last_thread + 1) % settings.num_threads;

	LIBEVENT_THREAD *thread = threads + tid;

	last_thread = tid;

	item->sfd = sfd;
	item->init_state = init_state;
	item->event_flags = event_flags;
	item->read_buffer_size = read_buffer_size;
	item->transport = transport;

	cq_push(thread->new_conn_queue, item);

	buf[0] = 'c';
	if (write(thread->notify_send_fd, buf, 1) != 1) {
		perror("Writing to thread notify pipe");
	}
}

/*
 * Initializes the thread subsystem, creating various worker threads.
 *
 * nthreads  Number of worker event handler threads to spawn
 */
void conn_thread_init(int nthreads) {
	int i;

	pthread_mutex_init(&init_lock, NULL);
	pthread_cond_init(&init_cond, NULL);

	cq_freelist_init();

	threads = (LIBEVENT_THREAD *)calloc(nthreads, sizeof(LIBEVENT_THREAD));
	if (!threads) {
		MY_LOGE("Can't allocate thread descriptors");
		exit(1);
	}

	for (i = 0; i < nthreads; i++) {
		int fds[2];
		if (pipe(fds)) {
			MY_LOGE("Can't create notify pipe");
			exit(1);
		}

		threads[i].notify_receive_fd = fds[0];
		threads[i].notify_send_fd = fds[1];

		setup_thread(&threads[i]);
		/* Reserve three fds for the libevent base, and two for the pipe */
		stats_state.reserved_fds += 5;
	}

	/* Create threads after we've done all the libevent setup. */
	for (i = 0; i < nthreads; i++) {
		create_worker(worker_libevent, &threads[i]);
	}

	/* Wait for all the threads to set themselves up before returning. */
	pthread_mutex_lock(&init_lock);
	wait_for_thread_registration(nthreads);
	pthread_mutex_unlock(&init_lock);
}


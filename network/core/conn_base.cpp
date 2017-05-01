/*
 * Copyright (c) <2017>, Memcached
 * All rights reserved.
 * This source code copy from Memcached Open Source
 * format for Network bu Jeffrey..
 */
#include <vutils/Logger.h>
#include <network/core/conn_utils.h>
#include <network/core/conn_wrap.h>
#include <network/core/conn_base.h>
#include <network/core/conn_thread.h>
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "conn_base"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif

/** exported globals **/
struct stats stats;
struct stats_state stats_state;
struct settings settings;
time_t process_started; /* when the process was started */
conn **conns;
msg_callback_t* m_callback;
/** file scope variables **/
static conn *listen_conn = NULL;
static int max_fds;
static struct event_base *main_base;

static struct event clockevent;
/*
 * We keep the current time of day in a global variable that's updated by a
 * timer event. This saves us a bunch of time() system calls (we really only
 * need to get the time once a second, whereas there can be tens of thousands
 * of requests a second) and allows us to use server-start-relative timestamps
 * rather than absolute UNIX timestamps, a space savings on systems where
 * sizeof(time_t) > sizeof(unsigned int).
 */
volatile rel_time_t current_time;

/* Connection lock around accepting new connections */
pthread_mutex_t conn_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool allow_new_conns = true;
static void accept_new_conns(const bool do_accept);
static void event_handler(const int fd, const short which, void *arg);

/******************************* GLOBAL STATS ******************************/
/* Lock for global stats */
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

void STATS_LOCK() {
	pthread_mutex_lock(&stats_lock);
}

void STATS_UNLOCK() {
	pthread_mutex_unlock(&stats_lock);
}
/******************************* GLOBAL STATS ******************************/

const char *prot_text(enum protocol prot) {
	char *rv = "unknown";
	switch (prot) {
	case ascii_prot:
		rv = "ascii";
		break;
	case binary_prot:
		rv = "binary";
		break;
	case negotiating_prot:
		rv = "auto-negotiate";
		break;
	}
	return rv;
}

/**
 * Convert a state name to a human readable form.
 */
const char *state_text(enum conn_states state) {
	const char* const statenames[] = { "conn_listening", "conn_new_cmd",
			"conn_waiting", "conn_read", "conn_parse_cmd", "conn_write",
			"conn_nread", "conn_swallow", "conn_closing", "conn_mwrite",
			"conn_closed", "conn_watch" };
	return statenames[state];
}

static struct event maxconnsevent;
static void maxconns_handler(const int fd, const short which, void *arg) {
	struct timeval t;
	t.tv_sec = 0;
	t.tv_usec = 10000;
	if (fd == -42 || allow_new_conns == false) {
		/* reschedule in 10ms if we need to keep polling */
		evtimer_set(&maxconnsevent, maxconns_handler, 0);
		event_base_set(main_base, &maxconnsevent);
		evtimer_add(&maxconnsevent, &t);
	} else {
		evtimer_del(&maxconnsevent);
		accept_new_conns(true);
	}
}

/*
 * Sets a connection's current state in the state machine. Any special
 * processing that needs to happen on certain state transitions can
 * happen here.
 */
void conn_set_state(conn *c, enum conn_states state) {
	assert(c != NULL);
	assert(state >= conn_listening && state < conn_max_state);

	if (state != c->state) {
		if (settings.verbose > 2) {
			MY_LOGE( "%d: going from %s to %s\n",
					c->sfd, state_text(c->state), state_text(state));
		}
#if 0
		if (state == conn_write || state == conn_mwrite) {
			MEMCACHED_PROCESS_COMMAND_END(c->sfd, c->wbuf, c->wbytes);
		}
#endif
		c->state = state;
	}
}

/* Connection timeout thread bits */
static pthread_t conn_timeout_tid;

#define CONNS_PER_SLICE 100
#define TIMEOUT_MSG_SIZE (1 + sizeof(int))
/* libevent uses a monotonic clock when available for event scheduling. Aside
 * from jitter, simply ticking our internal timer here is accurate enough.
 * Note that users who are setting explicit dates for expiration times *must*
 * ensure their clocks are correct before starting memcached. */

static void clock_handler(const int fd, const short which, void *arg) {
	struct timeval t;
	t.tv_sec = 1;
	t.tv_usec = 0;
	static bool initialized = false;

#if defined(TARGET_ANDROID) || defined(TARGET_POSIX)
	static bool monotonic = false;
	static time_t monotonic_start;
#endif

	if (initialized) {
		/* only delete the event if it's actually there. */
		evtimer_del(&clockevent);
	} else {
		initialized = true;
		/* process_started is initialized to time() - 2. We initialize to 1 so
		 * flush_all won't underflow during tests. */
#if defined(TARGET_ANDROID) || defined(TARGET_POSIX)
		struct timespec ts;
		if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
			monotonic = true;
			monotonic_start = ts.tv_sec - ITEM_UPDATE_INTERVAL - 2;
		}
#endif
	}

	evtimer_set(&clockevent, clock_handler, 0);
	event_base_set(main_base, &clockevent);
	evtimer_add(&clockevent, &t);

#if defined(TARGET_ANDROID) || defined(TARGET_POSIX)
	if (monotonic) {
		struct timespec ts;
		if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		return;
		current_time = (rel_time_t) (ts.tv_sec - monotonic_start);
		return;
	}
#endif
	{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		current_time = (rel_time_t) (tv.tv_sec - process_started);
	}
}

static void *conn_timeout_thread(void *arg) {
	int i;
	conn *c;
	char buf[TIMEOUT_MSG_SIZE];
	rel_time_t oldest_last_cmd;
	int sleep_time;
	useconds_t timeslice = 1000000 / (max_fds / CONNS_PER_SLICE);

	while (1) {
		if (settings.verbose > 2)
			MY_LOGE( "idle timeout thread at top of connection list\n");

		oldest_last_cmd = current_time;

		for (i = 0; i < max_fds; i++) {
			if ((i % CONNS_PER_SLICE) == 0) {
				if (settings.verbose > 2)
					MY_LOGE( "idle timeout thread sleeping for %ulus\n",
							timeslice);
				usleep(timeslice);
			}

			if (!conns[i])
				continue;

			c = conns[i];

			if (!IS_TCP(c->transport))
				continue;

			if (c->state != conn_states::conn_new_cmd
					&& c->state != conn_states::conn_read)
				continue;

			if ((current_time - c->last_cmd_time) > settings.idle_timeout) {
				buf[0] = 't';
				memcpy(&buf[1], &i, sizeof(int));
				if (write(c->thread->notify_send_fd, buf,
						TIMEOUT_MSG_SIZE) != TIMEOUT_MSG_SIZE)
					MY_LOGE("Failed to write timeout to notify pipe");
			} else {
				if (c->last_cmd_time < oldest_last_cmd)
					oldest_last_cmd = c->last_cmd_time;
			}
		}

		/* This is the soonest we could have another connection time out */
		sleep_time = settings.idle_timeout - (current_time - oldest_last_cmd)
				+ 1;
		if (sleep_time <= 0)
			sleep_time = 1;

		if (settings.verbose > 2)
			MY_LOGE( "idle timeout thread finished pass, sleeping for %ds\n",
					sleep_time);
		usleep((useconds_t) sleep_time * 1000000);
	}

	return NULL;
}

static int start_conn_timeout_thread() {
	int ret;

	if (settings.idle_timeout == 0)
		return -1;

	if ((ret = pthread_create(&conn_timeout_tid, NULL, conn_timeout_thread,
			NULL)) != 0) {
		MY_LOGE( "Can't create idle connection timeout thread: %s\n",
				strerror(ret));
		return -1;
	}

	return 0;
}

bool update_event(conn *c, const int new_flags) {
	assert(c != NULL);

	struct event_base *base = c->event.ev_base;
	if (c->ev_flags == new_flags)
		return true;
	if (event_del(&c->event) == -1)
		return false;
	event_set(&c->event, c->sfd, new_flags, event_handler, (void *) c);
	event_base_set(base, &c->event);
	c->ev_flags = new_flags;
	if (event_add(&c->event, 0) == -1)
		return false;
	return true;
}

/*
 * Sets whether we are listening for new connections or not.
 */
static void do_accept_new_conns(const bool do_accept) {
	conn *next;

	for (next = listen_conn; next; next = next->next) {
		if (do_accept) {
			update_event(next, EV_READ | EV_PERSIST);
			if (listen(next->sfd, settings.backlog) != 0) {
				perror("listen");
			}
		} else {
			update_event(next, 0);
			if (listen(next->sfd, 0) != 0) {
				perror("listen");
			}
		}
	}

	if (do_accept) {
		struct timeval maxconns_exited;
		uint64_t elapsed_us;
		gettimeofday(&maxconns_exited, NULL);
		STATS_LOCK();
		elapsed_us = (maxconns_exited.tv_sec - stats.maxconns_entered.tv_sec)
				* 1000000
				+ (maxconns_exited.tv_usec - stats.maxconns_entered.tv_usec);
		stats.time_in_listen_disabled_us += elapsed_us;
		stats_state.accepting_conns = true;
		STATS_UNLOCK();
	} else {
		STATS_LOCK();
		stats_state.accepting_conns = false;
		gettimeofday(&stats.maxconns_entered, NULL);
		stats.listen_disabled_num++;
		STATS_UNLOCK();
		allow_new_conns = false;
		maxconns_handler(-42, 0, 0);
	}
}

/*
 * Sets whether or not we accept new connections.
 */
static void accept_new_conns(const bool do_accept) {
	pthread_mutex_lock(&conn_lock);
	do_accept_new_conns(do_accept);
	pthread_mutex_unlock(&conn_lock);
}

static void reset_cmd_handler(conn *c) {
	c->cmd = -1;
	c->substate = bin_no_state;
#if 0
	if(c->item != NULL) {
		item_remove(c->item);
		c->item = NULL;
	}
#endif
	conn_shrink(c);
	if (c->rbytes > 0) {
		conn_set_state(c, conn_parse_cmd);
	} else {
		conn_set_state(c, conn_waiting);
	}
}

static void drive_machine(conn *c) {
	bool stop = false;
	int sfd;
	socklen_t addrlen;
	struct sockaddr_storage addr;
	int nreqs = settings.reqs_per_event;
	int res;
	const char *str;
#ifdef HAVE_ACCEPT4
	static int use_accept4 = 1;
#else
	static int use_accept4 = 0;
#endif

	assert(c != NULL);

	while (!stop) {

		switch (c->state) {
		case conn_listening:
			addrlen = sizeof(addr);
#ifdef HAVE_ACCEPT4
			if (use_accept4) {
				sfd = accept4(c->sfd, (struct sockaddr *)&addr, &addrlen, SOCK_NONBLOCK);
			} else {
				sfd = accept(c->sfd, (struct sockaddr *)&addr, &addrlen);
			}
#else
			sfd = accept(c->sfd, (struct sockaddr *) &addr, &addrlen);
#endif
			if (sfd == -1) {
				if (use_accept4 && errno == ENOSYS) {
					use_accept4 = 0;
					continue;
				}
				MY_LOGE(use_accept4 ? "accept4()" : "accept()");
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					/* these are transient, so don't log anything */
					stop = true;
				} else if (errno == EMFILE) {
					if (settings.verbose > 0)
						MY_LOGE( "Too many open connections\n");
					accept_new_conns(false);
					stop = true;
				} else {
					MY_LOGE("accept()");
					stop = true;
				}
				break;
			}
			if (!use_accept4) {
				if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL) | O_NONBLOCK) < 0) {
					MY_LOGE("setting O_NONBLOCK");
					close(sfd);
					break;
				}
			}

			if (settings.maxconns_fast
					&& stats_state.curr_conns + stats_state.reserved_fds
							>= settings.maxconns - 1) {
				str = "ERROR Too many open connections\r\n";
				res = write(sfd, str, strlen(str));
				close(sfd);
				STATS_LOCK();
				stats.rejected_conns++;
				STATS_UNLOCK();
			} else {
				dispatch_conn_new(sfd, conn_new_cmd, EV_READ | EV_PERSIST,
						DATA_BUFFER_SIZE, c->transport);
			}
			stop = true;
			break;

		case conn_waiting:
			if (!update_event(c, EV_READ | EV_PERSIST)) {
				if (settings.verbose > 0)
					MY_LOGE( "Couldn't update event\n");
				conn_set_state(c, conn_closing);
				break;
			}

			conn_set_state(c, conn_read);
			stop = true;
			break;

		case conn_read:
			res = IS_UDP(c->transport) ? try_read_udp(c) : try_read_network(c);

			switch (res) {
			case READ_NO_DATA_RECEIVED:
				conn_set_state(c, conn_waiting);
				break;
			case READ_DATA_RECEIVED:
				conn_set_state(c, conn_parse_cmd);
				break;
			case READ_ERROR:
				conn_set_state(c, conn_closing);
				break;
			case READ_MEMORY_ERROR: /* Failed to allocate more memory */
				/* State already set by try_read_network */
				break;
			}
			break;

		case conn_parse_cmd:
			if (try_read_command(c) == 0) {
				/* wee need more data! */
				conn_set_state(c, conn_waiting);
			}
			break;

		case conn_new_cmd:
			/* Only process nreqs at a time to avoid starving other
			 connections */
			--nreqs;
			if (nreqs >= 0) {
				reset_cmd_handler(c);
			} else {
				pthread_mutex_lock(&c->thread->stats.mutex);
				c->thread->stats.conn_yields++;
				pthread_mutex_unlock(&c->thread->stats.mutex);
				if (c->rbytes > 0) {
					/* We have already read in data into the input buffer,
					 so libevent will most likely not signal read events
					 on the socket (unless more data is available. As a
					 hack we should just put in a request to write data,
					 because that should be possible ;-)
					 */
					if (!update_event(c, EV_WRITE | EV_PERSIST)) {
						if (settings.verbose > 0)
							MY_LOGE( "Couldn't update event\n");
						conn_set_state(c, conn_closing);
						break;
					}
				}
				stop = true;
			}
			break;

		case conn_nread:

			break;

		case conn_swallow:
			/* we are reading sbytes and throwing them away */
			if (c->sbytes == 0) {
				conn_set_state(c, conn_new_cmd);
				break;
			}

			/* first check if we have leftovers in the conn_read buffer */
			if (c->rbytes > 0) {
				int tocopy = c->rbytes > c->sbytes ? c->sbytes : c->rbytes;
				c->sbytes -= tocopy;
				c->rcurr += tocopy;
				c->rbytes -= tocopy;
				break;
			}

			/*  now try reading from the socket */
			res = read(c->sfd, c->rbuf,
					c->rsize > c->sbytes ? c->sbytes : c->rsize);
			if (res > 0) {
				pthread_mutex_lock(&c->thread->stats.mutex);
				c->thread->stats.bytes_read += res;
				pthread_mutex_unlock(&c->thread->stats.mutex);
				c->sbytes -= res;
				break;
			}
			if (res == 0) { /* end of stream */
				conn_set_state(c, conn_closing);
				break;
			}
			if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				if (!update_event(c, EV_READ | EV_PERSIST)) {
					if (settings.verbose > 0)
						fprintf(stderr, "Couldn't update event\n");
					conn_set_state(c, conn_closing);
					break;
				}
				stop = true;
				break;
			}
			/* otherwise we have a real error, on which we close the connection */
			if (settings.verbose > 0)
				fprintf(stderr, "Failed to read, and not due to blocking\n");
			conn_set_state(c, conn_closing);
			break;

			break;

		case conn_write:
			/*
			 * We want to write out a simple response. If we haven't already,
			 * assemble it into a msgbuf list (this will be a single-entry
			 * list for TCP or a two-entry list for UDP).
			 */
			if (c->iovused == 0 || (IS_UDP(c->transport) && c->iovused == 1)) {
				if (add_iov(c, c->wcurr, c->wbytes) != 0) {
					if (settings.verbose > 0)
						MY_LOGE( "Couldn't build response\n");
					conn_set_state(c, conn_closing);
					break;
				}
			}
			/* fall through... */
		case conn_mwrite:
			if (IS_UDP(c->transport) && c->msgcurr == 0
					&& build_udp_headers(c) != 0) {
				if (settings.verbose > 0)
					MY_LOGE( "Failed to build UDP headers\n");
				conn_set_state(c, conn_closing);
				break;
			}
			switch (transmit(c)) {
			case TRANSMIT_COMPLETE:
				if (c->state == conn_mwrite) {
#if 0
					conn_release_items(c);
#endif
					/* XXX:  I don't know why this wasn't the general case */
					if (c->protocol == binary_prot) {
						conn_set_state(c, c->write_and_go);
					} else {
						conn_set_state(c, conn_new_cmd);
					}
				} else if (c->state == conn_write) {
					if (c->write_and_free) {
						free(c->write_and_free);
						c->write_and_free = 0;
					}
					conn_set_state(c, c->write_and_go);
				} else {
					if (settings.verbose > 0)
						MY_LOGE( "Unexpected state %d\n", c->state);
					conn_set_state(c, conn_closing);
				}
				break;

			case TRANSMIT_INCOMPLETE:
			case TRANSMIT_HARD_ERROR:
				break; /* Continue in state machine. */

			case TRANSMIT_SOFT_ERROR:
				stop = true;
				break;
			}
			break;
			break;
		case conn_closing:
			if (IS_UDP(c->transport))
				conn_cleanup(c);
			else
				conn_close(c);
			stop = true;
			break;

		case conn_closed:
			/* This only happens if dormando is an idiot. */
			abort();
			break;

		case conn_watch:
			/* We handed off our connection to the logger thread. */
			stop = true;
			break;
		case conn_max_state:
			assert(false);
			break;
		}
	}

	return;
}

static void event_handler(const int fd, const short which, void *arg) {
	conn *c;

	c = (conn *) arg;
	assert(c != NULL);

	c->which = which;

	/* sanity */
	if (fd != c->sfd) {
		if (settings.verbose > 0)
			MY_LOGE( "Catastrophic: event fd doesn't match conn fd!\n");
		conn_close(c);
		return;
	}

	drive_machine(c);

	/* wait for next event */
	return;
}

/*************************************************************/
void conn_cleanup(conn *c) {
	assert(c != NULL);
#if 0
	conn_release_items(c);
#endif
	if (c->write_and_free) {
		free(c->write_and_free);
		c->write_and_free = 0;
	}
#if 0
	if (c->sasl_conn) {
		assert(settings.sasl);
		sasl_dispose(&c->sasl_conn);
		c->sasl_conn = NULL;
	}
#endif
	if (IS_UDP(c->transport)) {
		conn_set_state(c, conn_read);
	}
}

/*
 * Frees a connection.
 */
void conn_free(conn *c) {
	if (c) {
		assert(c != NULL);
		assert(c->sfd >= 0 && c->sfd < max_fds);

		conns[c->sfd] = NULL;
		if (c->hdrbuf)
			free(c->hdrbuf);
		if (c->msglist)
			free(c->msglist);
		if (c->rbuf)
			free(c->rbuf);
		if (c->wbuf)
			free(c->wbuf);
#if 0
		if (c->ilist)
		free(c->ilist);
		if (c->suffixlist)
		free(c->suffixlist);
#endif
		if (c->iov)
			free(c->iov);
		free(c);
	}
}

void conn_close(conn *c) {
	assert(c != NULL);

	/* delete the event, the socket and the conn */
	event_del(&c->event);

	if (settings.verbose > 1)
		MY_LOGE( "<%d connection closed.\n", c->sfd);

	conn_cleanup(c);

	conn_set_state(c, conn_closed);
	close(c->sfd);

	pthread_mutex_lock(&conn_lock);
	allow_new_conns = true;
	pthread_mutex_unlock(&conn_lock);

	STATS_LOCK();
	stats_state.curr_conns--;
	STATS_UNLOCK();

	return;
}

/*
 * Shrinks a connection's buffers if they're too big.  This prevents
 * periodic large "get" requests from permanently chewing lots of server
 * memory.
 *
 * This should only be called in between requests since it can wipe output
 * buffers!
 */
void conn_shrink(conn *c) {
	assert(c != NULL);

	if (IS_UDP(c->transport))
		return;

	if (c->rsize > READ_BUFFER_HIGHWAT && c->rbytes < DATA_BUFFER_SIZE) {
		char *newbuf;

		if (c->rcurr != c->rbuf)
			memmove(c->rbuf, c->rcurr, (size_t) c->rbytes);

		newbuf = (char *) realloc((void *) c->rbuf, DATA_BUFFER_SIZE);

		if (newbuf) {
			c->rbuf = newbuf;
			c->rsize = DATA_BUFFER_SIZE;
		}
		/* TODO check other branch... */
		c->rcurr = c->rbuf;
	}
#if 0
	if (c->isize > ITEM_LIST_HIGHWAT) {
		item **newbuf = (item**) realloc((void *) c->ilist,
				ITEM_LIST_INITIAL * sizeof(c->ilist[0]));
		if (newbuf) {
			c->ilist = newbuf;
			c->isize = ITEM_LIST_INITIAL;
		}
		/* TODO check error condition? */
	}
#endif
	if (c->msgsize > MSG_LIST_HIGHWAT) {
		struct msghdr *newbuf = (struct msghdr *) realloc((void *) c->msglist,
				MSG_LIST_INITIAL * sizeof(c->msglist[0]));
		if (newbuf) {
			c->msglist = newbuf;
			c->msgsize = MSG_LIST_INITIAL;
		}
		/* TODO check error condition? */
	}

	if (c->iovsize > IOV_LIST_HIGHWAT) {
		struct iovec *newbuf = (struct iovec *) realloc((void *) c->iov,
				IOV_LIST_INITIAL * sizeof(c->iov[0]));
		if (newbuf) {
			c->iov = newbuf;
			c->iovsize = IOV_LIST_INITIAL;
		}
		/* TODO check return value */
	}
}

void conn_close_idle(conn *c) {
	if (settings.idle_timeout > 0
			&& (current_time - c->last_cmd_time) > settings.idle_timeout) {
		if (c->state != conn_new_cmd && c->state != conn_read) {
			if (settings.verbose > 1)
				MY_LOGE( "fd %d wants to timeout, but isn't in read state",
						c->sfd);
			return;
		}

		if (settings.verbose > 1)
			MY_LOGE( "Closing idle fd %d\n", c->sfd);

		c->thread->stats.idle_kicks++;

		conn_set_state(c, conn_closing);
		drive_machine(c);
	}
}

/* bring conn back from a sidethread. could have had its event base moved. */
void conn_worker_readd(conn *c) {
	c->ev_flags = EV_READ | EV_PERSIST;
	event_set(&c->event, c->sfd, c->ev_flags, event_handler, (void *) c);
	event_base_set(c->thread->base, &c->event);
	c->state = conn_new_cmd;

	if (event_add(&c->event, 0) == -1) {
		MY_LOGE("event_add");
	}
}

conn *conn_new(const int sfd, enum conn_states init_state,
		const int event_flags, const int read_buffer_size,
		enum network_transport transport, struct event_base *base) {
	conn *c;

	assert(sfd >= 0 && sfd < max_fds);
	c = conns[sfd];

	if (NULL == c) {
		if (!(c = (conn *) calloc(1, sizeof(conn)))) {
			STATS_LOCK();
			stats.malloc_fails++;
			STATS_UNLOCK();
			MY_LOGE( "Failed to allocate connection object\n");
			return NULL;
		}

		c->rbuf = c->wbuf = 0;
#if 0
		c->ilist = 0;
		c->suffixlist = 0;
#endif
		c->iov = 0;
		c->msglist = 0;
		c->hdrbuf = 0;

		c->rsize = read_buffer_size;
		c->wsize = DATA_BUFFER_SIZE;
#if 0
		c->isize = ITEM_LIST_INITIAL;
		c->suffixsize = SUFFIX_LIST_INITIAL;
#endif
		c->iovsize = IOV_LIST_INITIAL;
		c->msgsize = MSG_LIST_INITIAL;
		c->hdrsize = 0;

		c->rbuf = (char *) malloc((size_t) c->rsize);
		c->wbuf = (char *) malloc((size_t) c->wsize);
#if 0
		c->ilist = (item **) malloc(sizeof(item *) * c->isize);
		c->suffixlist = (char **) malloc(sizeof(char *) * c->suffixsize);
#endif
		c->iov = (struct iovec *) malloc(sizeof(struct iovec) * c->iovsize);
		c->msglist = (struct msghdr *) malloc(
				sizeof(struct msghdr) * c->msgsize);

		if (c->rbuf == 0 || c->wbuf == 0/* || c->ilist == 0*/|| c->iov == 0
				|| c->msglist == 0/* || c->suffixlist == 0*/) {
			conn_free(c);
			STATS_LOCK();
			stats.malloc_fails++;
			STATS_UNLOCK();
			MY_LOGE( "Failed to allocate buffers for connection\n");
			return NULL;
		}

		STATS_LOCK();
		stats_state.conn_structs++;
		STATS_UNLOCK();

		c->sfd = sfd;
		conns[sfd] = c;
	}

	c->transport = transport;
	c->protocol = settings.binding_protocol;

	/* unix socket mode doesn't need this, so zeroed out.  but why
	 * is this done for every command?  presumably for UDP
	 * mode.  */
	if (!settings.socketpath) {
		c->request_addr_size = sizeof(c->request_addr);
	} else {
		c->request_addr_size = 0;
	}

	if (transport == tcp_transport && init_state == conn_new_cmd) {
		if (getpeername(sfd, (struct sockaddr *) &c->request_addr,
				&c->request_addr_size)) {
			MY_LOGE("getpeername");
			memset(&c->request_addr, 0, sizeof(c->request_addr));
		}
		struct sockaddr_in *request_addr =
				(struct sockaddr_in *) &c->request_addr;
		char clie_ip[BUFSIZ];
		MY_LOGD("client IP:%s,port:%d\n",
				inet_ntop(AF_INET,
						&request_addr->sin_addr.s_addr, clie_ip, sizeof(clie_ip)),
				ntohs(request_addr->sin_port));
	}

	if (settings.verbose > 1) {
		if (init_state == conn_listening) {
			MY_LOGE( "<%d server listening (%s)\n",
					sfd, prot_text(c->protocol));
		} else if (IS_UDP(transport)) {
			MY_LOGE( "<%d server listening (udp)\n", sfd);
		} else if (c->protocol == negotiating_prot) {
			MY_LOGE( "<%d new auto-negotiating client connection\n", sfd);
		} else if (c->protocol == ascii_prot) {
			MY_LOGE( "<%d new ascii client connection.\n", sfd);
		} else if (c->protocol == binary_prot) {
			MY_LOGE( "<%d new binary client connection.\n", sfd);
		} else {
			MY_LOGE( "<%d new unknown (%d) client connection\n",
					sfd, c->protocol);
			assert(false);
		}
	}

	c->state = init_state;
#if 0
	c->rlbytes = 0;
#endif
	c->cmd = -1;
	c->rbytes = c->wbytes = 0;
	c->wcurr = c->wbuf;
	c->rcurr = c->rbuf;
#if 0
	c->ritem = 0;
	c->icurr = c->ilist;
	c->suffixcurr = c->suffixlist;
	c->ileft = 0;
	c->suffixleft = 0;
#endif
	c->iovused = 0;
	c->msgcurr = 0;
	c->msgused = 0;
	c->authenticated = false;
	c->last_cmd_time = current_time; /* initialize for idle kicker */

	c->write_and_go = init_state;
	c->write_and_free = 0;
#if 0
	c->item = 0;
#endif
	c->noreply = false;

	event_set(&c->event, sfd, event_flags, event_handler, (void *) c);
	event_base_set(base, &c->event);
	c->ev_flags = event_flags;

	if (event_add(&c->event, 0) == -1) {
		MY_LOGE("event_add");
		return NULL;
	}

	STATS_LOCK();
	stats_state.curr_conns++;
	stats.total_conns++;
	STATS_UNLOCK();

	return c;
}

void conn_new_listen_add(const int sfd, enum network_transport transport) {
	conn *listen_conn_add = conn_new(sfd, conn_listening, EV_READ | EV_PERSIST,
			1, transport, main_base);
	if (!listen_conn_add) {
		MY_LOGE("failed to create listening connection\n");
		exit(EXIT_FAILURE);
	}
	if (transport != local_transport)
		listen_conn_add->next = listen_conn;
	listen_conn = listen_conn_add;
}

static void settings_init(void) {
	settings.access = 0700;
	settings.port = 11211;
	settings.udpport = 11211;
	/* By default this string should be NULL for getaddrinfo() */
	settings.inter = NULL;
	settings.maxconns = 1024; /* to limit connections*/
	settings.verbose = 3;
	settings.oldest_live = 0;/*last live time..*/
	settings.socketpath = NULL; /* by default, not using a unix socket */
	settings.num_threads = 10; /* N workers */
	settings.num_threads_per_udp = 0;
	settings.reqs_per_event = 20;
	settings.binding_protocol = ascii_prot;
	settings.backlog = 1024;
	settings.maxconns_fast = false;
	settings.idle_timeout = 0; /* disabled */
	settings.sasl = false;
}

/*
 * Initializes the connections array. We don't actually allocate connection
 * structures until they're needed, so as to avoid wasting memory when the
 * maximum connection count is much higher than the actual number of
 * connections.
 *
 * This does end up wasting a few pointers' worth of memory for FDs that are
 * used for things other than connections, but that's worth it in exchange for
 * being able to directly index the conns array by FD.
 */
static void conn_init(void) {
	/* We're unlikely to see an FD much higher than maxconns. */
	int next_fd = dup(1);
	int headroom = 10; /* account for extra unexpected open FDs */
	struct rlimit rl;

	max_fds = settings.maxconns + headroom + next_fd;

	/* But if possible, get the actual highest FD we can possibly ever see. */
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		max_fds = rl.rlim_max;
	} else {
		MY_LOGE("Failed to query maximum file descriptor; "
		"falling back to maxconns\n");
	}

	close(next_fd);

	if ((conns = (conn **) calloc(max_fds, sizeof(conn *))) == NULL) {
		MY_LOGE("Failed to allocate connection structures\n");
		/* This is unrecoverable so bail out early. */
		exit(1);
	}MY_LOGD("Allow max_fds = %d", max_fds);
}

static void stats_init(void) {
	memset(&stats, 0, sizeof(struct stats));
	memset(&stats_state, 0, sizeof(struct stats_state));
	stats_state.accepting_conns = true; /* assuming we start in this state. */
	/* make the time we started always be 2 seconds before we really
	 did, so time(0) - time.started is never zero.  if so, things
	 like 'settings.oldest_live' which act as booleans as well as
	 values are now false in boolean context... */
	process_started = time(0) - ITEM_UPDATE_INTERVAL - 2;
	//stats_prefix_init();
}

static void sig_handler(const int sig) {
	printf("Signal handled: %s.\n", strsignal(sig));
	exit(EXIT_SUCCESS);
}

static void usage(void) {
	MY_LOGD("-p <num>      TCP port number to listen on (default: 11211)\n"
			"-U <num>      UDP port number to listen on (default: 11211, 0 is off)\n"
			"-s <file>     UNIX socket path to listen on (disables network support)\n"
			"-a <mask>     access mask for UNIX socket, in octal (default: 0700)\n"
			"-l <addr>     interface to listen on (default: INADDR_ANY, all addresses)\n"
			"              <addr> may be specified as host:port. If you don't specify\n"
			"              a port number, the value you specified with -p or -U is\n"
			"              used. You may specify multiple addresses separated by comma\n"
			"              or by using -l multiple times\n"
			"-l 192.168.1.1  \n"
			"-d            run as a daemon\n"
			"-r            maximize core file limit\n"
			"-u <username> assume identity of <username> (only when run as root)\n"
			"-c <num>      max simultaneous connections (default: 1024)\n"
			"-v            verbose (print errors/warnings while in event loop)\n"
			"-vv           very verbose (also print client commands/reponses)\n"
			"-vvv          extremely verbose (also print internal state transitions)\n"
			"-h            print this help and exit\n");

	MY_LOGD("-t <num>      number of threads to use (default: 4)\n");

	MY_LOGD("-R            Maximum number of requests per event, limits the number of\n"
			"              requests process for a given connection to prevent \n"
			"              starvation (default: 20)\n");

	MY_LOGD("-b <num>      Set the backlog queue limit (default: 1024)\n");

	MY_LOGD("-B            Binding protocol - one of ascii, binary, or auto (default)\n");MY_LOGD("-o            Comma separated list of extended or experimental options\n"
			"              - maxconns_fast: immediately close new\n"
			"                connections if over maxconns limit\n"
			"          - idle_timeout: Timeout for idle connections\n");
	return;
}

int start_server(int argc, char **argv, msg_callback_t *callback) {
	int c;
	bool do_daemonize = false;
	bool tcp_specified = false;
	bool udp_specified = false;
	int maxcore = 0;
	struct rlimit rlim;
	int retval = EXIT_SUCCESS;
	static int *l_socket = NULL;
	/* udp socket */
	static int *u_socket = NULL;
	bool protocol_specified = false;

	char *subopts, *subopts_orig;
	char *subopts_value;
	enum {
		MAXCONNS_FAST = 0, IDLE_TIMEOUT, MAX_UNKNOW,
	};
	char * const subopts_tokens[] = { "maxconns_fast", "idle_timeout", NULL };

	/* handle SIGINT and SIGTERM */
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler); //request term process..

	if (!callback) {
		MY_LOGE("callback can not NULL");
		exit(EXIT_FAILURE);
	}
	m_callback = callback;
	/* init settings */
	settings_init();

	/* process arguments */
	while (-1 != (c = getopt(argc, argv,
			"a:" /* access mask for unix socket */
			"p:" /* TCP port number to listen on */
			"s:" /* unix socket path to listen on */
			"U:" /* UDP port number to listen on */
			"c:" /* max simultaneous connections */
			"hV" /* help, licence info, version */
			"d" /* daemon mode */
			"v" /* verbose */
			"l:" /* interface to listen on */
			"t:" /* threads */
			"r" /* maximize core file limit */
			"R:" /* max requests per event */
			"b:" /* backlog queue limit */
			"B:" /* Binding protocol */
			"o:" /* Extended generic options */
	))) {
		switch (c) {
		case 'a':
			/* access for unix domain socket, as octal mask (like chmod)*/
			settings.access = strtol(optarg, NULL, 8);
			break;
		case 'p':
			settings.port = atoi(optarg);
			tcp_specified = true;
			break;
		case 's':
			settings.socketpath = optarg;
			break;
		case 'U':
			settings.udpport = atoi(optarg);
			udp_specified = true;
			break;
		case 'c':
			settings.maxconns = atoi(optarg);
			if (settings.maxconns <= 0) {
				MY_LOGD("Maximum connections must be greater than 0\n");
				return 1;
			}
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case 'V':
			MY_LOGD("version:%d",1.0);
			exit(EXIT_SUCCESS);
		case 'd':
			do_daemonize = true;
			break;
		case 'v':
			settings.verbose++;
			break;
		case 'l': //interface to listen on
			if (settings.inter != NULL) {
				if (strstr(settings.inter, optarg) != NULL) {
					break;
				}
				size_t len = strlen(settings.inter) + strlen(optarg) + 2;
				char *p = (char *) malloc(len);
				if (p == NULL) {
					MY_LOGD("Failed to allocate memory\n");
					return 1;
				}
				snprintf(p, len, "%s,%s", settings.inter, optarg);
				free(settings.inter);
				settings.inter = p;
			} else {
				settings.inter = strdup(optarg);
				MY_LOGD("settings.inter = %s",settings.inter);
			}
			break;
		case 't':
			settings.num_threads = atoi(optarg);
			if (settings.num_threads <= 0) {
				MY_LOGD("Number of threads must be greater than 0\n");
				return 1;
			}
			/* There're other problems when you get above 64 threads.
			 * In the future we should portably detect # of cores for the
			 * default.
			 */
			if (settings.num_threads > 64) {
				MY_LOGD("WARNING: Setting a high number of worker"
						"threads is not recommended.\n"
						" Set this value to the number of cores in"
						" your machine or less.\n");
			}
			break;
		case 'T':
			settings.idle_timeout = atoi(subopts_value);
			break;
		case 'r':
			maxcore = 1;
			break;
		case 'R':
			settings.reqs_per_event = atoi(optarg);
			if (settings.reqs_per_event == 0) {
				MY_LOGD("Number of requests per event must be greater than 0\n");
				return 1;
			}
			break;

		case 'b':
			settings.backlog = atoi(optarg);
			break;
		case 'B':
			protocol_specified = true;
			if (strcmp(optarg, "auto") == 0) {
				settings.binding_protocol = negotiating_prot;
			} else if (strcmp(optarg, "binary") == 0) {
				settings.binding_protocol = binary_prot;
			} else if (strcmp(optarg, "ascii") == 0) {
				settings.binding_protocol = ascii_prot;
			} else {
				MY_LOGD("Invalid value for binding protocol: %s\n"
						" -- should be one of auto, binary, or ascii\n", optarg);
				exit(EX_USAGE);
			}
			break;

		case 'F':
			settings.maxconns_fast = true;
			break;

		default:
			MY_LOGD("Illegal argument \"%c\"\n", c);
			return 1;
		}
	}

	/*
	 * Use one workerthread to serve each UDP port if the user specified
	 * multiple ports
	 */
	if (settings.inter != NULL && strchr(settings.inter, ',')) {
		settings.num_threads_per_udp = 1;
	} else {
		settings.num_threads_per_udp = settings.num_threads;
	}

	if (settings.sasl) {
		if (!protocol_specified) {
			settings.binding_protocol = binary_prot;
		} else {
			if (settings.binding_protocol != binary_prot) {
				MY_LOGE(
						"ERROR: You cannot allow the ASCII protocol while using SASL.\n");
				exit(EX_USAGE);
			}
		}
	}

	if (tcp_specified && !udp_specified) {
		settings.udpport = settings.port;
	} else if (udp_specified && !tcp_specified) {
		settings.port = settings.udpport;
	}

	if (maxcore != 0) {
		struct rlimit rlim_new;
		/*
		 * First try raising to infinity; if that fails, try bringing
		 * the soft limit to the hard.
		 */
		if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
			rlim_new.rlim_cur = rlim_new.rlim_max = RLIM_INFINITY;
			if (setrlimit(RLIMIT_CORE, &rlim_new) != 0) {
				/* failed. try raising just to the old max */
				rlim_new.rlim_cur = rlim_new.rlim_max = rlim.rlim_max;
				(void) setrlimit(RLIMIT_CORE, &rlim_new);
			}
		}
		/*
		 * getrlimit again to see what we ended up with. Only fail if
		 * the soft limit ends up 0, because then no core files will be
		 * created at all.
		 */

		if ((getrlimit(RLIMIT_CORE, &rlim) != 0) || rlim.rlim_cur == 0) {
			MY_LOGE( "failed to ensure corefile creation\n");
			exit(EX_OSERR);
		}
	}

	/*
	 * If needed, increase rlimits to allow as many connections
	 * as needed.
	 */

	if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
		MY_LOGE( "failed to getrlimit number of files\n");
		exit(EX_OSERR);
	} else {
		rlim.rlim_cur = settings.maxconns;
		rlim.rlim_max = settings.maxconns;
		if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
			MY_LOGE(
					"failed to set rlimit for open files. Try starting as root or requesting smaller maxconns value.\n");
			exit(EX_OSERR);
		}
	}
#if 0
	/* lose root privileges if we have them */
	if (getuid() == 0 || geteuid() == 0) {
		if (username == 0 || *username == '\0') {
			MY_LOGE( "can't run as root without the -u switch\n");
			exit (EX_USAGE);
		}
		if ((pw = getpwnam(username)) == 0) {
			MY_LOGE( "can't find the user %s to switch to\n", username);
			exit (EX_NOUSER);
		}
		if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
			MY_LOGE( "failed to assume identity of user %s\n", username);
			exit (EX_OSERR);
		}
	}

	/* Initialize Sasl if -S was specified */
	if (settings.sasl) {
		init_sasl();
	}

	/* daemonize if requested */
	/* if we want to ensure our ability to dump core, don't chdir to / */
	if (do_daemonize) {
		if (sigignore(SIGHUP) == -1) {
			perror("Failed to ignore SIGHUP");
		}
		if (daemonize(maxcore, settings.verbose) == -1) {
			MY_LOGE( "failed to daemon() in order to daemonize\n");
			exit(EXIT_FAILURE);
		}
	}
#endif

	/* initialize main thread libevent instance */
	main_base = event_init();

	stats_init();

	conn_init();
	/*
	 * ignore SIGPIPE signals; we can use errno == EPIPE if we
	 * need that information
	 */
	if (sigignore(SIGPIPE) == -1) {
		perror("failed to ignore SIGPIPE; sigaction");
		exit(EX_OSERR);
	}
	/* start up worker threads if MT mode */
	conn_thread_init(settings.num_threads);

	if (settings.idle_timeout && start_conn_timeout_thread() == -1) {
		exit(EXIT_FAILURE);
	}

	/* initialise clock event */
	clock_handler(0, 0, 0);

	/* create unix mode sockets after dropping privileges */
	if (settings.socketpath != NULL) {
		errno = 0;
		if (server_socket_unix(settings.socketpath, settings.access)) {
			vperror("failed to listen on UNIX socket: %s", settings.socketpath);
			exit(EX_OSERR);
		}
	}

	/* create the listening socket, bind it, and init */
	if (settings.socketpath == NULL) {
		const char *portnumber_filename = getenv("VMODULE_PORT_FILENAME");
		char *temp_portnumber_filename = NULL;
		size_t len;
		FILE *portnumber_file = NULL;

		if (portnumber_filename != NULL) {
			len = strlen(portnumber_filename) + 4 + 1;
			temp_portnumber_filename = (char *) malloc(len);
			snprintf(temp_portnumber_filename, len, "%s.lck",
					portnumber_filename);

			portnumber_file = fopen(temp_portnumber_filename, "a");
			if (portnumber_file == NULL) {
				MY_LOGE( "Failed to open \"%s\": %s\n",
						temp_portnumber_filename, strerror(errno));
			}
		}

		errno = 0;
		if (settings.port
				&& server_sockets(settings.port, tcp_transport,
						portnumber_file)) {
			vperror("failed to listen on TCP port %d", settings.port);
			exit(EX_OSERR);
		}

		/*
		 * initialization order: first create the listening sockets
		 * (may need root on low ports), then drop root if needed,
		 * then daemonise if needed, then init libevent (in some cases
		 * descriptors created by libevent wouldn't survive forking).
		 */

		/* create the UDP listening socket and bind it */errno = 0;
		if (settings.udpport
				&& server_sockets(settings.udpport, udp_transport,
						portnumber_file)) {
			vperror("failed to listen on UDP port %d", settings.udpport);
			exit(EX_OSERR);
		}

		if (portnumber_file) {
			fclose(portnumber_file);
			rename(temp_portnumber_filename, portnumber_filename);
			free(temp_portnumber_filename);
		}
	}

	/* Give the sockets a moment to open. I know this is dumb, but the error
	 * is only an advisory.
	 */
	usleep(1000);
	if (stats_state.curr_conns + stats_state.reserved_fds
			>= settings.maxconns - 1) {
		MY_LOGE( "Maxconns setting is too low, use -c to increase.\n");
		exit(EXIT_FAILURE);
	}
#if 0
	if (pid_file != NULL) {
		save_pid(pid_file);
	}
#endif

	/* enter the event loop */
	if (event_base_loop(main_base, 0) != 0) {
		retval = EXIT_FAILURE;
	}
#if 0
	/* remove the PID file if we're a daemon */
	if (do_daemonize)
	remove_pidfile(pid_file);
#endif
	/* Clean up strdup() call for bind() address */
	if (settings.inter)
		free(settings.inter);
	if (l_socket)
		free(l_socket);
	if (u_socket)
		free(u_socket);

	return retval;
}


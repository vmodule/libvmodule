/*
 * conn_base.h
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
#ifndef CONN_BASE_H_
#define CONN_BASE_H_
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sysexits.h>
#include <netinet/in.h>
#include <event.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <network/core/protocol_binary.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inlined from memcached.h - should go into sub header */
typedef unsigned int rel_time_t;

/** Maximum length of a key. */
#define KEY_MAX_LENGTH 250

/** Size of an incr buf. */
#define INCR_MAX_STORAGE_LEN 24

#define DATA_BUFFER_SIZE 2048
#define UDP_READ_BUFFER_SIZE 65536
#define UDP_MAX_PAYLOAD_SIZE 1400
#define UDP_HEADER_SIZE 8
#define MAX_SENDBUF_SIZE (256 * 1024 * 1024)
/* Up to 3 numbers (2 32bit, 1 64bit), spaces, newlines, null 0 */
#define SUFFIX_SIZE 50

/** Initial size of list of items being returned by "get". */
#define ITEM_LIST_INITIAL 200

/** Initial size of list of CAS suffixes appended to "gets" lines. */
#define SUFFIX_LIST_INITIAL 100

/** Initial size of the sendmsg() scatter/gather array. */
#define IOV_LIST_INITIAL 400

/** Initial number of sendmsg() argument structures to allocate. */
#define MSG_LIST_INITIAL 10

/** High water marks for buffer shrinking */
#define READ_BUFFER_HIGHWAT 8192
#define ITEM_LIST_HIGHWAT 400
#define IOV_LIST_HIGHWAT 600
#define MSG_LIST_HIGHWAT 100

/* Binary protocol stuff */
#define MIN_BIN_PKT_LENGTH 16
#define BIN_PKT_HDR_WORDS (MIN_BIN_PKT_LENGTH/sizeof(uint32_t))

/* Initial power multiplier for the hash table */
#define HASHPOWER_DEFAULT 16

/*
 * We only reposition items in the LRU queue if they haven't been repositioned
 * in this many seconds. That saves us from churning on frequently-accessed
 * items.
 */
#define ITEM_UPDATE_INTERVAL 60

/*
 * NOTE: If you modify this table you _MUST_ update the function state_text
 */
/**
 * Possible states of a connection.
 */
enum conn_states {
	conn_listening, /**< the socket which listens for connections */
	conn_new_cmd, /**< Prepare connection for next command */
	conn_waiting, /**< waiting for a readable socket */
	conn_read, /**< reading in a command line */
	conn_parse_cmd, /**< try to parse a command from the input buffer */
	conn_write, /**< writing out a simple response */
	conn_nread, /**< reading in a fixed number of bytes */
	conn_swallow, /**< swallowing unnecessary bytes w/o storing */
	conn_closing, /**< closing this connection */
	conn_mwrite, /**< writing out many items sequentially */
	conn_closed, /**< connection is closed */
	conn_watch, /**< held by the logger thread as a watcher */
	conn_max_state /**< Max state value (used for assertion) */
};

enum bin_substates {
	bin_no_state,
	bin_reading_set_header,
	bin_reading_cas_header,
	bin_read_set_value,
	bin_reading_get_key,
	bin_reading_stat,
	bin_reading_del_header,
	bin_reading_incr_header,
	bin_read_flush_exptime,
	bin_reading_sasl_auth,
	bin_reading_sasl_auth_data,
	bin_reading_touch_key,
};

enum protocol {
	ascii_prot = 3, /* arbitrary value. */
	binary_prot, negotiating_prot /* Discovering the protocol */
};

enum network_transport {
	local_transport, /* Unix sockets*/
	tcp_transport, udp_transport
};

#define IS_TCP(x) (x == tcp_transport)
#define IS_UDP(x) (x == udp_transport)

/**
 * Global stats. Only resettable stats should go into this structure.
 */
struct stats {
	uint64_t total_items;
	uint64_t total_conns;
	uint64_t rejected_conns;
	uint64_t malloc_fails;
	uint64_t listen_disabled_num;
	uint64_t time_in_listen_disabled_us; /* elapsed time in microseconds while server unable to process new connections */
	struct timeval maxconns_entered; /* last time maxconns entered */
};

/**
 * Global "state" stats. Reflects state that shouldn't be wiped ever.
 * Ordered for some cache line locality for commonly updated counters.
 */
struct stats_state {
	uint64_t curr_items;
	uint64_t curr_bytes;
	uint64_t curr_conns;
	unsigned int conn_structs;
	unsigned int reserved_fds;
	bool accepting_conns; /* whether we are currently accepting */
};

/* When adding a setting, be sure to update process_stat_settings */
/**
 * Globally accessible settings as derived from the commandline.
 */
struct settings {
	int maxconns;
	int port;
	int udpport;
	char *inter;
	int verbose;
	rel_time_t oldest_live; /* ignore existing items older than this */
	char *socketpath; /* path to unix socket if using local socket */
	int access; /* access mask (a la chmod) for unix domain socket */
	int num_threads; /* number of worker (without dispatcher) libevent threads to run */
	int num_threads_per_udp; /* number of worker threads serving each udp socket */
	int reqs_per_event; /* Maximum number of io to process on each io-event. */
	bool use_cas;
	enum protocol binding_protocol;
	int backlog;
	bool sasl; /* SASL on/off */
	bool maxconns_fast; /* Whether or not to early close connections */
	int idle_timeout; /* Number of seconds to let connections idle */
};

extern struct stats stats;
extern struct stats_state stats_state;
extern time_t process_started;
extern struct settings settings;

struct LIBEVENT_THREAD;
/**
 * The structure representing a connection into memcached.
 */
typedef struct conn conn;
struct conn {
	int sfd;
#if 0
	sasl_conn_t *sasl_conn;
#endif
	bool authenticated;
	enum conn_states state;
	enum bin_substates substate;
	rel_time_t last_cmd_time;
	struct event event;
	short ev_flags;
	short which; /** which events were just triggered */

	char *rbuf; /** buffer to read commands into */
	char *rcurr; /** but if we parsed some already, this is where we stopped */
	int rsize; /** total allocated size of rbuf */
	int rbytes; /** how much data, starting from rcur, do we have unparsed */

	char *wbuf;
	char *wcurr;
	int wsize;
	int wbytes;
	/** which state to go into after finishing current write */
	enum conn_states write_and_go;
	void *write_and_free; /** free this memory after finishing writing */
#if 0
	char *ritem; /** when we read in an item's value, it goes here */
	int rlbytes;

	/* data for the nread state */

	/**
	 * item is used to hold an item structure created after reading the command
	 * line of set/add/replace commands, but before we finished reading the actual
	 * data. The data is read into ITEM_data(item) to avoid extra copying.
	 */

	void *item; /* for commands set/add/replace  */
#endif
	/* data for the swallow state */
	int sbytes; /* how many bytes to swallow */

	/* data for the mwrite state */
	struct iovec *iov;
	int iovsize; /* number of elements allocated in iov[] */
	int iovused; /* number of elements used in iov[] */

	struct msghdr *msglist;
	int msgsize; /* number of elements allocated in msglist[] */
	int msgused; /* number of elements used in msglist[] */
	int msgcurr; /* element in msglist[] being transmitted now */
	int msgbytes; /* number of bytes in current msg */
#if 0
	item **ilist; /* list of items to write out */
	int isize;
	item **icurr;
	int ileft;

	char **suffixlist;
	int suffixsize;
	char **suffixcurr;
	int suffixleft;
#endif
	enum protocol protocol; /* which protocol this connection speaks */
	enum network_transport transport; /* what transport is used by this connection */

	/* data for UDP clients */
	int request_id; /* Incoming UDP request ID, if this is a UDP "connection" */
	struct sockaddr_in6 request_addr; /* udp: Who sent the most recent request */
	socklen_t request_addr_size;
	unsigned char *hdrbuf; /* udp packet headers */
	int hdrsize; /* number of headers' worth of space is allocated */

	bool noreply; /* True if the reply should not be sent. */
	/* current stats command */
	struct {
		char *buffer;
		size_t size;
		size_t offset;
	} stats;
	/* Binary protocol stuff */
	/* This is where the binary header goes */
	protocol_binary_request_header binary_header;
	uint64_t cas; /* the cas to return */
	short cmd; /* current command being processed */
	int opaque;
	int keylen;
	conn *next; /* Used for generating a list of conn structures */
	LIBEVENT_THREAD *thread; /* Pointer to the thread object serving this connection */
};

typedef struct msg_callback{
	virtual ~msg_callback(){};
	virtual void onBinaryEventDispatch(conn *c) = 0;
	virtual void onAsciiEventDispatch(conn *c) = 0;
} msg_callback_t;

/* array of conn structures, indexed by file descriptor */
extern conn **conns;
extern msg_callback_t* m_callback;

/* current time of day (updated periodically) */
extern volatile rel_time_t current_time;

void STATS_LOCK();
void STATS_UNLOCK();

/**
 * Convert a state name to a human readable form.
 */
const char *state_text(enum conn_states state);
void conn_set_state(conn *c, enum conn_states state);
const char *prot_text(enum protocol prot);
bool update_event(conn *c, const int new_flags);
void conn_cleanup(conn *c);
void conn_free(conn *c);
void conn_close(conn *c);
void conn_shrink(conn *c);
/******************start call back in thread_libevent_process **/
conn *conn_new(const int sfd, const enum conn_states init_state,
		const int event_flags, const int read_buffer_size,
		enum network_transport transport, struct event_base *base);
void conn_new_listen_add(const int sfd, enum network_transport transport);

void conn_worker_readd(conn *c);
void conn_close_idle(conn *c);
int start_server(int argc, char **argv,
		msg_callback_t *callback);
/******************start call back in thread_libevent_process **/
#ifdef __cplusplus
}
#endif

#endif

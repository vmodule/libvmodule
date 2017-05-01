/*
 * conn_wrap.cpp
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
#include <network/core/conn_wrap.h>
#include <network/core/conn_utils.h>
#include <network/core/conn_thread.h>
#include <vutils/Logger.h>
#include <sys/stat.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "conn_wrap"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif
# define IOV_MAX 1024

/*
 * Transmit the next chunk of data from our list of msgbuf structures.
 *
 * Returns:
 *   TRANSMIT_COMPLETE   All done writing.
 *   TRANSMIT_INCOMPLETE More data remaining to write.
 *   TRANSMIT_SOFT_ERROR Can't write any more right now.
 *   TRANSMIT_HARD_ERROR Can't write (c->state is set to conn_closing)
 */
enum transmit_result transmit(conn *c) {
	assert(c != NULL);

	if (c->msgcurr < c->msgused && c->msglist[c->msgcurr].msg_iovlen == 0) {
		/* Finished writing the current msg; advance to the next. */
		c->msgcurr++;
	}
	if (c->msgcurr < c->msgused) {
		ssize_t res;
		struct msghdr *m = &c->msglist[c->msgcurr];

		res = sendmsg(c->sfd, m, 0);
		if (res > 0) {
			pthread_mutex_lock(&c->thread->stats.mutex);
			c->thread->stats.bytes_written += res;
			pthread_mutex_unlock(&c->thread->stats.mutex);

			/* We've written some of the data. Remove the completed
			 iovec entries from the list of pending writes. */
			while (m->msg_iovlen > 0 && res >= m->msg_iov->iov_len) {
				res -= m->msg_iov->iov_len;
				m->msg_iovlen--;
				m->msg_iov++;
			}

			/* Might have written just part of the last iovec entry;
			 adjust it so the next write will do the rest. */
			if (res > 0) {
				m->msg_iov->iov_base = (caddr_t) m->msg_iov->iov_base + res;
				m->msg_iov->iov_len -= res;
			}
			return TRANSMIT_INCOMPLETE;
		}
		if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			if (!update_event(c, EV_WRITE | EV_PERSIST)) {
				if (settings.verbose > 0)
					fprintf(stderr, "Couldn't update event\n");
				conn_set_state(c, conn_closing);
				return TRANSMIT_HARD_ERROR;
			}
			return TRANSMIT_SOFT_ERROR;
		}
		/* if res == 0 or res == -1 and error is not EAGAIN or EWOULDBLOCK,
		 we have a real error, on which we close the connection */
		if (settings.verbose > 0)
			perror("Failed to write, and not due to blocking");

		if (IS_UDP(c->transport))
			conn_set_state(c, conn_read);
		else
			conn_set_state(c, conn_closing);
		return TRANSMIT_HARD_ERROR;
	} else {
		return TRANSMIT_COMPLETE;
	}
}

/*
 * Ensures that there is room for another struct iovec in a connection's
 * iov list.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
int ensure_iov_space(conn *c) {
	assert(c != NULL);

	if (c->iovused >= c->iovsize) {
		int i, iovnum;
		struct iovec *new_iov = (struct iovec *) realloc(c->iov,
				(c->iovsize * 2) * sizeof(struct iovec));
		if (!new_iov) {
			STATS_LOCK();
			stats.malloc_fails++;
			STATS_UNLOCK();
			return -1;
		}
		c->iov = new_iov;
		c->iovsize *= 2;

		/* Point all the msghdr structures at the new list. */
		for (i = 0, iovnum = 0; i < c->msgused; i++) {
			c->msglist[i].msg_iov = &c->iov[iovnum];
			iovnum += c->msglist[i].msg_iovlen;
		}
	}

	return 0;
}

/*
 * Adds data to the list of pending data that will be written out to a
 * connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 * Note: This is a hot path for at least ASCII protocol. While there is
 * redundant code in splitting TCP/UDP handling, any reduction in steps has a
 * large impact for TCP connections.
 */

int add_iov(conn *c, const void *buf, int len) {
	struct msghdr *m;
	int leftover;

	assert(c != NULL);

	if (IS_UDP(c->transport)) {
		do {
			m = &c->msglist[c->msgused - 1];

			/*
			 * Limit UDP packets to UDP_MAX_PAYLOAD_SIZE bytes.
			 */

			/* We may need to start a new msghdr if this one is full. */
			if (m->msg_iovlen == IOV_MAX
					|| (c->msgbytes >= UDP_MAX_PAYLOAD_SIZE)) {
				add_msghdr(c);
				m = &c->msglist[c->msgused - 1];
			}

			if (ensure_iov_space(c) != 0)
				return -1;

			/* If the fragment is too big to fit in the datagram, split it up */
			if (len + c->msgbytes > UDP_MAX_PAYLOAD_SIZE) {
				leftover = len + c->msgbytes - UDP_MAX_PAYLOAD_SIZE;
				len -= leftover;
			} else {
				leftover = 0;
			}

			m = &c->msglist[c->msgused - 1];
			m->msg_iov[m->msg_iovlen].iov_base = (void *) buf;
			m->msg_iov[m->msg_iovlen].iov_len = len;

			c->msgbytes += len;
			c->iovused++;
			m->msg_iovlen++;

			buf = ((char *) buf) + len;
			len = leftover;
		} while (leftover > 0);
	} else {
		/* Optimized path for TCP connections */
		m = &c->msglist[c->msgused - 1];
		if (m->msg_iovlen == IOV_MAX) {
			add_msghdr(c);
			m = &c->msglist[c->msgused - 1];
		}

		if (ensure_iov_space(c) != 0)
			return -1;

		m->msg_iov[m->msg_iovlen].iov_base = (void *) buf;
		m->msg_iov[m->msg_iovlen].iov_len = len;
		c->msgbytes += len;
		c->iovused++;
		m->msg_iovlen++;
	}

	return 0;
}

/*
 * Adds a message header to a connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
int add_msghdr(conn *c) {
	struct msghdr *msg;

	assert(c != NULL);

	if (c->msgsize == c->msgused) {
		msg = (struct msghdr *) realloc(c->msglist,
				c->msgsize * 2 * sizeof(struct msghdr));
		if (!msg) {
			STATS_LOCK();
			stats.malloc_fails++;
			STATS_UNLOCK();
			return -1;
		}
		c->msglist = msg;
		c->msgsize *= 2;
	}

	msg = c->msglist + c->msgused;

	/* this wipes msg_iovlen, msg_control, msg_controllen, and
	 msg_flags, the last 3 of which aren't defined on solaris: */
	memset(msg, 0, sizeof(struct msghdr));

	msg->msg_iov = &c->iov[c->iovused];

	if (IS_UDP(c->transport) && c->request_addr_size > 0) {
		msg->msg_name = &c->request_addr;
		msg->msg_namelen = c->request_addr_size;
	}

	c->msgbytes = 0;
	c->msgused++;

	if (IS_UDP(c->transport)) {
		/* Leave room for the UDP header, which we'll fill in later. */
		return add_iov(c, NULL, UDP_HEADER_SIZE);
	}

	return 0;
}

/* set up a connection to write a buffer then free it, used for stats */
void write_and_free(conn *c, char *buf, int bytes) {
	if (buf) {
		c->write_and_free = buf;
		c->wcurr = buf;
		c->wbytes = bytes;
		conn_set_state(c, conn_write);
		c->write_and_go = conn_new_cmd;
	} else {
		out_of_memory(c, "SERVER_ERROR out of memory writing stats");
	}
}

/*
 * Constructs a set of UDP headers and attaches them to the outgoing messages.
 */
int build_udp_headers(conn *c) {
	int i;
	unsigned char *hdr;

	assert(c != NULL);

	if (c->msgused > c->hdrsize) {
		void *new_hdrbuf;
		if (c->hdrbuf) {
			new_hdrbuf = realloc(c->hdrbuf, c->msgused * 2 * UDP_HEADER_SIZE);
		} else {
			new_hdrbuf = malloc(c->msgused * 2 * UDP_HEADER_SIZE);
		}

		if (!new_hdrbuf) {
			STATS_LOCK();
			stats.malloc_fails++;
			STATS_UNLOCK();
			return -1;
		}
		c->hdrbuf = (unsigned char *) new_hdrbuf;
		c->hdrsize = c->msgused * 2;
	}

	hdr = c->hdrbuf;
	for (i = 0; i < c->msgused; i++) {
		c->msglist[i].msg_iov[0].iov_base = (void*) hdr;
		c->msglist[i].msg_iov[0].iov_len = UDP_HEADER_SIZE;
		*hdr++ = c->request_id / 256;
		*hdr++ = c->request_id % 256;
		*hdr++ = i / 256;
		*hdr++ = i % 256;
		*hdr++ = c->msgused / 256;
		*hdr++ = c->msgused % 256;
		*hdr++ = 0;
		*hdr++ = 0;
		assert(
				(void *) hdr == (caddr_t)c->msglist[i].msg_iov[0].iov_base + UDP_HEADER_SIZE);
	}

	return 0;
}

void out_string(conn *c, const char *str) {
	size_t len;

	assert(c != NULL);

	if (c->noreply) {
		if (settings.verbose > 1)
			MY_LOGE( ">%d NOREPLY %s\n", c->sfd, str);
		c->noreply = false;
		conn_set_state(c, conn_new_cmd);
		return;
	}

	if (settings.verbose > 1)
		MY_LOGE( ">%d %s\n", c->sfd, str);

	/* Nuke a partial output... */
	c->msgcurr = 0;
	c->msgused = 0;
	c->iovused = 0;
	add_msghdr(c);

	len = strlen(str);
	if ((len + 2) > c->wsize) {
		/* ought to be always enough. just fail for simplicity */
		str = "SERVER_ERROR output line too long";
		len = strlen(str);
	}

	memcpy(c->wbuf, str, len);
	memcpy(c->wbuf + len, "\r\n", 2);
	c->wbytes = len + 2;
	c->wcurr = c->wbuf;

	conn_set_state(c, conn_write);
	c->write_and_go = conn_new_cmd;
	return;
}

/*
 * Outputs a protocol-specific "out of memory" error. For ASCII clients,
 * this is equivalent to out_string().
 */
void out_of_memory(conn *c, char *ascii_error) {
	const static char error_prefix[] = "SERVER_ERROR ";
	const static int error_prefix_len = sizeof(error_prefix) - 1;
#if 0
	if (c->protocol == binary_prot) {
		/* Strip off the generic error prefix; it's irrelevant in binary */
		if (!strncmp(ascii_error, error_prefix, error_prefix_len)) {
			ascii_error += error_prefix_len;
		}
		write_bin_error(c, PROTOCOL_BINARY_RESPONSE_ENOMEM, ascii_error, 0);
	} else {
		out_string(c, ascii_error);
	}
#else
	out_string(c, ascii_error);
#endif
}

/*
 * if we have a complete line in the buffer, process it.
 */
int try_read_command(conn *c) {
	assert(c != NULL);
	assert(c->rcurr <= (c->rbuf + c->rsize));
	assert(c->rbytes > 0);

	if (c->protocol == negotiating_prot || c->transport == udp_transport) {
		if ((unsigned char) c->rbuf[0] == (unsigned char) PROTOCOL_BINARY_REQ) {
			c->protocol = binary_prot;
		} else {
			c->protocol = ascii_prot;
		}

		if (settings.verbose > 1) {
			MY_LOGE( "%d: Client using the %s protocol\n",
					c->sfd, prot_text(c->protocol));
		}
	}

	if (c->protocol == binary_prot) {
		/* Do we have the complete packet header? */
		if (c->rbytes < sizeof(c->binary_header)) {
			/* need more data! */
			return 0;
		} else {
#ifdef NEED_ALIGN
			if (((long) (c->rcurr)) % 8 != 0) {
				/* must realign input buffer */
				memmove(c->rbuf, c->rcurr, c->rbytes);
				c->rcurr = c->rbuf;
				if (settings.verbose > 1) {
					MY_LOGE( "%d: Realign input buffer\n", c->sfd);
				}
			}
#endif
			protocol_binary_request_header* req;
			req = (protocol_binary_request_header*) c->rcurr;

			if (settings.verbose > 1) {
				/* Dump the packet before we convert it to host order */
				int ii;
				MY_LOGE( "<%d Read binary protocol data:", c->sfd);
				for (ii = 0; ii < sizeof(req->bytes); ++ii) {
					if (ii % 4 == 0) {
						MY_LOGE( "\n<%d   ", c->sfd);
					}
					MY_LOGE( " 0x%02x", req->bytes[ii]);
				}
				MY_LOGE( "\n");
			}

			c->binary_header = *req;
			c->binary_header.request.keylen = ntohs(req->request.keylen);
			c->binary_header.request.bodylen = ntohl(req->request.bodylen);
			c->binary_header.request.cas = ntohll(req->request.cas);

			if (c->binary_header.request.magic != PROTOCOL_BINARY_REQ) {
				if (settings.verbose) {
					MY_LOGE( "Invalid magic:  %x\n",
							c->binary_header.request.magic);
				}
				conn_set_state(c, conn_closing);
				return -1;
			}

			c->msgcurr = 0;
			c->msgused = 0;
			c->iovused = 0;
			if (add_msghdr(c) != 0) {
				out_of_memory(c,
						"SERVER_ERROR Out of memory allocating headers");
				return 0;
			}

			c->cmd = c->binary_header.request.opcode;
			c->keylen = c->binary_header.request.keylen;
			c->opaque = c->binary_header.request.opaque;
			/* clear the returned cas value */
			c->cas = 0;

			if (m_callback)
				m_callback->onBinaryEventDispatch(c);
			//dispatch_bin_command(c);

			c->rbytes -= sizeof(c->binary_header);
			c->rcurr += sizeof(c->binary_header);
		}
	} else {

		char *el, *cont;

		if (c->rbytes == 0)
			return 0;

		el = (char *) memchr((void *) c->rcurr, '\n', c->rbytes);
		if (!el) {
#if 0
			if (c->rbytes > 1024) {
				/*
				 * We didn't have a '\n' in the first k. This _has_ to be a
				 * large multiget, if not we should just nuke the connection.
				 */
				char *ptr = c->rcurr;
				while (*ptr == ' ') { /* ignore leading whitespaces */
					++ptr;
				}

				if (ptr - c->rcurr > 100
						|| (strncmp(ptr, "get ", 4) && strncmp(ptr, "gets ", 5))) {

					conn_set_state(c, conn_closing);
					return 1;
				}
			}
#endif
			return 0;
		}
		cont = el + 1;
		if ((el - c->rcurr) > 1 && *(el - 1) == '\r') {
			el--;
		}
		*el = '\0';

		assert(cont <= (c->rcurr + c->rbytes));

		c->last_cmd_time = current_time;
		if (m_callback)
			m_callback->onAsciiEventDispatch(c);

		c->rbytes -= (cont - c->rcurr);
		c->rcurr = cont;
		assert(c->rcurr <= (c->rbuf + c->rsize));

	}

	return 1;
}

/*
 * read a UDP request.
 */
enum try_read_result try_read_udp(conn *c) {
	int res;

	assert(c != NULL);

	c->request_addr_size = sizeof(c->request_addr);
	res = recvfrom(c->sfd, c->rbuf, c->rsize, 0,
			(struct sockaddr *) &c->request_addr, &c->request_addr_size);
	if (res > 8) {
		unsigned char *buf = (unsigned char *) c->rbuf;
		pthread_mutex_lock(&c->thread->stats.mutex);
		c->thread->stats.bytes_read += res;
		pthread_mutex_unlock(&c->thread->stats.mutex);

		/* Beginning of UDP packet is the request ID; save it. */
		c->request_id = buf[0] * 256 + buf[1];

		/* If this is a multi-packet request, drop it. */
		if (buf[4] != 0 || buf[5] != 1) {
			out_string(c, "SERVER_ERROR multi-packet request not supported");
			return READ_NO_DATA_RECEIVED;
		}

		/* Don't care about any of the rest of the header. */
		res -= 8;
		memmove(c->rbuf, c->rbuf + 8, res);

		c->rbytes = res;
		c->rcurr = c->rbuf;
		return READ_DATA_RECEIVED;
	}
	return READ_NO_DATA_RECEIVED;
}

/*
 * read from network as much as we can, handle buffer overflow and connection
 * close.
 * before reading, move the remaining incomplete fragment of a command
 * (if any) to the beginning of the buffer.
 *
 * To protect us from someone flooding a connection with bogus data causing
 * the connection to eat up all available memory, break out and start looking
 * at the data I've got after a number of reallocs...
 *
 * @return enum try_read_result
 */
enum try_read_result try_read_network(conn *c) {
	enum try_read_result gotdata = READ_NO_DATA_RECEIVED;
	int res;
	int num_allocs = 0;
	assert(c != NULL);

	if (c->rcurr != c->rbuf) {
		if (c->rbytes != 0) /* otherwise there's nothing to copy */
			memmove(c->rbuf, c->rcurr, c->rbytes);
		c->rcurr = c->rbuf;
	}

	while (1) {
		if (c->rbytes >= c->rsize) {
			if (num_allocs == 4) {
				return gotdata;
			}
			++num_allocs;
			char *new_rbuf = (char *) realloc(c->rbuf, c->rsize * 2);
			if (!new_rbuf) {
				STATS_LOCK();
				stats.malloc_fails++;
				STATS_UNLOCK();
				if (settings.verbose > 0) {
					MY_LOGE( "Couldn't realloc input buffer\n");
				}
				c->rbytes = 0; /* ignore what we read */
				out_of_memory(c, "SERVER_ERROR out of memory reading request");
				c->write_and_go = conn_closing;
				return READ_MEMORY_ERROR;
			}
			c->rcurr = c->rbuf = new_rbuf;
			c->rsize *= 2;
		}

		int avail = c->rsize - c->rbytes;
		res = read(c->sfd, c->rbuf + c->rbytes, avail);
		if (res > 0) {
			pthread_mutex_lock(&c->thread->stats.mutex);
			c->thread->stats.bytes_read += res;
			pthread_mutex_unlock(&c->thread->stats.mutex);
			gotdata = READ_DATA_RECEIVED;
			c->rbytes += res;
			if (res == avail) {
				continue;
			} else {
				break;
			}
		}
		if (res == 0) {
			return READ_ERROR;
		}
		if (res == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			return READ_ERROR;
		}
	}
	return gotdata;
}

/*
 * Sets a socket's send buffer size to the maximum allowed by the system.
 */
void maximize_sndbuf(const int sfd) {
	socklen_t intsize = sizeof(int);
	int last_good = 0;
	int min, max, avg;
	int old_size;

	/* Start with the default size. */
	if (getsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &old_size, &intsize) != 0) {
		if (settings.verbose > 0)
			MY_LOGE("getsockopt(SO_SNDBUF)");
		return;
	}

	/* Binary-search for the real maximum. */
	min = old_size;
	max = MAX_SENDBUF_SIZE;

	while (min <= max) {
		avg = ((unsigned int) (min + max)) / 2;
		if (setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, (void *) &avg, intsize)
				== 0) {
			last_good = avg;
			min = avg + 1;
		} else {
			max = avg - 1;
		}
	}

	if (settings.verbose > 1)
		MY_LOGE( "<%d send buffer was %d, now %d\n", sfd, old_size, last_good);
}

int new_socket_unix(void) {
	int sfd;
	int flags;

	if ((sfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		MY_LOGE("socket()");
		return -1;
	}

	if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
			|| fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		MY_LOGE("setting O_NONBLOCK");
		close(sfd);
		return -1;
	}
	return sfd;
}

int server_socket_unix(const char *path, int access_mask) {
	int sfd;
	struct linger ling = { 0, 0 };
	struct sockaddr_un addr;
	struct stat tstat;
	int flags = 1;
	int old_umask;

	if (!path) {
		return 1;
	}

	if ((sfd = new_socket_unix()) == -1) {
		return 1;
	}

	/*
	 * Clean up a previous socket file if we left it around
	 */
	if (lstat(path, &tstat) == 0) {
		if (S_ISSOCK(tstat.st_mode))
			unlink(path);
	}

	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *) &flags, sizeof(flags));
	setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *) &flags, sizeof(flags));
	setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *) &ling, sizeof(ling));

	/*
	 * the memset call clears nonstandard fields in some impementations
	 * that otherwise mess things up.
	 */
	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	assert(strcmp(addr.sun_path, path) == 0);
	old_umask = umask(~(access_mask & 0777));
	if (bind(sfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		MY_LOGE("bind()");
		close(sfd);
		umask(old_umask);
		return 1;
	}
	umask(old_umask);
	if (listen(sfd, settings.backlog) == -1) {
		MY_LOGE("listen()");
		close(sfd);
		return 1;
	}
	conn_new_listen_add(sfd, local_transport);
	return 0;
}

int new_socket(struct addrinfo *ai) {
	int sfd;
	int flags;

	if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
		return -1;
	}

	if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
			|| fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		MY_LOGE("setting O_NONBLOCK");
		close(sfd);
		return -1;
	}
	return sfd;
}

/**
 * Create a socket and bind it to a specific port number
 * @param interface the interface to bind to
 * @param port the port number to bind to
 * @param transport the transport protocol (TCP / UDP)
 * @param portnumber_file A filepointer to write the port numbers to
 *        when they are successfully added to the list of ports we
 *        listen on.
 */
int server_socket(const char *interface, int port,
		enum network_transport transport, FILE *portnumber_file) {
	int sfd;
	struct linger ling = { 0, 0 };
	struct addrinfo *ai;
	struct addrinfo *next;
	struct addrinfo hints;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	char port_buf[NI_MAXSERV];
	int error;
	int success = 0;
	int flags = 1;

	hints.ai_socktype = IS_UDP(transport) ? SOCK_DGRAM : SOCK_STREAM;

	if (port == -1) {
		port = 0;
	}
	snprintf(port_buf, sizeof(port_buf), "%d", port);
	error = getaddrinfo(interface, port_buf, &hints, &ai);
	if (error != 0) {
		if (error != EAI_SYSTEM)
			MY_LOGE( "getaddrinfo(): %s\n", gai_strerror(error));
		else
			MY_LOGE("getaddrinfo()");
		return 1;
	}

	for (next = ai; next; next = next->ai_next) {
		if ((sfd = new_socket(next)) == -1) {
			/* getaddrinfo can return "junk" addresses,
			 * we make sure at least one works before erroring.
			 */
			if (errno == EMFILE) {
				/* ...unless we're out of fds */
				MY_LOGE("server_socket");
				exit(EX_OSERR);
			}
			continue;
		}

#ifdef IPV6_V6ONLY
		if (next->ai_family == AF_INET6) {
			error = setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &flags,
					sizeof(flags));
			if (error != 0) {
				MY_LOGE("setsockopt");
				close(sfd);
				continue;
			}
		}
#endif

		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *) &flags,
				sizeof(flags));
		if (IS_UDP(transport)) {
			maximize_sndbuf(sfd);
		} else {
			error = setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *) &flags,
					sizeof(flags));
			if (error != 0)
				MY_LOGE("setsockopt");

			error = setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *) &ling,
					sizeof(ling));
			if (error != 0)
				MY_LOGE("setsockopt");

			error = setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *) &flags,
					sizeof(flags));
			if (error != 0)
				MY_LOGE("setsockopt");
		}

		if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) {
			if (errno != EADDRINUSE) {
				MY_LOGE("bind()");
				close(sfd);
				freeaddrinfo(ai);
				return 1;
			}
			close(sfd);
			continue;
		} else {
			success++;
			if (!IS_UDP(transport) && listen(sfd, settings.backlog) == -1) {
				MY_LOGE("listen()");
				close(sfd);
				freeaddrinfo(ai);
				return 1;
			}
			if (portnumber_file != NULL
					&& (next->ai_addr->sa_family == AF_INET
							|| next->ai_addr->sa_family == AF_INET6)) {
				union {
					struct sockaddr_in in;
					struct sockaddr_in6 in6;
				} my_sockaddr;
				socklen_t len = sizeof(my_sockaddr);
				if (getsockname(sfd, (struct sockaddr*) &my_sockaddr, &len)
						== 0) {
					if (next->ai_addr->sa_family == AF_INET) {
						fprintf(portnumber_file, "%s INET: %u\n",
								IS_UDP(transport) ? "UDP" : "TCP",
								ntohs(my_sockaddr.in.sin_port));
					} else {
						fprintf(portnumber_file, "%s INET6: %u\n",
								IS_UDP(transport) ? "UDP" : "TCP",
								ntohs(my_sockaddr.in6.sin6_port));
					}
				}
			}
		}

		if (IS_UDP(transport)) {
			int c;
			for (c = 0; c < settings.num_threads_per_udp; c++) {
				/* Allocate one UDP file descriptor per worker thread;
				 * this allows "stats conns" to separately list multiple
				 * parallel UDP requests in progress.
				 *
				 * The dispatch code round-robins new connection requests
				 * among threads, so this is guaranteed to assign one
				 * FD to each thread.
				 */
				int per_thread_fd = c ? dup(sfd) : sfd;
				dispatch_conn_new(per_thread_fd, conn_read,
						EV_READ | EV_PERSIST, UDP_READ_BUFFER_SIZE, transport);
			}
		} else {
			conn_new_listen_add(sfd, transport);
		}
	}

	freeaddrinfo(ai);

	/* Return zero iff we detected no errors in starting up connections */
	return success == 0;
}

int server_sockets(int port, enum network_transport transport,
		FILE *portnumber_file) {
	if (settings.inter == NULL) {
		return server_socket(settings.inter, port, transport, portnumber_file);
	} else {
		// tokenize them and bind to each one of them..
		char *b;
		int ret = 0;
		char *list = strdup(settings.inter);

		if (list == NULL) {
			MY_LOGE(
					"Failed to allocate memory for parsing server interface string\n");
			return 1;
		}
		for (char *p = strtok_r(list, ";,", &b); p != NULL;
				p = strtok_r(NULL, ";,", &b)) {
			int the_port = port;

			char *h = NULL;
			if (*p == '[') {
				// expecting it to be an IPv6 address enclosed in []
				// i.e. RFC3986 style recommended by RFC5952
				char *e = strchr(p, ']');
				if (e == NULL) {
					MY_LOGE( "Invalid IPV6 address: \"%s\"", p);
					return 1;
				}
				h = ++p; // skip the opening '['
				*e = '\0';
				p = ++e; // skip the closing ']'
			}

			char *s = strchr(p, ':');
			if (s != NULL) {
				// If no more semicolons - attempt to treat as port number.
				// Otherwise the only valid option is an unenclosed IPv6 without port, until
				// of course there was an RFC3986 IPv6 address previously specified -
				// in such a case there is no good option, will just send it to fail as port number.
				if (strchr(s + 1, ':') == NULL || h != NULL) {
					*s = '\0';
					++s;
					if (!safe_strtol(s, &the_port)) {
						MY_LOGE( "Invalid port number: \"%s\"", s);
						return 1;
					}
				}
			}

			if (h != NULL)
				p = h;

			if (strcmp(p, "*") == 0) {
				p = NULL;
			}
			ret |= server_socket(p, the_port, transport, portnumber_file);
		}
		free(list);
		return ret;
	}
}

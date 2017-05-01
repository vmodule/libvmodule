/*
 * SampleServer.h
 *
 *  Created on: May 1, 2017
 *      Author: jeffrey
 */

#ifndef SAMPLESERVER_H_
#define SAMPLESERVER_H_
#include <network/core/conn_base.h>
#include <network/core/conn_queue.h>
#include <network/core/conn_thread.h>
#include <network/core/conn_utils.h>
#include <network/core/conn_wrap.h>
class SampleServer : public msg_callback{
public:
	SampleServer();
	virtual ~SampleServer();
protected:
	virtual void onBinaryEventDispatch(conn *c);
	virtual void onAsciiEventDispatch(conn *c);
};

#endif /* SAMPLESERVER_H_ */

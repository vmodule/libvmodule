/*
 * RtspServer.h
 *
 *  Created on: Apr 30, 2017
 *      Author: jeffrey
 */
#ifndef _RTSP_SERVER_H_
#define _RTSP_SERVER_H_

#include <network/core/conn_base.h>
#include <network/core/conn_queue.h>
#include <network/core/conn_thread.h>
#include <network/core/conn_utils.h>
#include <network/core/conn_wrap.h>

namespace vmodule {
class RtspServer: public msg_callback {
public:
	RtspServer();
	virtual ~RtspServer();
protected:
	virtual void onBinaryEventDispatch(conn *c);
	virtual void onAsciiEventDispatch(conn *c);
};

} /* namespace vmodule */
#endif /* RTSPSERVER_H_ */

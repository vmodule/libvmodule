/*
 * RtspServer.cpp
 *
 *  Created on: Apr 30, 2017
 *      Author: jeffrey
 */

#include <network/RtspServer.h>
#include <vutils/Logger.h>
#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "RtspServer"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)
#endif
namespace vmodule {

RtspServer::RtspServer() {

}

RtspServer::~RtspServer() {

}

void RtspServer::onBinaryEventDispatch(conn *c) {

}

void RtspServer::onAsciiEventDispatch(conn *c) {
	for (int i = 0; i < c->rbytes; i++)
		c->rcurr[i] = toupper(c->rcurr[i]);
	/*
	 * for commands set/add/replace, we build an item and read the data
	 * directly into it, then continue in nread_complete().
	 */
	out_string(c, c->rcurr);
}

} /* namespace vmodule */

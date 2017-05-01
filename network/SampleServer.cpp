/*
 * SampleServer.cpp
 *
 *  Created on: May 1, 2017
 *      Author: jeffrey
 */

#include <network/SampleServer.h>

SampleServer::SampleServer() {
	// TODO Auto-generated constructor stub

}

SampleServer::~SampleServer() {
	// TODO Auto-generated destructor stub
}

void SampleServer::onBinaryEventDispatch(conn *c) {

}

void SampleServer::onAsciiEventDispatch(conn *c) {
	for (int i = 0; i < c->rbytes; i++)
		c->rcurr[i] = toupper(c->rcurr[i]);
	/*
	 * for commands set/add/replace, we build an item and read the data
	 * directly into it, then continue in nread_complete().
	 */
	out_string(c, c->rcurr);
}


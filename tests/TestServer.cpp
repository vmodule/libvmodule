#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <vutils/Logger.h>
#include <network/core/conn_base.h>
#include <network/core/conn_queue.h>
#include <network/core/conn_thread.h>
#include <network/SampleServer.h>
#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "TestServer"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)
#endif
using namespace std;
using namespace vmodule;


//TestServer -l 192.168.0.11,127.0.0.1
int main(int argc, char **argv) {
	char environment[80];
	snprintf(environment, sizeof(environment),
			"VNETWORK_PORT_FILENAME=/tmp/ports.%lu", (long) getpid());
	char *filename = environment + strlen("VNETWORK_PORT_FILENAME=");
	remove(filename);
	MY_LOGD("environment = %s",environment);
	putenv(environment);
	SampleServer mSampleServer;
	return start_server(argc, argv,&mSampleServer);
}

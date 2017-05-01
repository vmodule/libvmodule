#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <vmodule.h>
#include <vutils/Logger.h>
#include <vutils/RefBase.h>
#include <threads/Thread.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "VMODULE"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif

namespace vmodule {

void welcome_using_vmodule() {

	MY_LOGD("%s",__func__);
}

}


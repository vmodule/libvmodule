/*
 * Looper.cpp
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <vutils/Logger.h>
#include <threads/Looper.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "CLooper"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif

CLooper::CLooper()
	:head(NULL){
    MY_LOGD("enter %s",__func__);		
    sem_init(&dataavailable, 0, 0);
    sem_init(&writeprotect, 0, 1);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&worker, &attr, 
		(void*(*)(void*))loop, this);
    running = true;
}

CLooper::~CLooper() {
    if (running) {
        MY_LOGD("Looper deleted while still running. Some messages will not be processed");
        quit();
    }
}

void CLooper::post(int what, void *data, bool flush) {
    CLooperMsg *msg = new CLooperMsg();
    msg->what = what;
    msg->obj = data;
    msg->next = NULL;
    msg->quit = false;
	MY_LOGD("%s:post msg %d",__func__, msg->what);
    addmsg(msg, flush);
}

void CLooper::post(int what, void *data,int delay,bool flush) 
{
	if (pthread_self() == worker) {
		MY_LOGE("Current can't sleep here..");		
		return;
	}
	sleep(delay);//sleep ms
	CLooperMsg *msg = new CLooperMsg();
	msg->what = what;
	msg->obj = data;
	msg->next = NULL;
	msg->quit = false;
	MY_LOGD("%s:post msg %d",__func__, msg->what);
	addmsg(msg, flush);
}

void CLooper::addmsg(CLooperMsg *msg, bool flush) {
    sem_wait(&this->writeprotect);
    CLooperMsg *h = this->head;
    if (flush) {
        while(h) {
            CLooperMsg *next = h->next;
            delete h;
            h = next;
        }
        h = NULL;
    }
    if (NULL != h) {
        while (h->next) {
            h = h->next;
        }
        h->next = msg;
    } else {
        this->head = msg;
    }
    sem_post(&this->writeprotect);
    sem_post(&this->dataavailable);
}

int CLooper::loop(void* user) {
	CLooper* pThread = static_cast<CLooper*>(user);
	if (!pThread) {
		MY_LOGE("%s, sanity failed. thread is NULL.", __FUNCTION__);
		return -1;
	}	
    while(true) {
        // wait for available message
        int Val;
		//sem_getvalue(&pThread->dataavailable, &Val);
		//MY_LOGD("%s:wait for available message Val(%d)",__func__,Val);	
        sem_wait(&pThread->dataavailable);

		//sem_getvalue(&pThread->writeprotect, &Val);
		//MY_LOGD("%s:wait for available message Val(%d)",__func__,Val);	
        // get next available message
        sem_wait(&pThread->writeprotect);
		MY_LOGD("%s:get next available message 3",__func__);	

        CLooperMsg *msg = pThread->head;
        if (msg == NULL) {
            MY_LOGD("no msg");
            sem_post(&pThread->writeprotect);
            continue;
        }
        pThread->head = msg->next;
        sem_post(&pThread->writeprotect);

        if (msg->quit) {
            MY_LOGD("quitting");
            delete msg;
            return 0;
        }
        MY_LOGD("processing msg %d", msg->what);
        pThread->handle(msg->what, msg->obj);
        delete msg;
    }

	return 0;
}

void CLooper::quit() {
    MY_LOGD("quit");
    CLooperMsg *msg = new CLooperMsg();
    msg->what = 0;
    msg->obj = NULL;
    msg->next = NULL;
    msg->quit = true;
    addmsg(msg, false);
    void *retval;
    pthread_join(worker, &retval);
    sem_destroy(&dataavailable);
    sem_destroy(&writeprotect);
    running = false;
}

void CLooper::handle(int what, void* obj) {
    MY_LOGD("dropping msg %d %p", what, obj);
}

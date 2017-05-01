/*
 * Mutex.h
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#pragma once

#include <pthread.h>
#include <semaphore.h>

struct CLooperMsg;
typedef struct CLooperMsg CLooperMsg;

struct CLooperMsg {
	int what;
	void *obj;
	CLooperMsg *next;
	bool quit;
};

class CLooper {
public:
    CLooper();
    virtual ~CLooper();

    void post(int what, void *data, bool flush = false);
    void post(int what, void *data, int delay,bool flush = false);
	
    void quit();

    virtual void handle(int what, void *data);

private:
    void addmsg(CLooperMsg *msg, bool flush);
    static int loop(void* user);
    CLooperMsg *head;
    pthread_t worker;
    sem_t writeprotect;
    sem_t dataavailable;
    bool running;
};

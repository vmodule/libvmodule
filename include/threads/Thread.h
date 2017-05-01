/*
 * Thread.h
 *
 *  Created on: Jan 1, 2017
 *      Author: jeffrey
 */

#pragma once
#include <string>
#include <stdint.h>
#include <memory>

#include <vutils/RefBase.h>
#include <vutils/error.h>
#include <threads/Condition.h>
#include <threads/SingleLock.h>
#include <threads/Event.h>
#include <threads/Helpers.h>

namespace vmodule {
class CThread: virtual public RefBase {
public:
	CThread(/*const char* ThreadName*/);
	virtual ~CThread();
	virtual status_t run(const char* name = 0, size_t stack = 0);
	virtual void requestExit();
	virtual status_t readyToRun();
	status_t requestExitAndWait();
	status_t join();
	bool isRunning() const;
	bool IsCurrentThread() const;
	ThreadIdentifier ThreadId() const;
	static bool IsCurrentThread(const ThreadIdentifier tid);
	static ThreadIdentifier GetCurrentThreadId();
	static CThread* GetCurrentThread();
	//void sleep(unsigned int milliseconds);
protected:
	// exitPending() returns true if requestExit() has been called.
	bool exitPending() const;

private:
	// Derived class must implement threadLoop(). The thread starts its life
	// here. There are two ways of using the Thread object:
	// 1) loop: if threadLoop() returns true, it will be called again if
	//          requestExit() wasn't called.
	// 2) once: if threadLoop() returns false, the thread will exit upon return.
	virtual bool threadLoop() = 0;

private:
	CThread& operator=(const CThread&);
	static int _threadLoop(void* user);
	// always hold mLock when reading or writing
	ThreadIdentifier m_ThreadId;
	status_t mStatus;
	// note that all accesses of mExitPending and mRunning need to hold mLock
	volatile bool mExitPending;
	volatile bool mRunning;
	sp<CThread> mHoldSelf;
	CEvent m_StopEvent;
	CEvent m_TermEvent;
	CEvent m_StartEvent;
	mutable CCriticalSection m_CriticalSection;
	std::string m_ThreadName;
};
};


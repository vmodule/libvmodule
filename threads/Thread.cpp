/*
 * Thread.cpp
 *
 *  Created on: Jan 1, 2017
 *      Author: jeffrey
 */

#include <limits.h>
#if defined(TARGET_ANDROID) || defined(TARGET_POSIX)
#include <unistd.h>
#else
#include <sys/syscall.h>
#endif
#include <sys/resource.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <cstdlib>
#include <vutils/Logger.h>
#include <threads/ThreadLocal.h>
#include <threads/Thread.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "CThread"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif

namespace vmodule {

static vmodule::ThreadLocal<CThread> currentThread;

CThread::CThread(/*const char* ThreadName*/) :
		m_ThreadId(ThreadIdentifier(-1)), mStatus(NO_ERROR), mExitPending(
				false), mRunning(false), m_StopEvent(true, true), m_TermEvent(
				true), m_StartEvent(true) {
	MY_LOGD("%s", __FUNCTION__);

}

CThread::~CThread() {
	MY_LOGD("%s", __FUNCTION__);
}

status_t CThread::readyToRun() {
	MY_LOGD("%s", __FUNCTION__);
	return NO_ERROR;
}

status_t CThread::run(const char* name, size_t stack) {
	CSingleLock lock(m_CriticalSection);
	MY_LOGD("%s", __FUNCTION__);
	if (name)
		m_ThreadName = name;
	else {
		m_ThreadName = "default CThread";
	}
	if (mRunning) {
		// thread already started
		return INVALID_OPERATION;
	}
	// reset status and exitPending to their default value, so we can
	// try again after an error happened (either below, or in readyToRun())
	mStatus = NO_ERROR;
	mExitPending = false;
	m_ThreadId = ThreadIdentifier(-1);
	// hold a strong reference on ourself
	mHoldSelf = this;
	mRunning = true;
	m_StopEvent.Reset();
	m_TermEvent.Reset();
	m_StartEvent.Reset();
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (stack) {
		pthread_attr_setstacksize(&attr, stack);
	}
	errno = 0;
	int result = pthread_create(&m_ThreadId, &attr,
			(void*(*)(void*))_threadLoop, this);
	pthread_attr_destroy(&attr);
	if (result != 0) {
		MY_LOGE( "%s - fatal error creating thread", __FUNCTION__);
		mStatus = UNKNOWN_ERROR; // something happened!
		mRunning = false;
		m_ThreadId = ThreadIdentifier(-1);
		mHoldSelf.clear(); // "this" may have gone away after this.
		lock.Leave();
		return UNKNOWN_ERROR;
	}
	return NO_ERROR;
}

int CThread::_threadLoop(void* user) {
	MY_LOGD("%s", __FUNCTION__);
	CThread* const self = static_cast<CThread*>(user);
	std::string name;
	ThreadIdentifier id;
	sp<CThread> strong(self->mHoldSelf);
	wp<CThread> weak(strong);
	self->mHoldSelf.clear();
	currentThread.set(self);
	self->m_StartEvent.Set();
	name = self->m_ThreadName;
	id = self->m_ThreadId;
	MY_LOGD("Thread %s %llu starting...", name.c_str(), (uint64_t)id);
	bool first = true;
	do {
		bool result;
		if (first) {
			first = false;
			self->mStatus = self->readyToRun();
			result = (self->mStatus == NO_ERROR);
			if (result && !self->exitPending()) {
				result = self->threadLoop();
			}
		} else {
			result = self->threadLoop();
		}
		{
			CSingleLock lock(self->m_CriticalSection);
			if (result == false || self->mExitPending) {
				self->mExitPending = true;
				self->mRunning = false;
				self->m_ThreadId = ThreadIdentifier(-1);
				self->m_StopEvent.Set();
				break;
			}
		}
		strong.clear();
		strong = weak.promote();
	} while (strong != 0);
	MY_LOGD("Thread %s %llu terminating...", name.c_str(), (uint64_t)id);
	return 0;
}

void CThread::requestExit() {
	CSingleLock lock(m_CriticalSection);
	MY_LOGD("%s", __FUNCTION__);
	mExitPending = true;
}

status_t CThread::requestExitAndWait() {
	{
		CSingleLock lock(m_CriticalSection);
		MY_LOGD("%s", __FUNCTION__);
		if (m_ThreadId == GetCurrentThreadId()) {
			MY_LOGE( "Thread (this=%p): don't call waitForExit() from this "
			"Thread object's thread. It's a guaranteed deadlock!", this);
			return WOULD_BLOCK;
		}
		mExitPending = true;
	}
	while (mRunning == true) {
		m_StopEvent.Wait();
	}
	// This next line is probably not needed any more, but is being left for
	// historical reference. Note that each interested party will clear flag.
	mExitPending = false;

	return mStatus;
}

status_t CThread::join() {
	{
		CSingleLock lock(m_CriticalSection);
		MY_LOGD("%s", __FUNCTION__);
		if (m_ThreadId == GetCurrentThreadId()) {
			MY_LOGE("Thread (this=%p): don't call join() from this "
			"Thread object's thread. It's a guaranteed deadlock!", this);
			return WOULD_BLOCK;
		}
	}
	while (mRunning == true) {
		m_StopEvent.Wait();
	}

	return mStatus;
}

#if 0
void CThread::sleep(unsigned int milliseconds) {
	MY_LOGD("%s", __FUNCTION__);
	if (milliseconds > 10 && IsCurrentThread()) {
		MY_LOGD("IsCurrentThread Sleep which will call m_StopEvent.WaitMSec");
		m_StopEvent.WaitMSec(milliseconds);
	} else {
		MY_LOGD("thread sleep %d milliseconds",milliseconds);
		usleep(milliseconds * 1000);
	}
}

int CThread::requestExitAndWait(bool bWait /*= true*/) {
	CSingleLock lock(m_CriticalSection);
	int result;
	if (m_ThreadId == GetCurrentThreadId()) {
		MY_LOGD(
				"Thread (this=%p): don't call requestExitAndWait() from this "
				"Thread object's thread. It's a guaranteed deadlock!", this);
		return -EAGAIN;
	} MY_LOGD( "%s,ThreadId(%ld),CurrentThreadId(%ld) ..",
			__FUNCTION__, m_ThreadId, GetCurrentThreadId());
	mExitPending = true;
	m_StopEvent->Set();
	while (IsRunning() && bWait) {
		lock.Leave();
		result = WaitForThreadExit(0xFFFFFFFF);
	}
	// This next line is probably not needed any more, but is being left for
	// historical reference. Note that each interested party will clear flag.
	mExitPending = false;
	return result;
}

bool CThread::WaitForThreadExit(unsigned int milliseconds) {
	bool bReturn = m_TermEvent->WaitMSec(milliseconds);
	return bReturn;
}

#endif

bool CThread::isRunning() const {
	MY_LOGD("%s", __FUNCTION__);
	CSingleLock lock(m_CriticalSection);
	return mRunning;
}

bool CThread::exitPending() const {
	CSingleLock lock(m_CriticalSection);
	return mExitPending;
}

ThreadIdentifier CThread::ThreadId() const {
	return m_ThreadId;
}

bool CThread::IsCurrentThread() const {
	return IsCurrentThread(ThreadId());
}

bool CThread::IsCurrentThread(const ThreadIdentifier tid) {
	return pthread_equal(pthread_self(), tid);
}

CThread* CThread::GetCurrentThread() {
	return currentThread.get();
}

ThreadIdentifier CThread::GetCurrentThreadId() {
	return pthread_self();
}

}


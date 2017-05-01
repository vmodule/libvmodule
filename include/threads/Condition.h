/*
 * Condition.h
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#pragma once

#include <pthread.h>
#include <threads/CriticalSection.h>
#include <threads/SingleLock.h>
#include <threads/SystemClock.h>
#include <threads/Helpers.h>
#ifdef TARGET_DARWIN
#include <sys/time.h> //for gettimeofday
#else
#include <time.h> //for clock_gettime
#endif
namespace vmodule {

class CCondition: public NonCopyable {
private:
	pthread_cond_t cond;

public:
	inline CCondition() {
		pthread_cond_init(&cond, NULL);
	}

	inline ~CCondition() {
		pthread_cond_destroy(&cond);
	}

	inline void wait(CCriticalSection& lock) {
		int count = lock.count;
		lock.count = 0;
		pthread_cond_wait(&cond, &lock.get_underlying().mutex);
		lock.count = count;
	}

	inline bool wait(CCriticalSection& lock, unsigned long milliseconds) {
		struct timespec ts;

#ifdef TARGET_DARWIN
		struct timeval tv;
		gettimeofday(&tv, NULL);
		ts.tv_nsec = tv.tv_usec * 1000;
		ts.tv_sec = tv.tv_sec;
#else
		clock_gettime(CLOCK_REALTIME, &ts);
#endif

		ts.tv_nsec += milliseconds % 1000 * 1000000;
		ts.tv_sec += milliseconds / 1000 + ts.tv_nsec / 1000000000;
		ts.tv_nsec %= 1000000000;
		int count = lock.count;
		lock.count = 0;
		int res = pthread_cond_timedwait(&cond, &lock.get_underlying().mutex,
				&ts);
		lock.count = count;
		return res == 0;
	}

	inline void wait(CSingleLock& lock) {
		wait(lock.get_underlying());
	}
	inline bool wait(CSingleLock& lock, unsigned long milliseconds) {
		return wait(lock.get_underlying(), milliseconds);
	}

	inline void notifyAll() {
		pthread_cond_broadcast(&cond);
	}

	inline void notify() {
		pthread_cond_signal(&cond);
	}
};

/**
 * This is a condition variable along with its predicate. This allows the use of a
 *  condition variable without the spurious returns since the state being monitored
 *  is also part of the condition.
 *
 * L should implement the Lockable concept
 *
 * The requirements on P are that it can act as a predicate (that is, I can use
 *  it in an 'while(!predicate){...}' where 'predicate' is of type 'P').
 */
template<typename P>
class TightConditionVariable {
	CCondition& cond;
	P predicate;

public:
	inline TightConditionVariable(CCondition& cv, P predicate_) :
			cond(cv), predicate(predicate_) {
	}

	template<typename L> inline void wait(L& lock) {
		while (!predicate)
			cond.wait(lock);
	}

	template<typename L>
	inline bool wait(L& lock, unsigned long milliseconds) {
		bool ret = true;
		if (!predicate) {
			if (!milliseconds) {
				cond.wait(lock, milliseconds /* zero */);
				return !(!predicate); // eh? I only require the ! operation on P
			} else {
				EndTime endTime((unsigned int) milliseconds);
				for (bool notdone = true; notdone && ret == true;
						ret = (notdone = (!predicate)) ? ((milliseconds =
								endTime.MillisLeft()) != 0) :
															true)
					cond.wait(lock, milliseconds);
			}
		}
		return ret;
	}

	inline void notifyAll() {
		cond.notifyAll();
	}
	inline void notify() {
		cond.notify();
	}
};
}; /* namespace vmodule */

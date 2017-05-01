/*
 * SingleLock.h
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */
#pragma once
#include <threads/Mutex.h>
#include <threads/Lockables.h>

class CSingleLock: public vmodule::UniqueLock<CCriticalSection> {
public:
	inline CSingleLock(CCriticalSection& cs) :
			vmodule::UniqueLock<CCriticalSection>(cs) {
	}
	inline CSingleLock(const CCriticalSection& cs) :
			vmodule::UniqueLock<CCriticalSection>((CCriticalSection&) cs) {
	}

	inline void Leave() {
		unlock();
	}
	inline void Enter() {
		lock();
	}
protected:
	inline CSingleLock(CCriticalSection& cs, bool dicrim) :
			vmodule::UniqueLock<CCriticalSection>(cs, true) {
	}
};

/**
 * This implements a "guard" pattern for a CCriticalSection that
 *  works like a CSingleLock but only "try"s the lock and so
 *  it's possible it doesn't actually get it..
 */
class CSingleTryLock: public CSingleLock {
public:
	inline CSingleTryLock(CCriticalSection& cs) :
			CSingleLock(cs, true) {
	}

	inline bool IsOwner() const {
		return owns_lock();
	}
};

/**
 * This implements a "guard" pattern for exiting all locks
 *  currently being held by the current thread and restoring
 *  those locks on destruction.
 *
 * This class can be used on a CCriticalSection that isn't owned
 *  by this thread in which case it will do nothing.
 */
class CSingleExit {
	CCriticalSection& sec;
	unsigned int count;
public:
	inline CSingleExit(CCriticalSection& cs) :
			sec(cs), count(cs.exit()) {
	}
	inline ~CSingleExit() {
		sec.restore(count);
	}
};


/*
 * Mutex.h
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */
#pragma once

#include <threads/Helpers.h>
#include <threads/Condition.h>

namespace vmodule {
template<class L>
class CountingLockable: public NonCopyable {
	friend class CCondition;
protected:
	L mutex;
	unsigned int count;

public:
	inline CountingLockable() :
			count(0) {
	}
	// boost::thread Lockable concept
	inline void lock() {
		mutex.lock();
		count++;
	}
	inline bool try_lock() {
		return mutex.try_lock() ? count++, true : false;
	}
	inline void unlock() {
		count--;
		mutex.unlock();
	}

	/**
	 * This implements the "exitable" behavior mentioned above.
	 *
	 * This can be used to ALMOST exit, but not quite, by passing
	 *  the number of locks to leave. This is used in the windows
	 *  ConditionVariable which requires that the lock be entered
	 *  only once, and so it backs out ALMOST all the way, but
	 *  leaves one still there.
	 */
	inline unsigned int exit(unsigned int leave = 0) {
		// it's possibe we don't actually own the lock
		// so we will try it.
		unsigned int ret = 0;
		if (try_lock()) {
			if (leave < (count - 1)) {
				ret = count - 1 - leave; // The -1 is because we don't want
										 //  to count the try_lock increment.
				// We must NOT compare "count" in this loop since
				// as soon as the last unlock is called another thread
				// can modify it.
				for (unsigned int i = 0; i < ret; i++)
					unlock();
			}
			unlock(); // undo the try_lock before returning
		}

		return ret;
	}

	/**
	 * Restore a previous exit to the provided level.
	 */
	inline void restore(unsigned int restoreCount) {
		for (unsigned int i = 0; i < restoreCount; i++)
			lock();
	}

	/**
	 * Some implementations (see pthreads) require access to the underlying
	 *  CCriticalSection, which is also implementation specific. This
	 *  provides access to it through the same method on the guard classes
	 *  UniqueLock, and SharedLock.
	 *
	 * There really should be no need for the users of the threading library
	 *  to call this method.
	 */
	inline L& get_underlying() {
		return mutex;
	}
};

/**
 * This template can be used to define the base implementation for any UniqueLock
 * (such as CSingleLock) that uses a Lockable as its mutex/critical section.
 */
template<typename L>
class UniqueLock: public NonCopyable {
protected:
	L& mutex;
	bool owns;
	inline UniqueLock(L& lockable) :
			mutex(lockable), owns(true) {
		mutex.lock();
	}
	inline UniqueLock(L& lockable, bool try_to_lock_discrim) :
			mutex(lockable) {
		owns = mutex.try_lock();
	}
	inline ~UniqueLock() {
		if (owns)
			mutex.unlock();
	}

public:

	inline bool owns_lock() const {
		return owns;
	}

	//This also implements lockable
	inline void lock() {
		mutex.lock();
		owns = true;
	}

	inline bool try_lock() {
		return (owns = mutex.try_lock());
	}

	inline void unlock() {
		if (owns) {
			mutex.unlock();
			owns = false;
		}
	}

	/**
	 * See the note on the same method on CountingLockable
	 */
	inline L& get_underlying() {
		return mutex;
	}
};

/**
 * This template can be used to define the base implementation for any SharedLock
 * (such as CSharedLock) that uses a Shared Lockable as its mutex/critical section.
 *
 * Something that implements the "Shared Lockable" concept has all of the methods
 * required by the Lockable concept and also:
 *
 * void lock_shared();
 * bool try_lock_shared();
 * void unlock_shared();
 */
template<typename L>
class SharedLock: public NonCopyable {
protected:
	L& mutex;
	bool owns;
	inline SharedLock(L& lockable) :
			mutex(lockable), owns(true) {
		mutex.lock_shared();
	}
	inline ~SharedLock() {
		if (owns)
			mutex.unlock_shared();
	}

	inline bool owns_lock() const {
		return owns;
	}
	inline void lock() {
		mutex.lock_shared();
		owns = true;
	}
	inline bool try_lock() {
		return (owns = mutex.try_lock_shared());
	}
	inline void unlock() {
		if (owns)
			mutex.unlock_shared();
		owns = false;
	}
	/**
	 * See the note on the same method on CountingLockable
	 */
	inline L& get_underlying() {
		return mutex;
	}
};

} /* namespace vmodule */


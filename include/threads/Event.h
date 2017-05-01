/*
 * Event.h
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#pragma once

#include <threads/Helpers.h>
#include <threads/Condition.h>
#include <threads/SingleLock.h>
class CEvent: public vmodule::NonCopyable {

	bool manualReset;
	volatile bool signaled;
	unsigned int numWaits;

	/**
	 * To satisfy the TightConditionVariable requirements and allow the
	 *  predicate being monitored to include both the signaled and interrupted
	 *  states.
	 */
	vmodule::CCondition actualCv;
	vmodule::TightConditionVariable<volatile bool&> condVar;
	CCriticalSection mutex;

	// helper for the two wait methods
	inline bool prepReturn() {
		bool ret = signaled;
		if (!manualReset && numWaits == 0)
			signaled = false;
		return ret;
	}

public:
	inline CEvent(bool manual = false, bool signaled_ = false) :
			manualReset(manual), signaled(signaled_), numWaits(0), condVar(
					actualCv, signaled) {
	}

	inline void Reset() {
		CSingleLock lock(mutex);
		signaled = false;
	}

	void Set();

	/** Returns true if Event has been triggered and not reset, false otherwise. */
	inline bool Signaled() {
		CSingleLock lock(mutex);
		return signaled;
	}

	/**
	 * This will wait up to 'milliSeconds' milliseconds for the Event
	 *  to be triggered. The method will return 'true' if the Event
	 *  was triggered. Otherwise it will return false.
	 */
	inline bool WaitMSec(unsigned int milliSeconds) {
		CSingleLock lock(mutex);
		numWaits++;
		condVar.wait(mutex, milliSeconds);
		numWaits--;
		return prepReturn();
	}

	/**
	 * This will wait for the Event to be triggered. The method will return
	 * 'true' if the Event was triggered. If it was either interrupted
	 * it will return false. Otherwise it will return false.
	 */
	inline bool Wait() {
		CSingleLock lock(mutex);
		numWaits++;
		condVar.wait(mutex);
		numWaits--;
		return prepReturn();
	}

	/**
	 * This is mostly for testing. It allows a thread to make sure there are
	 *  the right amount of other threads waiting.
	 */
	inline int getNumWaits() {
		CSingleLock lock(mutex);
		return numWaits;
	}

};


/*
 * Event.cpp
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#include <threads/Event.h>

// locking is ALWAYS done in this order:
//  CEvent::groupListMutex -> CEventGroup::mutex -> CEvent::mutex
void CEvent::Set() {
	// Originally I had this without locking. Thanks to FernetMenta who
	// pointed out that this creates a race condition between setting
	// checking the signal and calling wait() on the Wait call in the
	// CEvent class. This now perfectly matches the boost example here:
	// http://www.boost.org/doc/libs/1_41_0/doc/html/thread/synchronization.html#thread.synchronization.condvar_ref
	{
		CSingleLock slock(mutex);
		signaled = true;
	}
	condVar.notifyAll();
}


/*
 * Mutex.cpp
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#include <threads/Mutex.h>

namespace vmodule
{

// ==========================================================
static pthread_mutexattr_t recursiveAttr;

static bool setMutexAttr()
{
	static bool alreadyCalled = false; // initialized to 0 in the data segment prior to startup init code running
	if (!alreadyCalled)
	{
		pthread_mutexattr_init(&recursiveAttr);
		pthread_mutexattr_settype(&recursiveAttr, PTHREAD_MUTEX_RECURSIVE);
		alreadyCalled = true;
	}
	return true; // note, we never call destroy.
}

static bool recursiveAttrSet = setMutexAttr();
pthread_mutexattr_t* CMutex::getMutexAttr()
{
	if (!recursiveAttrSet) // this is only possible in the single threaded startup code
		recursiveAttrSet = setMutexAttr();
	return &recursiveAttr;
}
} /* namespace vmodule */

/*
 * Mutex.h
 *
 *  Created on: Jan 2, 2017
 *      Author: jeffrey
 */

#pragma once
#include <pthread.h>
namespace vmodule
{
class CCondition;
class CMutex
{
	friend class CCondition;
	pthread_mutex_t mutex;
public:
	inline CMutex()
	{
		pthread_mutex_init(&mutex, getMutexAttr());
	}
	inline ~CMutex()
	{
		pthread_mutex_destroy(&mutex);
	}
	inline void lock()
	{
		pthread_mutex_lock(&mutex);
	}
	inline void unlock()
	{
		pthread_mutex_unlock(&mutex);
	}
	inline bool try_lock()
	{
		return (pthread_mutex_trylock(&mutex) == 0);
	}
	pthread_mutexattr_t* getMutexAttr();
};


}; /* namespace vmodule */

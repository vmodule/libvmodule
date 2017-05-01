#pragma once

#include <pthread.h>

namespace vmodule {
/**
 * A thin wrapper around pthreads thread specific storage
 * functionality.
 */
template<typename T>
class ThreadLocal {
	pthread_key_t key;
public:
	inline ThreadLocal() :
			key(0) {
		pthread_key_create(&key, NULL);
	}

	inline ~ThreadLocal() {
		pthread_key_delete(key);
	}

	inline void set(T* val) {
		pthread_setspecific(key, (void*) val);
	}

	inline T* get() {
		return (T*) pthread_getspecific(key);
	}
};
};

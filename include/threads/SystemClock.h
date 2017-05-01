#pragma once

#include <limits>
#include <string>
namespace vmodule {

	typedef int64_t nsecs_t; // nano-seconds

	enum {
		SYSTEM_TIME_REALTIME = 0, // system-wide realtime clock
		SYSTEM_TIME_MONOTONIC = 1, // monotonic time since unspecified starting point
		SYSTEM_TIME_PROCESS = 2, // high-resolution per-process clock
		SYSTEM_TIME_THREAD = 3, // high-resolution per-thread clock
	};

	/**
	 * This function returns the system clock's number of milliseconds but with
	 *  an arbitrary reference point. It handles the wrapping of any underlying
	 *  system clock by setting a starting point at the first call. It should
	 *  only be used for measuring time durations.
	 *
	 * Of course, on windows it just calls timeGetTime, so you're on your own.
	 */
	unsigned int SystemClockMillis();

	static inline nsecs_t systemTime(int clock) {
	#if defined(TARGET_ANDROID) || defined(TARGET_POSIX)
		static const clockid_t clocks[] = {
			CLOCK_REALTIME,
			CLOCK_MONOTONIC,
			CLOCK_PROCESS_CPUTIME_ID,
			CLOCK_THREAD_CPUTIME_ID
		};
		struct timespec t;
		t.tv_sec = t.tv_nsec = 0;
		clock_gettime(clocks[clock], &t);
		return nsecs_t(t.tv_sec)*1000000000LL + t.tv_nsec;
	#else
		// we don't support the clocks here.
		struct timeval t;
		t.tv_sec = t.tv_usec = 0;
		gettimeofday(&t, NULL);
		return nsecs_t(t.tv_sec) * 1000000000LL + nsecs_t(t.tv_usec) * 1000LL;
	#endif
	}

	static inline nsecs_t nanoseconds_to_milliseconds(nsecs_t secs) {
		return secs / 1000000;
	}

	static inline struct tm* systemTimeInfo() {
		struct tm *timeinfo = NULL;
		time_t rawtime;
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		return timeinfo;
	}

	static inline unsigned long systemMaketime(const unsigned int year0,
			const unsigned int mon0, const unsigned int day,
			const unsigned int hour, const unsigned int min,
			const unsigned int sec) {
		unsigned int mon = mon0, year = year0;

		/* 1..12 -> 11,12,1..10 */
		if (0 >= (int) (mon -= 2)) {
			mon += 12; /* Puts Feb last since it has leap day */
			year -= 1;
		}
		return ((((unsigned long) (year / 4 - year / 100 + year / 400
				+ 367 * mon / 12 + day) + year * 365 - 719499) * 24 + hour /* now have hours */
		) * 60 + min /* now have minutes */
		) * 60 + sec; /* finally seconds */
	}

	static inline std::string systemDateTimeToString() {
		struct tm *timeinfo;
		time_t rawtime;
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		char *buf = new char[30];
		strftime(buf, 30, "%Y%m%dT%H%M%S", timeinfo);
		std::string datetime(buf);
		delete buf;
		return datetime;
	}

	/**
	 * DO NOT compare the results from SystemClockMillis() to an expected end time
	 *  that was calculated by adding a number of milliseconds to some start time.
	 *  The reason is because the SystemClockMillis could wrap. Instead use this
	 *  class which uses differences (which are safe accross a wrap).
	 */
	class EndTime {
		unsigned int startTime;
		unsigned int totalWaitTime;
	public:
		static const unsigned int InfiniteValue;

		inline EndTime() :
				startTime(0), totalWaitTime(0) {
		}

		inline EndTime(unsigned int millisecondsIntoTheFuture) :
				startTime(SystemClockMillis()), totalWaitTime(
						millisecondsIntoTheFuture) {
		}

		inline void Set(unsigned int millisecondsIntoTheFuture) {
			startTime = SystemClockMillis();
			totalWaitTime = millisecondsIntoTheFuture;
		}

		inline bool IsTimePast() const {
			return totalWaitTime == InfiniteValue ?
					false :
					(totalWaitTime == 0 ?
							true :
							(SystemClockMillis() - startTime) >= totalWaitTime);
		}

		inline unsigned int MillisLeft() const {
			if (totalWaitTime == InfiniteValue)
				return InfiniteValue;
			if (totalWaitTime == 0)
				return 0;
			unsigned int timeWaitedAlready = (SystemClockMillis() - startTime);
			return (timeWaitedAlready >= totalWaitTime) ?
					0 : (totalWaitTime - timeWaitedAlready);
		}

		inline void SetExpired() {
			totalWaitTime = 0;
		}

		inline void SetInfinite() {
			totalWaitTime = InfiniteValue;
		}

		inline bool IsInfinite(void) const {
			return (totalWaitTime == InfiniteValue);
		}

		inline unsigned int GetInitialTimeoutValue(void) const {
			return totalWaitTime;
		}

		inline unsigned int GetStartTime(void) const {
			return startTime;
		}
	};
}

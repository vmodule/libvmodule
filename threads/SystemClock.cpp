#include <stdint.h>

#if defined(TARGET_DARWIN)
#include <mach/mach_time.h>
#include <CoreVideo/CVHostTime.h>
#elif defined(TARGET_WINDOWS)
#include <windows.h>
#else
#include <time.h>
#endif
#include <threads/SystemClock.h>

namespace vmodule {

unsigned int SystemClockMillis() {
	uint64_t now_time;
	static uint64_t start_time = 0;
	static bool start_time_set = false;
#if defined(TARGET_DARWIN)
	now_time = CVGetCurrentHostTime() * 1000 / CVGetHostClockFrequency();
#elif defined(TARGET_WINDOWS)
	now_time = (uint64_t)timeGetTime();
#else
	struct timespec ts = { };
#ifdef CLOCK_MONOTONIC_RAW
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
	clock_gettime(CLOCK_MONOTONIC, &ts);
#endif // CLOCK_MONOTONIC_RAW
	now_time = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif
	if (!start_time_set) {
		start_time = now_time;
		start_time_set = true;
	}
	return (unsigned int) (now_time - start_time);
}
const unsigned int EndTime::InfiniteValue =
		std::numeric_limits<unsigned int>::max();

}

#include "Util/Stats.h"

#include "pyconfig.h"

#if HAVE_GETTIMEOFDAY
#include <sys/time.h>
int64_t Timer::GetTime()
{
    struct timeval tv;
#ifdef GETTIMEOFDAY_NO_TZ
    gettimeofday(&tv);
#else
    gettimeofday(&tv, 0);
#endif
    return int64_t(tv.tv_sec) * 1000000000 + int64_t(tv.tv_usec) * 1000;
}
#else  // Need a different definition on other platforms.
int64_t Timer::GetTime()
{
    return 0;
}
#endif  // HAVE_GETTIMEOFDAY

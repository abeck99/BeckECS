#ifndef HELPERS_H__
#define HELPERS_H__
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Mmsystem.h>
#endif

#include "defines.h"

#include <assert.h>
#include <cstdlib>
#include <stdio.h>
#ifdef _WIN32
#else
#include <algorithm>
#include <sys/time.h>
#include <execinfo.h>
#endif
#include <signal.h>
#include <stdlib.h>

static unsigned long
clockTimeMS (void)
{
#ifdef _WIN32
    return timeGetTime();
#else
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) (tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

#ifdef _WIN32
// TODO, figure out the right way to do this in windows
template <typename X>
X Constrain(X minVal, X maxVal, X inVal)
{
    if ( inVal < minVal )
    {
        inVal = minVal;
    }
    if ( inVal > maxVal )
    {
        inVal = maxVal;
    }

    return inVal;
}
#else
template <typename X>
X Constrain(X minVal, X maxVal, X inVal)
{
    return std::min(maxVal, std::max(minVal, inVal));
}
#endif

inline int rangedRand(int N)
{
    if ( N < 2 )
    {
        return 0;
    } // http://eternallyconfuzzled.com/arts/jsw_art_rand.aspx
    return rand() / ( RAND_MAX / N + 1 );
}

inline float rangedRand(float lo, float hi)
{
    int x = rangedRand((int) 1000);
    return lo + ((hi-lo)*(x/1000.f));
}

#ifdef _WIN32
#define STACK_TRACE_TO_STD_ERR
#else
#define STACK_TRACE_TO_STD_ERR void *array[10]; size_t size; size = backtrace(array, 10); backtrace_symbols_fd(array, size, STDERR_FILENO);
#endif

void breakHere();

#ifdef DEBUG_ASSERT
#   define ASSERT(condition, message) { if ( !(condition) ) { breakHere(); printf("`" #condition "` failed in %s line %d : `" #message "`\n", __FILE__, __LINE__ ); STACK_TRACE_TO_STD_ERR; std::exit(EXIT_FAILURE); } }
#else
#   define ASSERT(condition, message)
#endif

#endif // HELPERS_H__
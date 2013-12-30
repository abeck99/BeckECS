#ifndef HELPERS_H__
#define HELPERS_H__
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Mmsystem.h>
#endif

#define EntityID unsigned long long

#define MAP_TYPE std::map
#define MAP_TYPE_HEADER <map>

#include "defines.h"

#include MAP_TYPE_HEADER
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


#define DEBUG_ASSERT

template <typename K, typename V>
bool exists_in(MAP_TYPE<K,V> const& haystack, K const& needle) {
    return haystack.find(needle) != haystack.end();
}  

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

#define INTERNAL_SOCKET_TYPE "inproc://"

#define INTERNAL_SOCKET_NAME(socketName) "" INTERNAL_SOCKET_TYPE #socketName ""
#define EXTERNAL_SOCKET_NAME(socketName) "" EXTERNAL_SOCKET_TYPE #socketName ""

#define SUSPEND_SYSTEMS_MSG INTERNAL_SOCKET_NAME(suspendSystems)
#define RESUME_SYSTEMS_MSG INTERNAL_SOCKET_NAME(resumeSystems)
#define SUSPEND_SYSTEMS_COMPLETE_MSG INTERNAL_SOCKET_NAME(suspendCompleteSystems)
#define SHUTDOWN_SYSTEMS_COMPLETE_MSG INTERNAL_SOCKET_NAME(shutdownCompleteSystems)
#define LOG_MSG_NAME INTERNAL_SOCKET_NAME(log)
#define CREATE_ENTITY INTERNAL_SOCKET_NAME(createEntity)
#define DESTROY_ENTITY INTERNAL_SOCKET_NAME(destroyEntity)
#define ENTITY_DESTROYED INTERNAL_SOCKET_NAME(entityDestroyed)
#define NEW_PLAYER_ID INTERNAL_SOCKET_NAME(newPlayerID)
#define COMPONENT_ADDED_MSG INTERNAL_SOCKET_TYPE



struct ShutdownMsg
{
    bool isFullShutdown;
};


// No struct for CREATE_ENTITY
#define CREATE_ENTITY_MAX_SIZE 1024

struct DestroyEntityMsg
{
    unsigned long timestamp;
    EntityID entityID;
};

struct ComponentAddedMsg
{
    int entityID;
    unsigned long timestamp;
    char serializedData[CREATE_ENTITY_MAX_SIZE];
};

struct PlayerID
{
    EntityID newPlayerID;
};

#endif // HELPERS_H__
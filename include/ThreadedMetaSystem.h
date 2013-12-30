#ifndef THREADED_META_SYSTEM_H__
#define THREADED_META_SYSTEM_H__

#include "ThreadedComponentSystem.h"
#include "Messaging.h"

class ThreadedMetaSystem
{
public:
    ThreadedMetaSystem(float setSleepTime, std::string setName)
    : sleepTime(setSleepTime)
    , name(setName)
    {
    }

    void AddSystem(ThreadedComponentSystem* system);

    void Start();

    // Called by a thread, do not call
    void RunLoop();

    std::string GetSystemMessageName(const char* baseMessageName)
    {
        return std::string(baseMessageName) + name;
    }

private:
    int FillPollInfo(zmq_pollitem_t*& pollItems, void* suspend_shutdown_systems_r);

#ifdef _WIN32
#else
    pthread_t thread;
#endif
    float sleepTime;
    std::string name;

    std::vector<ThreadedComponentSystem*> systems;

    int suspendCountdown;
};


#endif // THREADED_META_SYSTEM_H__
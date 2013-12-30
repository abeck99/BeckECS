#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "ThreadedMetaSystem.h"
#ifdef _WIN32
#else
#include <unistd.h>
#endif
#include "Messaging.h"

#ifdef _WIN32
DWORD WINAPI startSystemRun( LPVOID systemRef )
#else
void* startSystemRun(void* systemRef)
#endif
{
    ThreadedMetaSystem* threadedSystem = (ThreadedMetaSystem*) systemRef;
    threadedSystem->RunLoop();

#ifdef _WIN32
    return 0;
#else
    pthread_exit(NULL);

    return NULL;
#endif
}

int ThreadedMetaSystem::FillPollInfo(zmq_pollitem_t*& pollItems, void* suspend_shutdown_systems_r)
{
    int pollSize = 1;
    for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
    {
        pollSize = pollSize + (*it)->GetNumberReceiveSockets();
    }

    pollItems = (zmq_pollitem_t*) realloc(pollItems, sizeof(zmq_pollitem_t) * pollSize);

    for ( int i=0; i<pollSize; i++ )
    {
        pollItems[i].socket = NULL;
        pollItems[i].fd = 0;
        pollItems[i].events = ZMQ_POLLIN;
    }

    int pollRes;
    zmq_pollitem_t* pollItemsIter = pollItems;
    pollItemsIter->socket = *((void**) suspend_shutdown_systems_r);
    pollItemsIter++;

    for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
    {
        pollItemsIter = (*it)->FillPollInformation(pollItemsIter);
    }

    return pollSize;
}

// Called by a thread, do not call
void ThreadedMetaSystem::RunLoop()
{
    void* shutdown_systems_complete_s = GetSocketSendMessage1ToN(GetSystemMessageName(SHUTDOWN_SYSTEMS_COMPLETE_MSG).c_str());
    void* suspend_systems_complete_s = GetSocketSendMessage1ToN(GetSystemMessageName(SUSPEND_SYSTEMS_COMPLETE_MSG).c_str());

    for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
    {
        (*it)->CreateBindSockets();
    }

    // TODO: Sync with threads...
    // Sleep for 2 seconds, letting everyone create their send sockets
#ifdef _WIN32
    Sleep(1);
#else
    sleep(2);
#endif

    // Communicating with other threads
    void* suspend_shutdown_systems_r = GetSocketReceiveMessage1ToN(SUSPEND_SYSTEMS_MSG, sleepTime*1000);
    void* resume_systems_r = GetSocketReceiveMessage1ToN(RESUME_SYSTEMS_MSG, sleepTime*1000);

    for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
    {
        (*it)->CreateConnectSockets();
    }

    zmq_pollitem_t* pollItems = NULL;
    int pollSize = FillPollInfo(pollItems, suspend_shutdown_systems_r);
    int pollRes;

    bool running = true;
    bool suspending = false;
    bool shuttingDown = false;

    ShutdownMsg shutdownMsg;
    long sleepTimeMs = sleepTime*1000;
    while (running)
    {
        bool didMessage = false;

        for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
        {
            didMessage = (*it)->Tick(suspending || shuttingDown) || didMessage;
        }


        unsigned long curTime = clockTimeMS();
        long curSleepTime = sleepTimeMs;
        if ( suspending || shuttingDown )
        {
            curSleepTime = 100;
        }

#ifdef SYSTEM_DYNAMIC_LINKING
        pollSize = FillPollInfo(pollItems, suspend_shutdown_systems_r);
#endif
        do {
            unsigned long waitTime = curSleepTime - (clockTimeMS()-curTime);
            if ( waitTime <= 0 )
            {
                break;
            }
            pollRes = zmq_poll( pollItems, pollSize, waitTime );
            ASSERT( pollRes >= 0 || errno == EINTR, "An error occured during poll!" );

            if ( pollRes > 0 )
            {
                zmq_pollitem_t* pollItemsIter = pollItems;
                didMessage = true;

                if ( pollItemsIter->revents & ZMQ_POLLIN )
                {
                    ReceiveMessageBlocking<ShutdownMsg>(suspend_shutdown_systems_r, &shutdownMsg);
                    if ( !shutdownMsg.isFullShutdown )
                    {
                        ASSERT( !suspending, "Thread somehow received serialized state request while already shutting down!" );
                        suspending = true;
                    }
                    if ( shutdownMsg.isFullShutdown )
                    {
                        if ( !shuttingDown )
                        {
                            shuttingDown = true;
                            for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
                            {
                                (*it)->Shutdown();
                            }
                        }
                        else
                        {
                            //printf("Received second shutdown\n");
                            running = false;
                            break;
                        }
                    }
                    didMessage = true;
                    suspendCountdown = 5;
                }
                pollItemsIter++;

                for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
                {
                    pollItemsIter = (*it)->ProcessEvents(pollItemsIter);
                }
            }

            if ( suspending || shuttingDown )
            {
                curSleepTime = 100;
            }
        } while (pollRes > 0 && (clockTimeMS() - curTime) < curSleepTime );

        if ( !running )
        {
            break;
        }

        if ( suspending || shuttingDown )
        {
            if ( !didMessage )
            {
                suspendCountdown--;
            }
            else
            {
                suspendCountdown = 5;
            }
        }

        if ( suspending && !didMessage && suspendCountdown <= 0 )
        {
            SendMessage<ShutdownMsg>(suspend_systems_complete_s, &shutdownMsg);
            if ( ReceiveMessageBlocking<ShutdownMsg>(resume_systems_r, &shutdownMsg) )
            {
                suspending = false;
            }
        }

        if ( shuttingDown && !suspending && !didMessage && suspendCountdown <= 0 )
        {
            running = false;
            break;
        }
    }

    DestroySocket(suspend_shutdown_systems_r);
    DestroySocket(resume_systems_r);
    DestroySocket(suspend_systems_complete_s);

    //printf("%s shutdown! Informing run loop\n", GetSystemMessageName("MetaSystem-").c_str());
    SendMessage<ShutdownMsg>(shutdown_systems_complete_s, &shutdownMsg);
    DestroySocket(shutdown_systems_complete_s);

    for ( std::vector<ThreadedComponentSystem*>::iterator it = systems.begin(); it != systems.end(); ++it )
    {
        (*it)->CheckSocketsAndDestroy();
    }

    free(pollItems);
}

void ThreadedMetaSystem::Start()
{
#ifdef _WIN32
    HANDLE thread;
    DWORD threadID;

    thread = CreateThread(
            NULL,
            0,
            startSystemRun,
            this,
            0,
            &threadID);

#else
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // Start myself
    pthread_create(&thread, &attr, 
                      startSystemRun, this);
#endif
}

void ThreadedMetaSystem::AddSystem(ThreadedComponentSystem* system)
{
    systems.push_back(system);
}

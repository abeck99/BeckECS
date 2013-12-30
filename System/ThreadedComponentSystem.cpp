#include "ThreadedComponentSystem.h"
#include "SystemManager.h"

// These should both be "friends" with SystemManager
bool ThreadedComponentSystem::Tick(bool isSuspended)
{
    didMessage = false;

    if ( !isSuspended )
    {
        RunGameLogic();
    }

    return didMessage;
}

void ThreadedComponentSystem::Shutdown()
{
    
}

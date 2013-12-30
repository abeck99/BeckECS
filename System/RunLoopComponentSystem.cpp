#include "RunLoopComponentSystem.h"
#include "SystemManager.h"

// These should both be "friends" with SystemManager
bool RunLoopComponentSystem::Tick(const float& timePassed, bool isSuspended)
{
    didMessage = false;

    if ( !isSuspended )
    {
        RunGameLogic(timePassed);
    }

    return didMessage;
}

#include "TimerOwnerSystem.h"

ComponentStore_* TimerOwnerSystem::CreateComponentStore()
{
    return new ComponentStore<TimerOwnerComponent>();
}

extern "C" ThreadedComponentSystem* GetNewTimerOwnerSystem()
{
    return new TimerOwnerSystem();
}

void TimerOwnerSystem::RunGameLogic()
{

}

void TimerOwnerSystem::ComponentAdded(EntityID entityID, Component* component)
{
    TimerOwnerComponent* ownerComponent = (TimerOwnerComponent*) component;
    // Simple caching, assumes only one owner (in a real implementation it would be a map to a vector or set)
    timerIDtoOwnerID[ownerComponent->timerid()] = entityID;
}

void TimerOwnerSystem::ComponentRemoved(EntityID entityID, Component* removedComponent)
{
    TimerOwnerComponent* ownerComponent = (TimerOwnerComponent*) removedComponent;
    timerIDtoOwnerID.erase(ownerComponent->timerid());
}

EntityID TimerOwnerSystem::OwnerOfTimer(EntityID timerID)
{
    std::map<EntityID, EntityID>::iterator compIT = timerIDtoOwnerID.find(timerID);
    if ( compIT != timerIDtoOwnerID.end() )
    {
        return (*compIT).second;
    }

    return 0;
}

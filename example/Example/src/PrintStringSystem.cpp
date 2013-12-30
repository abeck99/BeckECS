#include "PrintStringSystem.h"

ComponentStore_* PrintStringSystem::CreateComponentStore()
{
    return new ComponentStore<PrintStringComponent>();
}

extern "C" ThreadedComponentSystem* GetNewPrintStringSystem()
{
    return new PrintStringSystem();
}

void PrintStringSystem::RunGameLogic()
{
    printf("Print String System running logic!\n");
}

void PrintStringSystem::ReceiveTimerFired(timerFiredStruct& msg)
{
    GET_COMPONENTS(PrintStringComponent)

    MAP_TYPE<EntityID, PrintStringComponent*>::iterator compIT = components.find(msg.triggeredID);

    if ( compIT == components.end() )
    {
        // Ignore
        return;
    }

    PrintStringComponent*& comp = (*compIT).second;
    printf("Printing from trigger: %s\n", comp->displaymessage().c_str());

    if ( timerOwnerSystem->OwnerOfTimer(msg.timerID) == msg.triggeredID )
    {
        // Just showing how friend systems work
        printf("Print String system just checked, and the timer triggered it's owner\n");
    }
}

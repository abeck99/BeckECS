#include "PrintIntSystem.h"

ComponentStore_* PrintIntSystem::CreateComponentStore()
{
    return new ComponentStore<PrintIntComponent>();
}

extern "C" ThreadedComponentSystem* GetNewPrintIntSystem()
{
    return new PrintIntSystem();
}

void PrintIntSystem::RunGameLogic()
{
    printf("Print Int System running logic!\n");
}

void PrintIntSystem::ReceiveTimerFired(timerFiredStruct& msg)
{
    GET_COMPONENTS(PrintIntComponent)

    MAP_TYPE<EntityID, PrintIntComponent*>::iterator compIT = components.find(msg.triggeredID);

    if ( compIT == components.end() )
    {
        // Ignore
        return;
    }

    PrintIntComponent*& comp = (*compIT).second;
    printf("Printing from trigger: %u\n", comp->displayint());
}

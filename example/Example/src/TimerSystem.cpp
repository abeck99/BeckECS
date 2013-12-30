#include "TimerSystem.h"

ComponentStore_* TimerSystem::CreateComponentStore()
{
    return new ComponentStore<TimerComponent>();
}

extern "C" RunLoopComponentSystem* GetNewTimerSystem()
{
    return new TimerSystem();
}

void TimerSystem::RunGameLogic(const float& timePassed)
{
    ITERATE_THROUGH_COMPONENTS_START(TimerComponent)
        if ( comp->curtime() < comp->triggertime() )
        {
            comp->set_curtime( comp->curtime() + timePassed );
            if ( comp->curtime() >= comp->triggertime() )
            {
                timerFiredStruct timerFired;
                timerFired.timerID = entityID;
                timerFired.triggeredID = comp->triggerentityid();
                SendTimerFired(timerFired);

                if ( comp->repeats() )
                {
                    comp->set_curtime( comp->triggertime() - comp->curtime() );
                }
            }
        }
    ITERATE_THROUGH_COMPONENTS_END
}

// Response is required, read from triggerTimerRequest and fill in triggerTimerReply to reply
void TimerSystem::ProvideTriggerTimer()
{
    GET_COMPONENTS(TimerComponent)

    MAP_TYPE<EntityID, TimerComponent*>::iterator it = components.find(triggerTimerRequest.timerid());
    if ( it == components.end() )
    {
        triggerTimerReply.set_didtrigger(false);
        triggerTimerReply.set_failurereason( TimerRequests::TriggerResultMsg_TriggerFailureReason_TRIGGER_DOES_NOT_EXIST );
        return;
    }

    TimerComponent*& comp = (*it).second;
    if ( comp->curtime() < comp->triggertime() )
    {
        triggerTimerReply.set_didtrigger(false);
        triggerTimerReply.set_failurereason( TimerRequests::TriggerResultMsg_TriggerFailureReason_TRIGGER_ALREADY_RUNNING );
        return;
    }

    comp->set_curtime(0.0);
    triggerTimerReply.set_didtrigger(true);
}

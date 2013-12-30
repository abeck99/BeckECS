#ifndef RUN_LOOP_COMPONENT_SYSTEM_H__
#define RUN_LOOP_COMPONENT_SYSTEM_H__

#include "ComponentSystem.h"

class RunLoopComponentSystem : public ComponentSystem
{
public:
    RunLoopComponentSystem()
    : ComponentSystem()
    {}


    // These should both be "friends" with SystemManager
    bool Tick(const float& timePassed, bool isSuspended);

protected:
    virtual void RunGameLogic(const float& timePassed) {}
};


#endif // RUN_LOOP_COMPONENT_SYSTEM_H__
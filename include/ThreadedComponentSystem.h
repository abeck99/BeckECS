#ifndef THREADED_COMPONENT_SYSTEM_H__
#define THREADED_COMPONENT_SYSTEM_H__

#include "ComponentSystem.h"

class ThreadedComponentSystem : public ComponentSystem
{
public:
    ThreadedComponentSystem()
    : ComponentSystem()
    {}
    

    // These should both be "friends" with SystemManager
    bool Tick(bool isSuspended);

    virtual void Shutdown();

protected:
    virtual void RunGameLogic() {}
};


#endif // THREADED_COMPONENT_SYSTEM_H__
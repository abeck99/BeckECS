#ifndef ENTITY_MANAGER_H__
#define ENTITY_MANAGER_H__

#include "defines.h"
#include MAP_TYPE_HEADER
#include <string>

class EntityManager
{
public:
    EntityManager(EntityID setCurEntityID);
    // Need to call CheckSocketsAndDestroy before destroying
    ~EntityManager();

    void SetCurEntityID(EntityID setCurEntityID, bool force = false)
    {
        if ( !force && curEntityID < setCurEntityID )
           curEntityID = setCurEntityID;
    }

    // Returns true if processed (send or receive) a message
    bool Tick(unsigned long ticks, bool isSuspended);

    // Called from system manager to create component
    //      create send sockets
    void CreateComponentSocket(std::string componentName);

    // Due to the order that send receive sockets must be preped in,
    //      this is not part of the init
    // Create receive sockets...
    void CreateEntitySockets();

    void CreateConnectSockets();

    void CheckSocketsAndDestroy();

private:
    EntityID curEntityID;

    void CreateEntity(std::string data);
    void DestroyEntity(EntityID id);

    void* createEntity_r;
    void* destroyEntity_r;
    void* entityDestroy_s;

    MAP_TYPE<std::string, void*> createComponent_sMap;
};

#endif // ENTITY_MANAGER_H__
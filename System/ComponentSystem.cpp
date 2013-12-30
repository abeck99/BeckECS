#include "ComponentSystem.h"

void ComponentSystem::init(SystemManager* sysManager)
{
    GetFriends(sysManager);
}

void ComponentSystem::ComponentAdded(EntityID entityID, Component* component)
{
    requests = (EntityID*) realloc(requests, sizeof(EntityID) * componentStore->StoreSize());
}

void ComponentSystem::ComponentRemoved(EntityID entityID, Component* removedComponent)
{

}

// Entity destroyed will call component removed (just in case it exists) and should also check for
//  entity references if they exist
void ComponentSystem::EntityDestroyed(EntityID entityID)
{

}

void ComponentSystem::CreateBindSockets()
{

}

void ComponentSystem::GetFriends(SystemManager* interface)
{
    
}

void ComponentSystem::StripEntityReferences(EntityID entityID)
{

}

void ComponentSystem::EntityRefRemoved(EntityID referringID, EntityID referredID)
{

}

void ComponentSystem::CreateConnectSockets()
{
    destroyEntity_r = GetSocketReceiveMessage1ToN( ENTITY_DESTROYED );
    componentUpdated_r = GetSocketReceiveMessage1ToN( GetSystemMessageName(COMPONENT_ADDED_MSG).c_str() );
    newPlayer_r = GetSocketReceiveMessage1ToN( NEW_PLAYER_ID );

    logError.CreateConnectSocket(SystemName().c_str(), MSG_ERROR);
    logGameplay.CreateConnectSocket(SystemName().c_str(), MSG_IMPORTANT_GAMEPLAY);
    logWarning.CreateConnectSocket(SystemName().c_str(), MSG_WARNING);
    logDebug.CreateConnectSocket(SystemName().c_str(), MSG_DEBUG);
    logInfo.CreateConnectSocket(SystemName().c_str(), MSG_INFO);

}

void ComponentSystem::CheckSocketsAndDestroy()
{
    ComponentAddedMsg componentAddedMsg;
    while ( ReceiveMessage<ComponentAddedMsg>(componentUpdated_r, &componentAddedMsg) )
    {
        logError << "ComponentSystem: Received ComponentUpdated that was swallowed by shutdown! " << componentAddedMsg.entityID << std::endl;
    }
    DestroySocket(componentUpdated_r);
    componentUpdated_r = NULL;

    DestroyEntityMsg destroyStruct;
    while ( ReceiveMessage<DestroyEntityMsg>(destroyEntity_r, &destroyStruct) )
    {
        logError << "ComponentSystem: Received DestroyEntity that was swallowed by shutdown! " << destroyStruct.entityID << std::endl;
    }
    DestroySocket(destroyEntity_r);
    destroyEntity_r = NULL;

        PlayerID playerIDStruct;
        while ( ReceiveMessage<PlayerID>(newPlayer_r, &playerIDStruct) )

    PlayerID playerIDStruct;
    while ( ReceiveMessage<PlayerID>(newPlayer_r, &playerIDStruct) )
    {
    }
    DestroySocket(newPlayer_r);
    newPlayer_r = NULL;

    logError.CloseSocket();
    logGameplay.CloseSocket();
    logWarning.CloseSocket();
    logDebug.CloseSocket();
    logInfo.CloseSocket();
}

void ComponentSystem::AddComponentToStore(EntityID entityID, std::string data, unsigned long timestamp)
{
    CreateComponentStoreIfNeeded();
    Component* component = componentStore->AddComponentToStore(entityID, data, timestamp);
    if ( component != NULL )
    {
        ComponentAdded(entityID, component);
    }
}

bool ComponentSystem::RemoveComponentFromStore(EntityID entityID, unsigned long timestamp)
{
    Component* removedComponent = componentStore->RemoveComponentFromStore(entityID, timestamp);
    if ( removedComponent != NULL )
    {
        ComponentRemoved(entityID, removedComponent);
        componentStore->FinalizeRemove(removedComponent);
        return true;
    }

    return false;
}

int ComponentSystem::GetNumberReceiveSockets()
{
    return 3;
}

zmq_pollitem_t* ComponentSystem::FillPollInformation(zmq_pollitem_t* pollItems)
{
    pollItems[0].socket = *((void**) destroyEntity_r);
    pollItems[1].socket = *((void**) componentUpdated_r);
    pollItems[2].socket = *((void**) newPlayer_r);

    return (pollItems + 3);
}

zmq_pollitem_t* ComponentSystem::ProcessEvents(zmq_pollitem_t* pollItems)
{
    // TODO_ENTITY_REUSE: Test reusing entity ids on component system:
    //      Have an entity, say ID 1, with a component on a system that has a large tick time (say 10 seconds)
    //      Delete the entity
    //      Immedietly reuse the entity ID and create a new component
    //      Check that the component exists after the large tick
    //      Try again, removing the timestamp check below and see if it exists
    // TODO_ENTITY_REUSE: How to cover the case where a system adds a component?
    //      Creating a component from a system should reuse the timestamp from it's component
    //      Therefore, all components have the same timestamp for an entity
    //      Run a test as above for that


    //      For example, system A hasn't received a delete entity request yet, it spawns a new component to B
    //      System B did, and deleted his component
    //      System B receives system As component next tick, and now B has a hanging component

    //      TODO: Entity manager should send a "sanity check" entity removed to cleanup after the above case

    if ( pollItems[0].revents & ZMQ_POLLIN )
    {
        ComponentAddedMsg componentUpdatedMsg;

        ReceiveMessage<ComponentAddedMsg>(componentUpdated_r, &componentUpdatedMsg);
        if ( strlen(componentUpdatedMsg.serializedData) == 0 )
        {
            if ( !RemoveComponentFromStore(componentUpdatedMsg.entityID, componentUpdatedMsg.timestamp) )
            {
                logDebug << "Removing a component that does not exist! {{" << componentUpdatedMsg.entityID << "}}" << std::endl;
            }
        }
        else
        {
            AddComponentToStore(componentUpdatedMsg.entityID, std::string(componentUpdatedMsg.serializedData), componentUpdatedMsg.timestamp);
        }

        didMessage = true;
    }
    if ( pollItems[1].revents & ZMQ_POLLIN )
    {
        DestroyEntityMsg destroyStruct;
        while ( ReceiveMessage<DestroyEntityMsg>(destroyEntity_r, &destroyStruct) )
        {
            RemoveComponentFromStore(destroyStruct.entityID, destroyStruct.timestamp);

            EntityDestroyed(destroyStruct.entityID);
            didMessage = true;
        }
    }
    if ( pollItems[2].revents & ZMQ_POLLIN )
    {
        PlayerID playerIDStruct;
        while ( ReceiveMessage<PlayerID>(newPlayer_r, &playerIDStruct) )
        {
            curPlayerID = playerIDStruct.newPlayerID;
            didMessage = true;
        }
    }

    return (pollItems + 3);
}

void ComponentSystem::ClearStore()
{
    CreateComponentStoreIfNeeded();
    componentStore->ClearStore();
}

std::string ComponentSystem::GetSystemMessageName(const char* baseMessageName)
{
    CreateComponentStoreIfNeeded();
    return std::string(baseMessageName) + componentStore->GetComponentName();
}

MAP_TYPE<EntityID, std::string> ComponentSystem::GetAllComponentsDataFromStore()
{
    CreateComponentStoreIfNeeded();
    return componentStore->GetAllComponentsData();
}

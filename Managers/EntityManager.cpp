#include "EntityManager.h"
#include "Messaging.h"
#include "helpers.h"
#include "Components/Entity.pb.h"
#include "Logging.h"

LogStream logError;

EntityManager::EntityManager(EntityID setCurEntityID)
: createEntity_r(NULL)
, destroyEntity_r(NULL)
, entityDestroy_s(NULL)
, curEntityID(setCurEntityID)
{
    ASSERT(curEntityID >= 0, "Invalid Starting entityID!");
}

EntityManager::~EntityManager()
{
    // Need to call CheckSocketsAndDestroy before destroying
    ASSERT(createEntity_r == NULL, "EntityManager: Sockets were not shutdown!");
    ASSERT(destroyEntity_r == NULL, "EntityManager: Sockets were not shutdown!");
    ASSERT(entityDestroy_s == NULL, "EntityManager: Sockets were not shutdown!");
}

void EntityManager::CreateComponentSocket(std::string componentName)
{
    bool exists = exists_in<std::string, void*>(createComponent_sMap, componentName);
    ASSERT ( !exists, "Component name added twice!" );
    std::string addName = std::string(COMPONENT_ADDED_MSG) + componentName;
    void* createComponent_s = GetSocketSendMessage1ToN( addName.c_str() );
    createComponent_sMap[componentName] = createComponent_s;
}

void EntityManager::CreateEntitySockets()
{
    ASSERT(createEntity_r == NULL, "EntityManager: Sockets were already created!");
    ASSERT(destroyEntity_r == NULL, "EntityManager: Sockets were already created!");
    ASSERT(entityDestroy_s == NULL, "EntityManager: Sockets were already created!");

    createEntity_r = GetSocketReceiveMessageNTo1(CREATE_ENTITY);
    destroyEntity_r = GetSocketReceiveMessageNTo1(DESTROY_ENTITY);
    entityDestroy_s = GetSocketSendMessage1ToN(ENTITY_DESTROYED);
}

void EntityManager::CreateConnectSockets()
{
    logError.CreateConnectSocket("EntityManager", MSG_ERROR);
}

bool EntityManager::Tick(unsigned long ticks, bool isSuspended)
{
    bool didMessage = false;

    // Destroying entities
    DestroyEntityMsg destroyStruct;
    while ( ReceiveMessage<DestroyEntityMsg>(destroyEntity_r, &destroyStruct) )
    {
        SendMessage<DestroyEntityMsg>(entityDestroy_s, &destroyStruct);
        didMessage = true;
    }

    // Creating entities
    char serializedEntity[CREATE_ENTITY_MAX_SIZE+1];
    while (1)
    {
        int dataSize = ReceiveRawMessage(createEntity_r, serializedEntity, CREATE_ENTITY_MAX_SIZE);
        if ( dataSize > 0 )
        {
            ASSERT(dataSize != CREATE_ENTITY_MAX_SIZE, "Serialized data can't be larger than CREATE_ENTITY_MAX_SIZE");
            if ( isSuspended )
            {
                logError << "An entity is being created right at save/quit time! This information could be lost!" << std::endl;
                logError << serializedEntity << std::endl;
            }

            curEntityID++;

            ASSERT(curEntityID != 0, "Looks like a wrap around occured, we may have a max size problem...");

            // 0MQ messages are not terminated, so terminate the data
            serializedEntity[dataSize] = '\0';

            // Deserialize and dispatch
            Entity::CreateEntity entity;
            bool res = entity.ParseFromString(std::string(serializedEntity, (size_t) dataSize));
            ASSERT( res, "Parsing entity failed!" );

            const google::protobuf::RepeatedPtrField<Entity::ComponentData>& components = entity.components(); 
            for (google::protobuf::RepeatedPtrField<Entity::ComponentData>::const_iterator it = components.begin(); it != components.end(); ++it )
            {
                ComponentAddedMsg componentAddedMsg;
                componentAddedMsg.entityID = curEntityID;
                componentAddedMsg.timestamp = ticks;

                bool exists = exists_in<std::string, void*>(createComponent_sMap, (*it).name());
                ASSERT( exists, "Component type not found when deserializing entity!" );
                sprintf( componentAddedMsg.serializedData, "%s", ((*it).data()).c_str() );
                SendMessage<ComponentAddedMsg>(createComponent_sMap[(*it).name()], &componentAddedMsg);
            }

            didMessage = true;
        }
        else
        {
            break;
        }
    }

    return didMessage;
}

void EntityManager::CheckSocketsAndDestroy()
{
    DestroyEntityMsg destroyStruct;
    while ( ReceiveMessage<DestroyEntityMsg>(destroyEntity_r, &destroyStruct) )
    {
        logError << "Received a destroy entity request that was swallowed by shutdown! {{" << destroyStruct.entityID << "}}" << std::endl;
    }

    while (1)
    {
        // Creating entities
        char serializedEntity[CREATE_ENTITY_MAX_SIZE+1];
        int dataSize = ReceiveRawMessage(createEntity_r, serializedEntity, CREATE_ENTITY_MAX_SIZE);
        ASSERT(dataSize != CREATE_ENTITY_MAX_SIZE, "Serialized data can't be larger than CREATE_ENTITY_MAX_SIZE");
        if ( dataSize > 0 )
        {
            logError << "Received a create entity request that was swallowed by shutdown!" << std::endl;
            logError << serializedEntity << std::endl;
        }
        else
        {
            break;
        }
    }

    DestroySocket(createEntity_r);
    createEntity_r = NULL;

    DestroySocket(destroyEntity_r);
    destroyEntity_r = NULL;

    DestroySocket(entityDestroy_s);
    entityDestroy_s = NULL;

    for( MAP_TYPE<std::string, void*>::iterator i = createComponent_sMap.begin(); i != createComponent_sMap.end(); ++i )
    {
        DestroySocket((*i).second);
    }
    createComponent_sMap.clear();
    
    logError.CloseSocket();
}

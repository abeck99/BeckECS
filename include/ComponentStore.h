#ifndef COMPONENT_STORE_H__
#define COMPONENT_STORE_H__

#include "EntityManager.h"
#include "Component.h"
#include "helpers.h"
#include MAP_TYPE_HEADER


class ComponentStore_
{
public:
    virtual Component* AddComponentToStore(EntityID entityID, std::string serializedData, unsigned long timestamp) = 0;
    virtual Component* RemoveComponentFromStore(EntityID entityID, unsigned long timestamp) = 0;
    virtual void FinalizeRemove(Component* removedComponent) = 0;
    virtual MAP_TYPE<EntityID, std::string> GetAllComponentsData() = 0;
    virtual Component* GetComponent(EntityID entityID) = 0;
    virtual std::string GetComponentName() = 0;
    virtual int StoreSize() = 0;
    virtual void ClearStore() = 0;
    virtual ~ComponentStore_() {}
};

template <class componentType>
class ComponentStore : public ComponentStore_
{
public:
    ComponentStore()
    {
        componentType testType(0);
        ASSERT( dynamic_cast<Component*>(&testType) != NULL, "Type is not a component!");

        // This will probably work as a static_assert...
        componentName = componentType::componentName();
    }

    virtual Component* AddComponentToStore(EntityID entityID, std::string data, unsigned long timestamp)
    {
        if ( exists_in<EntityID, componentType*>(components, entityID) )
        {
            componentType* component = components[entityID];
            if ( timestamp < component->Timestamp() )
            {
                return NULL;
            }
            ASSERT( timestamp != component->Timestamp(), "Got two components for the same entity with the same timestamp, EntityManager or serialized data likely made a mistake!" ());
        }

        componentType* component = new componentType(timestamp);
        bool res = component->ParseFromString(data);
        ASSERT( res, "Adding component to store failed!" );
        components[entityID] = component;
        return component;
    }

    virtual Component* RemoveComponentFromStore(EntityID entityID, unsigned long timestamp)
    {
        if ( exists_in<EntityID, componentType*>(components, entityID) )
        {
            componentType* component = components[entityID];
            if ( timestamp >= component->Timestamp() )
            {
                components.erase(entityID);
                return component;
            }
        }
        return NULL;
    }

    virtual void FinalizeRemove(Component* removedComponent)
    {
        delete ((componentType*) removedComponent);
    }

    virtual MAP_TYPE<EntityID, std::string> GetAllComponentsData()
    {
        MAP_TYPE<EntityID, std::string> retMap;

        for( typename MAP_TYPE<EntityID, componentType*>::iterator i = components.begin(); i != components.end(); ++i )
        {
            std::string retString;
            bool res = (*i).second->SerializeToString(&retString);
            ASSERT( res, "Serlizaing component from store failed!" );
            retMap[(*i).first] = retString;
        }

        return retMap;
    }

    virtual Component* GetComponent(EntityID entityID)
    {
        if ( exists_in<EntityID, componentType*>(components, entityID) )
        {
            return (componentType*) components[entityID];
        }
        return NULL;
    }

    virtual std::string GetComponentName()
    {
        return componentName;
    }

    // Not thread safe! Do you know what you're doing when you do this?
    virtual void ClearStore()
    {
        for( typename MAP_TYPE<EntityID, componentType*>::iterator i = components.begin(); i != components.end(); ++i )
        {
            delete (*i).second;
        }

        components.clear();
    }

    virtual int StoreSize()
    {
        return components.size();
    };


    MAP_TYPE<EntityID, componentType*> components;
    std::string componentName;
};

#endif
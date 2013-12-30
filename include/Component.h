#ifndef COMPONENT_H__
#define COMPONENT_H__

#include "helpers.h"
#include <string>

class Component {
public:
    Component(unsigned long setTimestamp)
    : timestamp(setTimestamp)
    {}

    float Timestamp()
    {
        return timestamp;
    }

protected:
    
    // This is actually the entities timestamp
    //  if adding a component to an existing entity, you should
    //  always use the same timestamp as an existing component
    unsigned long timestamp;
};

#endif

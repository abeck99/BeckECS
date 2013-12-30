#ifndef DEFINES_H__
#define DEFINES_H__

#define EntityID unsigned long long

#define MAP_TYPE std::map
#define MAP_TYPE_HEADER <map>

struct timerFiredStruct
{
    EntityID timerID;
    EntityID triggeredID;
};

#endif //DEFINES_H__
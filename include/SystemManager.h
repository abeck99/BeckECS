#ifndef SYSTEM_MANAGER_H__
#define SYSTEM_MANAGER_H__

#include "ComponentSystem.h"
#include "ComponentStore.h"
#include "ThreadedComponentSystem.h"
#include "ThreadedMetaSystem.h"
#include "RunLoopComponentSystem.h"
#include <vector>
#include <set>
#include MAP_TYPE_HEADER
#include <string>
#include "Messages/basics.pb.h"
#include "Logging.h"

#ifndef SYSTEM_DYNAMIC_LINKING
#define ADD_SYSTEM(SysName) {SysName* newSys = new SysName(); AddRunLoopSystem(newSys);}
#define ADD_THREADED_SYSTEM(SysName, metaSystemVar) {SysName* newSys = new SysName(); AddThreadedSystem(newSys, metaSystemVar);}
#endif

// All SystemManager calls must be made on the MAIN THREAD!
class SystemManager
{
public:
    SystemManager(float setRunLoopSleepTime = 1.f, double setTimeScale = 0.0);
    virtual ~SystemManager();

    // StartupSystems probably shouldn't be called
    //      multiple times, instead a new system manager must be created
    void StartUpSystems(const char* data, size_t size, EntityID setCurEntityID = 0);

    // Override this function to call "CustomSystem->Register(this);" to add gameplay systems
    //      This will automatically add itself to the correct main or threaded system
    virtual bool RegisterAllSystems() = 0;
#ifdef SYSTEM_DYNAMIC_LINKING
    virtual bool CreateAllSystems() = 0;
#endif

    // Threaded, do not call
    bool RunLoop(void* suspend_shutdown_systems_s, void* resume_systems_s, void* system_request_provide, void* setTimeScale_provide);

    // These should be friend functions, please do not call directly
    bool AddThreadedMetaSystem(ThreadedMetaSystem* metaSystem);
    bool AddThreadedSystem(ThreadedComponentSystem* system, ThreadedMetaSystem* metaSystem);
    bool AddRunLoopSystem(RunLoopComponentSystem* system);
#ifdef SYSTEM_DYNAMIC_LINKING
    bool AddDynamicSystem(std::string filename, std::string systemName);
#endif

    virtual ComponentSystem* GetComponentSystem(std::string systemName)
    {
        return nameToSystemMap[systemName];
    };

private:
    // Used for loading initial state from saved data or world generation
    //      pass in an empty string to set state to nothing
    bool SetInitialStates(const char* data, size_t size);

    std::string _InternalGetSerializedData();

    EntityManager* entityManager;

    MAP_TYPE<std::string, ComponentSystem*> nameToSystemMap;

    // Ticked on their own threads, redundant for easy use
    std::vector<ThreadedMetaSystem*> threadedSystems;
    // Ticked on the run loop thread, redundant for easy use
    std::vector<RunLoopComponentSystem*> runLoopSystems;
    
    // This number doesn't change and the vector is
    //      being used on the run loop, so we remember
    //      this for the main thread
    int numThreadedSystems;

    bool creatingSystems;
    bool systemsStarted;

#ifdef _WIN32
#else
    pthread_t thread;
#endif
    // Protobufs for communicationing interprocess
    BuiltIn::SetTimeScale setTimeScaleMsg;
    BuiltIn::SystemRequest systemRequestMsg;
    BuiltIn::SystemResponse systemResponseMsg;


    float sleepTime;
    double timeScale;

    int suspendCountdown;

    LogOutput logOutput;

    int FillPollInfo(zmq_pollitem_t*& pollItems, void* setTimeScale_provide, void* system_request_provide);

    void CleanSystems();

    std::string storedState;
#ifdef SYSTEM_DYNAMIC_LINKING
protected:
    void LoadSystems();
    bool HasSystemChanged(std::string targetname, std::string filename);
    bool HasAnySystemChanged();

    std::map< std::string, std::string > dynamicWatches;
    std::set< std::string > systemsToRecompile;
    std::map< std::string, std::string > dynamicSystems;
    std::map< std::string, int > fileTimestamps;
    std::map< std::string, unsigned int > fileHashes;
    std::map< std::string, void* > systemLibs;
#endif
};

#endif // SYSTEM_MANAGER_H__
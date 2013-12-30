#include "helpers.h"
#include "SystemManager.h"
#include "RunLoopComponentSystem.h"
#include "ThreadedComponentSystem.h"
#include "Messaging.h"
#include "Components/Entity.pb.h"


#ifdef _WIN32
#else
#include <unistd.h>
#ifdef SYSTEM_DYNAMIC_LINKING
#include <sys/types.h>
#include <sys/stat.h> 
#include <dlfcn.h>

void* RecompileSystemThread(void* systemVoid)
{
    std::set< std::string >* systems = (std::set< std::string >*) systemVoid;
    for ( std::set< std::string >::iterator it = systems->begin(); it != systems->end(); ++it )
    {
        std::string systemName = *it;
        std::string wafCommand = std::string("./waf -b --target ") + systemName;
        system(wafCommand.c_str());
    }

    pthread_exit(NULL);
    return NULL;
}
#endif
#endif // _WIN32

SystemManager::SystemManager(float setRunLoopSleepTime, double setTimeScale)
: creatingSystems(false)
, systemsStarted(false)
, sleepTime(setRunLoopSleepTime)
, timeScale(setTimeScale)
, entityManager(NULL)
{}

SystemManager::~SystemManager()
{
    // We should make sure that ShutDownSystems() has finished before entering here
    ASSERT(systemsStarted == false, "Systems still hanging on quit!");
}

void SystemManager::StartUpSystems(const char* data, size_t size, EntityID setCurEntityID)
{
    ASSERT(systemsStarted == false, "Systems have already been started!");
    systemsStarted = true;

    SetupMessaging();

    entityManager = new EntityManager(setCurEntityID);

#ifdef _WIN32
    srand((int)clockTimeMS());
#else
    srand((int)time(NULL));
#endif

    creatingSystems = true;
    RegisterAllSystems();
#ifdef SYSTEM_DYNAMIC_LINKING
    LoadSystems();
#endif
    creatingSystems = false;

    for( MAP_TYPE<std::string, ComponentSystem*>::iterator i = nameToSystemMap.begin(); i != nameToSystemMap.end(); ++i )
    {
        (*i).second->init(this);
    }

    SetInitialStates(data, size);

    // Communicating with other threads
    void* suspend_shutdown_systems_s = GetSocketSendMessage1ToN(SUSPEND_SYSTEMS_MSG);
    void* resume_systems_s = GetSocketSendMessage1ToN(RESUME_SYSTEMS_MSG);
    // Communicating to control process
#ifdef USE_TCP
    void* system_request_provide = GetSocketProvideMessage(EXTERNAL_SOCKET_NAME(60001));
    void* setTimeScale_provide = GetSocketProvideMessage(EXTERNAL_SOCKET_NAME(60002));
#else
    void* system_request_provide = GetSocketProvideMessage(EXTERNAL_SOCKET_NAME(systemRequest));
    void* setTimeScale_provide = GetSocketProvideMessage(EXTERNAL_SOCKET_NAME(setTimeScale));
#endif

    while (RunLoop(suspend_shutdown_systems_s, resume_systems_s, system_request_provide, setTimeScale_provide))
    {
#ifdef SYSTEM_DYNAMIC_LINKING
        creatingSystems = true;
        LoadSystems();
        creatingSystems = false;
        for( MAP_TYPE<std::string, ComponentSystem*>::iterator i = nameToSystemMap.begin(); i != nameToSystemMap.end(); ++i )
        {
            (*i).second->init(this);
        }
        SetInitialStates(storedState.data(), storedState.size());
#endif
    }

    DestroySocket(suspend_shutdown_systems_s);
    DestroySocket(resume_systems_s);
    DestroySocket(setTimeScale_provide);
    DestroySocket(system_request_provide);

    delete entityManager;
    entityManager = NULL;

#ifdef _WIN32
    Sleep(1);
#else
    sleep(2);
#endif

    TeardownMessaging();
    systemsStarted = false;
    std::exit(0);
}

int SystemManager::FillPollInfo(zmq_pollitem_t*& pollItems, void* setTimeScale_provide, void* system_request_provide)
{
    int pollSize = 2;
    for ( std::vector<RunLoopComponentSystem*>::iterator i = runLoopSystems.begin(); i != runLoopSystems.end(); ++i )
    {
        pollSize = pollSize + (*i)->GetNumberReceiveSockets();
    }

    pollItems = (zmq_pollitem_t*) realloc(pollItems, sizeof(zmq_pollitem_t) * pollSize);

    for ( int i=0; i<pollSize; i++ )
    {
        pollItems[i].socket = NULL;
        pollItems[i].fd = 0;
        pollItems[i].events = ZMQ_POLLIN;
    }

    int pollRes;
    zmq_pollitem_t* pollItemsIter = pollItems;
    pollItemsIter->socket = *((void**) setTimeScale_provide);
    pollItemsIter++;
    pollItemsIter->socket = *((void**) system_request_provide);
    pollItemsIter++;

    for ( std::vector<RunLoopComponentSystem*>::iterator i = runLoopSystems.begin(); i != runLoopSystems.end(); ++i )
    {
        pollItemsIter = (*i)->FillPollInformation(pollItemsIter);
    }

    return pollSize;
}

bool SystemManager::RunLoop(void* suspend_shutdown_systems_s, void* resume_systems_s, void* system_request_provide, void* setTimeScale_provide)
{
    // Entity Manager send sockets
    for( MAP_TYPE<std::string, ComponentSystem*>::iterator i = nameToSystemMap.begin(); i != nameToSystemMap.end(); ++i )
    {
        entityManager->CreateComponentSocket((*i).second->ComponentName());
    }

    // Run loop send sockets
    for ( std::vector<RunLoopComponentSystem*>::iterator i = runLoopSystems.begin(); i != runLoopSystems.end(); ++i )
    {
        (*i)->CreateBindSockets();
    }

    // start the threaded systems!
    for ( std::vector<ThreadedMetaSystem*>::iterator i = threadedSystems.begin(); i != threadedSystems.end(); ++i )
    {
        (*i)->Start();
    }

    // Entity receive sockets
    entityManager->CreateEntitySockets();

    logOutput.CreateSocket();

    // TODO: Sync with threads...
    // Sleep for 2 seconds, letting everyone create their send sockets
#ifdef _WIN32
    Sleep(1);
#else
    sleep(1);
#endif

    // Then create all receive sockets
    // Run loop receive sockets
    for ( std::vector<RunLoopComponentSystem*>::iterator i = runLoopSystems.begin(); i != runLoopSystems.end(); ++i )
    {
        (*i)->CreateConnectSockets();
    }

    entityManager->CreateConnectSockets();

    bool running = true;
    bool suspending = false;
    bool shuttingDown = false;
    bool reloadingSystems = false;

    ShutdownMsg shutdownMsg;

    printf("All systems started, entering game...\n");

    zmq_pollitem_t* pollItems = NULL;
    int pollSize = FillPollInfo(pollItems, setTimeScale_provide, system_request_provide);
    int pollRes;

    std::vector<void*>shutdownSockets;
    std::vector<void*>shutdownSocketsLoc;

    // TODO: Determine if suspending during a large task can have hanging receives?
    //          Add some checking that can see if it's happening
    unsigned long numTicks = 0;
    long sleepTimeMs = sleepTime * 1000;
    unsigned long lastTime = clockTimeMS();
#ifdef SYSTEM_DYNAMIC_LINKING
    pthread_t recompileThread;
#endif

    while ( running )
    {
#ifdef SYSTEM_DYNAMIC_LINKING
        if ( !suspending && !shuttingDown )
        {
            bool systemsChanged = HasAnySystemChanged();
            if ( systemsChanged )
            {
                {
                    // TODO: Assert this finished properly
                    int rc = pthread_create (&recompileThread, NULL, RecompileSystemThread, &systemsToRecompile);
                }

                shuttingDown = true;
                reloadingSystems = true;
                suspendCountdown = 5;
                for ( std::vector<ThreadedMetaSystem*>::iterator i = threadedSystems.begin(); i != threadedSystems.end(); ++i )
                {
                    void* shutdown_systems_complete_r = GetSocketReceiveMessage1ToN(
                        (*i)->GetSystemMessageName(SHUTDOWN_SYSTEMS_COMPLETE_MSG).c_str(), 10*1000);
                    shutdownSocketsLoc.push_back(shutdown_systems_complete_r);
                }
                shutdownMsg.isFullShutdown = true;
                SendMessage<ShutdownMsg>(suspend_shutdown_systems_s, &shutdownMsg);
            }
        }
#endif
        float timeDelta = 0;
        if (!suspending && !shuttingDown)
        {
            if ( clockTimeMS() - lastTime  > sleepTimeMs * 4 )
            {
                printf("Jump in time! Not updating clock this tick\n");
                timeDelta = 0.f;
            }
            else
            {
                timeDelta = ((clockTimeMS() - lastTime) / 1000.f) * timeScale;
            }
        }
        lastTime = clockTimeMS();

        numTicks = numTicks + 1;

        // This can run for more than 38 years at a 0.2 tick... sounds like enough time!
        ASSERT( numTicks < ULONG_MAX-1, "Max time reached! Can't play no more :( hope u savd" );

        bool didMessage = entityManager->Tick(numTicks, suspending || shuttingDown);

        for ( std::vector<RunLoopComponentSystem*>::iterator i = runLoopSystems.begin(); i != runLoopSystems.end(); ++i )
        {
            bool didSysMessage = (*i)->Tick(timeDelta, suspending || shuttingDown);
            didMessage = didSysMessage || didMessage;
        }

        logOutput.ProcessEvents();


        unsigned long curTime = clockTimeMS();
        long curSleepTime = sleepTimeMs;
        if ( suspending || shuttingDown )
        {
            curSleepTime = 100;
        }

#ifdef SYSTEM_DYNAMIC_LINKING
        pollSize = FillPollInfo(pollItems, setTimeScale_provide, system_request_provide);
#endif
        do {
            unsigned long waitTime = curSleepTime - (clockTimeMS()-curTime);
            if ( waitTime <= 0 )
            {
                break;
            }

            pollRes = zmq_poll( pollItems, pollSize, waitTime );

            ASSERT( pollRes >= 0 || errno == EINTR, "An error occured during poll!" );

            if ( pollRes > 0 )
            {
                zmq_pollitem_t* pollItemsIter = pollItems;
                didMessage = true;

                // TODO: we could add some reference cleanup - components could have some rules that dictate
                //          what combinations are ok, clean up known "dead" entities, etc
                if ( pollItemsIter->revents & ZMQ_POLLIN )
                {
                    CheckForRequest<BuiltIn::SetTimeScale>(setTimeScale_provide, &setTimeScaleMsg);
                    timeScale = setTimeScaleMsg.timescale();
                    ProvideResponse<BuiltIn::SetTimeScale>(setTimeScale_provide, &setTimeScaleMsg);
                }
                pollItemsIter++;

                if ( pollItemsIter->revents & ZMQ_POLLIN )
                {
                    CheckForRequest<BuiltIn::SystemRequest>(system_request_provide, &systemRequestMsg);
                    switch ( systemRequestMsg.request() )
                    {
                        case BuiltIn::SystemRequest_RequestType_SHUT_DOWN:
                            ASSERT( !shuttingDown, "System somehow received shutdown message while already shutting down!" );
                            shuttingDown = true;
                            reloadingSystems = false;
                            suspendCountdown = 5;
                            for ( std::vector<ThreadedMetaSystem*>::iterator i = threadedSystems.begin(); i != threadedSystems.end(); ++i )
                            {
                                void* shutdown_systems_complete_r = GetSocketReceiveMessage1ToN(
                                    (*i)->GetSystemMessageName(SHUTDOWN_SYSTEMS_COMPLETE_MSG).c_str(), 10*1000);
                                shutdownSocketsLoc.push_back(shutdown_systems_complete_r);
                            }
                            shutdownMsg.isFullShutdown = true;
                            SendMessage<ShutdownMsg>(suspend_shutdown_systems_s, &shutdownMsg);
                            break;
                        case BuiltIn::SystemRequest_RequestType_SERIALIZED_STATE:
                            ASSERT( !suspending, "System somehow received serialized state request while already shutting down!" );
                            suspending = true;
                            suspendCountdown = 5;
                            if ( threadedSystems.size() > 0 )
                            {
                                for ( std::vector<ThreadedMetaSystem*>::iterator i = threadedSystems.begin(); i != threadedSystems.end(); ++i )
                                {
                                    void* shutdown_systems_complete_r = GetSocketReceiveMessage1ToN(
                                        (*i)->GetSystemMessageName(SUSPEND_SYSTEMS_COMPLETE_MSG).c_str(), sleepTime*1000);
                                    shutdownSockets.push_back(shutdown_systems_complete_r);
                                }
                            }
                            shutdownMsg.isFullShutdown = false;
                            SendMessage<ShutdownMsg>(suspend_shutdown_systems_s, &shutdownMsg);
                            break;
                        default:
                            systemResponseMsg.Clear();
                            systemResponseMsg.set_response(BuiltIn::SystemResponse_ResponseType_INVALID_REQUEST);
                            ProvideResponse<BuiltIn::SystemResponse>(system_request_provide, &systemResponseMsg);
                            break;
                    }
                }

                pollItemsIter++;

                for ( std::vector<RunLoopComponentSystem*>::iterator i = runLoopSystems.begin(); i != runLoopSystems.end(); ++i )
                {
                    pollItemsIter = (*i)->ProcessEvents(pollItemsIter);
                }
            }

            if ( suspending || shuttingDown )
            {
                curSleepTime = 100;
            }
        } while (pollRes > 0 && (clockTimeMS() - curTime) < curSleepTime );


        if ( suspending || shuttingDown )
        {
            if ( !didMessage )
            {
                suspendCountdown--;
            }
            else
            {
                suspendCountdown = 5;
            }
        }

        if ( suspending && !didMessage && suspendCountdown <= 0 )
        {

            if ( shutdownSockets.size() > 0 )
            {
                for ( std::vector<void*>::iterator i = shutdownSockets.begin(); i != shutdownSockets.end(); )
                {
                    if ( ReceiveMessageBlocking<ShutdownMsg>(*i, &shutdownMsg) )
                    {
                        DestroySocket(*i);
                        i = shutdownSockets.erase(i);
                    }
                    else
                    {
                        ++i;
                    }
                }
            }
            else
            {
                systemResponseMsg.Clear();
                systemResponseMsg.set_response(BuiltIn::SystemResponse_ResponseType_SERIALIZED_STATE_RESPONSE);
                systemResponseMsg.set_state(_InternalGetSerializedData());
                ProvideResponse<BuiltIn::SystemResponse>(system_request_provide, &systemResponseMsg);

                SendMessage<ShutdownMsg>(resume_systems_s, &shutdownMsg);
                suspending = false;
            }
        }

        if ( shuttingDown && !suspending && !didMessage && suspendCountdown <= 0 )
        {
            //printf("Run Loop shutdown, waiting for threaded systems!\n");

            if ( threadedSystems.size() > 0 )
            {
                for ( std::vector<void*>::iterator i = shutdownSocketsLoc.begin(); i != shutdownSocketsLoc.end(); ++i )
                {
                    if ( !ReceiveMessageBlocking<ShutdownMsg>(*i, &shutdownMsg) )
                    {
                        systemResponseMsg.Clear();
                        systemResponseMsg.set_response(BuiltIn::SystemResponse_ResponseType_SHUT_DOWN_RESPONSE);
                        systemResponseMsg.set_shutdownstatus(BuiltIn::SystemResponse_ShutdownStatus_UNCLEAN_SHUTDOWN);
                        ProvideResponse<BuiltIn::SystemResponse>(system_request_provide, &systemResponseMsg);
                        DestroySocket(system_request_provide);


                        printf( "System was locked when shutting down, exiting uncleanly\n" );

#ifdef _WIN32
                        Sleep(1);
#else
                        sleep(1);
#endif
                        std::exit(-1);
                    }
                    //printf("One system shut down!\n");
                    DestroySocket(*i);
                }

                if ( reloadingSystems )
                {
                    storedState = _InternalGetSerializedData();
                }
            }

            running = false;
            break;
        }
    }

    entityManager->CheckSocketsAndDestroy();

    for ( std::vector<RunLoopComponentSystem*>::iterator i = runLoopSystems.begin(); i != runLoopSystems.end(); ++i )
    {
        (*i)->CheckSocketsAndDestroy();
    }

#if SYSTEM_DYNAMIC_LINKING
    if ( reloadingSystems )
    {
        int rc = pthread_join (recompileThread, NULL);
        // TODO: Assert this joined fine
        systemsToRecompile.clear();
    }
#endif

    CleanSystems();

    if ( !reloadingSystems )
    {
        systemResponseMsg.Clear();
        systemResponseMsg.set_response(BuiltIn::SystemResponse_ResponseType_SHUT_DOWN_RESPONSE);
        systemResponseMsg.set_shutdownstatus(BuiltIn::SystemResponse_ShutdownStatus_CLEAN_SHUTDOWN);
        ProvideResponse<BuiltIn::SystemResponse>(system_request_provide, &systemResponseMsg);
    }

    logOutput.CloseSocket();

    free(pollItems);


    return reloadingSystems;
}

void SystemManager::CleanSystems()
{
    for ( std::vector<ThreadedMetaSystem*>::iterator i = threadedSystems.begin(); i != threadedSystems.end(); ++i )
    {
        delete (*i);
    }
    threadedSystems.clear();

    for( MAP_TYPE<std::string, ComponentSystem*>::iterator i = nameToSystemMap.begin(); i != nameToSystemMap.end(); ++i )
    {
        delete (*i).second;
    }
    nameToSystemMap.clear();

    runLoopSystems.clear();
}

// TODO: These should be friend functions, please do not call directly
bool SystemManager::AddThreadedSystem(ThreadedComponentSystem* componentSystem, ThreadedMetaSystem* metaSystem)
{
    ASSERT(creatingSystems == true, "Trying to add system when it's not allowed!");

    std::string systemName = componentSystem->SystemName();
    bool exists = exists_in<std::string, ComponentSystem*>(nameToSystemMap, systemName);
    ASSERT(!exists, "Using a duplicate system name!");

    nameToSystemMap[systemName] = componentSystem;
    metaSystem->AddSystem(componentSystem);

    return true;
}

bool SystemManager::AddRunLoopSystem(RunLoopComponentSystem* componentSystem)
{
    ASSERT(creatingSystems == true, "Trying to add system when it's not allowed!");

    std::string systemName = componentSystem->SystemName();
    bool exists = exists_in<std::string, ComponentSystem*>(nameToSystemMap, systemName);
    ASSERT(!exists, "Using a duplicate system name!");

    nameToSystemMap[systemName] = componentSystem;
    runLoopSystems.push_back(componentSystem);

    return true;
}

#ifdef SYSTEM_DYNAMIC_LINKING
void SystemManager::LoadSystems()
{
    HasAnySystemChanged();

    for ( std::map< std::string, void* >::iterator it = systemLibs.begin(); it != systemLibs.end(); ++it )
    {
        dlclose((*it).second);
    }
    systemLibs.clear();

    for ( std::map< std::string, std::string >::iterator it = dynamicSystems.begin(); it != dynamicSystems.end(); ++it )
    {
        const std::string& systemName = (*it).first;
        std::string libFile = std::string("./build/lib") + systemName + ".dylib";
        void* dlHandle = dlopen(libFile.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        systemLibs[systemName] = dlHandle;
    }

    CreateAllSystems();
}

unsigned int generateHash(const char * string, size_t len) {

    unsigned int hash = 0;
    for(size_t i = 0; i < len; ++i) 
        hash = 65599 * hash + string[i];
    return hash ^ (hash >> 16);
}

bool SystemManager::HasSystemChanged(std::string targetName, std::string fileName)
{
    struct stat st;
    int ierr = stat (fileName.c_str(), &st);
    ASSERT( ierr == 0, "Error getting timestamp!" );
    int date = st.st_mtime;

    bool needsHash = false;
    std::map< std::string, int >::iterator timestampIter = fileTimestamps.find(fileName);
    if ( timestampIter == fileTimestamps.end() )
    {
        fileTimestamps[fileName] = date;
        needsHash = true;
    }
    else
    {
        needsHash = date != (*timestampIter).second;
    }

    if ( needsHash )
    {
        extern char* getFileString(const char* filename, size_t& size);

        size_t size;
        char* filecontents = getFileString(fileName.c_str(), size);
        unsigned int hash = generateHash(filecontents, size);
        free(filecontents);

        std::map< std::string, unsigned int >::iterator hashIter = fileHashes.find(fileName);
        if ( hashIter == fileHashes.end() )
        {
            fileHashes[fileName] = hash;
        }
        else
        {
            bool hasChanged = hash != (*hashIter).second;
            if ( hasChanged )
            {
                fileHashes[fileName] = hash;
                systemsToRecompile.insert(targetName);
                return true;
            }
        }
    }

    return false;
}

bool SystemManager::HasAnySystemChanged()
{
    bool hasAnyChanged = false;
    for ( std::map< std::string, std::string >::iterator it = dynamicSystems.begin(); it != dynamicSystems.end(); ++it )
    {
        const std::string& systemName = (*it).first;
        const std::string& filename = (*it).second;

        if ( HasSystemChanged(systemName, filename) )
        {
            hasAnyChanged = true;
        }
    }

    for ( std::map< std::string, std::string >::iterator it = dynamicWatches.begin(); it != dynamicWatches.end(); ++it )
    {
        const std::string& targetName = (*it).first;
        const std::string& filename = (*it).second;

        if ( HasSystemChanged(targetName, filename) )
        {
            hasAnyChanged = true;
        }
    }

    return hasAnyChanged;
}

bool SystemManager::AddDynamicSystem(std::string filename, std::string systemName)
{
    dynamicSystems[systemName] = filename;
    return true;
}
#endif

// Used for loading initial state from saved data or world generation
//      pass in an empty string to set state to nothing
bool SystemManager::SetInitialStates(const char* data, size_t size)
{
    MAP_TYPE<std::string, ComponentSystem*> componentNameToSystem;

    for( MAP_TYPE<std::string, ComponentSystem*>::iterator i = nameToSystemMap.begin(); i != nameToSystemMap.end(); ++i )
    {
        (*i).second->ClearStore();

        componentNameToSystem[(*i).second->ComponentName()] = (*i).second;
    }

    EntityID maxEntID = 0;

    Entity::Entities entitiesMsg;
    bool res = entitiesMsg.ParsePartialFromArray(data, size);
    ASSERT( res, "Error loading inital state data!" );

    const google::protobuf::RepeatedPtrField<Entity::Entity>& entities = entitiesMsg.entities(); 
    for (google::protobuf::RepeatedPtrField<Entity::Entity>::const_iterator entIT = entities.begin(); entIT != entities.end(); ++entIT )
    {
        const Entity::Entity& entityMsg = *entIT;

        EntityID entityID = entityMsg.entityid();

        if ( entityID > maxEntID )
            maxEntID = entityID;

        const google::protobuf::RepeatedPtrField<Entity::ComponentData>& components = entityMsg.components(); 
        for (google::protobuf::RepeatedPtrField<Entity::ComponentData>::const_iterator compIT = components.begin(); compIT != components.end(); ++compIT )
        {
            const Entity::ComponentData& compData = *compIT;

            const std::string& componentName = compData.name();
            bool exists = componentNameToSystem.find(componentName) != componentNameToSystem.end();
            if ( !exists )
            {
                printf("[ERROR] Unknown Component %s\n", componentName.c_str());
            }
            ASSERT( exists, "Unknown Component in state!")
            componentNameToSystem[componentName]->AddComponentToStore(entityID, compData.data(), 0);
        }
    }

    entityManager->SetCurEntityID(maxEntID);

    return true;
}

std::string SystemManager::_InternalGetSerializedData()
{
    Entity::Entities entitiesMsg;
    MAP_TYPE<EntityID, Entity::Entity*> entityMsgMap;

    for( MAP_TYPE<std::string, ComponentSystem*>::iterator i = nameToSystemMap.begin(); i != nameToSystemMap.end(); ++i )
    {
        std::string componentName = (*i).second->ComponentName();

        MAP_TYPE<EntityID, std::string> componentData = (*i).second->GetAllComponentsDataFromStore();

        for( MAP_TYPE<EntityID, std::string>::iterator j = componentData.begin(); j != componentData.end(); ++j )
        {
            if ( entityMsgMap.find((*j).first) == entityMsgMap.end() )
            {
                Entity::Entity* entity = entitiesMsg.add_entities();
                entity->set_entityid((*j).first);
                entityMsgMap[(*j).first] = entity;
            }

            Entity::Entity* entity = entityMsgMap[(*j).first];
            Entity::ComponentData* compData = entity->add_components();
            compData->set_name(componentName);
            compData->set_data((*j).second);
        }
    }

    std::string retString;
    bool res = entitiesMsg.SerializeToString(&retString);
    ASSERT( res, "Serializing entity state failed!" );

    return retString;
}

bool SystemManager::AddThreadedMetaSystem(ThreadedMetaSystem* metaSystem)
{
    threadedSystems.push_back( metaSystem );
    return true;
}

#include "Messaging.h"
#include "helpers.h"

#define CONNECT_RETRIES 5

void* gContext = NULL;

//int gCurNum = 0;

// Called once at startup
void SetupMessaging()
{
    ASSERT(gContext == NULL, "Messaging already started!");

    gContext = zmq_ctx_new();
    ASSERT(gContext != NULL, "Failed to start messaging!");
}

// Called once at shutdown
//      It is on the developer to ensure that all sockets are shutdown first
void TeardownMessaging()
{
    ASSERT(gContext != NULL, "Can't teardown messaging! Never started!");
    int rc = zmq_ctx_destroy(gContext);
    gContext = NULL;
    if ( rc != 0 )
    {
        printf("[ERROR] Messaging not shut down cleanly!\n");
    }
}

// Bound socket MUST be started before receiving socket
void PrepSocket(void* socket, const char* socketAddress)
{
    SocketDesc* desc = (SocketDesc*) socket;

    int rc = 1;
    int numTries = 0;
    if ( desc->bindSocket )
    {
        while ( (numTries++) < CONNECT_RETRIES )
        {
            rc = zmq_bind(desc->sock, socketAddress);
            if ( rc == 0 )
                break;
            else
                printf("binding %s, errno: %u\n", socketAddress, errno);
        }
        ASSERT (rc == 0, "Bind failed!");
    }
    else
    {
        while ( (numTries++) < CONNECT_RETRIES )
        {
            rc = zmq_connect(desc->sock, socketAddress);
            if ( rc == 0 )
                break;
            else
                printf("connecting %s, errno: %u\n", socketAddress, errno);
        }
        ASSERT (rc == 0, "Connection failed!");
    }

    if ( desc->type == ZMQ_RECEIVE_TYPE )
    {
        zmq_setsockopt(desc->sock, ZMQ_SUBSCRIBE, "", 0);    // no filter
    }
    
    if ( desc->timeoutMS >= 0 )
    {
        zmq_setsockopt(desc->sock, ZMQ_RCVTIMEO, &desc->timeoutMS, sizeof(desc->timeoutMS));  // Set receive timeout
    }

    // This is a bad place to set linger, and also doesn't work
    //static int lingerTime = 500;
    //zmq_setsockopt(desc->sock, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));  // Set receive timeout
}

inline SocketDesc* _CreateSocketDesc(int type, const char* msgName, int timeoutMS, bool bindSocket)
{
    SocketDesc* desc = (SocketDesc*) malloc(sizeof(SocketDesc));
    desc->type = type;
    desc->sock = zmq_socket(gContext, type);
    desc->timeoutMS = timeoutMS;
    desc->bindSocket = bindSocket;
#ifdef DEBUG_ASSERT
    strcpy(desc->name, msgName);
#endif

    PrepSocket((void*) desc, msgName);
 //   gCurNum++;
 //   printf("SocketOpened: %u :: %d :: %s\n", desc->sock, gCurNum, desc->msgName);
    return desc;
}

// These sockets must always run on the same thread
void* GetSocketSendMessage1ToN(const char* msgType)
{
    return _CreateSocketDesc(ZMQ_SEND_TYPE, msgType, -1, true);
}

void* GetSocketReceiveMessage1ToN(const char* msgType, int timeoutMS)
{
    return _CreateSocketDesc(ZMQ_RECEIVE_TYPE, msgType, timeoutMS, false);
}

void* GetSocketSendMessageNTo1(const char* msgType)
{
    return _CreateSocketDesc(ZMQ_SEND_TYPE, msgType, -1, false);
}

void* GetSocketReceiveMessageNTo1(const char* msgType, int timeoutMS)
{
    return _CreateSocketDesc(ZMQ_RECEIVE_TYPE, msgType, timeoutMS, true);
}

void* GetSocketProvideMessage(const char* msgType, int timeoutMS)
{
    return _CreateSocketDesc(ZMQ_PROVIDE_TYPE, msgType, timeoutMS, true);
}

void* GetSocketRequestMessage(const char* msgType)
{
    return _CreateSocketDesc(ZMQ_REQUEST_TYPE, msgType, -1, false);
}

void DestroySocket(void* socket)
{
    SocketDesc* desc = (SocketDesc*) socket;
    ASSERT(desc != NULL, "Destroy failed! Socket is NULL!");
    zmq_close(desc->sock);

//    gCurNum--;
//    printf("SocketClosed: %u :: %d :: %s\n", desc->sock, gCurNum, desc->msgName);
    free(socket);
}

void breakHere()
{
    printf("Assert failed!\n");
}

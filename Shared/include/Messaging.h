#ifndef MESSAGING_H__
#define MESSAGING_H__

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string.h>

#include "zmq.h"
#include "helpers.h"

#define MAX_MESSAGE_NAME_SIZE 256

#ifndef __cplusplus
STATIC_ASSERT(FALSE, "Don't use C to run this!")
#endif

#define SHUTDOWN_TICK_RATE 0.25

#define ZMQ_SEND_TYPE ZMQ_PUB
#define ZMQ_RECEIVE_TYPE ZMQ_SUB

#define ZMQ_PROVIDE_TYPE ZMQ_REP
#define ZMQ_REQUEST_TYPE ZMQ_REQ

#ifdef USE_TCP
#define EXTERNAL_SOCKET_TYPE "tcp://127.0.0.1:"
#else
#define EXTERNAL_SOCKET_TYPE "ipc://"
#endif

struct SocketDesc
{
    void* sock;
    int type;
    int timeoutMS;
    bool bindSocket;
#ifdef DEBUG_ASSERT
    char name[512];
#endif
};

// These sockets are used for simple send/receive operations, and are
//      used to transport events INBOUND and OUTBOUND
void* GetSocketSendMessage1ToN(const char* msgType);
void* GetSocketReceiveMessage1ToN(const char* msgType, int timeout = -1); // Timeout here is used instead of sleep for shutdown/suspend

/*
//  These may be neccessary if we cannot bind/connect on one thread and then use on another
#define CREATE_SEND_SOCKETS_MSG "CreateBindSockets"
#define CREATE_SEND_SOCKETS_COMPLETE_MSG "CreateBindSocketsComplete"
#define CREATE_RECEIVE_SOCKETS_MSG "CreateConnectSockets"
*/


// These sockets must properly be paired
// TODO: Add some static analysis to confirm this, or just do it silently by the header generation
void* GetSocketSendMessageNTo1(const char* msgType);
void* GetSocketReceiveMessageNTo1(const char* msgType, int timeout = -1); // Timeout here is used instead of sleep for shutdown/suspend

/*
    For SocketSendMessage, it can always potentially block, use the functions:
        SendMessage<structType>(socket, structType*)
        SendRawMessage(socket, char* data, dataSize)
    For SocketReceiveMessage, use the functions:
        ReceiveMessage<structType>(socket, structType*)
        ReceiveMessageBlocking<structType>(socket, structType*)
        ReceiveRawMessage(socket, char* data, dataSize, isBlocking)
*/

// These sockets are used to request information blocking
//      used to communication with the main game thread
void* GetSocketProvideMessage(const char* msgType, int timeout = -1);
void* GetSocketRequestMessage(const char* msgType);
/*
    For SocketProvideMessage, it can always potentially block, use the functions:
        CheckForRequest<requestType>(socket, requestType*)
        ProvideResponse<replyType>(socket, replyType*)
    For SocketRequestMessage, it will always block, use the function:
        replyType RequestData<requestType, replyType>(socket, requestType*)
*/

// Connect/Bind to additional addresses
void PrepSocket(void* socket, const char* socketAddress);

// Called once at startup
void SetupMessaging();

// Called once at shutdown
//      It is on the developer to ensure that all sockets are shutdown first
void TeardownMessaging();

// Do not access this after destroying, in any way
void DestroySocket(void* socket);

// Just for these templates
inline SocketDesc* _AssertAndGetSocket(void* socket, int expectedType)
{
    // TODO: Assert that message is the one requested originally
    SocketDesc* desc = (SocketDesc*) socket;
    ASSERT(desc != NULL, "Socket is NULL!");
    ASSERT(desc->type == expectedType, "Socket is wrong type for operation!");
    return desc;
}

template <typename requestType, typename replyType>
void RequestData(void* socket, requestType* requestObject, replyType* replyObject)
{
    int rc;
    bool res;

    SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_REQUEST_TYPE);

    zmq_msg_t zmq_send_msg;
    size_t reqMsgSize = requestObject->ByteSize();
    rc = zmq_msg_init_size(&zmq_send_msg, reqMsgSize);
    ASSERT( rc == 0, "Error creating ZMQ MSG!" );

    res = requestObject->SerializeToArray(zmq_msg_data(&zmq_send_msg), reqMsgSize);
    ASSERT( res, "Serializing protobuf to request failed" );

    rc = zmq_msg_send(&zmq_send_msg, desc->sock, 0);
    ASSERT(rc == reqMsgSize, "Wrong size was sent in a request!");

    rc = zmq_msg_close(&zmq_send_msg);
    ASSERT( rc == 0, "Error closing ZMQ MSG!" );

    // Send complete, await response
    zmq_msg_t zmq_recv_msg;
    rc = zmq_msg_init(&zmq_recv_msg);
    ASSERT( rc == 0, "Error creating ZMQ MSG!" );

    rc = zmq_msg_recv(&zmq_recv_msg, desc->sock, 0);
    ASSERT( rc >= 0, "Reply receive failed" );

    res = replyObject->ParseFromArray(zmq_msg_data(&zmq_recv_msg), zmq_msg_size(&zmq_recv_msg));
    ASSERT( res, "Deserializing protobuf from reply failed" );

    rc = zmq_msg_close(&zmq_recv_msg);
    ASSERT( rc == 0, "Error closing ZMQ MSG!" );
}

// Must call provide right after check
template <typename requestType>
bool CheckForRequest(void* socket, requestType* requestObject)
{
    int rc;
    bool res;

    SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_PROVIDE_TYPE);

    zmq_msg_t zmq_msg;
    rc = zmq_msg_init(&zmq_msg);
    ASSERT( rc == 0, "Error creating ZMQ MSG!" );

    int flags = desc->timeoutMS < 0 ? ZMQ_DONTWAIT : 0;

    rc = zmq_msg_recv(&zmq_msg, desc->sock, flags);
    if ( rc < 0 )
    {
#if _WIN32
        ASSERT( errno == 0 || errno == EAGAIN, "Receive request failed (Non blocking)!" );
#else
        ASSERT( errno == EAGAIN, "Receive request failed (Non blocking)!" );
#endif

        rc = zmq_msg_close(&zmq_msg);
        ASSERT( rc == 0, "Error closing ZMQ MSG!" );
        return false;
    }

    res = requestObject->ParseFromArray(zmq_msg_data(&zmq_msg), zmq_msg_size(&zmq_msg));
    rc = zmq_msg_close(&zmq_msg);
    ASSERT( rc == 0, "Error closing ZMQ MSG!" );


    if ( !res )
    {
        // This is the only place where it could be coming from outside the program
        //      so let's not assert, instead just log an error
        printf("[ERROR] Deserializing protobuf from request failed\n");

        // Send something back to resolve the state
        static const char* failedMessage = "Failed to process request";
        int sendMsgSize = strlen(failedMessage);
        rc = zmq_send( desc->sock, failedMessage, sendMsgSize, 0 );
        ASSERT( rc == sendMsgSize, "Sending error message back on socket failed!" );
        return false;
    }

    return true;
}

template <typename replyType>
void ProvideResponse(void* socket, replyType* replyObjet)
{
    int rc;
    bool res;

    SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_PROVIDE_TYPE);

    zmq_msg_t zmq_msg;
    size_t repMsgSize = replyObjet->ByteSize();
    rc = zmq_msg_init_size(&zmq_msg, repMsgSize);
    ASSERT( rc == 0, "Error creating ZMQ MSG!" );

    res = replyObjet->SerializeToArray(zmq_msg_data(&zmq_msg), repMsgSize);
    ASSERT( res, "Serializing protobuf to reply failed" );

    rc = zmq_msg_send(&zmq_msg, desc->sock, 0);
    ASSERT(rc == repMsgSize, "Wrong size was sent in a reply!");

    rc = zmq_msg_close(&zmq_msg);
    ASSERT( rc == 0, "Error closing ZMQ MSG!" );
}

template <typename msgType>
void SendProtobufMessage(void* socket, msgType* msg)
{
    int rc;
    bool res;

    SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_SEND_TYPE);

    zmq_msg_t zmq_msg;
    size_t repMsgSize = msg->ByteSize();
    rc = zmq_msg_init_size(&zmq_msg, repMsgSize);
    ASSERT( rc == 0, "Error creating ZMQ MSG!" );

    res = msg->SerializeToArray(zmq_msg_data(&zmq_msg), repMsgSize);
    ASSERT( res, "Serializing protobuf to reply failed" );

    rc = zmq_msg_send(&zmq_msg, desc->sock, 0);
    ASSERT(rc == repMsgSize, "Wrong size was sent in a reply!");

    rc = zmq_msg_close(&zmq_msg);
    ASSERT( rc == 0, "Error closing ZMQ MSG!" );
}

template <typename msgType>
bool ReceiveProtobufMessage(void* socket, msgType* msg)
{
    int rc;
    bool res;

    SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_RECEIVE_TYPE);

    zmq_msg_t zmq_msg;
    rc = zmq_msg_init(&zmq_msg);
    ASSERT( rc == 0, "Error creating ZMQ MSG!" );

    int flags = desc->timeoutMS < 0 ? ZMQ_DONTWAIT : 0;

    rc = zmq_msg_recv(&zmq_msg, desc->sock, flags);
    if ( rc < 0 )
    {
#if _WIN32
        ASSERT( errno == 0 || errno == EAGAIN, "Receive request failed (Non blocking)!" );
#else
        ASSERT( errno == EAGAIN, "Receive request failed (Non blocking)!" );
#endif

        rc = zmq_msg_close(&zmq_msg);
        ASSERT( rc == 0, "Error closing ZMQ MSG!" );
        return false;
    }

    res = msg->ParseFromArray(zmq_msg_data(&zmq_msg), zmq_msg_size(&zmq_msg));
    ASSERT( res, "Parsing protobuf on internal message failed!" );

    return true;
}

// Send can always potentially block, but usually doesn't
inline void SendRawMessage(void* socket, void* msg, size_t msgSize, int flags = 0)
{
    SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_SEND_TYPE);
    size_t sendSize = zmq_send(desc->sock, msg, msgSize, flags);
    ASSERT(sendSize == msgSize, "Wrong size was sent!");
}

// These sockets must always run on the same thread
// Send can always potentially block, but usually doesn't
template <typename msgType>
void SendMessage (void* socket, msgType* msg, int flags = 0)
{
    size_t msgSize = sizeof(msgType);
    SendRawMessage(socket, (char*) msg, msgSize, flags);
}


inline int ReceiveRawMessage(void* socket, void* msg, size_t msgSize, bool blocking = false)
{
    SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_RECEIVE_TYPE);
    // Since this is inline, compiler optimization should strip this if
    int flags = blocking ? 0 : ZMQ_DONTWAIT;
    int recvSize = zmq_recv(desc->sock, msg, msgSize, flags);
    return recvSize;
}

template <typename msgType>
bool ReceiveMessageBlocking (void* socket, msgType* recvMsg)
{
    size_t msgSize = sizeof(msgType);
    int recvSize = ReceiveRawMessage(socket, (void*) recvMsg, msgSize, true);
    if ( recvSize < 0 )
    {
#if _WIN32
        ASSERT( errno == 0 || errno == EAGAIN, "Receive request failed (Blocking)!" );
#else
        ASSERT( errno == EAGAIN, "Receive failed (Blocking)!" );
#endif

        return false;
    }
    ASSERT(recvSize == msgSize, "Received wrong size for that message (blocking)!");
    return true;
}

template <typename msgType>
bool ReceiveMessage (void* socket, msgType* recvMsg)
{
    size_t msgSize = sizeof(msgType);
    int recvSize = ReceiveRawMessage(socket, (void*) recvMsg, msgSize, false);
    if ( recvSize < 0 )
    {
//#ifdef DEBUG_ASSERT
//        if ( errno != 0 && errno != EAGAIN )
//        {
//            SocketDesc* desc = _AssertAndGetSocket(socket, ZMQ_RECEIVE_TYPE);
//            printf("%s (%d): %d, %u, %x\n", desc->name, recvSize, errno, errno, errno);
//        }
//#endif
#if _WIN32
        ASSERT( errno == 0 || errno == EAGAIN, "Receive failed!" );
#else
        ASSERT( errno == EAGAIN, "Receive failed!" );
#endif
        return false;
    }
    ASSERT(recvSize == msgSize, "Received wrong size for that message!");
    return true;
}

#endif // MESSAGING_H__
#include "helpers.h"
#include "Logging.h"
#include "Messaging.h"

void LogStream::CreateConnectSocket(const char* systemName, char priority)
{
    ASSERT( strlen(systemName) < MAX_SYSTEM_NAME, "System name is too long!" );

    logMessage.priority = priority;
    sprintf(logMessage.systemName, "%s", systemName);

    log_s = GetSocketSendMessageNTo1(LOG_MSG_NAME);
}

void LogStream::CloseSocket()
{
    DestroySocket(log_s);
    log_s = NULL;
}

LogStream& LogStream::operator<<(std::ostream& (*pf)(std::ostream&))
{
    int rc;

    SendMessage<LogMessage>(log_s, &logMessage, ZMQ_SNDMORE);

    const char* msg = out.str().c_str();
    size_t msgSize = strlen(msg);

    zmq_msg_t zmq_msg;
    rc = zmq_msg_init_size(&zmq_msg, msgSize);
    ASSERT( rc == 0, "Log send - Error creating ZMQ MSG!" );

    memcpy(zmq_msg_data(&zmq_msg), msg, msgSize);

    SocketDesc* desc = _AssertAndGetSocket(log_s, ZMQ_SEND_TYPE);
    rc = zmq_msg_send(&zmq_msg, desc->sock, 0);
    ASSERT(rc == msgSize, "Wrong size was sent in a reply!");

    out.str("");

    return *this;
}

void LogOutput::CreateSocket()
{
    log_r = GetSocketReceiveMessageNTo1(LOG_MSG_NAME);

#ifdef USE_TCP
    logLevel_provide = GetSocketProvideMessage(EXTERNAL_SOCKET_NAME(60003));
#else
    logLevel_provide = GetSocketProvideMessage(EXTERNAL_SOCKET_NAME(setLogLevel));
#endif
    reqLogLevel.set_loglevel(DEFAULT_LOG_LEVEL);
}

void LogOutput::ProcessEvents()
{
    while ( CheckForRequest<BuiltIn::ChangeLogLevel>(logLevel_provide, &reqLogLevel) )
    {
        includedSystems.clear();
        excludedSystems.clear();

        const google::protobuf::RepeatedPtrField<std::string>& includedsystems = reqLogLevel.includesystems(); 
        for (google::protobuf::RepeatedPtrField<std::string>::const_iterator it = includedsystems.begin(); it != includedsystems.end(); ++it )
        {
            includedSystems.push_back(*it);
        }

        const google::protobuf::RepeatedPtrField<std::string>& excludedsystems = reqLogLevel.excludesystems(); 
        for (google::protobuf::RepeatedPtrField<std::string>::const_iterator it = excludedsystems.begin(); it != excludedsystems.end(); ++it )
        {
            excludedSystems.push_back(*it);
        }

        repLogLevelResponse.set_response(BuiltIn::SystemResponse_ResponseType_LOG_LEVEL_CHANGED);
        ProvideResponse<BuiltIn::SystemResponse>(logLevel_provide, &repLogLevelResponse);
    }

    SocketDesc* desc = _AssertAndGetSocket(log_r, ZMQ_RECEIVE_TYPE);
    int rc;

    int msgSize = sizeof(logMessage);
    int more;
    size_t moreSize = sizeof(more);

    while (true)
    {
        // Since this is inline, compiler optimization should strip this if
        int recvSize = zmq_recv(desc->sock, &logMessage, msgSize, ZMQ_DONTWAIT);
        if ( recvSize < 0 )
        {
#if _WIN32
            ASSERT( errno == 0 || errno == EAGAIN, "Receive log failed!" );
#else
            ASSERT( errno == EAGAIN, "Receive log failed!" );
#endif
            break;
        }

        rc = zmq_getsockopt (desc->sock, ZMQ_RCVMORE, &more, &moreSize);
        ASSERT( rc == 0, "Failure on log socket!" );
        ASSERT( more, "No more values are sent, log is being misused!" );

        zmq_msg_t zmq_msg;
        rc = zmq_msg_init(&zmq_msg);
        ASSERT( rc == 0, "[Log] Error creating ZMQ MSG!" );

        rc = zmq_msg_recv(&zmq_msg, desc->sock, 0);
        ASSERT( rc >= 0, "Error when receiving message!" );

        std::string logString((const char*) zmq_msg_data(&zmq_msg), (size_t)zmq_msg_size(&zmq_msg));
        rc = zmq_msg_close(&zmq_msg);
        ASSERT( rc == 0, "[Log] Error closing ZMQ MSG!" );


        std::string systemName(logMessage.systemName);
        bool shouldLog = true;
        if ( std::find(excludedSystems.begin(), excludedSystems.end(), systemName) != excludedSystems.end() )
        {
            shouldLog = false;
        }

        if ( includedSystems.size() > 0 && 
                std::find(includedSystems.begin(), includedSystems.end(), systemName ) == includedSystems.end() )
        {
            shouldLog = false;
        }

        if ( logMessage.priority < reqLogLevel.loglevel() )
        {
            shouldLog = false;
        }

        if ( shouldLog )
        {
            printf("[%s] [%u] %s\n", logMessage.systemName, logMessage.priority, logString.c_str());
        }
    }
}

void LogOutput::CloseSocket()
{
    ProcessEvents();
    DestroySocket(log_r);
    log_r = NULL;
    DestroySocket(logLevel_provide);
    logLevel_provide = NULL;
}
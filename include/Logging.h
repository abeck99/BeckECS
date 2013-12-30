#ifndef LOGGING_H__
#define LOGGING_H__

#include <sstream>
#include "Messages/basics.pb.h"
#include "Messaging.h"

#define MSG_INFO 0
#define MSG_DEBUG 1
#define MSG_WARNING 2
#define MSG_IMPORTANT_GAMEPLAY 3
#define MSG_ERROR 4

#define DEFAULT_LOG_LEVEL MSG_IMPORTANT_GAMEPLAY

#define MAX_SYSTEM_NAME 256

struct LogMessage
{
    char systemName[MAX_SYSTEM_NAME];
    char priority;
};

class LogStream {
public:
    void CreateConnectSocket(const char* systemName, char priority);

    void CloseSocket();

    template<typename T>
    LogStream& operator<<( T& v )  {
        out << v;
        return *this;
    }

    LogStream& operator<<( google::protobuf::uint64 v )
    {
        out << v;
        return *this;
    }

    LogStream& operator<<(std::ostream& (*pf)(std::ostream&));

private:
    std::stringstream out;
    LogMessage logMessage;
    void* log_s;
};


class LogOutput {
public:
    void CreateSocket();

    void ProcessEvents();

    void CloseSocket();

private:
    void* log_r;
    void* logLevel_provide;
    BuiltIn::SystemResponse repLogLevelResponse;
    BuiltIn::ChangeLogLevel reqLogLevel;
    std::vector<std::string> includedSystems;
    std::vector<std::string> excludedSystems;
    LogMessage logMessage;
};

#endif //LOGGING_H__
# TODO: Macros for fetching component set
import sys
import json
import re
import string
import os
import collections
import jinja2
import fnmatch

try:
    SYSTEM_MANAGER_OUTPUT_FILES = [
        {
            "forceOverwrite": True,
            "outDir": "Systems",
            "extension": ".config",
            "templateFile": "../../EntitySystem/buildTools/templates/systemlist.template",
        },
        {
            "forceOverwrite": True,
            "outDir": "Autogen",
            "extension": ".h",
            "templateFile": "../../EntitySystem/buildTools/templates/manager.template",
        },
    ]

    SYSTEM_OUTPUT_FILES = [
        {
            "forceOverwrite": True,
            "outDir": "Autogen",
            "extension": ".h",
            "templateFile": "../../EntitySystem/buildTools/templates/header.template",
        },
        {
            "forceOverwrite": False,
            "outDir": "src",
            "extension": ".cpp",
            "templateFile": "../../EntitySystem/buildTools/templates/cpp.template",
        }
    ]

    PYTHON_OUTPUT_FILES = [
        {
            "forceOverwrite": True,
            "outDir": "../PyPolitico",
            "extension": ".py",
            "templateFile": "../../EntitySystem/buildTools/templates/python.template",
        }
    ]

    PROTO_BUF_DIRS = [
        "../EntitySystem/Messages",
        "../EntitySystem/Components",
        sys.argv[1] + "/Messages",
        sys.argv[1] + "/Components",
    ]

    protoMessageDescs = {}

    def GetNameNoExt(inName):
        return os.path.splitext(os.path.basename(inName))[0].strip()

    def ProcessProtoFile(protoFile, headerRoot):
        f = open(protoFile)
        s = f.read()
        f.close()
        headerFile = os.path.join(headerRoot, GetNameNoExt(protoFile) + ".pb.h")
        pyPackage = GetNameNoExt(protoFile) + "_pb2"

        curPackage = ""
        for line_ in s.split("\n"):
            if "//" in line_:
                line_ = line_[:line_.find("//")]
            for line in line_.split(";"):
                if line.startswith("package"):
                    curPackage = line.split(" ")[1] + "::"
                elif line.startswith("message"):
                    message = line.split(" ")[1].replace("{", "")
                    protoMessageDescs[message] = {
                        "headerFile": headerFile,
                        "fullName": curPackage + message,
                        "pyPackage": pyPackage,
                    }

    for directory in PROTO_BUF_DIRS:
        # TODO: Won't work if we go more than one deep
        headerRoot = os.path.split(directory)[1]
        for root, dirs, files in os.walk(directory):
            for filename in fnmatch.filter(files, '*.proto'):
                fullName = os.path.join(root, filename)
                ProcessProtoFile(fullName, headerRoot)


    jinEnv = jinja2.Environment(trim_blocks=True, extensions=["jinja2.ext.do"])

    def convertCamel(name):
        s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
        return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

    def Macroize(name):
        nameUnderscore = convertCamel(name)
        return nameUnderscore.upper() + "_H__"

    jsonFilename = "./" + sys.argv[1] + "/Systems"

    if not os.path.exists(jsonFilename):
        raise RuntimeError("File/Directory %s does not exist!" % (jsonFilename,))

    fullData = {}

    def LoadAndCombine(theFilename):
        f = open(theFilename, "r")
        x = f.read()
        fileData = json.loads(x)
        f.close()

        for key, val in fileData.iteritems():
            if key in fullData:
                fullData[key].extend(val)
            else:
                fullData[key] = val
       

    if not os.path.isdir:
        LoadAndCombine(jsonFilename)
    else:
        for root, dirs, files in os.walk(jsonFilename):
            for filename in fnmatch.filter(files, '*.json'):
                LoadAndCombine( os.path.join(root, filename) )

    useTCP = bool(int(os.environ.get("USE_TCP", 0)))

    INTERNAL_SOCKET_TYPE = "inproc://"

    winSockPorts = fullData.get("tcpSockPorts", {})
    def TCPExternalSocketName(inName):
        if inName not in winSockPorts:
            print "Could not find win port num for", inName
            return "ERROR"
        return "tcp://127.0.0.1:%d" % (winSockPorts[inName],)

    def ExternalSocketName(inName):
        return "ipc://" + inName

    systems = fullData["Systems"]
    systemManagers = fullData["SystemManagers"]

    threadedSystems = set([])
    systemToThreadName = collections.defaultdict(lambda: "RunLoop")
    # Determine which systems are threaded systems
    for systemManager in systemManagers:
        for metaSystem in systemManager.get("metaSystems", []):
            metaName = metaSystem["name"]
            includedSystems = metaSystem.get("systems", [])
            threadedSystems.update(includedSystems)
            for metaSystem in includedSystems:
                systemToThreadName[metaSystem] = metaName


    class FriendSystemError(Exception):
        pass

    def NamesFromDict(inDict):
        baseName = system.get("baseName", "")
        if baseName:
            baseName = baseName[0].upper() + baseName[1:]

        systemName = system.get("systemName", baseName + "System")
        systemName = systemName[0].upper() + systemName[1:]
        componentName = system.get("componentName", baseName + "Component")
        componentName = componentName[0].upper() + componentName[1:]

        return systemName, componentName

    # Validate that friend systems all live on the same thread
    # Very important check for ensuring thread safety, since it's an easy mistake to make
    for system in systems:
        systemName, componentName = NamesFromDict(system)

        friendSystems = system.get("friendSystems", [])

        myThread = systemToThreadName[systemName]
        for friendSystem in friendSystems:
            friendThread = systemToThreadName[friendSystem]
            if friendThread != myThread:
                raise FriendSystemError("The systems %s and %s are friends but are on different threads %s and %s, this will break things!" % (
                    systemName,
                    friendSystem,
                    myThread,
                    friendThread,
                    ))



    # Validate sockets, and determine if the sends/receives are 1 to N or N to 1 or unused 
    socketCounts = {}
    socketCounts["send"] = collections.defaultdict(int)
    socketCounts["receive"] = collections.defaultdict(int)
    socketCounts["dataSource"] = collections.defaultdict(int)
    socketCounts["request"] = collections.defaultdict(int)

    socketDescs = {}
    socketDescs["send"] = collections.defaultdict(dict)
    socketDescs["receive"] = collections.defaultdict(dict)
    socketDescs["dataSource"] = collections.defaultdict(dict)
    socketDescs["request"] = collections.defaultdict(dict)


    funcNamePrefixes = {
        "send": "Send",
        "receive": "Receive",
        "dataSource": "Provide",
        "request": "Request",
    }

    pyFuncNamePrefixes = {
        "send": "Send",
        "receive": "Receive",
        "dataSource": "Provide",
        "request": "Request",
    }

    varNamePostFix = {
        "send": "_s",
        "receive": "_r",
        "dataSource": "_provide",
        "request": "_request",
    }

    def OutputFiles(ouputDesc, referenceFileLocation, baseFilename, templateData):
        for outputFile in ouputDesc:
            outFile = os.path.join(
                        os.path.join(
                            os.path.dirname(referenceFileLocation),
                            outputFile["outDir"]),
                        baseFilename + outputFile["extension"])

            if not outputFile["forceOverwrite"] and os.path.exists(outFile):
                continue

            templateFile = os.path.join( os.path.dirname(referenceFileLocation), outputFile["templateFile"] )

            template = jinEnv.from_string((open(templateFile).read())) 

            f = open(outFile, "w")
            f.write(template.render(templateData))
            f.close()


    class SocketSetupError(Exception):
        pass

    def GetFunctionName(socketFunction, socketName):
        return funcNamePrefixes[socketFunction] + socketName[0].upper() + socketName[1:]

    def GetVarName(inName):
        return inName[0].lower() + inName[1:]

    socketThreads = {}
    socketThreads["dataSource"] = collections.defaultdict(set)
    socketThreads["request"] = collections.defaultdict(set)


    # Getting all the sockets referenced and collecting information, while validating
    # TODO: Could split up validation/collecting tasks, harder to do but would be easier to read
    for system in systems:
        systemSockets = {}
        systemSockets["send"] = system.get("sends", [])
        systemSockets["receive"] = system.get("receives", [])
        systemSockets["dataSource"] = system.get("dataSources", [])
        systemSockets["request"] = system.get("requests", [])
        systemName, componentName = NamesFromDict(system)
        thread = systemToThreadName[systemName]

        for socketFunction, sockets in systemSockets.iteritems():
            for socket in sockets:
                socketName = socket[0]
                structType = socket[1]
                socketCounts[socketFunction][socketName] = socketCounts[socketFunction][socketName] + 1

                if socketFunction in ["send", "receive"]:
                    msgVarName = None
                    isProtobuf = False
                    if structType in protoMessageDescs:
                        msgVarName = structType[0].lower() + structType[1:]
                        structType = protoMessageDescs[structType]["fullName"]
                        isProtobuf = True

                    if socketName in socketDescs[socketFunction]:
                        if structType != socketDescs[socketFunction][socketName]["structType"]:
                            raise SocketSetupError("%s socket has conflicting struct types: %s and %s" % (
                                socketName, structType, socketDescs[socketFunction][socketName]["structType"]))

                    socketDescs[socketFunction][socketName] = {
                        "funcName": GetFunctionName(socketFunction, socketName),
                        "structType": structType,
                        "varName": convertCamel(socketName) + varNamePostFix[socketFunction],
                        "socketName": socketName,
                        "binded": None,
                        "internalTransportType": INTERNAL_SOCKET_TYPE,
                        "isProtobuf": isProtobuf,
                        "msgVarName": msgVarName,
                        "thread": thread,
                    }
                else:
                    provideStruct = socket[2]
                    if socketName in socketDescs[socketFunction]:
                        if structType != socketDescs[socketFunction][socketName]["request"]["pyStructType"]:
                            raise SocketSetupError("Datasource socket %s has conflicting request struct types: %s and %s" % (
                                socketName, structType, socketDescs[socketFunction][socketName]["request"]["pyStructType"]))
                        if provideStruct != socketDescs[socketFunction][socketName]["provide"]["pyStructType"]:
                            raise SocketSetupError("Datasource socket %s has conflicting provide struct types: %s and %s" % (
                                socketName, provideStruct, socketDescs[socketFunction][socketName]["provide"]["pyStructType"]))

                    socketDescs[socketFunction][socketName] = {
                        "socketName": socketName,
                        "varName": convertCamel(socketName) + varNamePostFix[socketFunction],
                        "funcName": GetFunctionName(socketFunction, socketName),
                        "request": {
                            "structType": protoMessageDescs[structType]["fullName"],
                            "varName": GetVarName(socketName + "Request"),
                            "pyPackage": protoMessageDescs[structType]["pyPackage"],
                            "pyStructType": structType,
                            },
                        "provide": {
                            "structType": protoMessageDescs[provideStruct]["fullName"],
                            "varName": GetVarName(socketName + "Reply"),
                            "pyPackage": protoMessageDescs[provideStruct]["pyPackage"],
                            "pyStructType": provideStruct,
                            },
                        "internalTransportType": INTERNAL_SOCKET_TYPE,
                        "externalTransport": ExternalSocketName(socketName),
                        "tcpExternalTransport": TCPExternalSocketName(socketName),
                        "pyFuncName": "Request" + socketName[0].upper() + socketName[1:],
                        "pyVarName": socketName,
                        "pyGetRequestVarFuncName": "Get%sRequestMsg" % (socketName[0].upper() + socketName[1:],),
                        "thread": thread,
                    }

                    socketThreads[socketFunction][socketName].add(thread)

    # Deadlocks can occur with request/provide sockets when threads have two way requests
    # Identify this and treat it as a critical error, as it is near impossible to do this correctly

    # Dict represents requestingThread->sourceThreads, to detect a two-way request dependency
    threadRequests = collections.defaultdict(set)
    for system in systems:
        systemName, componentName = NamesFromDict(system)
        thread = systemToThreadName[systemName]

        requests = system.get("requests", [])
        for request in requests:
            socketName = request[0]

            sourceThread = socketDescs["dataSource"][socketName]["thread"]
            if thread in threadRequests[sourceThread]:
                raise SocketSetupError("Two way request dependency on threads %s and %s! This is too tricky to allow! Refactor using sends/receives" % (thread, sourceThread,))

            threadRequests[thread].add(sourceThread)


    # Count validation
    for socketName in set(socketDescs["send"].keys()).union(set(socketDescs["receive"].keys())):
        if socketCounts["send"][socketName] == 0 or socketCounts["receive"][socketName] == 0:
            print "Unused socket", socketName, "setting to bind"
            if socketName in socketDescs["send"]:
                socketDescs["send"][socketName]["binded"] = True
            if socketName in socketDescs["receive"]:
                socketDescs["receive"][socketName]["binded"] = True
            continue

        if socketDescs["send"][socketName]["structType"] != socketDescs["receive"][socketName]["structType"]:
            raise SocketSetupError("Socket %s has conflicting struct defines for send and receive! %s and %s" % (
                socketName, socketDescs["send"][socketName]["structType"], socketDescs["receive"][socketName]["structType"]))

        if socketCounts["send"][socketName] == 1 and socketCounts["receive"][socketName] > 0:
            print "Socket", socketName + ", SEND will bind"
            socketDescs["send"][socketName]["binded"] = True
            socketDescs["receive"][socketName]["binded"] = False
            continue

        if socketCounts["send"][socketName] > 0 and socketCounts["receive"][socketName] == 1:
            print "Socket", socketName + ", RECEIVE will bind"
            socketDescs["send"][socketName]["binded"] = False
            socketDescs["receive"][socketName]["binded"] = True
            continue

        if socketCounts["send"][socketName] > 0 and socketCounts["receive"][socketName] > 0:
            raise SocketSetupError("Socket %s is N-to-N, this is not currently supported!")

    # Datasource is always bind, so the check is much simpler
    for socketName in set(socketDescs["dataSource"].keys()).union(set(socketDescs["request"].keys())):
        if socketCounts["dataSource"][socketName] > 1:
            raise SocketSetupError("Multiple sockets for data source %s" % (socketName,))

        for thread in socketThreads["dataSource"][socketName]:
            if thread in socketThreads["request"][socketName]:
                raise SocketSetupError("Socket %s exists on the same thread \"%s\" for both request and data" % (socketName, thread))


    del socketCounts
    del funcNamePrefixes
    del varNamePostFix


    # We could get this a smart way, but since we're already iterating through
    #      the systems, it's better not to repeat the naming logic
    allSystemNames = []


    # This is simply building the template data for jinja2 to run
    for system in systems:
        systemName, componentName = NamesFromDict(system)

        allSystemNames.append(systemName)

        componentData = system.get("componentData", [])
        friendSystems = system.get("friendSystems", [])
        functions = system.get("functions", [])
        sends = system.get("sends", [])
        receives = system.get("receives", [])
        dataSources = system.get("dataSources", [])
        requests = system.get("requests", [])
        includes = system.get("includes", [])
        variables = system.get("vars", [])
        variables = [{"type": v[0], "name": v[1], "default": v[2]} for v in variables]
        forwards = set(system.get("forwards", []))

        macroName = Macroize(systemName)

        includes = set(includes)
        includes.add("\"SystemManager.h\"")
        includes.add("\"Messaging.h\"")
        includes.add("\"RunLoopComponentSystem.h\"")
        includes.add("\"Component.h\"")
        includes.add("\"%s\"" % (protoMessageDescs[componentName]["headerFile"],))


        for dataSource in dataSources:
            if dataSource[1] not in protoMessageDescs:
                raise SocketSetupError( "Protobuf struct not found in proto files: " +str(dataSource[1]) )
            if dataSource[2] not in protoMessageDescs:
                raise SocketSetupError( "Protobuf struct not found in proto files: " +str(dataSource[2]) )
            includes.add("\"%s\"" % (protoMessageDescs[dataSource[1]]["headerFile"],))
            includes.add("\"%s\"" % (protoMessageDescs[dataSource[2]]["headerFile"],))
        for request in requests:
            if request[1] not in protoMessageDescs:
                raise SocketSetupError( "Protobuf struct not found in proto files: " +str(request[1]) )
            if request[2] not in protoMessageDescs:
                raise SocketSetupError( "Protobuf struct not found in proto files: " +str(request[2]) )
            includes.add("\"%s\"" % (protoMessageDescs[request[1]]["headerFile"],))
            includes.add("\"%s\"" % (protoMessageDescs[request[2]]["headerFile"],))
        for send in sends:
            if send[1] in protoMessageDescs:
                includes.add("\"%s\"" % (protoMessageDescs[send[1]]["headerFile"], ))
        for receive in receives:
            if receive[1] in protoMessageDescs:
                includes.add("\"%s\"" % (protoMessageDescs[receive[1]]["headerFile"], ))


        hasEntityRef = False
        for data in componentData:
            if data[0] in ["entityRef", "entityRefSet"]:
                hasEntityRef = True
                includes.add("<set>")
                break
            elif data[0] in ["doubleVector"]:
                includes.add("<vector>")

        hasBindedSockets = False
        hasConnectedSockets = False

        sendSockets = [socketDescs["send"][socketName] for socketName, _ in sends]
        receiveSockets = [socketDescs["receive"][socketName] for socketName, _ in receives]
        dataSourceSockets = [socketDescs["dataSource"][socketName] for socketName, _, _ in dataSources]
        requestSockets = [socketDescs["request"][socketName] for socketName, _, _ in requests]

        allSockets = sendSockets + receiveSockets
        hasSockets = allSockets or dataSourceSockets or requestSockets
        hasBindedSockets = dataSourceSockets or len([desc for desc in allSockets if desc["binded"] == True]) > 0
        hasConnectedSockets = requestSockets or len([desc for desc in allSockets if desc["binded"] == False]) > 0

        componentData = [{"type":data[0], "name": data[1], "default": data[2] if len(data) > 2 else "0"} for data in componentData]
        friendSystems = [{  "systemName": friend[0].upper() + friend[1:],
                            "varName": friend[0].lower() + friend[1:]}
                        for friend in friendSystems]

        for friendSystem in friendSystems:
            if friendSystem["systemName"] not in forwards:
                includes.add("\"%s.h\"" % (friendSystem["systemName"],))

        def GetFunctionDict(function):
            function = function.strip()
            if function[-1] == ";":
                function = function[0:-1]

            functionParts = [f.strip() for f in function.split(" ")]
            if functionParts[0] == "virtual":
                functionParts = functionParts[1:]

            if functionParts[0][0] == "~":
                functionParts[0] = systemName + "::" + functionParts[0]
            else:
                functionParts[1] = systemName + "::" + functionParts[1]
            return {
                "def": function,
                "imp": " ".join(functionParts),
                }

        functions = [GetFunctionDict(function) for function in functions]

        systemType = "Threaded" if systemName in threadedSystems else "RunLoop"


        class Counter(object):
            def Reset(self):
                self.i = 0
            def Increment(self):
                ret = self.i
                self.i = self.i + 1
                return ret
        counter = Counter()

        def getArrayLengths(*args):
            return sum([len(arg) for arg in args])


        templateData = {
            "systemName": systemName,
            "componentName": componentName,
            "macroName": macroName,
            "componentData": componentData,
            "includes": includes,
            "hasEntityRef": hasEntityRef,
            "sends": sendSockets,
            "receives": receiveSockets,
            "dataSources": dataSourceSockets,
            "requests": requestSockets,
            "hasSockets": hasSockets,
            "hasBindedSockets": hasBindedSockets,
            "hasConnectedSockets": hasConnectedSockets,
            "friendSystems": friendSystems,
            "functions": functions,
            "systemType": systemType,
            "vars": variables,
            "len": getArrayLengths,
            "resetCounter": counter.Reset(),
            "incCounter": counter.Increment(),
            "forwards": forwards,
            "useTCP": useTCP,
        }

        OutputFiles(SYSTEM_OUTPUT_FILES, jsonFilename, systemName, templateData)


    for systemManager in systemManagers:
        managerName = systemManager["managerName"]
        defaultTickTime = systemManager.get("defaultTickTime", 1.0)

        metaSystems = systemManager.get("metaSystems", [])
        dynamicFiles = systemManager.get("dynamicFiles", [])
        runloopSystems = [sysName for sysName in allSystemNames if sysName not in threadedSystems]


        systems = systemManager.get("systems", allSystemNames)
        


        templateData = {
            "systemManagerName": managerName,
            "macroName": Macroize(managerName),
            "defaultTickTime": defaultTickTime,
            "systems": runloopSystems,
            "metaSystems": metaSystems,
            "cppDir": "./" + sys.argv[1] + "/src/",
            "dynamicFiles": dynamicFiles,
        }

        OutputFiles(SYSTEM_MANAGER_OUTPUT_FILES, jsonFilename, managerName, templateData)

    pyDataSources = [info for msg, info in socketDescs["dataSource"].iteritems()]
    pyPackages = set([info["request"]["pyPackage"] for info in pyDataSources])
    pyPackages = pyPackages.union(set([info["provide"]["pyPackage"] for info in pyDataSources]))

    templateData = {
        "dataSources": pyDataSources,
        "pyPackages": pyPackages,
        "useTCP": useTCP,
    }

    OutputFiles(PYTHON_OUTPUT_FILES, jsonFilename, "__init__", templateData)


except Exception as e:
    import traceback
    traceback.print_exc()
    sys.exit(1)


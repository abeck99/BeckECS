# Getting Started


## Building the server

The best way to get started is to take a look at the example project. Here's what you need to get it started:

1.    Make a copy of the "example" subdirectory into the same directory that "EntitySystem" exists in.
2.    Copy a local waf script to the newly created "example" directory
3.    Configure waf:
*        OSX (darwin): CXX=clang++ ./waf configure
*        Windows: python waf configure --msvc_targets="x86"
4.    Resolve any dependencies (protobuf and zmq), and make sure to install the python support to try the python client
5.    Build:
*        OSX (darwin): ./waf
*        Windows: python python waf
6.    Run:
*        OSX: ./runExampleGame.sh
*        Windows: python ConvertJsonToBuf.py saves/example.json saves/example.buf
*                 build/exampleGame saves/example.buf

All you will see is log output showing the different tick times.


## Running a simple python client

Run Python from a seperate terminal window to see what you can do.

```
import PyClientExample

# The game starts paused, so if you read example.json and are expecting a hello world output, 
#       you are not getting it because the time scale is 0, set it with this command
PyClientExample.SetTimeScale(2) # Twice speed

# The only game exposed function is "TriggerTimer", try running it with timerID=2 and follow the code to
#       understand the results
PyClientExample.RequestTriggerTimer(timerID=2)

# Get the current state as a protobuf string:
PyClientExample.GetState()

# Finally, shutdown
PyClientExample.Shutdown()
```

## Dynamic system recompiling

One great advantage of using a highly data driven approach is we can easily pull out systems, since they should be essientially stateless (this specific definition is defined below), and relink to a new dynamic lib.

This feature is incompatible with windows, but works very well (as long as the systems are stateless) in *nix systems.

When configuring waf, pass in --dynamic or --dev flags, IE:
./waf configure --dynamic
-- or, to include debug symbols as well --
./waf configure --dev

This will build the systems as dynamic libs. When the server is running, try modifiying a system, it should seemlessly pause the game, recompile the dynamic library, and link it in without any extra effort.


## Stateless systems

I owe you an explanation of what I mean by stateless. To be a "pure" entity system, we must not include anything in the systems themselves. If you want global variables, you create a system that only contains one component, you do not store this in the system. This allows the game state to be easily serializable, and allows us to do the dynamic loading as above.

Now, paraphrasing Knuth, you need the right data structure for the right job. And the reality is the component data structure is not always the right one. In this case you will need to store some state on the systems. For example, you have an "OfficeOwnerComponent" but you frequently want to know who owns X office, as well which offices Y owns. You will need to cache this data in a map to get it efficiently.

This kind of state information in the system is acceptable, and demonstrated in the example app. This is because the state is driven by the Add/RemoveComponent functions, so all the "state" is filled up as the components are serialized into the system.

Any other state is unacceptable in an ECS, and can easily be implemented as a component.


## Structure overview of the example project

A little guidance to help understand the structure:

The systems for exampleGame are located in a json file, Example/Systems. This file also contains some documentation.
The components definitions are located in Example/Components/components.proto
Everything under Autogen is created from these files
Implementation of game systems is under Example/src
saves/example.json is a json representation of the game state.
ConvertJsonToBuf.py will convert the json to protobuf, which can be loaded by the exampleGame.
The server will not save any data, it is the clients responsibility to get the state and save it


# BeckECS: A highly threaded game logic engine

BeckECS is an entity-component system implemented to draw on the strengths of the ECS approach. It is very easy to create a multi-threaded application, running logic and passing messages along to different threads. It achieves this ease by using the jinja2 templating library to build headers and python interface code from an easily readable/editable json file, leaving the amount of actual implementation to a minimum.

This is a very pure implementation of ECS, there is no Entity Object, an entity is simply an integer that is only referenced in the component systems. The engine itself treats every component independently and has no idea that these pieces of data are related to other pieces of data. At first, this is a very difficult concept to wrap your head around, but you must understand this before attempting to use this engine.

The header generation step also includes some validation to avoid common mistakes with threading. 

The engine also supports dynamic system loading, meaning you can edit system code on the fly and the code is automatically recompiled and linked. Please refer to GettingStarted.md for more information on the dynamic reloading.


The dependencies for running the engine are all very widely used libraries, and should be very simple to install and deploy across multiple platforms.

## Dependencies for building
--------------------

1.    Python 2.7ish:
    *        http://www.python.org/
2.    Jinja2:
    *        http://jinja.pocoo.org/
3.    Waf bin:
    *        http://code.google.com/p/waf/
4.    ZMQ3, built as static lib:
    *        https://github.com/zeromq/zeromq3-x
5.    Google Protobufs:
    *        http://code.google.com/p/protobuf/

## Dependencies for running the python client example
--------------------
1.    pyzmq, ZMQ Python wrappers 
    *        https://github.com/zeromq/pyzmq
2.    Python Google Protobufs
    *        As above, setup.py located in python subdirectory
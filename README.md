EmPOWER: Mobile Networks Operating System
=========================================

### What is EmPOWER?
EmPOWER is a new network operating system designed for heterogeneous mobile networks.

### Top-Level Features
* Supports both LTE and WiFi radio access networks
* Northbound abstractions for a global network view, network graph, and
  application intents.
* REST API and native (Python) API for accessing the Northbound abstractions
* Support for Click-based Lightweight Virtual Networks Functions
* Declarative VNF chaning on precise portion of the flowspace
* Flexible southbound interface supporting WiFi APs LTE eNBs

Checkout out our [website](http://empower.create-net.org/) and our [wiki](https://github.com/5g-empower/empower-runtime/wiki)

This repository includes the EmPOWER LVAP Agent. The Agent is built using the Click modular router and implements a reference EmPOWER WiFi Access Point.

### Changes
This fork adds a WTP/controller discovery feature: Instead of statically defining the controller (runtime) IP address in the Click router configuration file, this version first sends a UDP broadcast message and waits for a controller to reply. Only then, a TCP connection is established and the normal EmPOWER protocol is started.

It also extends the EmPOWER controller/agent communication protocol with a bidirectional heartbeat to improve error management. This is done by simply returning the hello packet sent by the agent to the controller. Therefore, the agent knows if the controller is not reachable anymore and can restart the controller discovery protocol.

### Usage
Compile with:
./configure --enable-userlevel --disable-linuxmodule --enable-wifi --enable-empower --enable-empsocket
make

Code is released under the Apache License, Version 2.0.

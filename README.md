<span class="badge-paypal"><a href="https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=GME2V7WLLFRT2" title="Donate to this project using Paypal"><img src="https://img.shields.io/badge/paypal-donate-yellow.svg" alt="PayPal donate button" /></a></span>
<span class="badge-bitcoin"><a href="https://www.coinbase.com/checkouts/95751916234a5d59b197d8ef1916cfe6" title="Donate once-off to this project using Bitcoin"><img src="https://img.shields.io/badge/bitcoin-donate-yellow.svg" alt="Bitcoin donate button" /></a></span>

# Club

A lightweight Peer-to-Peer networking engine for real time applications written in C++14.

# Motivation

Real time applications such as Online games, VoIP, Instant Messaging or Videoconference apps need a way to communicate and synchronize state among interested peers. The traditional way of doing so is to have a dedicated _server_ that receives commands from _clients_ while assigning an implicit order (order in which messages are received) to those commands.

Such centralized structure is relatively easy to implement and works great for many aplications, but comes with drawbacks in the form of server bandwidth, maintenance and attack protection costs. This renders the centralized approach [infeasible for applications with a smaller budget](https://hookrace.net/blog/ddnet-evolution-architecture-technology/).

On the other hand, decentralization adds another layers of complexity on top of already complex matter. There are problems such as not every node can connect to every other node directly, packets from different sources may be received by other nodes in different order, and in cases of network partition, there is no central authority that says who is in the game and who is not.

Club implements solutions to such problems and hides this complexity behind a simple to use API.


# Dependencies

* Cmake (version >= 3.2)
* Decent C++14 compiler. Tested on g++5.4
* Boost (version >= 1.58)

# Build & run

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release
# To make everything (the library, tests, benchmark and a demo) run
# make without arguments. Alternatively use these targets:
#  club                  : Builds the libclub.a library
#  club-tests            : Builds tests
#  transport-speed-bench : Builds a not-so-complete transport benchmark
#  rendezvous-server     : Builds the rendezvous server
make
```
## Run tests

```
# The stun_client test expects stun servers (not part of Club) to run on localhost.
stunserver --primaryport=3478 &
stunserver --primaryport=3480 &
stunserver --primaryport=3482 &
stunserver --primaryport=3484 &
stunserver --primaryport=3486 &
./club-tests --run_test="*" --log_level=test_suite
```

## Run chat demo
To run the _club-chat_ demo, invoke `./club-chat` from multiple teminals. The terminals need not be on the same PC nor LAN but the app does need an internet acess.

# API Description

There are essentially only two classes that developers need to know about: [_Socket_](https://github.com/inetic/libclub/blob/readme/include/club/socket.h#L712) and [_Hub_](https://github.com/inetic/libclub/blob/readme/include/club/hub.h). _Socket_ can be seen as a cross between the traditional TCP and UDP socket, in that it is connection oriented, provides both reliable and unreliable message delivery and congestion control.

_Hub_ is an interface to the network of connected nodes. It provides an interface to send messages and merge networks together. _Hub_ currently provides two types of send operations.

* unreliable broadcast - fast - can be used e.g. for sending player position updates or voice data.
* reliable-totally-ordered (RTO) broadcast - slow - to make decisions about a global state. E.g. in games: is a particular switch on or off, who is the owner of an item,...

The total-order property makes this guarantee: *If a node received a message M1 before a message M2, then every node that received both of those messages shall receive in the same order*.

# Demos

* [club-chat](https://github.com/inetic/libclub/blob/readme/demo/club-chat.cpp) - A simple CLI chat application. Demonstrates how to merge networks and send RTO broadcast.
* [sicoop](https://play.google.com/store/apps/details?id=com.sicoop) - A silly coop multiplayer game for Android phones where Club can be seen in action.

# Features

* Network membership consensus - Information about who is in the network is also totally ordered
* Implicit packet routing - Full graph topology is not necessary
* Totally ordered reliable message broadcast
* Fast unreliable broadcast
* [NAT traversal through UDP hole punching](http://www.brynosaurus.com/pub/net/p2pnat/)
* Implicit full graph formation - TODO
* [STUN](https://tools.ietf.org/html/rfc5389) client - See [implementation](https://github.com/inetic/libclub/blob/readme/src/club/stun_client.cpp) and [tests](https://github.com/inetic/libclub/blob/readme/tests/test-stun.cpp)
* [LEBDAT](https://tools.ietf.org/html/rfc6817#section-3.3) congestion control for low latency message transmission
* Small number of dependencies
* Rendezvous server - A simple server where nodes find each other.


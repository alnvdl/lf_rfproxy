# libfluid-rfproxy

This is an implementation of RFProxy using libfluid. It is a standalone
application, meaning that it can run by itself, without the need for an
OpenFlow controller (in a sense, it is a very lean and specific-purpose 
OpenFlow controller).

## Building
0. Install the dependencies:
```
$ sudo apt-get install build-essential git libboost-dev libboost-program-options-dev libboost-thread-dev libboost-filesystem-dev iproute-dev openvswitch-switch mongodb python-pymongo
```

0. Install libfluid following [these instructions](http://opennetworkingfoundation.github.io/libfluid/md_doc_QuickStart.html).

0. Clone RouteFlow's main repository and follow the build and run 
instructions in its README:
```
$ git clone ...
$ cd RouteFlow
[Perform build and run instructions]
```

0. Inside the RouteFlow directory, clone the `libfluid-rfproxy` 
repository:
```
$ cd RouteFlow
$ git clone ...
```

0. Go the `libfluid-rfproxy` directory and run `make`:
```
$ cd libfluid-rfproxy
$ make
```

## Running
These steps assume you are already able to run `rftest1` with the 
default POX implementation. That means you have followed the RouteFlow 
README instructions.

0. Copy the `rftest-libfluid` script to the `rftest` directory in 
RouteFlow:
```
$ cd ..
$ cp libfluid-rfproxy/rftest-libluid rftest
```

0. Run the test script:
```
$ cd rftest
$ sudo ./rftest-libfluid
```

0. Log in to the `b1` container and make sure it works:
```
$ sudo lxc-console -n b1
[Login with ubuntu/ubuntu user/password]
$ ping 172.31.2.2
```

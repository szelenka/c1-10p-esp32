# ROS2 Reference

The Robot Operating System (ROS) is a set of software libraries and tools for building robot applications. From drivers and state-of-the-art algorithms to powerful developer tools, ROS has the open source tools you need for your next robotics project.

[ROS2](https://github.com/ros2/ros2) code is located in build/ros2 folder, if not present clone the repository to that location.

### Pros

- The concept of a Graph of Nodes and passing messages between nodes is appealing, but we need to focus the full graph execution on a single ESP32.

### Cons

- ROS2 Agents are compute intensive, making them unsuitable for ESP32.


## Graph

The ROS graph is a network of ROS2 elements processing data together at the same time. It encompasses all executables and the connections between them if you were to map them all out and visualize them.

## Nodes

Each node in ROS2 should be responsible for a single, modular purpose, e.g. controlling the wheel motors or publishing the sensor data from a laser range-finder. Each node can send and receive data from other nodes via topics, services, actions, or parameters.

A full robotic system is comprised of many nodes working in concert. In ROS2, a single executable can contain one or more nodes.

## Topics 

ROS2 breaks complex systems down into many modular nodes. Topics are a vital element of the ROS2 graph that act as a bus for nodes to exchange messages.

A node may publish data to any number of topics and simultaneously have subscriptions to any number of topics.

Topics are one of the main ways in which data is moved between nodes and therefore between different parts of the system.

## Services

Services are another method of communication for nodes in the ROS2 graph. Services are based on a call-and-response model versus the publisher-subscriber model of topics. While topics allow nodes to subscribe to data streams and get continual updates, services only provide data when they are specifically called by a client.

## Parameters

A parameter is a configuration value of a node. You can think of parameters as node settings. A node can store parameters as integers, floats, booleans, strings, and lists. In ROS2, each node maintains its own parameters. 

Parameters in ROS 2 are associated with individual nodes. Parameters are used to configure nodes at startup (and during runtime), without changing the code. The lifetime of a parameter is tied to the lifetime of the node (though the node could implement some sort of persistence to reload values after restart).

Parameters are addressed by node name, node namespace, parameter name, and parameter namespace. Providing a parameter namespace is optional.

Each parameter consists of a key, a value, and a descriptor. The key is a string and the value is one of the following types: bool, int64, float64, string, byte[], bool[], int64[], float64[] or string[]. By default all descriptors are empty, but can contain parameter descriptions, value ranges, type information, and additional constraints.

## Actions

Actions are one of the communication types in ROS 2 and are intended for long running tasks. They consist of three parts: a goal, feedback, and a result.

Actions are built on topics and services. Their functionality is similar to services, except actions can be canceled. They also provide steady feedback, as opposed to services which return a single response.

Actions use a client-server model, similar to the publisher-subscriber model (described in topics section). An “action client” node sends a goal to an “action server” node that acknowledges the goal and returns a stream of feedback and a result.

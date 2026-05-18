# Keyframes Reference

Keyframes are certain positions where characters have to be at a specific point in time. Those keyframes can be seconds apart, and computers calculate all the frames in-between for a smooth movement. 

In robotics, keyframe animation refers to a method of programming robot movements by defining key poses or positions at specific points in time, allowing the robot to smoothly transition between these poses. This technique is analogous to keyframe animation in 2D and 3D animation, where artists define key poses for characters and the software interpolates the in-between frames. In robotics, these keyframes define the robot's joint angles or end-effector positions at specific timestamps, and the robot's controller calculates the necessary joint movements to achieve those poses. 

## Dope Sheet

A dope sheet is a visual representation of keyframes and their timing. It is a grid of time and space, where each row represents a keyframe and each column represents a joint or end-effector. The dope sheet is used to animate characters by defining the keyframes and their timing. 

## Interpolation

Interpolation is the process of estimating the values of unknown data points between known data points. In robotics, interpolation is used to estimate the positions of joints or end-effectors between keyframes. 

## Timeline

The sequence of time in which the animation occurs, with keyframes placed at specific points on the timeline. 

## Joint Angles

The angles of the robot's joints, which determine the robot's configuration. 

## End-Effector Position

The position of the robot's end-effector (e.g., hand or gripper) in space. 

## Motion Planning

The process of generating a sequence of robot motions to achieve a desired task, often using keyframe animation as a basis. 

## Robotic Operating System (ROS)

A popular framework for developing robot software, which includes tools for motion planning and keyframe animation. 

## Real-time actuator control Keyframe

Early brainstorming came up with a simple structure for keyframes, which included the following. This can be used as a guide, but not hard rule:

- Timestamp for execution
- Target system (ESP32, SAMD21, etc.)
- Object ID (e.g. servo, motor, etc.)
- Action Type (e.g. set, move, etc.)
- Action Data (e.g. position, velocity, etc.)

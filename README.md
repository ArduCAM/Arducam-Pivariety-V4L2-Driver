# Arducam-Pivariety-V4L2-Driver
This driver is used for Arducam mipi camera with Pivariety board 
# Driver support
This repository only provides the driver and camera software tools for Raspberry pi. This driver is based on Arducam Pivariety Project.

Pivariety is a Raspberry Pi V4L2 kernel camera driver framework which can support any MIPI cameras Arducam provides but natively not supported by the Raspberry Pi. If you have native Raspberry pi camera modules like OV5647, IMX219 and IMX477, please do not use this driver.

A single-camera driver for all is the main goal of Pivariety project, the user doesn't need to develop their own camera driver for Nvidia Jetson boards and even more, user can switch between different Arducam cameras without switching camera driver. Software compatibility for Jetvariety V4L2 driver is also another consideration for this project.
Current driver supports the following kerner version, for other kernel please send us request for adding support or source code to compile by yourself.

- 5.10.17
- 5.10.17-v7+
- 5.10-17-v7l+

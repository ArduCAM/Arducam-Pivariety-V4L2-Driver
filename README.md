# Arducam-Pivariety-V4L2-Driver
This driver is used for Arducam mipi camera with Pivariety board 

**Note: Since [5.15.38](https://github.com/raspberrypi/firmware/commit/1441400b6b3bfeac36422a636776c60b8983a7d3), the [arducam-pivariety driver](https://github.com/raspberrypi/linux/blob/rpi-5.15.y/drivers/media/i2c/arducam-pivariety.c) has been merged into the Raspberry Pi kernel and the name of the device tree is changed to arducam-pivariety, so dtoverlay=arducam-pivariety is required to set the overlay**

# Supported Camera Modules
[B0323](https://www.uctronics.com/arducam-pivariety-16mp-imx298-color-camera-module-for-rpi-4b-3b-2b-3a-pi-zero-cm3-cm4.html)  
[B0324](https://www.uctronics.com/arducam-pivariety-21mp-imx230-color-camera-module-for-rpi-4b-3b-2b-3a-pi-zero-cm3-cm4.html)  
[B0333](https://www.uctronics.com/arducam-for-raspberry-pi-ultra-low-light-camera-1080p-hd-wide-angle-pivariety-camera-module-based-on-1-2-7inch-2mp-starvis-sensor-imx462-compatible-with-raspberry-pi-isp-and-gstreamer-plugin.html)  
[B0350](https://www.uctronics.com/arducam-8mp-synchronized-stereo-camera-bundle-kit-for-raspberry-pi.html)  
[B0353](https://www.uctronics.com/arducam-full-hd-color-global-shutter-camera-for-raspberry-pi-2-3mp-ar0234-wide-angle-pivariety-camera-module.html)  
[B0367](https://www.uctronics.com/arducam-18mp-ar1820hs-camera-module-for-raspberry-pi-pivariety.html)  
[B0381](https://www.uctronics.com/2mp-global-shutter-ov2311-mono-camera-modules-pivariety.html)  

# Driver support
This repository only provides the driver and camera software tools for Raspberry pi. This driver is based on Arducam Pivariety Project.

Pivariety is a Raspberry Pi V4L2 kernel camera driver framework which can support any MIPI cameras Arducam provides but natively not supported by the Raspberry Pi. If you have native Raspberry pi camera modules like OV5647, IMX219 and IMX477, please do not use this driver.

A single-camera driver for all is the main goal of Pivariety project, the user doesn't need to develop their own camera driver for Nvidia Jetson boards and even more, user can switch between different Arducam cameras without switching camera driver. Software compatibility for Jetvariety V4L2 driver is also another consideration for this project.


# For IMX519
For the relevant source code of IMX519, please refer to:
[IMX519_AK7375](https://github.com/ArduCAM/IMX519_AK7375)
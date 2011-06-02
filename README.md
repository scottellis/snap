  snap
=======

Derived from the V4L2 example here:

http://v4l2spec.bytesex.org/spec/capture-example.html

Written for testing a specific camera driver for an Aptina mt9p031 sensor.
Testing was done using a custom gumstix Overo board and an LI5M03 Leopardboard
with a Beagle XM.

Run with no arguments, the program request one picture from /dev/video0 and 
saves the image as uyvy.img in the current working directory.

Used mostly to control a camera that is already streaming video using another
application. Usually gstreamer or a custom app.


  Build
-------

There is a makefile for cross-builds using the OE tools.

Customize for your OETMP or define OETMP it in your environment,
then run make.

Copy the snap program to the gumstix or beagleboard.


  Usage
-------

	root@overo:~# ./snap -h
	Usage: ./snap [options]

	Options:
	-f | --format        Pixel format [uyvy, yuyv, bayer] (default uyvy)
	-s | --size          Image size  0:2560x1920 1:1280x960 2:640x480  3:320x240
	-e | --exposure      Exposure time in microseconds
	-r | --red           Red gain
	-b | --blue          Blue gain
	-G | --green1        Green1 gain
	-g | --green2        Green2 gain
	-n | --gain          Global gain
	-k | --skip          Sensor skip mode 0,1,3 default 0 affects max frame size
	-o | --nosnap        Only apply gain/exposure settings, no picture
	-d | --dump          Dump current gain/exposure settings
	-h | --help          Print this message


If the -o option is not provided, snap will take one picture and save
the image in the local directory as <format>.img. All parameters, gain, 
exposure, skip, image size and format will be used if provided. The camera
is turned off after the snapshot is retrieved.

The default image size is 2560x1920 and the default format is uyvy.

The skip option can be set at any time, but only takes affect when the camera
starts streaming. So if the camera is running via another application, you will
have to stop it first in order for the skip param to take effect.

A skip option of 1 or 3 tells the sensor to skip that many rows and columns
as it scans the array. Currently pixels are always binned the same amount as the
skip parameter. (Might make this configurable in the driver later.) Skipping 
results in less clear images, but increases the framerate. 

Framerates for Overo (48MHz pixclock, 1.8vdd_io)
Skip = 0: 2560x1920: 7  fps
Skip = 1: 1280x960 : 28  fps
Skip = 3: 640x480  : 60 fps

Framerates for LI5M03 (96MHz pixclock, 2.8vdd_io)
Skip = 0: 2560x1920: 14  fps
Skip = 1: 1280x960 : 56  fps
Skip = 3: 640x480  : 120 fps


If the -o option is passed, gains, exposure and skip will be changed, but 
image size and format are ignored and no picture is requested.

The -o option is useful if you are using the camera with another application
such as gstreamer.

For example:

	root@overo:~# gst-launch -e v4l2src ! video/x-raw-yuv,width=640,height=480,framerate=56/1 ! xvimagesink


then you can make image adjustments on-the-fly with snap this way

	root@overo:~# ./snap -o -e80000
	root@overo:~# ./snap -o -r4 -g2 -G2




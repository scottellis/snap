  snap
=======

Derived from the V4L2 example here:

http://v4l2spec.bytesex.org/spec/capture-example.html

Customized for testing a specific camera driver for an Aptina mt9p031 sensor.

Run with no arguments, the program request one picture from /dev/video0 and 
saves the image as rawimage.dat in the current working directory.


  Build
-------

There are two makefiles, one native, one for cross-builds using the OE tools.

Customize Makefile-cross for your OETMP or define OETMP it in your environment,
then run

	make -f Makefile-cross

Then copy the snap program to the gumstix.


  Usage
-------

	root@overo:~# ./snap -h
	Usage: ./snap [options]

	Options:
	-s | --size          size= 0->2560x1920 1->1280x960 2->640x480
	-e | --exposure      Exposure time in microseconds
	-r | --red           Red gain
	-b | --blue          Blue gain
	-G | --green1        Green1 gain
	-g | --green2        Green2 gain
	-n | --gain          Global gain
	-a | --bayer         Request image in bayer GRBG format
	-y | --yuv           Request image in YUYV format
	-o | --nosnap        Only apply gain/exposure settings, no picture
	-h | --help          Print this message


If the -o option is not provided, snap will take one picture and save
the image in the local directory as rawimage.dat. All parameters, gain, exposure,
image size and format will be used if provided.

The default image size is 2560x1920 and the default format is Bayer. If you
want to change image size you must request YUV format (-y) and then provide
the new image size as either -s1 or -s2 for 1280x960 or 640x480 sizes respectivelly.

If the -o option is passed, gain and exposure will be changed, but image size
and format are ignored and no picture is requested.

The -o option is useful if you are using the camera with another application
such as gstreamer.

For example:

	root@overo:~# gst-launch v4l2src ! video/x-raw-yuv,format=\(fourcc\)YUY2,width=640,height=480 ! ffmpegcolorspace ! xvimagesink


then you can make image adjustments on-the-fly with snap this way

	root@overo:~# ./snap -o -e100000
	root@overo:~# ./snap -o -r4 -g2 -G2


The load is pretty low while running gstreamer and displaying the image.

	top - 14:44:52 up  3:57,  3 users,  load average: 0.00, 0.00, 0.00
	Tasks:  65 total,   1 running,  64 sleeping,   0 stopped,   0 zombie
	Cpu(s):  6.6%us,  2.3%sy,  0.0%ni, 90.8%id,  0.0%wa,  0.3%hi,  0.0%si,  0.0%st
	Mem:    501868k total,   241476k used,   260392k free,      600k buffers
	Swap:        0k total,        0k used,        0k free,   180968k cached

	  PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND            
	 5367 root      20   0 37720 7072 4880 S  4.6  1.4   1:47.58 gst-launch-0.10    
	 4886 root      10 -10 66888  13m 7436 S  3.0  2.8   5:24.16 enlightenment      
	 4823 root      20   0 15004 8144 3700 S  2.0  1.6   1:28.80 Xorg               
	 5432 root      20   0  2344 1084  892 R  0.7  0.2   0:00.08 top                
	    3 root      20   0     0    0    0 S  0.3  0.0   0:00.19 ksoftirqd/0        
	 4911 root      20   0  3128  776  616 S  0.3  0.2   0:08.63 ipaq-sleep         
	    1 root      20   0  1560  580  516 S  0.0  0.1   0:05.47 init               
	    2 root      20   0     0    0    0 S  0.0  0.0   0:00.00 kthreadd           
	    4 root      RT   0     0    0    0 S  0.0  0.0   0:00.00 watchdog/0         
	    5 root      20   0     0    0    0 S  0.0  0.0   0:02.85 events/0           
	    6 root      20   0     0    0    0 S  0.0  0.0   0:00.00 khelper            
	    7 root      20   0     0    0    0 S  0.0  0.0   0:00.00 async/mgr          
	    8 root      20   0     0    0    0 S  0.0  0.0   0:00.06 sync_supers        
	    9 root      20   0     0    0    0 S  0.0  0.0   0:00.12 bdi-default        
	   10 root      20   0     0    0    0 S  0.0  0.0   0:00.00 kblockd/0          
	   11 root      20   0     0    0    0 S  0.0  0.0   0:00.00 omap2_mcspi        
	   12 root      20   0     0    0    0 S  0.0  0.0   0:00.00 ksuspend_usbd      




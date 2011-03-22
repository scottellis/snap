  snap
=======

Derived from the V4L2 example here:

http://v4l2spec.bytesex.org/spec/capture-example.html

Customized for testing specific cameras. Right now a mt9p031 sensor.

Run with no arguments, the program request one picture from /dev/video0 and 
saves the image as rawimage.dat in the current working directory.

Any more then that, refer to the source.


  Build
-------

There are two makefiles, one native, one for cross-builds using the OE tools.

Customize Makefile-cross for your OETMP or define it in your environment.





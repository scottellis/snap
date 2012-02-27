/*
   V4L2 video capture example
 
   This program can be used and distributed without restrictions.
 
   Modified for Gumstix Caspa (MT9V032) driver.

   The following controls are provided

   V4L2_CID_BRIGHTNESS : 0-255, step 1, default 16
   V4L2_CID_CONTRAST : 0-255, step 1, default 16
   V4L2_CID_EXPOSURE : 2-566, step 1, default 480
   V4L2_CID_AUTOGAIN : boolean
   V4L2_CID_GAIN : 16-64, step 1, default 16
   V4L2_CID_HFLIP : boolean
   V4L2_CID_VFLIP : boolean
   V4L2_CID_COLORFX : 0-2 { none, bw, sepia }
   V4L2_CID_EXPOSURE_AUTO : boolean

   Image Dimensions : w x h : 752 x 480
	
   But we ask for resized images at 640x480 to prevent page faults
   in the ISP/V4L2 code retrieving the image.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h> 
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>

#define CASPA_IMAGE_WIDTH 640
#define CASPA_IMAGE_HEIGHT 480

struct buffer {
        void *start;
        size_t length;
};


char dev_name[] = "/dev/video0";

int format;
int brightness;
int contrast;
int exposure;
int gain;
int auto_gain;
int auto_exposure;
int color_effects;
int hflip;
int vflip;
int snap;

int image_width = CASPA_IMAGE_WIDTH;
int image_height = CASPA_IMAGE_HEIGHT;

int fd = -1;
struct buffer *buffers;
unsigned int num_buffers;

static void errno_exit(const char *s)
{
	fprintf (stderr, "%s error %d, %s\n", s, errno, strerror (errno));
	exit (EXIT_FAILURE);
}

static int xioctl(int fd, int request, void *arg)
{
	int r;

	do {
		r = ioctl(fd, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static void write_image(const void *p, size_t length)
{
	int fd;
	int flags = O_CREAT | O_RDWR | O_TRUNC;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;

	if (format == V4L2_PIX_FMT_SGRBG10)
		fd = open("bayer.img", flags, mode);
	else if (format == V4L2_PIX_FMT_YUYV)
		fd = open("yuyv.img", flags, mode);
	else if (format == V4L2_PIX_FMT_UYVY)
		fd = open("uyvy.img", flags, mode);
	else
		fd = open("mono.img", flags, mode);

	if (fd < 0) {
		perror("open(<image>)");
		return;
	}

	write(fd, p, length);

	close(fd);
}

static int read_frame(void)
{
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
                switch (errno) {
                case EAGAIN:
                        return 0;

                case EIO:
                        /* Could ignore EIO, see spec. */
                        /* fall through */

                default:
                        errno_exit("VIDIOC_DQBUF");
                }
        }

        assert(buf.index < num_buffers);

	write_image(buffers[buf.index].start, buffers[buf.index].length);

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                errno_exit("VIDIOC_QBUF");

        return 1;
}

static void mainloop(void)
{
	unsigned int count;
	fd_set fds;
	struct timeval tv;
	int r;

	count = 1;

	while (count-- > 0) {
		for (;;) {
			FD_ZERO (&fds);
			FD_SET (fd, &fds);

			/* Timeout. */
			tv.tv_sec = 30;
			tv.tv_usec = 0;

			r = select(fd + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				if (EINTR == errno)
					continue;

				errno_exit("select");
			}

			if (0 == r) {
				fprintf(stderr, "select timeout\n");
				exit(EXIT_FAILURE);
			}

			printf("snap done, writing image\n");

			if (read_frame())
				break;

			/* EAGAIN - continue select loop. */
		}
	}
}

static void set_control(int id, int value, const char *name)
{
	char buff[256];
	struct v4l2_control control;

	memset(&control, 0, sizeof (control));
	control.id = id;
	control.value = value;

	if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control)) {
		sprintf(buff, "VIDIOC_S_CTRL - %s", name);
        perror(buff);
	}
}

static void set_controls()
{	
	if (brightness >= 0)
		set_control(V4L2_CID_BRIGHTNESS, brightness, "brightness");

	if (contrast >= 0)
		set_control(V4L2_CID_CONTRAST, contrast, "contrast");

	if (exposure >= 0)
		set_control(V4L2_CID_EXPOSURE, exposure, "exposure");

	if (gain >= 0)
		set_control(V4L2_CID_GAIN, gain, "gain");

	if (auto_gain >= 0)
		set_control(V4L2_CID_AUTOGAIN, auto_gain, "auto-gain");

	if (auto_exposure >= 0)
		set_control(V4L2_CID_EXPOSURE_AUTO, auto_exposure, "auto-exposure");

	if (color_effects >= 0)
		set_control(V4L2_CID_COLORFX, color_effects, "color-effects");

	if (hflip >= -1)
		set_control(V4L2_CID_HFLIP, hflip, "hflip");

	if (vflip >= -1)
		set_control(V4L2_CID_VFLIP, vflip, "vflip");
}

static int read_control(int id)
{
	struct v4l2_control control;

	memset(&control, 0, sizeof (control));
	control.id = id;
	control.value = 0;

	if (-1 == ioctl(fd, VIDIOC_G_CTRL, &control))
		return -1;

	return control.value;
}

static void show_setting(int id, const char *name)
{
	char buff[256];
	int val = read_control(id);

	if (val == -1) {
		sprintf(buff, "VIDIOC_G_CTRL - %s", name);
        perror(buff);
	}
	else {
		printf("%s:%d\n", name, val);
	}
}

static void show_settings()
{
	show_setting(V4L2_CID_BRIGHTNESS, "brightness");
	show_setting(V4L2_CID_CONTRAST, "contrast");
	show_setting(V4L2_CID_EXPOSURE, "exposure");
	show_setting(V4L2_CID_GAIN, "gain");
	show_setting(V4L2_CID_EXPOSURE_AUTO, "auto-exposure");
	show_setting(V4L2_CID_AUTOGAIN, "auto-gain");
	show_setting(V4L2_CID_COLORFX, "color-effects");
	show_setting(V4L2_CID_HFLIP, "hflip");
	show_setting(V4L2_CID_VFLIP, "vflip");
}

static void stop_capturing(void)
{
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
		errno_exit("VIDIOC_STREAMOFF");
}

static void start_capturing(void)
{
	unsigned int i;
	enum v4l2_buf_type type;
	struct v4l2_buffer buf;

	for (i = 0; i < num_buffers; ++i) {
		memset(&buf, 0, sizeof(buf));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
			errno_exit ("VIDIOC_QBUF");
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
		errno_exit("VIDIOC_STREAMON");
}

static void cleanup_device(void)
{
	unsigned int i;

	for (i = 0; i < num_buffers; ++i)
		if (-1 == munmap (buffers[i].start, buffers[i].length))
			errno_exit("munmap");

	free (buffers);
}

static void init_mmap(void)
{
	int i;
	struct v4l2_requestbuffers req;

	memset(&req, 0, sizeof(req));

	num_buffers = 0;
	req.count = 2;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support "
			"memory mapping\n", dev_name);
			exit(EXIT_FAILURE);
		} 
		else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n",
		dev_name);
		exit(EXIT_FAILURE);
	}

	buffers = calloc(req.count, sizeof (*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < req.count; i++) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			errno_exit ("VIDIOC_QUERYBUF");

		buffers[i].length = buf.length;
		buffers[i].start = mmap(NULL, buf.length, 
						PROT_READ | PROT_WRITE,
						MAP_SHARED,
						fd, buf.m.offset);

		if (MAP_FAILED == buffers[i].start)
			errno_exit("mmap");
	}

	num_buffers = 2;
}

static void init_device(void)
{
	struct v4l2_format fmt;
	unsigned int min;

	memset(&fmt, 0, sizeof(fmt));

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = image_width;
	fmt.fmt.pix.height = image_height;
	fmt.fmt.pix.pixelformat = format;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (snap) {
		if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
			errno_exit("VIDIOC_S_FMT");

		if (fmt.fmt.pix.width != image_width || fmt.fmt.pix.height != image_height) {
			printf("width and height changed after VIDIOC_S_FMT\n");
			printf("fmt.fmt.pix.width = %d\n", fmt.fmt.pix.width);
			printf("fmt.fmt.pix.height = %d\n", fmt.fmt.pix.height);
		}

		if (fmt.fmt.pix.pixelformat != format)
			printf("pixelformat changed after VIDIOC_S_FMT\n");

		//printf("fmt.fmt.pix.bytesperline = %d\n", fmt.fmt.pix.bytesperline);
		//printf("fmt.fmt.pix.sizeimage = %d\n", fmt.fmt.pix.sizeimage);

		min = fmt.fmt.pix.width * 2;
		if (fmt.fmt.pix.bytesperline < min) {
			printf("fixing bytesperline from %d to %d\n",
				fmt.fmt.pix.bytesperline, min);		
			fmt.fmt.pix.bytesperline = min;
		}

		min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
		if (fmt.fmt.pix.sizeimage < min) {
			printf("fixing sizeimage from %d to %d\n",
				fmt.fmt.pix.sizeimage, min);
			fmt.fmt.pix.sizeimage = min;
		}
	}

	if (snap)
		init_mmap();
}

static void close_device(void)
{
	if (-1 == close(fd))
		errno_exit("close");

	fd = -1;
}

static void open_device(void)
{
	struct stat st; 

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n",
			dev_name, errno, strerror (errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n",
			dev_name, errno, strerror (errno));
		exit(EXIT_FAILURE);
	}
}

static void usage(char *argv_0)
{
	printf("Usage: %s [options]\n\n"
		"Options:\n"
		"-f | --format         Pixel format [uyvy, yuyv, bayer] (default uyvy)\n"
		"-b | --brightness     Brightness, 0-255, default 16\n"
		"-c | --contrast       Contrast, 0-255, default 16\n"
		"-e | --exposure       Exposure 2-566, default 480\n"
		"-g | --gain           Analog gain, 16-64, default 16\n"

		"-E | --auto-exposure  0 or 1\n"
		"-G | --auto-gain      0 or 1\n"
		"-x | --color-effects  0-2\n"
		"-H | --hflip          0 or 1\n"
		"-V | --vflip          0 or 1\n"

		"-n | --nosnap         Do not take picture\n"	
		"-s | --show           Show current settings\n"
		"-h | --help           Print this message\n"
		"\n",
		argv_0);

	exit(1);
}

static const char short_opts [] = "f:b:c:e:g:E:G:x:H:V:nsh";

static const struct option long_opts [] = {
	{ "format",    	    required_argument,  NULL,  'f' },
	{ "brightness",	    required_argument,  NULL,  'b' },
	{ "contrast",       required_argument,  NULL,  'c' },
	{ "exposure",       required_argument,  NULL,  'e' },
	{ "gain",           required_argument,  NULL,  'g' },
    { "auto-exposure",  required_argument,  NULL,  'E' },
    { "auto-gain",      required_argument,  NULL,  'G' },
    { "color-effects",  required_argument,  NULL,  'x' },
    { "hflip",          required_argument,  NULL,  'H' },
	{ "vflip",          required_argument,  NULL,  'V' },
	{ "nosnap",         no_argument,        NULL,  'n' },
	{ "show",           no_argument,        NULL,  's' },
	{ "help",           no_argument,        NULL,  'h' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	int c;
	int show;

	format = V4L2_PIX_FMT_YUYV;
	brightness = -1;
	contrast = -1;
	exposure = -1;
	gain = -1;
	auto_gain = -1;
	auto_exposure = -1;
	color_effects = -1;
	hflip = -1;
	vflip = -1;
	show = 0;
	snap = 1;
 
	for (;;) {
		c = getopt_long(argc, argv, short_opts, long_opts, NULL);

		if (-1 == c)
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			break;

		case 'f':
			if (!strcasecmp(optarg, "bayer")) {
				//format = V4L2_PIX_FMT_SGRBG10;
				printf("Format bayer disabled. Page fault retrieving image.\n");
				exit(1);
			}
			else if (!strcasecmp(optarg, "yuyv")) {
				format = V4L2_PIX_FMT_YUYV;
			}
			else if (!strcasecmp(optarg, "uyvy")) {
				format = V4L2_PIX_FMT_UYVY;				
			}
			else {
				printf("Invalid pixel format: %s\n", optarg);
				usage(argv[0]);
			}
			break;

		case 'b':
			brightness = atol(optarg);
			if (brightness < 0 || brightness > 255) {
				printf("Invalid brightness: %d\n", brightness);
				usage(argv[0]);
			}
			break;

		case 'c':
			contrast = atol(optarg);
			if (contrast < 0 || contrast > 255) {
				printf("Invalid contrast: %d\n", contrast);
				usage(argv[0]);
			}
			break;

		case 'e':
			exposure = atol(optarg);
			if (exposure < 2 || exposure > 566) {
				printf("Invalid exposure: %d\n", exposure);
				usage(argv[0]);
			}
			break;

		case 'g':
			gain = atol(optarg);
			if (gain < 16 || gain > 64) {
				printf("Invalid gain: %d\n", gain);
				usage(argv[0]);
			}
			break;

		case 'E':
			auto_exposure = atol(optarg);
			if (auto_exposure < 0 || auto_exposure > 1) {
				printf("Invalid auto-exposure: %d\n", auto_exposure);
				usage(argv[0]);
			}
			break;

		case 'G':
			auto_gain = atol(optarg);
			if (auto_gain < 0 || auto_gain > 1) {
				printf("Invalid auto-gain: %d\n", auto_gain);
				usage(argv[0]);
			}
			break;

		case 'x':
			color_effects = atol(optarg);
			if (color_effects < 0 || color_effects > 2) {
				printf("Invalid color-effects: %d\n", color_effects);
				usage(argv[0]);
			}
			break;

		case 'H':
			hflip = atol(optarg);
			if (hflip < 0 || hflip > 1) {
				printf("Invalid hflip: %d\n", hflip);
				usage(argv[0]);
			}
			break;

		case 'V':
			vflip = atol(optarg);
			if (vflip < 0 || vflip > 1) {
				printf("Invalid vflip: %d\n", vflip);
				usage(argv[0]);
			}
			break;

		case 'n':
			snap = 0;
			break;

		case 's':
			show = 1;
			break;

		case 'h':
			usage(argv[0]);
			break;

		default:
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	open_device();
	init_device();
	set_controls();

	if (show)
		show_settings();

	if (snap) {
		start_capturing();
		mainloop();
		stop_capturing();
	}

	cleanup_device();
	close_device();

	exit (EXIT_SUCCESS);

	return 0;
}


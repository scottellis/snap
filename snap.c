/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 *
 *  Modified and hard-coded for my own camera testing.
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

#define V4L2_MT9P031_GREEN1_GAIN		(V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_MT9P031_BLUE_GAIN			(V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_MT9P031_RED_GAIN			(V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_MT9P031_GREEN2_GAIN		(V4L2_CID_PRIVATE_BASE + 3)


struct buffer {
        void *start;
        size_t length;
};

#define GREEN1_GAIN 0
#define BLUE_GAIN 1
#define RED_GAIN 2
#define GREEN2_GAIN 3
#define GLOBAL_GAIN 4

int gain[5];

char dev_name[] = "/dev/video0";
int exposure_us;
int pixel_format;
int image_width = 2560;
int image_height = 1920;
int no_snap;
int fd = -1;
struct buffer *buffers;
unsigned int n_buffers;

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

static void set_exposure(int fd, int exposure)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_control control;

	memset(&queryctrl, 0, sizeof (queryctrl));
	queryctrl.id = V4L2_CID_EXPOSURE;

	if (-1 == ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		if (errno != EINVAL) {
		        perror("VIDIOC_QUERYCTRL");
		} 
		else {
		        printf("V4L2_CID_EXPOSURE is not supported\n");
		}
	} 
	else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
		printf("V4L2_CID_EXPOSURE is not supported\n");
	} 
	else {
		memset(&control, 0, sizeof (control));
		control.id = V4L2_CID_EXPOSURE;
		control.value = exposure;

		if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control))
		        perror("VIDIOC_S_CTRL");
	}
}

static void set_gain(int fd, int control_id, int gain)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_control control;

	memset(&queryctrl, 0, sizeof (queryctrl));
	queryctrl.id = control_id;

	if (-1 == ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		if (errno != EINVAL) {
		        perror("VIDIOC_QUERYCTRL");
		} 
		else {
		        printf("Control is not supported\n");
		}
	} 
	else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
		printf("Control is not supported\n");
	} 
	else {
		memset(&control, 0, sizeof (control));
		control.id = control_id;
		control.value = gain;

		if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control))
		        perror("VIDIOC_S_CTRL");
	}
}

static void write_image(const void *p, size_t length)
{
	int fd;
	int flags = O_CREAT | O_RDWR | O_TRUNC;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;

	if (pixel_format == V4L2_PIX_FMT_SGRBG10)
		fd = open("bayer.img", flags, mode);
	else if (pixel_format == V4L2_PIX_FMT_YUYV)
		fd = open("yuyv.img", flags, mode);
	else 
		fd = open("uyvy.img", flags, mode);

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

        assert(buf.index < n_buffers);

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

	for (i = 0; i < n_buffers; ++i) {
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

static void uninit_device(void)
{
	unsigned int i;

	for (i = 0; i < n_buffers; ++i)
		if (-1 == munmap (buffers[i].start, buffers[i].length))
			errno_exit ("munmap");

	free (buffers);
}

static void init_mmap(void)
{
	struct v4l2_requestbuffers req;

	memset(&req, 0, sizeof(req));

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

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			errno_exit ("VIDIOC_QUERYBUF");

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(NULL, buf.length, 
						PROT_READ | PROT_WRITE,
						MAP_SHARED,
						fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit("mmap");
	}
}

static void init_device(void)
{
	struct v4l2_format fmt;
	unsigned int min;

	memset(&fmt, 0, sizeof(fmt));

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = image_width;
	fmt.fmt.pix.height = image_height;
	fmt.fmt.pix.pixelformat = pixel_format;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (!no_snap) {
		if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
			errno_exit("VIDIOC_S_FMT");

		if (fmt.fmt.pix.width != image_width || fmt.fmt.pix.height != image_height) {
			printf("width and height changed after VIDIOC_S_FMT\n");
			printf("fmt.fmt.pix.width = %d\n", fmt.fmt.pix.width);
			printf("fmt.fmt.pix.height = %d\n", fmt.fmt.pix.height);
		}

		if (fmt.fmt.pix.pixelformat != pixel_format)
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

	if (exposure_us > 0)
		set_exposure(fd, exposure_us);

	if (gain[GLOBAL_GAIN] > 0) {
		set_gain(fd, V4L2_CID_GAIN, gain[GLOBAL_GAIN]);
	}
	else {
		if (gain[GREEN1_GAIN] > 0)
			set_gain(fd, V4L2_MT9P031_GREEN1_GAIN, gain[GREEN1_GAIN]);

		if (gain[RED_GAIN] > 0)
			set_gain(fd, V4L2_MT9P031_RED_GAIN, gain[RED_GAIN]);

		if (gain[BLUE_GAIN] > 0)
			set_gain(fd, V4L2_MT9P031_BLUE_GAIN, gain[BLUE_GAIN]);

		if (gain[GREEN2_GAIN] > 0)
			set_gain(fd, V4L2_MT9P031_GREEN2_GAIN, gain[GREEN2_GAIN]);
	}

	if (!no_snap)
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

static void usage(FILE *fp, int argc, char **argv)
{
	fprintf (fp,
		 "Usage: %s [options]\n\n"
		 "Options:\n"
		 "-f | --format        Pixel format [uyvy, yuyv, bayer] (default uyvy)\n"
		 "-s | --size          Image size  0:2560x1920  1:1280x960  2:640x480\n"
		 "-e | --exposure      Exposure time in microseconds\n"
		 "-r | --red           Red gain\n"
		 "-b | --blue          Blue gain\n"
		 "-G | --green1        Green1 gain\n"
		 "-g | --green2        Green2 gain\n"
		 "-n | --gain          Global gain\n"
                 "-o | --nosnap        Only apply gain/exposure settings, no picture\n"
		 "-h | --help          Print this message\n"
		 "",
		 argv[0]);
}

static const char short_options [] = "f:s:e:r:b:G:g:n:oh";

static const struct option long_options [] = {
	{ "format",	required_argument,	NULL,	'f' },
	{ "size",	required_argument,	NULL,	's' },
	{ "exposure",	required_argument,	NULL,	'e' },
	{ "red",	required_argument,	NULL,	'r' },
	{ "blue",	required_argument,	NULL,	'b' },
	{ "green1",	required_argument,	NULL,	'G' },
	{ "green2",	required_argument,	NULL,	'g' },
	{ "gain",	required_argument,	NULL,	'n' },
	{ "nosnap",	no_argument,		NULL,	'o' },
	{ "help",	no_argument,		NULL,	'h' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	int index;
	int c, size;

	size = 0;
	pixel_format = V4L2_PIX_FMT_UYVY;
	no_snap = 0;

	for (;;) {
		c = getopt_long(argc, argv, short_options, long_options, &index);

		if (-1 == c)
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			break;

		case 's':
			size = atoi(optarg);

			if (size < 0 || size > 2) {
				printf("Invalid size parameter %d\n", size);
				exit(EXIT_FAILURE);
			}
				
			break;

		case 'e':
			exposure_us = atol(optarg);

			if (exposure_us < 63)
				exposure_us = 63;
			else if (exposure_us > 142644)
				exposure_us = 142644;

			break;

		case 'r':
			gain[RED_GAIN] = atol(optarg);

			if (gain[RED_GAIN] < 1 || gain[RED_GAIN] > 161)
				gain[RED_GAIN] = 0;

			break;

		case 'b':
			gain[BLUE_GAIN] = atol(optarg);

			if (gain[BLUE_GAIN] < 1 || gain[BLUE_GAIN] > 161)
				gain[BLUE_GAIN] = 0;

			break;

		case 'G':
			gain[GREEN1_GAIN] = atol(optarg);

			if (gain[GREEN1_GAIN] < 1 || gain[GREEN1_GAIN] > 161)
				gain[GREEN1_GAIN] = 0;

			break;

		case 'g':
			gain[GREEN2_GAIN] = atol(optarg);

			if (gain[GREEN2_GAIN] < 1 || gain[GREEN2_GAIN] > 161)
				gain[GREEN2_GAIN] = 0;

			break;

		case 'n':
			gain[GLOBAL_GAIN] = atol(optarg);

			if (gain[GLOBAL_GAIN] < 1 || gain[GLOBAL_GAIN] > 161)
				gain[GLOBAL_GAIN] = 0;

			break;

		case 'f':
			if (!strcasecmp(optarg, "bayer")) {
				pixel_format = V4L2_PIX_FMT_SGRBG10;
			}
			else if (!strcasecmp(optarg, "yuyv")) {
				pixel_format = V4L2_PIX_FMT_YUYV;
			}
			else if (!strcasecmp(optarg, "uyvy")) {
				pixel_format = V4L2_PIX_FMT_UYVY;
			}
			else {
				printf("Invalid pixel format: %s\n", optarg);
				exit(1);
			}
			
			break;
	
		case 'o':
			no_snap = 1;
			break;

		case 'h':
			usage(stdout, argc, argv);
			exit (EXIT_SUCCESS);

		default:
			usage(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	if (size && pixel_format == V4L2_PIX_FMT_SGRBG10) {
		printf("Bayer format restricted to size 2560x1920\n");
		exit(EXIT_FAILURE);
	}

	if (size == 2) {
		image_width = 640;
		image_height = 480;
	}
	else if (size == 1) {
		image_width = 1280;
		image_height = 960;
	}
	else {
		image_width = 2560;
		image_height = 1920;
	}

	open_device();
	init_device();

	if (!no_snap) {
		start_capturing();
		mainloop();
		stop_capturing();
	}

	uninit_device();
	close_device();

	exit (EXIT_SUCCESS);

	return 0;
}


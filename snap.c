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


struct buffer {
        void *start;
        size_t length;
};

static char *dev_name = NULL;
static int exposure_us;
static int pixel_format;
static int image_width = 2560;
static int image_height = 1920;
static int fd = -1;
struct buffer *buffers = NULL;
static unsigned int n_buffers = 0;

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

static void write_image(const void *p, size_t length)
{
	int flags = O_CREAT | O_RDWR | O_TRUNC;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;

	int fd = open("rawimage.dat", flags, mode);

	if (fd < 0) {
		perror("open(rawimage.dat)");
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

	if (exposure_us > 0)
		set_exposure(fd, exposure_us);

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
		 "-d | --device name   Video device name [/dev/video0]\n"
		 "-e | --exposure      Exposure time in microseconds\n"
		 "-b | --bayer         Request image in bayer GRBG format\n"
		 "-y | --yuv           Request image in YUYV format\n"
		 "-h | --help          Print this message\n"
		 "",
		 argv[0]);
}

static const char short_options [] = "d:e:byh";

static const struct option long_options [] = {
	{ "device",	required_argument,	NULL,	'd' },
	{ "exposure",	required_argument,	NULL,	'e' },
	{ "bayer",	no_argument,		NULL,	'b' },
	{ "yuv",	no_argument,		NULL,	'y' },	
	{ "help",	no_argument,		NULL,	'h' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	int index;
	int c;

	dev_name = "/dev/video0";
	pixel_format = V4L2_PIX_FMT_SGRBG10;


	for (;;) {
		c = getopt_long(argc, argv, short_options, long_options, &index);

		if (-1 == c)
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			break;

		case 'd':
			dev_name = optarg;
			break;

		case 'e':
			exposure_us = atol(optarg);

			if (exposure_us < 1)
				exposure_us = 1;
			else if (exposure_us > 1000000)
				exposure_us = 1000000;

			break;

		case 'b':
			pixel_format = V4L2_PIX_FMT_SGRBG10;
			break;

		case 'y':
			pixel_format = V4L2_PIX_FMT_YUYV;
			break;			

		case 'h':
			usage(stdout, argc, argv);
			exit (EXIT_SUCCESS);

		default:
			usage(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	open_device();
	init_device();
	start_capturing();
	mainloop();
	stop_capturing();
	uninit_device();
	close_device();

	exit (EXIT_SUCCESS);

	return 0;
}


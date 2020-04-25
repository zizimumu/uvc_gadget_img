/*
 * UVC gadget test application
 *
 * Copyright (C) 2010 Ideas on board SPRL <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 */

#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <linux/usb/ch9.h>
/* #include <jpeglib.h>  */
#include <linux/fb.h>  
#include <sys/time.h>

#if 1
#include <linux/usb/ch9.h>
/* #include "/home/emy/friendlyarm/linux/drivers/usb/gadget/function/uvc.h" */
/* #include "/home/emy/friendlyarm/linux/include/uapi/linux/usb/video.h" */

#include "uvc.h" 
#include "video.h"
#include <linux/videodev2.h>
/* #include "./drivers/usb/gadget/uvc.h" */
#endif

#define clamp(val, min, max) ({                 \
        typeof(val) __val = (val);              \
        typeof(min) __min = (min);              \
        typeof(max) __max = (max);              \
        (void) (&__val == &__min);              \
        (void) (&__val == &__max);              \
        __val = __val < __min ? __min: __val;   \
        __val > __max ? __max: __val; })

#define ARRAY_SIZE(a)	((sizeof(a) / sizeof(a[0])))

struct uvc_device
{
	int fd;

	struct uvc_streaming_control probe;
	struct uvc_streaming_control commit;

	int control;

	unsigned int fcc;
	unsigned int width;
	unsigned int height;

	void **mem;
	unsigned int nbufs;
	unsigned int bufsize;

	unsigned int bulk;
	uint8_t color;
	unsigned int imgsize;
	unsigned int mjpg_sz[4];
	void *mjpg_buff[4];
	void *imgdata;

	void *yuv_buff;
	void *fb_buff_tmp;
	void *fb_buff;
	void *jpg_buff;
	int fb_bpp;
};


static int mjpeg_mode = 0;
static int fb_mode = 0;
static int uvc_on = 0;

#define BUF_CNT 4
#define MAX_PACKET 1024


#define UVC_WIDTH 1280
#define UVC_HEIGHT 720



struct uvc_frame_info
{
	unsigned int width;
	unsigned int height;
	unsigned int intervals[8];
};

struct uvc_format_info
{
	unsigned int fcc;
	const struct uvc_frame_info *frames;
};

static const struct uvc_frame_info uvc_frames_yuyv[] = {
	{  640, 360, { 666666, 10000000, 50000000, 0 }, }, 
	 /*{  640, 480, { 333333, 0, 0, 0 }, }, */
	{ 1280, 720, { 5000000, 0 }, }, 
	{ 0, 0, { 0, }, },
};

static const struct uvc_frame_info uvc_frames_mjpeg[] = {
	{  640, 360, { 666666, 10000000, 50000000, 0 }, },
	/* { 1280, 720, { 5000000, 0 }, }, */
	{ 0, 0, { 0, }, },
};

static const struct uvc_format_info uvc_formats[] = {
	 { V4L2_PIX_FMT_YUYV, uvc_frames_yuyv }, 
	{ V4L2_PIX_FMT_MJPEG, uvc_frames_mjpeg },
};



static struct uvc_device *
uvc_open(const char *devname)
{
	struct uvc_device *dev;
	struct v4l2_capability cap;
	int ret;
	int fd;

	fd = open(devname, O_RDWR | O_NONBLOCK);
	if (fd == -1) {
		printf("v4l2 open failed: %s (%d)\n", strerror(errno), errno);
		return NULL;
	}

	printf("open succeeded, file descriptor = %d\n", fd);

	ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("unable to query device: %s (%d)\n", strerror(errno),
			errno);
		close(fd);
		return NULL;
        }

	printf("device is %s on bus %s\n", cap.card, cap.bus_info);

	dev = malloc(sizeof *dev);
	if (dev == NULL) {
		close(fd);
		return NULL;
	}

	memset(dev, 0, sizeof *dev);
	dev->fd = fd;

	return dev;
}

static void
uvc_close(struct uvc_device *dev)
{
	close(dev->fd);
	free(dev->imgdata);
	free(dev->mem);
	free(dev);
}

/* ---------------------------------------------------------------------------
 * Video streaming
 */
void dump_mem_s(void *buf,int len)
{
	int i;
	char *val = buf;

	for(i=0;i<len;i++){
		
		if(i % 16 == 0)
			printf("\n");

		printf("  %02x",*val++);
	}
	printf("\n");
}

static char * change2rgb888(int fb_bpp,char *src,char *dest,unsigned int pix)
{
	unsigned int i;
	unsigned short *pt;
	
	if(fb_bpp != 24){
		if(fb_bpp == 16){
			for(i=0;i<pix;i++){
				pt = (unsigned short *)&src[2*i];
				dest[3*i] = (char)(*pt & 0x1f);
				dest[3*i+1] = (char)( ((*pt)>>5) & 0x3f);
				dest[3*i+2] = (char)((*pt) >> 11);
			}
		}
		else{
			printf("dont support bpp %d \n",fb_bpp);
		}

		return dest;
	}else
		return src;

}


int tim_subtract(struct timeval *result, struct timeval *x, struct timeval *y)

{

	int nsec;

	if ( x->tv_sec > y->tv_sec )
		return   -1;

	if ((x->tv_sec==y->tv_sec) && (x->tv_usec>y->tv_usec))
		return   -1;

	result->tv_sec = ( y->tv_sec-x->tv_sec );
	result->tv_usec = ( y->tv_usec-x->tv_usec );
	if (result->tv_usec<0)
	{
		result->tv_sec--;
		result->tv_usec+=1000000;
	}

	return   0;

}

unsigned int jpg_compress(unsigned char  *src, unsigned char *dest, int width, int height)
{
	#define JPEG_QUALITY 80

#if 0
	int depth = 3;	
	JSAMPROW row_pointer[1];
	struct jpeg_compress_struct cinfo;	
	struct jpeg_error_mgr jerr;  
	FILE *outfile;	
	unsigned int size = 0;
	char *outbuf = NULL;
	int fd;

	struct   timeval   start,stop,diff;
  	gettimeofday(&start,0);
  
	cinfo.err = jpeg_std_error(&jerr);
	//* Now we can initialize the JPEG compression object.	
	jpeg_create_compress(&cinfo);  
  
  

	//outbuf = malloc(width*height*3);
	size = height*height;
	
	
	jpeg_mem_dest(&cinfo, &dest,&size);
	

	

	cinfo.image_width = width;			   //* image width and height, in pixels  
	cinfo.image_height = height;  
	cinfo.input_components = depth;    //* # of color components per pixel	
	cinfo.in_color_space = JCS_RGB; 	//* colorspace of input image  
	jpeg_set_defaults(&cinfo);	
  
	jpeg_set_quality(&cinfo, JPEG_QUALITY, TRUE ); //* limit to baseline-JPEG values  
	jpeg_start_compress(&cinfo, TRUE);	
  
	int row_stride = width * 3;  
	while (cinfo.next_scanline < cinfo.image_height)  
	{  
		row_pointer[0] = (JSAMPROW)(src + cinfo.next_scanline * row_stride);
		jpeg_write_scanlines(&cinfo, row_pointer, 1);  
	}  
  
	jpeg_finish_compress(&cinfo);  
	jpeg_destroy_compress(&cinfo);
	


	gettimeofday(&stop,0);
	tim_subtract(&diff,&start,&stop);
	printf("time %d\n",diff.tv_usec);

	return size;  
#endif 

    return 1;
}

static void
uvc_video_fill_buffer(struct uvc_device *dev, struct v4l2_buffer *buf)
{
	unsigned int bpl;
	unsigned int i;
	int fd[3];
	char *src;
	char *dest;
	
	switch (dev->fcc) {
	case V4L2_PIX_FMT_YUYV:
		/* Fill the buffer with video data. */
		bpl = dev->width * 2;
		for (i = 0; i < dev->height; ++i)
			memset(dev->mem[buf->index] + i*bpl, dev->color++, bpl);

        buf->bytesused = bpl * dev->height;

		break;

	case V4L2_PIX_FMT_MJPEG:


        memcpy(dev->mem[buf->index], (char *)dev->mjpg_buff[buf->index], dev->mjpg_sz[buf->index]);
        buf->bytesused = dev->mjpg_sz[buf->index];		


		break;
	}
	//dump_mem_s(dev->mem[buf->index]);

}

static int
uvc_video_process(struct uvc_device *dev)
{
	struct v4l2_buffer buf;
	int ret;

	if(uvc_on == 0)
		return 0;

	memset(&buf, 0, sizeof buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory = V4L2_MEMORY_MMAP;

	if ((ret = ioctl(dev->fd, VIDIOC_DQBUF, &buf)) < 0) {
		printf("Unable to dequeue buffer: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	uvc_video_fill_buffer(dev, &buf);

	if ((ret = ioctl(dev->fd, VIDIOC_QBUF, &buf)) < 0) {
		printf("Unable to requeue buffer: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	return 0;
}

static int
uvc_video_reqbufs(struct uvc_device *dev, int nbufs)
{
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer buf;
	unsigned int i;
	int ret;

	for (i = 0; i < dev->nbufs; ++i)
		munmap(dev->mem[i], dev->bufsize);

	free(dev->mem);
	dev->mem = 0;
	dev->nbufs = 0;

	memset(&rb, 0, sizeof rb);
	rb.count = nbufs;
	rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	rb.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		printf("Unable to allocate buffers: %s (%d).\n",
			strerror(errno), errno);
		return ret;
	}

	printf("%u buffers allocated.\n", rb.count);

	/* Map the buffers. */
	dev->mem = malloc(rb.count * sizeof dev->mem[0]);

	for (i = 0; i < rb.count; ++i) {
		memset(&buf, 0, sizeof buf);
		buf.index = i;
		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(dev->fd, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			printf("Unable to query buffer %u: %s (%d).\n", i,
				strerror(errno), errno);
			return -1;
		}
		printf("length: %u offset: %u\n", buf.length, buf.m.offset);

		dev->mem[i] = mmap(0, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, buf.m.offset);
		if (dev->mem[i] == MAP_FAILED) {
			printf("Unable to map buffer %u: %s (%d)\n", i,
				strerror(errno), errno);
			return -1;
		}
		printf("Buffer %u mapped at address %p.\n", i, dev->mem[i]);
	}

	dev->bufsize = buf.length;
	dev->nbufs = rb.count;

	return 0;
}

static int
uvc_video_stream(struct uvc_device *dev, int enable)
{
	struct v4l2_buffer buf;
	unsigned int i;
	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	int ret;

	if (!enable) {
		printf("Stopping video stream.\n");
		ioctl(dev->fd, VIDIOC_STREAMOFF, &type);
		return 0;
	}

	printf("Starting video stream.\n");

	for (i = 0; i < dev->nbufs; ++i) {
		memset(&buf, 0, sizeof buf);

		buf.index = i;
		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf.memory = V4L2_MEMORY_MMAP;

		uvc_video_fill_buffer(dev, &buf);

		printf("Queueing buffer %u.\n", i);
		if ((ret = ioctl(dev->fd, VIDIOC_QBUF, &buf)) < 0) {
			printf("Unable to queue buffer: %s (%d).\n",
				strerror(errno), errno);
			break;
		}
	}

	ret = ioctl(dev->fd, VIDIOC_STREAMON, &type);

	if(ret < 0)
		printf("VIDIOC_STREAMON ioctrl err %d\n",ret);
	return ret;
}

static int
uvc_video_set_format(struct uvc_device *dev)
{
	struct v4l2_format fmt;
	int ret;

	printf("Setting format to 0x%08x %ux%u\n",
		dev->fcc, dev->width, dev->height);

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.width = dev->width;
	fmt.fmt.pix.height = dev->height;
	fmt.fmt.pix.pixelformat = dev->fcc;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if (dev->fcc == V4L2_PIX_FMT_MJPEG){
		fmt.fmt.pix.sizeimage = dev->imgsize * 1.5;
		printf(" === MJPEG mode\n");

	}
	else
		printf("=== YUYV mode\n");

	if ((ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt)) < 0)
		printf("Unable to set format: %s (%d).\n",
			strerror(errno), errno);

	return ret;
}

static int
uvc_video_init(struct uvc_device *dev __attribute__((__unused__)))
{
	return 0;
}

/* ---------------------------------------------------------------------------
 * Request processing
 */


static void
uvc_fill_streaming_control(struct uvc_device *dev,
			   struct uvc_streaming_control *ctrl,
			   int iframe, int iformat)
{
	const struct uvc_format_info *format;
	const struct uvc_frame_info *frame;
	unsigned int nframes;

	if (iformat < 0)
		iformat = ARRAY_SIZE(uvc_formats) + iformat;
	if (iformat < 0 || iformat >= (int)ARRAY_SIZE(uvc_formats))
		return;
	format = &uvc_formats[iformat];

	nframes = 0;
	while (format->frames[nframes].width != 0)
		++nframes;

	if (iframe < 0)
		iframe = nframes + iframe;
	if (iframe < 0 || iframe >= (int)nframes)
		return;
	frame = &format->frames[iframe];

	memset(ctrl, 0, sizeof *ctrl);

	ctrl->bmHint = 1;


    ctrl->bFormatIndex = iformat + 1;
    ctrl->bFrameIndex = iframe + 1;

	printf("uvc_fill_streaming_control: bFormatIndex %d,bFrameIndex %d \n",ctrl->bFormatIndex,ctrl->bFrameIndex );

	
	ctrl->dwFrameInterval = frame->intervals[0];
	switch (format->fcc) {
	case V4L2_PIX_FMT_YUYV:
		ctrl->dwMaxVideoFrameSize = frame->width * frame->height * 2;
		break;
	case V4L2_PIX_FMT_MJPEG:
		ctrl->dwMaxVideoFrameSize = dev->imgsize;
		break;
	}
	ctrl->dwMaxPayloadTransferSize = MAX_PACKET;	/* TODO this should be filled by the driver. */
	ctrl->bmFramingInfo = 3;
	ctrl->bPreferedVersion = 1;
	ctrl->bMaxVersion = 1;
}

static void
uvc_events_process_standard(struct uvc_device *dev, struct usb_ctrlrequest *ctrl,
			    struct uvc_request_data *resp)
{
	printf("standard request\n");
	(void)dev;
	(void)ctrl;
	(void)resp;
}

static void
uvc_events_process_control(struct uvc_device *dev, uint8_t req, uint8_t cs,
			   struct uvc_request_data *resp)
{
	printf("control request (req %02x cs %02x)\n", req, cs);
	(void)dev;
	(void)resp;
}

static void
uvc_events_process_streaming(struct uvc_device *dev, uint8_t req, uint8_t cs,
			     struct uvc_request_data *resp)
{
	struct uvc_streaming_control *ctrl;

	printf("streaming request (req %02x cs %02x)\n", req, cs);

	if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL)
		return;

	ctrl = (struct uvc_streaming_control *)&resp->data;
	resp->length = sizeof *ctrl;

	switch (req) {
	case UVC_SET_CUR:
		dev->control = cs;
		resp->length = 34;
		break;

	case UVC_GET_CUR:
		if (cs == UVC_VS_PROBE_CONTROL)
			memcpy(ctrl, &dev->probe, sizeof *ctrl);
		else
			memcpy(ctrl, &dev->commit, sizeof *ctrl);
		break;

	case UVC_GET_MIN:
	case UVC_GET_MAX:
	case UVC_GET_DEF:
		uvc_fill_streaming_control(dev, ctrl, req == UVC_GET_MAX ? -1 : 0,
					   req == UVC_GET_MAX ? -1 : 0);
		break;

	case UVC_GET_RES:
		memset(ctrl, 0, sizeof *ctrl);
		break;

	case UVC_GET_LEN:
		resp->data[0] = 0x00;
		resp->data[1] = 0x22;
		resp->length = 2; //2;
		break;

	case UVC_GET_INFO:
		resp->data[0] = 0x03;
		resp->length = 1;
		break;
	}
}

static void
uvc_events_process_class(struct uvc_device *dev, struct usb_ctrlrequest *ctrl,
			 struct uvc_request_data *resp)
{
	if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE)
		return;

	switch (ctrl->wIndex & 0xff) {
	case UVC_INTF_CONTROL:
		uvc_events_process_control(dev, ctrl->bRequest, ctrl->wValue >> 8, resp);
		break;

	case UVC_INTF_STREAMING:
		uvc_events_process_streaming(dev, ctrl->bRequest, ctrl->wValue >> 8, resp);
		break;

	default:
		break;
	}
}

static void
uvc_events_process_setup(struct uvc_device *dev, struct usb_ctrlrequest *ctrl,
			 struct uvc_request_data *resp)
{
	dev->control = 0;

	printf("bRequestType %02x bRequest %02x wValue %04x wIndex %04x "
		"wLength %04x\n", ctrl->bRequestType, ctrl->bRequest,
		ctrl->wValue, ctrl->wIndex, ctrl->wLength);

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		uvc_events_process_standard(dev, ctrl, resp);
		break;

	case USB_TYPE_CLASS:
		uvc_events_process_class(dev, ctrl, resp);
		break;

	default:
		break;
	}
}


static void
uvc_events_process_data(struct uvc_device *dev, struct uvc_request_data *data)
{
	struct uvc_streaming_control *target;
	struct uvc_streaming_control *ctrl;
	const struct uvc_format_info *format;
	const struct uvc_frame_info *frame;
	const unsigned int *interval;
	unsigned int iformat, iframe;
	unsigned int nframes;

	switch (dev->control) {
	case UVC_VS_PROBE_CONTROL:
		printf("setting probe control, length = %d\n", data->length);
		target = &dev->probe;
		break;

	case UVC_VS_COMMIT_CONTROL:
		printf("setting commit control, length = %d\n", data->length);
		target = &dev->commit;
		break;

	default:
		printf("setting unknown control, length = %d\n", data->length);
		return;
	}



    ctrl = (struct uvc_streaming_control *)&data->data;
    iformat = clamp((unsigned int)ctrl->bFormatIndex, 1U, (unsigned int)ARRAY_SIZE(uvc_formats));




	if(mjpeg_mode == 1)
		iformat = 2; //mjpeg
	else
		iformat = 1; //yuv

	
    format = &uvc_formats[iformat - 1];

    nframes = 0;
    while (format->frames[nframes].width != 0)
        ++nframes;

    iframe = clamp((unsigned int)ctrl->bFrameIndex, 1U, nframes);
    frame = &format->frames[iframe - 1];
    interval = frame->intervals;

    while (interval[0] < ctrl->dwFrameInterval && interval[1])
        ++interval;

    target->bFormatIndex = iformat;
    target->bFrameIndex = iframe;




	printf("\nctrl->bFormatIndex = %d ,ctrl->bFrameIndex =%d,iformat %d,iframe %d,*interval %d\n",ctrl->bFormatIndex,ctrl->bFrameIndex,
		iformat,iframe,*interval);



	
	switch (format->fcc) {
	case V4L2_PIX_FMT_YUYV:
		target->dwMaxVideoFrameSize = frame->width * frame->height * 2;
		break;
	case V4L2_PIX_FMT_MJPEG:
		if (dev->imgsize == 0)
			printf("WARNING: MJPEG requested and no image loaded.\n");
		target->dwMaxVideoFrameSize = dev->imgsize;
		break;
	}
	target->dwFrameInterval = *interval;

	if (dev->control == UVC_VS_COMMIT_CONTROL) {
		dev->fcc = format->fcc;
		dev->width = frame->width;
		dev->height = frame->height;

		uvc_video_set_format(dev);
		if (dev->bulk)
			uvc_video_stream(dev, 1);
	}
}

static void
uvc_events_process(struct uvc_device *dev)
{
	struct v4l2_event v4l2_event;
	struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;
	struct uvc_request_data resp;
	int ret;

	ret = ioctl(dev->fd, VIDIOC_DQEVENT, &v4l2_event);
	if (ret < 0) {
		printf("VIDIOC_DQEVENT failed: %s (%d)\n", strerror(errno),
			errno);
		return;
	}

	memset(&resp, 0, sizeof resp);
	resp.length = -EL2HLT;

	switch (v4l2_event.type) {
	case UVC_EVENT_CONNECT:
	case UVC_EVENT_DISCONNECT:
		return;

	case UVC_EVENT_SETUP:
		uvc_events_process_setup(dev, &uvc_event->req, &resp);
		break;

	case UVC_EVENT_DATA:
		uvc_events_process_data(dev, &uvc_event->data);
		return;

	case UVC_EVENT_STREAMON:
		uvc_video_reqbufs(dev, BUF_CNT);
		uvc_video_stream(dev, 1);

		uvc_on = 1;
		return;

	case UVC_EVENT_STREAMOFF:
		uvc_video_stream(dev, 0);
		uvc_video_reqbufs(dev, 0);

		uvc_on = 0;
		return;
	}

	ioctl(dev->fd, UVCIOC_SEND_RESPONSE, &resp);
	if (ret < 0) {
		printf("UVCIOC_S_EVENT failed: %s (%d)\n", strerror(errno),
			errno);
		return;
	}
}

static void
uvc_events_init(struct uvc_device *dev)
{
	struct v4l2_event_subscription sub;

	uvc_fill_streaming_control(dev, &dev->probe, 0, 0);
	uvc_fill_streaming_control(dev, &dev->commit, 0, 0);

	if (dev->bulk) {
		/* FIXME Crude hack, must be negotiated with the driver. */
		dev->probe.dwMaxPayloadTransferSize = 16 * 1024;
		dev->commit.dwMaxPayloadTransferSize = 16 * 1024;
	}


	memset(&sub, 0, sizeof sub);
	sub.type = UVC_EVENT_SETUP;
	ioctl(dev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
	sub.type = UVC_EVENT_DATA;
	ioctl(dev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
	sub.type = UVC_EVENT_STREAMON;
	ioctl(dev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
	sub.type = UVC_EVENT_STREAMOFF;
	ioctl(dev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
}

/* ---------------------------------------------------------------------------
 * main
 */

static void image_load(struct uvc_device *dev, const char *img)
{

	int fd[4];
	int size;
	int i,max = 0;
	char *pt;


	if(mjpeg_mode == 1){
		fd[0] = open("red.jpg", O_RDONLY);
		fd[1] = open("green.jpg", O_RDONLY);
		fd[2] = open("blue.jpg", O_RDONLY);
		fd[3] = open("green.jpg", O_RDONLY);


		if (fd[0] <= 0) {
			printf("Unable to open MJPEG image '%s'\n", img);
			return;
		}

		for(i=0;i<4;i++){
			dev->mjpg_sz[i] = lseek(fd[i], 0, SEEK_END);
			lseek(fd[i], 0, SEEK_SET);

			dev->mjpg_buff[i] = malloc(dev->mjpg_sz[i] );
			if (dev->mjpg_buff[i]  == NULL) {
				printf("Unable to allocate memory for MJPEG image\n");
				dev->mjpg_sz[i] = 0;
				return;
			}

			if(dev->mjpg_sz[i] > max)
				max = dev->mjpg_sz[i];

			read(fd[i], dev->mjpg_buff[i], dev->mjpg_sz[i] );
			close(fd[i]);
		}
		dev->imgsize = max;

	}

	
}

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [options]\n", argv0);
	fprintf(stderr, "Available options are\n");
	fprintf(stderr, " -b		Use bulk mode\n");
	fprintf(stderr, " -d device	Video device\n");
	fprintf(stderr, " -h		Print this help screen and exit\n");
	fprintf(stderr, " -i image	MJPEG image\n");
}

int main(int argc, char *argv[])
{
	char *device = "/dev/video0";
	struct uvc_device *dev;
	int bulk_mode = 0;
	char *mjpeg_image = NULL;
	fd_set fds;
	int ret, opt;
	int fbfd = 0;	
	struct fb_var_screeninfo vinfo;   
	struct fb_fix_screeninfo finfo;   
	long int screensize = 0;  
	char *fbp;


	int fd;
	int size;
	char *g_buff;
	char*dest;
		

	mjpeg_mode = 0;

	while ((opt = getopt(argc, argv, "bd:hift")) != -1) {
		switch (opt) {
		case 'b':
			bulk_mode = 1;
			break;

		case 'd':
			device = optarg;
			break;

		case 'h':
			usage(argv[0]);
			return 0;

		case 'i':
			mjpeg_mode = 1;
			mjpeg_image = optarg;
			break;

		case 't':{
			printf("test mode\n");
			
		}
		return 0;

		default:
			fprintf(stderr, "Invalid option '-%c'\n", opt);
			usage(argv[0]);
			return 1;
		}
	}


	dev = uvc_open(device);
	if (dev == NULL)
		return 1;


	// load R,G,B jpeg file
	image_load(dev, mjpeg_image);

	dev->bulk = bulk_mode;

	uvc_events_init(dev);
	uvc_video_init(dev);

	FD_ZERO(&fds);
	FD_SET(dev->fd, &fds);

	while (1) {
		fd_set efds = fds;
		fd_set wfds = fds;

		ret = select(dev->fd + 1, NULL, &wfds, &efds, NULL);
		if (FD_ISSET(dev->fd, &efds))
			uvc_events_process(dev);
		if (FD_ISSET(dev->fd, &wfds))
			uvc_video_process(dev);
	}

	uvc_close(dev);

	if(fb_mode)
		close(fbfd);
	return 0;
}


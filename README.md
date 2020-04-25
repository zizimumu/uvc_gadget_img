# uvc_gadget_img
the latest linux UVC gadget application code from http://git.ideasonboard.org/uvc-gadget.git is more complicated, 
I just want to test UVC gadget with dummy data or a mjpeg picture, so my work is based on a old verson.

how to use it:

1. compile.  be sure to change the kernel directory in source file as bellow:

#include "your kernel source dir/drivers/usb/gadget/function/uvc.h"
#include "your kernel source dir/linux/include/uapi/linux/usb/video.h"

2. ./uvc-gadget   //test UVC mode
   ./uvc-gadget -i  // test mjpeg mode

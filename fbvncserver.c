/*
 * fbvncserver.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Started with original fbvncserver for the iPAQ and Zaurus.
 * 	http://fbvncserver.sourceforge.net/
 *
 * Modified by Jim Huang <jserv.tw@gmail.com>
 * 	- Simplified and sizing down
 * 	- Performance tweaks
 *
 * Modified by Steve Guo (letsgoustc)
 *  - Added keyboard/pointer input
 * 
 * Modified by Danke Xie (danke.xie@gmail.com)
 *  - Added input device search to support different devices
 *  - Added kernel vnckbd driver to allow full-keyboard input on 12-key hw
 *  - Supports Android framebuffer double buffering
 *  - Performance boost and fix GCC warnings in libvncserver-0.9.7
 *
 * NOTE: depends libvncserver.
 */

/* define the following to enable debug messages */
/* #define DEBUG */
/* #define DEBUG_VERBOSE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h>             /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"
#include "rfb/rfbregion.h"

#define APPNAME "fbvncserver"
#define VNC_DESKTOP_NAME "Android"

/* Android does not use /dev/fb0. */
#define FB_DEVICE "/dev/graphics/fb0"
static char KBD_DEVICE[256] = "/dev/input/event0";
static char TOUCH_DEVICE[256] = "/dev/input/event1";
static struct fb_var_screeninfo scrinfo;
static int fb_buf_num = 2; /* mmap 2 buffers for android */
static int fbfd = -1;
static int kbdfd = -1;
static int touchfd = -1;
static unsigned short int *fbmmap = MAP_FAILED;
static unsigned short int *vncbuf;
static unsigned short int *fbbuf;

struct timeval last_scr_updated;
#define SCR_MAX_FPS 5
static int scr_update_interval = 1000*1000/SCR_MAX_FPS;

/* Android already has 5900 bound natively. */
#define VNC_PORT 5901
static rfbScreenInfoPtr vncscr;

static int xmin, xmax;
static int ymin, ymax;

/* No idea, just copied from fbvncserver as part of the frame differerencing
 * algorithm.  I will probably be later rewriting all of this. */
static struct varblock_t
{
	int min_x;
	int min_y;
	int max_x;
	int max_y;
	int r_offset;
	int g_offset;
	int b_offset;
	int rfb_xres;
	int rfb_maxy;
	int pixels_per_int;
} varblock;

/*****************************************************************************/

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl);

/*****************************************************************************/

static void init_fb(void)
{
	size_t pixels;
	size_t bytespp;

	if ((fbfd = open(FB_DEVICE, O_RDONLY)) == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0) {
		perror("ioctl");
		exit(EXIT_FAILURE);
	}

	pixels = scrinfo.xres * scrinfo.yres;
	bytespp = scrinfo.bits_per_pixel / 8;

	printf("xres=%d, yres=%d, "
			"xresv=%d, yresv=%d, "
			"xoffs=%d, yoffs=%d, "
			"bpp=%d\n", 
	  (int)scrinfo.xres, (int)scrinfo.yres,
	  (int)scrinfo.xres_virtual, (int)scrinfo.yres_virtual,
	  (int)scrinfo.xoffset, (int)scrinfo.yoffset,
	  (int)scrinfo.bits_per_pixel);

	fbmmap = mmap(NULL, fb_buf_num * pixels * bytespp,
			PROT_READ, MAP_SHARED, fbfd, 0);

	if (fbmmap == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}
}

 
static void cleanup_fb(void)
{
	if(fbfd != -1)
	{
		close(fbfd);
	}
}

static void init_kbd()
{
	if((kbdfd = open(KBD_DEVICE, O_RDWR)) == -1)
	{
		printf("cannot open kbd device %s\n", KBD_DEVICE);
		exit(EXIT_FAILURE);
	}
}

static void cleanup_kbd()
{
	if(kbdfd != -1)
	{
		close(kbdfd);
	}
}

static void init_touch()
{
    struct input_absinfo info;
    if((touchfd = open(TOUCH_DEVICE, O_RDWR)) == -1)
    {
            printf("cannot open touch device %s\n", TOUCH_DEVICE);
            exit(EXIT_FAILURE);
    }
    // Get the Range of X and Y
    if(ioctl(touchfd, EVIOCGABS(ABS_MT_POSITION_X), &info)) {
        printf("cannot get ABS_X info, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    xmin = info.minimum;
    xmax = info.maximum;
    if(ioctl(touchfd, EVIOCGABS(ABS_MT_POSITION_Y), &info)) {
        printf("cannot get ABS_Y, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ymin = info.minimum;
    ymax = info.maximum;
    printf("touch range: x(%d~%d) y(%d~%d)\n", xmin, xmax, ymin, ymax);

}

static void cleanup_touch()
{
	if(touchfd != -1)
	{
		close(touchfd);
	}
}

/*****************************************************************************/

static void init_fb_server(int argc, char **argv)
{
	printf("Initializing server...\n");

	/* Allocate the VNC server buffer to be managed (not manipulated) by 
	 * libvncserver. */
	vncbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(vncbuf != NULL);

	/* Allocate the comparison buffer for detecting drawing updates from frame
	 * to frame. */
	fbbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(fbbuf != NULL);

	/* FIXME: This assumes scrinfo.bits_per_pixel is 16. */
	vncscr = rfbGetScreen(&argc, argv, scrinfo.xres, scrinfo.yres,
			5, /* bits per sample */
			2, /* samples per pixel */
			2  /* bytes/sample */ );

	assert(vncscr != NULL);

	vncscr->desktopName = VNC_DESKTOP_NAME;
	vncscr->frameBuffer = (char *)vncbuf;
	vncscr->alwaysShared = TRUE;
	vncscr->httpDir = NULL;
	vncscr->port = VNC_PORT;

	vncscr->kbdAddEvent = keyevent;
	vncscr->ptrAddEvent = ptrevent;

	rfbInitServer(vncscr);

	/* Mark as dirty since we haven't sent any updates at all yet. */
	gettimeofday(&last_scr_updated, 0);
	rfbMarkRectAsModified(vncscr, 0, 0, scrinfo.xres, scrinfo.yres);

	/* No idea. */
	varblock.r_offset = scrinfo.red.offset + scrinfo.red.length - 5;
	varblock.g_offset = scrinfo.green.offset + scrinfo.green.length - 5;
	varblock.b_offset = scrinfo.blue.offset + scrinfo.blue.length - 5;
	varblock.rfb_xres = scrinfo.yres;
	varblock.rfb_maxy = scrinfo.xres - 1;
	varblock.pixels_per_int = 8 * sizeof(int) / scrinfo.bits_per_pixel;
}

/*****************************************************************************/
void injectKeyEvent(uint16_t code, uint16_t value)
{
    struct input_event ev;
	
    memset(&ev, 0, sizeof(ev));
	
    gettimeofday(&ev.time,0);
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    if(write(kbdfd, &ev, sizeof(ev)) < 0)
    {
        printf("write event failed, %s\n", strerror(errno));
    }
	
    gettimeofday(&ev.time,0);
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if(write(kbdfd, &ev, sizeof(ev)) < 0)
    {
        printf("write event failed, %s\n", strerror(errno));
    }

    printf("injectKey (%d, %d)\n", code , value);    
}

static int keysym2scancode(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
    int scancode = 0;

    int code = (int)key;
    if (code>='0' && code<='9') {
        scancode = scancode - '0';
        if (scancode == 0) scancode += 10;
        scancode += KEY_1 - 1;
    } else if (code>=0xFF50 && code<=0xFF58) {
        static const uint16_t map[] =
             {  KEY_HOME, KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN,
                KEY_PAGEUP, KEY_PAGEDOWN, KEY_END, 0 };
        scancode = map[code & 0xF];
    } else if (code>=0xFFE1 && code<=0xFFEE) {
        static const uint16_t map[] =
             {  KEY_LEFTSHIFT, KEY_LEFTSHIFT,
                KEY_COMPOSE, KEY_COMPOSE,
                KEY_LEFTSHIFT, KEY_LEFTSHIFT,
                0,0,
                KEY_LEFTALT, KEY_RIGHTALT,
                0, 0, 0, 0 };
        scancode = map[code & 0xF];
    } else if ((code>='A' && code<='Z') || (code>='a' && code<='z')) {
        static const uint16_t map[] = {
                KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
                KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
                KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
                KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
                KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
        scancode = map[(code & 0x5F) - 'A'];
    } else {
        switch (code) {
            /* case 0x0003:    scancode = KEY_CENTER;      break; */
            case 0x0020:    scancode = KEY_SPACE;       break;
            /* case 0x0023:    scancode = KEY_SHARP;       break; */
            /* case 0x0033:    scancode = KEY_SHARP;       break; */
            case 0x002C:    scancode = KEY_COMMA;       break;
            case 0x003C:    scancode = KEY_COMMA;       break;
            case 0x002E:    scancode = KEY_DOT;         break;
            case 0x003E:    scancode = KEY_DOT;         break;
            case 0x002F:    scancode = KEY_SLASH;       break;
            case 0x003F:    scancode = KEY_SLASH;       break;
            case 0x0032:    scancode = KEY_EMAIL;       break;
            case 0x0040:    scancode = KEY_EMAIL;       break;
            case 0xFF08:    scancode = KEY_BACKSPACE;   break;
            case 0xFF1B:    scancode = KEY_BACK;        break;
            case 0xFF09:    scancode = KEY_TAB;         break;
            case 0xFF0D:    scancode = KEY_ENTER;       break;
            case 0x002A:    scancode = KEY_KPASTERISK;        break;
            case 0xFFBE:    scancode = KEY_F1;        break; // F1
            case 0xFFBF:    scancode = KEY_F2;         break; // F2
            case 0xFFC0:    scancode = KEY_F3;        break; // F3
            case 0xFFC5:    scancode = KEY_F4;       break; // F8
            case 0xFFC8:    rfbShutdownServer(cl->screen,TRUE);       break; // F11            
        }
    }

    return scancode;
}

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	int scancode;

	printf("Got keysym: %04x (down=%d)\n", (unsigned int)key, (int)down);

	if ((scancode = keysym2scancode(down, key, cl)))
	{
		injectKeyEvent(scancode, down);
	}
}

void injectTouchEvent(int down, int x, int y)
{
    struct input_event ev;

    //printf("%s down %d x %d y %d\n", __func__, down, x, y);
    
    // Calculate the final x and y
    x = xmin + (x * (xmax - xmin)) / (scrinfo.xres);
    y = ymin + (y * (ymax - ymin)) / (scrinfo.yres);
    
    memset(&ev, 0, sizeof(ev));

	if(down) {
	    // Then send the X
	    gettimeofday(&ev.time,0);
	    ev.type = EV_ABS;
	    ev.code = ABS_MT_POSITION_X;
	    ev.value = x;
	    if(write(touchfd, &ev, sizeof(ev)) < 0)
	    {
	        printf("write event failed, %s\n", strerror(errno));
	    }

	    // Then send the Y
	    gettimeofday(&ev.time,0);
	    ev.type = EV_ABS;
	    ev.code = ABS_MT_POSITION_Y;
	    ev.value = y;
	    if(write(touchfd, &ev, sizeof(ev)) < 0)
	    {
	        printf("write event failed, %s\n", strerror(errno));
	    }
	}

    // Finally send the SYN
    gettimeofday(&ev.time,0);
    ev.type = EV_SYN;
    ev.code = SYN_MT_REPORT;
    ev.value = 0;
    if(write(touchfd, &ev, sizeof(ev)) < 0)
    {
        printf("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time,0);
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if(write(touchfd, &ev, sizeof(ev)) < 0)
    {
        printf("write event failed, %s\n", strerror(errno));
    }

    printf("injectTouchEvent (x=%d, y=%d, down=%d)\n", x , y, down);    
}

static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl)
{
	/* Indicates either pointer movement or a pointer button press or release. The pointer is
		now at (x-position, y-position), and the current state of buttons 1 to 8 are represented
		by bits 0 to 7 of button-mask respectively, 0 meaning up, 1 meaning down (pressed).
		On a conventional mouse, buttons 1, 2 and 3 correspond to the left, middle and right
		buttons on the mouse. On a wheel mouse, each step of the wheel upwards is represented
		by a press and release of button 4, and each step downwards is represented by
		a press and release of button 5. 
  		From: http://www.vislab.usyd.edu.au/blogs/index.php/2009/05/22/an-headerless-indexed-protocol-for-input-1?blog=61 */

	static int lastMask, lastX, lastY;
	if(lastMask == buttonMask && buttonMask == 0)
		return;
	if(lastMask == buttonMask &&
		lastX == x &&
		lastY == y)
		return;

	printf("Got ptrevent: %04x (x=%d, y=%d)\n", buttonMask, x, y);

	if(buttonMask & 1) {
		// Simulate left mouse event as touch event
		injectTouchEvent(1, x, y);
		//injectTouchEvent(x, y);
	} else if(buttonMask == 0) {
		injectTouchEvent(0, x, y);
	}
	lastMask = buttonMask;
}

static int get_framebuffer_yoffset()
{
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) < 0) {
        printf("failed to get virtual screen info\n");
        return -1;
    }

    return scrinfo.yoffset;
}

#define PIXEL_FB_TO_RFB(p,r,g,b) \
	((p >> b) & 0x1f001f) | \
	(((p >> g) & 0x1f001f) << 5) | \
	(((p >> r) & 0x1f001f) << 10)


static void update_screen(void)
{
	unsigned int *f, *c, *r;
	int x, y, y_virtual;
	static struct timeval now;
	suseconds_t offset_us;

	gettimeofday(&now, 0);
	offset_us = (now.tv_sec - last_scr_updated.tv_sec)*1000000 
					+ now.tv_usec - last_scr_updated.tv_usec;
	if(offset_us < scr_update_interval)
		return;

	/* get virtual screen info */
	y_virtual = get_framebuffer_yoffset();
	if (y_virtual < 0)
		y_virtual = 0; /* no info, have to assume front buffer */

	varblock.min_x = varblock.min_y = INT_MAX;
	varblock.max_x = varblock.max_y = -1;

	f = (unsigned int *)fbmmap;        /* -> framebuffer         */
	c = (unsigned int *)fbbuf;         /* -> compare framebuffer */
	r = (unsigned int *)vncbuf;        /* -> remote framebuffer  */

	/* jump to right virtual screen */
	f += y_virtual * scrinfo.xres / varblock.pixels_per_int;

	for (y = 0; y < (int) scrinfo.yres; y++) {
		/* Compare every 2 pixels at a time, assuming that changes are
		 * likely in pairs. */
		for (x = 0; x < (int) scrinfo.xres; x += varblock.pixels_per_int) {
			unsigned int pixel = *f;

			if (pixel != *c) {
				*c = pixel; /* update compare buffer */

				/* XXX: Undo the checkered pattern to test the
				 * efficiency gain using hextile encoding. */
				#if 0
				if (pixel == 0x18e320e4 || pixel == 0x20e418e3)
					pixel = 0x18e318e3; /* still needed? */
				#endif
				/* update remote buffer */
				*r = PIXEL_FB_TO_RFB(pixel,
						varblock.r_offset,
						varblock.g_offset,
						varblock.b_offset);

				if (x < varblock.min_x)
					varblock.min_x = x;
				else if (x > varblock.max_x)
					varblock.max_x = x;
 
				if (y < varblock.min_y)
					varblock.min_y = y;
				else if (y > varblock.max_y)
					varblock.max_y = y;
			}

			f++, c++;
			r++;
		}
	}

	if (varblock.min_x < INT_MAX) {
		printf("Changed frame: %dx%d @ (%d,%d)...\n",
		  (varblock.max_x + 2) - varblock.min_x,
		  (varblock.max_y + 1) - varblock.min_y,
		  varblock.min_x, varblock.min_y);

		rfbMarkRectAsModified(vncscr, varblock.min_x, varblock.min_y,
		  varblock.max_x + 2, varblock.max_y + 1);

		rfbProcessEvents(vncscr, 10000); /* update quickly */
		last_scr_updated = now;
	}
}

void blank_framebuffer()
{
	int i, n = scrinfo.xres * scrinfo.yres / varblock.pixels_per_int;
	for (i = 0; i < n; i++) {
		((int *)vncbuf)[i] = 0;
		((int *)fbbuf)[i] = 0;
	}
}

/*****************************************************************************/

void print_usage(char **argv)
{
	printf("%s [-k device] [-t device] [-h]\n"
		"-k device: keyboard device node, default is /dev/input/event0\n"
		"-t device: touch device node, default is /dev/input/event1\n"
		"-h : print this help\n", argv[0]);
}

int main(int argc, char **argv)
{
	if(argc > 1)
	{
		int i=1;
		while(i < argc)
		{
			if(*argv[i] == '-')
			{
				switch(*(argv[i] + 1))
				{
					case 'h':
						print_usage(argv);
						exit(0);
						break;
					case 'k':
						i++;
						strcpy(KBD_DEVICE, argv[i]);
						break;
					case 't':
						i++;
						strcpy(TOUCH_DEVICE, argv[i]);
						break;
				}
			}
			i++;
		}
	}

	printf("Initializing framebuffer device " FB_DEVICE "...\n");
	init_fb();
	printf("Initializing keyboard device %s ...\n", KBD_DEVICE);
	init_kbd();
	printf("Initializing touch device %s ...\n", TOUCH_DEVICE);
	init_touch();

	printf("Initializing VNC server:\n");
	printf("	width:  %d\n", (int)scrinfo.xres);
	printf("	height: %d\n", (int)scrinfo.yres);
	printf("	bpp:    %d\n", (int)scrinfo.bits_per_pixel);
	printf("	port:   %d\n", (int)VNC_PORT);
	init_fb_server(argc, argv);

	/* Implement our own event loop to detect changes in the framebuffer. */
	while (1) {
		rfbClientPtr client_ptr;
		while (!vncscr->clientHead) {
			/* sleep until getting a client */
			rfbProcessEvents(vncscr, LONG_MAX);
		}

		/* refresh screen every 100 ms */
		rfbProcessEvents(vncscr, 100 * 1000 /* timeout in us */);

		/* all clients closed */
		if (!vncscr->clientHead) {
			blank_framebuffer(vncbuf);
		}

		/* scan screen if at least one client has requested */
		for (client_ptr = vncscr->clientHead; client_ptr; client_ptr = client_ptr->next)
		{
			// (!sraRgnEmpty(client_ptr->requestedRegion)) {
				update_screen();
				break;
			//
		}
	}
}

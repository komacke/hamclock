/* this is the same as Adafruit_RA8875 plus support for /dev/fb0 and X11 Windows.
 * N.B. we only remimplented the functions we use, we may have missed a few.
 */

#ifndef _Adafruit_RA8875_H
#define _Adafruit_RA8875_H

#include "Arduino.h"

#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#ifdef _USE_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#endif // _USE_X11

#ifdef _USE_FB0

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/input.h>

#else

// this originally comes from linux/fb.h but we reuse this portion of it for all cases
struct fb_var_screeninfo {
    int xres, yres;
};

#endif	// _USE_FB0


#include "gfxfont.h"
extern const GFXfont Courier_Prime_Sans6pt7b;

#define	RA8875_800x480 1
#define RA8875_PWM_CLK_DIV1024 1
#define	RA8875_MRWC 1


// collection of format conversions for 16 and 32 bit pixels, chosen at compile time.

#define RGB1632(C16)    ((((uint32_t)(C16)&0xF800)<<8) | (((uint32_t)(C16)&0x07E0)<<5) | (((C16)&0x001F)<<3))
#define	RGB3216(C32)	RGB565(((C32)>>16)&0xFF, ((C32)>>8)&0xFF, ((C32)&0xFF))

#define	RA8875_BLACK	RGB565(0,0,0)
#define	RA8875_WHITE	RGB565(255,255,255)
#define RA8875_RED	RGB565(255,0,0)
#define	RA8875_GREEN	RGB565(0,255,0)
#define	RA8875_BLUE	RGB565(0,0,255)
#define	RA8875_CYAN	RGB565(0,255,255)
#define	RA8875_MAGENTA	RGB565(255,0,255)
#define	RA8875_YELLOW	RGB565(255,255,0)

// honor the build depth
#if defined(_FB_DEPTH)
    #if _FB_DEPTH==16
        #define  _16BIT_FB
    #endif
#endif

#if defined(_16BIT_FB)
    typedef uint16_t fbpix_t;
    #define BYTESPFBPIX     2
    #define BITSPFBPIX      16
    #define RGB16TOFBPIX(x) x
    #define FBPIXTORGB16(x) x
    #define FBPIXTORGB32(x) RGB1632(x)
    #define RGB32TOFBPIX(x) RGB565( (((x)>>16)&0xff), (((x)>>8)&0xff), ((x)&0xff) )
#else   // 32 bpp
    typedef uint32_t fbpix_t;
    #define BYTESPFBPIX     4
    #define BITSPFBPIX      32
    #define RGB16TOFBPIX(x) RGB1632(x)
    #define FBPIXTORGB16(x) RGB3216(x)
    #define FBPIXTORGB32(x) x
    #define RGB32TOFBPIX(x) x
#endif



// basic background refresh interval, usecs
#define REFRESH_US      50000

class Adafruit_RA8875 {

    public:

	Adafruit_RA8875(uint8_t CS, uint8_t RST);

	void displayOn (int o) { (void)o; }

	void GPIOX (int x) { (void)x; }

	void PWM1config(bool t, int x) { (void)t; (void)x; }

	void graphicsMode(void) { }

	void writeCommand (uint8_t c) { (void)c; }

	void setRotation (int r) { rotation = r; }

	void textSetCursor(uint16_t x, uint16_t y) { (void)x; (void)y; }

	void PWM1out(uint16_t bpwm) { (void)bpwm; }

	void touchEnable (bool b) { (void)b; }

	bool begin (int x);
	uint16_t width(void);
	uint16_t height(void);
	void fillScreen (uint16_t color16);
	void setTextColor(uint16_t color16);
	void setCursor(uint16_t x, uint16_t y);
	void getTextBounds(const char *string, int16_t x, int16_t y,
		int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h);
	void print (char c);
	void print (char *s);
	void print (const char *s);
	void print (int i, int base = 10);
	void print (float f, int p = 2);
	void print (long l);
	void print (long long ll);
        void printf (const char *fmt, ...);
	void println (void);
	void println (char *s);
	void println (const char *s);
	void println (int i, int base = 10);
	void setXY (int16_t x, int16_t y);
	uint16_t readData(void);
	void setFont (const GFXfont *f);
	const GFXfont* getFont (void);
	int16_t getCursorX(void);
	int16_t getCursorY(void);
	bool touched(void);
	void touchRead (uint16_t *x, uint16_t *y, int *button);
	void drawPixel(int16_t x, int16_t y, uint16_t color16);
        void drawPixels(uint16_t * p, uint32_t count, int16_t x, int16_t y);
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color16);
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t color16);
	void drawRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void fillRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void drawCircle(int16_t x0, int16_t y0, uint16_t r, uint16_t color16);
	void fillCircle(int16_t x0, int16_t y0, uint16_t r, uint16_t color16);
	void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);
	void drawTriangleRaw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);
	void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);
	void fillTriangleRaw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);

        // non-standard access to full underlying resolution
	void drawPixelRaw(int16_t x, int16_t y, uint16_t color16);
	void drawLineRaw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t color16);
	void fillRectRaw(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void drawRectRaw(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void fillCircleRaw(int16_t x0, int16_t y0, uint16_t r, uint16_t color16);
	void drawCircleRaw(int16_t x0, int16_t y0, uint16_t r, uint16_t color16);
	void drawCircleRaw(int16_t x0, int16_t y0, uint16_t r, int thickness, uint16_t color16);

	// special method to draw hi res earth pixel
	void plotEarth (uint16_t x0, uint16_t y0, float lat0, float lng0,
            float dlatr, float dlngr, float dlatd, float dlngd, float fract_day);

        // methods to implement a protected rectangle drawn only with drawPR()
        void setPR (uint16_t x, uint16_t y, uint16_t w, uint16_t h);
        void drawPR(void);
        uint16_t pr_x, pr_y, pr_w, pr_h;
        volatile bool pr_draw;
	void drawCanvas(void);

	// real/app display size
	int SCALESZ;

        // put and get next keyboard character
        void putChar (char c, bool ctrl, bool shift);
        char getChar(bool *ctrl, bool *shift);

        // set and get current mouse position
        bool getMouse (uint16_t *x, uint16_t *y);
        void setMouse (int x, int y);
        bool warpCursor (char dir, unsigned n, int *xp, int *yp);

        // set mmap'ed location and size of day and night images, size in units of uint16_t
        void setEarthPix (char *day_pixels, char *night_pixels, int width, int height);

        // used to engage/disengage X11 fullscreen
        void X11OptionsEngageNow (bool fullscreen);

        // used to get total screen size
        void getScreenSize (int *w, int *h);

        // use to learn whether display is ready
        bool displayReady(void);

        // user direct pixel access
        bool getBackingStore (uint8_t *&bs, int x0, int y0, int w, int h);
        bool setBackingStore (uint8_t *&bs, int x0, int y0, int w, int h);
        bool restoreBackingRegion (uint8_t *bs, int x0, int y0, int w, int h, int sub_y0, int sub_h);
        bool getRawPix (uint8_t *rgb24, int npix);
        bool getPixelRaw (uint16_t x, uint16_t y, uint8_t *r, uint8_t *g, uint8_t *b);


        // control whether to display gray
        void setGrayDisplay (GrayDpy_t g) {
            gray_type = g;
        };

    protected:

	// 0: normal 2: 180 degs
	int rotation;

        // set when display is definitely up and running
        volatile bool ready;

	#define FB_CURSOR_W 16          // APP units
	int FB_CURSOR_SZ;

    private:

#if defined(_CLOCK_1600x960)

	#define FB_XRES 1600
	#define FB_YRES 960

#elif defined(_CLOCK_2400x1440)

	#define FB_XRES 2400
	#define FB_YRES 1440

#elif defined(_CLOCK_3200x1920)

	#define FB_XRES 3200
	#define FB_YRES 1920

#else   // original size

	#define FB_XRES 800
	#define FB_YRES 480

#endif

#ifdef _USE_X11

	Display *display;
	Window win;
        Visual *visual;
        int visdepth;
	GC black_gc;
	XImage *img;
	Pixmap pixmap;
        Atom wmDeleteMessage;

        // used by X11OptionsEngageNow
        volatile bool options_engage, options_fullscreen;

        void encodeKeyEvent (XKeyEvent *event);
        void captureSelection(void);
        bool requestSelection (KeySym ks, unsigned kb_state);
        int decodeMouseButton (XEvent event);

        void saveWinGeom(void);

#endif // _USE_X11

#ifdef _USE_FB0

	// mouse and/or touch screen is read in separate thread protected by mouse_lock
	static void *mouseThreadHelper(void *me);
	void mouseThread (void);
        void findMouse(void);
	int mouse_fd, touch_fd;

	// kb is read in separate thread protected by kb_lock
	static void *kbThreadHelper(void *me);
	void kbThread ();
        void findKeyboard(void);
        int kb_fd;

        void setCursorIfVis (uint16_t row, uint16_t col, fbpix_t color);

	int fb_fd;                      // frame buffer mmap file descriptor
	fbpix_t *fb_fb;                 // pointer to mmap fb
	fbpix_t *fb_cursor;             // temp image buffer for cursor overlay

#endif	// _USE_FB0

	pthread_mutex_t mouse_lock;
	volatile int16_t mouse_x, mouse_y;
        volatile int mouse_button;
	volatile int mouse_ups, mouse_downs;

        typedef struct {
            char c;
            bool control;
            bool shift;
        } KBState;
        #define KB_N 50                 // allow for longish pastes
        KBState kb_q[KB_N];
        int kb_qhead, kb_qtail;
	pthread_mutex_t kb_lock;

        struct timeval mouse_tv;
        int mouse_idle;
        #define MOUSE_FADE 30000        // ms

        // total display size
        volatile int screen_w, screen_h;

	// frame buffer is drawn in separate thread protected by fb_lock
        static void *fbThreadHelper(void *me);
        #define APP_WIDTH  800
        #define APP_HEIGHT 480
	void fbThread ();
	pthread_mutex_t fb_lock;
	struct fb_var_screeninfo fb_si;
	volatile bool fb_dirty;
	fbpix_t *fb_canvas;             // main drawing image buffer
	fbpix_t *fb_stage;              // temp image during staging to fb hw
	int fb_nbytes;                  // bytes in each in-memory image buffer
	void plotChar (char c);
	fbpix_t text_color;
	uint16_t cursor_x, cursor_y;
	uint16_t read_x, read_y;
	bool read_msb, read_first;
	const GFXfont *current_font;
	int FB_X0;
	int FB_Y0;

        // full res helpers
	void plotfb (int16_t x, int16_t y, fbpix_t color);
        void plotDrawRect (int16_t x0, int16_t y0, int16_t w, int16_t h, fbpix_t fbpix);
        void plotFillRect (int16_t x0, int16_t y0, int16_t w, int16_t h, fbpix_t fbpix);
        void plotDrawCircle (int16_t x0, int16_t y0, uint16_t r0, fbpix_t fbpix);
        void plotFillCircle(int16_t x0, int16_t y0, uint16_t r0, fbpix_t fbpix);

        // brezenham implementation
        void plotLineRaw (int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thick, fbpix_t color);
        void drawLineOverlap (int16_t aXStart, int16_t aYStart, int16_t aXEnd, int16_t aYEnd,
                        uint8_t aOverlap, fbpix_t aColor);
        void drawThickLine (int16_t aXStart, int16_t aYStart, int16_t aXEnd, int16_t aYEnd,
                        int16_t aThickness, uint8_t aThicknessMode, fbpix_t aColor);

	// big earth mmap'd maps, actually 2d EARTH_BIG_H rows x EARTH_BIG_W columns
        uint16_t *DEARTH_BIG;
        uint16_t *NEARTH_BIG;
        int EARTH_BIG_H, EARTH_BIG_W;

        // handy macro to implement the 2d nature of the arrays
        #define EPIXEL(a,r,c)   ((a)[(r)*EARTH_BIG_W + (c)])

        // swap two pairs of x and y
        void swap2 (int16_t &x0, int16_t &y0, int16_t &x1, int16_t &y1) {
            int16_t tx = x0; x0 = x1; x1 = tx;
            int16_t ty = y0; y0 = y1; y1 = ty;
        }

        // set whether to display gray
        GrayDpy_t gray_type;

};

#endif // _Adafruit_RA8875_H

/* connections between hamclock and our local ArduioLib
 */

#ifndef _ARDUINOLIB_H
#define _ARDUINOLIB_H

#include <stdint.h>
#include <time.h>
#include <string>


// N.B. keep showDefines() up to date


#if defined(ESP8266)
  #define _IS_ESP8266
#else
  #define _IS_UNIX
#endif

#if defined(__ANDROID__)
  #define _IS_ANDROID
#elif defined(__linux__)
  #define _IS_LINUX
#endif

#if defined(__FreeBSD__)
  #define _IS_FREEBSD
#endif

#if defined(__NetBSD__)
  #define _IS_NETBSD
#endif


#if defined (__APPLE__)
    #define _IS_APPLE
    #if defined(__aarch64__) || defined(__arm64__)
        #define _IS_APPLE_M
    #else
        #define _IS_APPLE_x86
    #endif
#endif


#if (defined(__arm__) || defined(__aarch64__)) && defined(_IS_LINUX)
  #if defined(__has_include)
      #if __has_include(<gpiod.h>) || __has_include(<bcm_host.h>) || __has_include(<pigpio.h>)
        #define _IS_LINUX_RPI
      #endif
      #if __has_include("/etc/armbian-release")
        #define _IS_LINUX_ARMBIAN
      #endif
  #endif
#endif

#if defined(_IS_LINUX)
  #if defined(__has_include)
      // alpine linux does not include linux_headers package by default
      #if __has_include(<linux/wireless.h>)
        #define _LINUX_WIRELESS_OK
      #endif
  #endif
  #if defined(_USE_GPIOD)
      #include <gpiod.h>
      #define _NATIVE_GPIO_LINUX              // set for either GPIOD or GPIOBC
      #define _NATIVE_GPIOD_LINUX             // gpiod is sufficiently mature to use
  #endif
#endif


#if defined(__has_include)
  #if defined(_IS_FREEBSD) && __has_include(<dev/iicbus/iic.h>)
    #define _NATIVE_I2C_FREEBSD
  #elif defined(_IS_LINUX) && (__has_include(<linux/i2c-dev.h>) || __has_include("linux/i2c-dev.h"))
    #define _NATIVE_I2C_LINUX
  #endif
#endif

#if defined(__has_include)
  #if defined(_IS_FREEBSD)
    #if __has_include(<libgpio.h>)
        #define _NATIVE_GPIO_FREEBSD
        #include <sys/types.h>
        #include <libgpio.h>
    #endif
  #elif defined(_IS_LINUX)
    #if __has_include(<pigpio.h>)
        #if !defined(_NATIVE_GPIO_LINUX)
            #define _NATIVE_GPIO_LINUX          // set for either GPIOD or GPIOBC; avoid dup
        #endif
        #define _NATIVE_GPIOBC_LINUX            // use old memory mapped broadcom interface
        #include <sys/mman.h>
        #include <sys/file.h>
    #endif
  #endif
#endif


// tcp ports
#define RESTFUL_PORT    8080
#define LIVEWEB_RW_PORT 8081
#define LIVEWEB_RO_PORT 8082

// character codes for tft.get/putChar(), mostly ASCII control plus a few more
#define CHAR_NONE       0
#define CHAR_BS         '\b'
#define CHAR_TAB        '\t'
#define CHAR_NL         '\n'
#define CHAR_CR         '\r'
#define CHAR_SPACE      ' '
#define CHAR_ESC        ((char)27)
#define CHAR_DEL        ((char)127)
#define CHAR_LEFT       ((char)-1)
#define CHAR_DOWN       ((char)-2)
#define CHAR_UP         ((char)-3)
#define CHAR_RIGHT      ((char)-4)

// settings for gray scale display
typedef enum {
    GRAY_OFF,
    GRAY_ALL,
    GRAY_MAP,
} GrayDpy_t;


// convert 8-bit each (R,G,B) to 5R : 6G : 5G
// would expect this to be in graphics lib but can't find it...
#define RGB565(R,G,B)   (uint16_t)(((((uint16_t)(R) & 0xF8) << 8) | (((uint16_t)(G) & 0xFC) << 3) | ((uint16_t)(B) >> 3)))

// extract 8-bit colors from uint16_t RGB565 color in range 0-255
#define RGB565_R(c)     (255*(((c) & 0xF800) >> 11)/((1<<5)-1))
#define RGB565_G(c)     (255*(((c) & 0x07E0) >> 5)/((1<<6)-1))
#define RGB565_B(c)     (255*((c) & 0x001F)/((1<<5)-1))

// #define RGB2GRAY(r,g,b) ((r)*0.26F + (g)*0.65F + (b)*0.09F)
#define RGB2GRAY(r,g,b) ((r)/4 + 2*(g)/3 + (b)/12)      // faster??

/* define debug subsystems
 */
#define DEBUG_SUBSYS                        \
    X(DEBUG_ADIF,       "ADIF")             \
    X(DEBUG_APRS,       "aprs")             \
    X(DEBUG_BMP,        "BMP")              \
    X(DEBUG_BC,         "BigClock")         \
    X(DEBUG_CACHE,      "cache")            \
    X(DEBUG_CONTESTS,   "contests")         \
    X(DEBUG_CTY,        "cty")              \
    X(DEBUG_DXC,        "dxcluster")        \
    X(DEBUG_DXPEDS,     "dxpeds")           \
    X(DEBUG_GIMBAL,     "gimbal")           \
    X(DEBUG_IO,         "io")               \
    X(DEBUG_WEB,        "liveweb")          \
    X(DEBUG_MENUS,      "menus")            \
    X(DEBUG_NMEA,       "NMEA")             \
    X(DEBUG_NVRAM,      "NVRAM")            \
    X(DEBUG_NET,        "network")          \
    X(DEBUG_RIG,        "rig")              \
    X(DEBUG_ESATS,      "esats")            \
    X(DEBUG_SCROLL,     "scroller")         \
    X(DEBUG_WL,         "watchlist")        \
    X(DEBUG_WIFI,       "wifi")             \
    X(DEBUG_WX,         "wx")               \
    X(DEBUG_ZONES,      "zones")

#define X(a,b) a,               // expands DEBUG_SUBSYS to each enum and comma
typedef enum {
    DEBUG_SUBSYS
    DEBUG_SUBSYS_N
} DebugSubsys;
#undef X

extern bool debugLevel (DebugSubsys s, int level);
extern bool setDebugLevel (const char *name, int level);
extern void getDebugs (const char *names[DEBUG_SUBSYS_N], int levels[DEBUG_SUBSYS_N]);



// glue
extern void setX11FullScreen (bool);
extern void setDemoMode(bool on);
extern void setCenterLng (int16_t l);
extern const char *backend_host;
extern const char *software_host;
extern int backend_port;
extern int liveweb_ro_port;
extern int liveweb_rw_port;
extern int liveweb_max;
extern int liveweb_to;
extern const int liveweb_maxmax;
extern int restful_port;
extern bool skip_skip;
extern bool init_iploc;
extern bool want_kbcursor;
extern const char *init_locip;
extern time_t usr_datetime;
extern const char *getI2CFilename(void);
extern bool GPIOOk(void);
extern const char *hc_version;
extern bool version_https;
extern std::string our_dir;
extern void doExit(void);
extern bool testPassword (const char *category, const char *candidate_pw);
extern const char *pw_file;
extern bool NVReadX11Geom (int &x, int &y, int &w, int &h);
extern void NVWriteX11Geom (int x, int y, int w, int h);
extern char *strTrimAll (char *str);



#define N_DIAG_FILES 4
extern const char *diag_files[N_DIAG_FILES];


// MCP23017 pin assignments. convenient to assign sequencially.
// see Adafruit_MCP23X17.cpp for mapping to RPi
typedef enum {
    SW_CD_RED_PIN   = 0,
    SW_CD_GRN_PIN   = 1,
    SW_CD_RESET_PIN = 2,
    SW_ALARMOUT_PIN = 3,
    SW_ALARMOFF_PIN = 4,
    SATALARM_PIN    = 5,
    ONAIR_PIN       = 6,
    RESERVED_PIN    = 7,
    USER1_PIN       = 8,
    USER2_PIN       = 9,
    USER3_PIN       = 10,
    USER4_PIN       = 11,
    USER5_PIN       = 12,
    USER6_PIN       = 13,
    USER7_PIN       = 14,
    USER8_PIN       = 15,
    MCP_N_LINES     = 16,               // number of real pins assigned
    MCP_FAKE_KX3    = 99                // see ArduinoLib/Adafruit_MCP23X17.cpp
} MCP23017Pins;

#if defined(_IS_ESP8266)
#define Elecraft_PIN           15       // huzzah
#else
#define Elecraft_PIN           14       // header 8
#endif

#endif // _ARDUINOLIB_H

#ifndef _ARDUINO_H
#define _ARDUINO_H

/* Arduino.h over unix
 */



#include <stdint.h>
#include <string>

#include "../ArduinoLib.h"

#define	String std::string

#define	PROGMEM	
#define	F(X)	 X
#define	PSTR(X)	 X
#define FPSTR(X) X
#define PGM_P    const char *
#define	__FlashStringHelper char
#define strlen_P  strlen
#define strcpy_P  strcpy
#define strncpy_P  strncpy
#define strcmp_P  strcmp
#define strncmp_P  strncmp
#define strspn_P  strspn
#define strstr_P  strstr

#define LSBFIRST 0
#define MSBFIRST 1

// normally in cores/esp8266/flash_utils.h
#define FLASH_SECTOR_SIZE       4096

#define HIGH 0x1
#define LOW  0x0
#define INPUT             0x00
#define INPUT_PULLUP      0x02
#define OUTPUT            0x01
#define	A0	0

#define	pgm_read_byte(a)	(*(a))
#define	pgm_read_word(a)	(*(a))
#define	pgm_read_dword(a)	(*(a))
#define	pgm_read_float(a)	(*(a))
#define	pgm_read_ptr(a)	        (*(a))

extern uint32_t millis(void);
extern long random(int max);
extern void randomSeed(int s);
extern void delay (uint32_t ms);
extern uint16_t analogRead(int pin);
extern void setup(void);
extern void loop(void);
extern bool rm_eeprom;
extern bool ignore_x11geom;


extern char **our_argv;
extern char our_make[];
extern char build_variables[];

extern void capturePasswords (const char *fn);
extern int hamclock_main (int ac, char *av[]);

#include "ESP.h"
#include "Serial.h"
#include "TimeLib.h"


#endif // _ARDUINO_H

# HamClock Client EEPROM File Layout

The HamClock Client uses an "EEPROM" file  to store configuration settings. It is located in `~/.hamclock/eeprom` of a client installation.

See [hceeprom.csv](./hceeprom.csv) for the definition of all the data elements in the file. It is created/updated with [generate.sh](./generate.sh).

The objective for documenting this file is so users can switch to other HamClock implementations and maintain their configuration.

NB: it is called an EEPROM file for historical reasons, as it was originally derived from the ESP8266 implementation that actually did use EEPROM to save settings.

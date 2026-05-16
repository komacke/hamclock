/* cfg_info_format.h
 *
 * Shared format definitions for the HamClock configuration backup sidecar
 * (.info.txt) files.
 *
 * Both HamClock and the hamclock-backup companion app include this header so
 * they agree on field names, the format version, and the file suffix. This is
 * the entire shared surface between the two programs -- no shared code is
 * required because each program performs disjoint operations on the format
 * (HamClock writes; the companion app reads).
 *
 * Sidecar file format (INI-style, plain ASCII, one section):
 *
 *   [hamclock_backup]
 *   format_version=1
 *   config_name=Home Station
 *   created_utc=2026-05-11T14:32:07Z
 *   hostname=hamclock-pi.local
 *   platform=HamClock-rpi
 *   os_description=Raspberry Pi OS (Debian 12)
 *   hamclock_version=4.24b05
 *   eeprom_size=8192
 *   eeprom_crc32=A3F2C1E8
 *   callsign=W1AW
 *   user_note=
 *
 * Field rules (v1):
 *   - All values are single-line, plain ASCII. Newlines in values are forbidden.
 *   - Unknown keys are ignored by readers (forward compatibility).
 *   - Missing optional keys degrade gracefully.
 *   - format_version=1 fields below are stable and will not be repurposed.
 */

#ifndef _CFG_INFO_FORMAT_H
#define _CFG_INFO_FORMAT_H

/* Current sidecar format version. Bump only when introducing a breaking change. */
#define HC_INFO_FORMAT_VERSION    1

/* Section header (single-section file). */
#define HC_INFO_SECTION           "hamclock_backup"

/* Field keys. */
#define HC_INFO_KEY_FORMAT_VER    "format_version"
#define HC_INFO_KEY_CONFIG_NAME   "config_name"
#define HC_INFO_KEY_CREATED       "created_utc"
#define HC_INFO_KEY_HOSTNAME      "hostname"
#define HC_INFO_KEY_PLATFORM      "platform"
#define HC_INFO_KEY_OS_DESC       "os_description"
#define HC_INFO_KEY_HC_VERSION    "hamclock_version"
#define HC_INFO_KEY_EEPROM_SIZE   "eeprom_size"
#define HC_INFO_KEY_EEPROM_CRC32  "eeprom_crc32"
#define HC_INFO_KEY_CALLSIGN      "callsign"
#define HC_INFO_KEY_USER_NOTE     "user_note"

/* File naming. For any saved configuration <name>.eeprom, the sidecar is
 * <name>.eeprom.info.txt in the same directory. */
#define HC_INFO_SUFFIX            ".info.txt"
#define HC_EEPROM_SUFFIX          ".eeprom"

#endif /* _CFG_INFO_FORMAT_H */

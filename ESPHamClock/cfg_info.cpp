/* manage backup sidecar (.info.txt) files for saved configurations.
 *
 * Each saved configuration <name>.eeprom in the configurations/ directory has
 * an optional companion <name>.eeprom.info.txt that describes when, where, and
 * by whom it was created. HamClock writes these sidecars; the hamclock-backup
 * companion app reads them. HamClock itself never reads them back.
 *
 * Sidecar maintenance hooks are called from configs.cpp:
 *   saveCfgFile   -> writeCfgInfo
 *   deleteCfgFile -> deleteCfgInfo
 *   renameCfgFile -> renameCfgInfo
 *
 * Sidecar errors are non-fatal: a failure here will be logged but will not
 * abort the underlying save/delete/rename operation.
 */

#include "HamClock.h"
#include "cfg_info_format.h"

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>             // gethostname
#include <sys/stat.h>           // stat


/* Build the sidecar path for the given eeprom file path.
 *   eeprom_path:  ".../configurations/Home_Station.eeprom"
 *   sidecar_out:  ".../configurations/Home_Station.eeprom.info.txt"
 * Caller-supplied buffer; truncates safely if too small.
 */
static void infoPathFromEepromPath (const char *eeprom_path, char *sidecar_out, size_t out_l)
{
    snprintf (sidecar_out, out_l, "%s%s", eeprom_path, HC_INFO_SUFFIX);
}

/* Same as above but starting from a config name. */
static void infoPathFromCfgName (const char *cfg_name, char *sidecar_out, size_t out_l)
{
    // Strip trailing slash from our_dir if present -- it may already end with '/'
    std::string base = our_dir;
    if (!base.empty() && base.back() == '/') base.pop_back();
    int n = snprintf (sidecar_out, out_l, "%s/%s/", base.c_str(), "configurations");
    if (n < 0 || (size_t)n >= out_l) { sidecar_out[0] = '\0'; return; }
    // mirror cfg2file's space->underscore substitution
    strncpySubChar (sidecar_out + n, cfg_name, '_', ' ', out_l - n);
    size_t cur = strlen(sidecar_out);
    snprintf (sidecar_out + cur, out_l - cur, "%s%s", HC_EEPROM_SUFFIX, HC_INFO_SUFFIX);
}


/* CRC32 of a file (standard reflected polynomial 0xEDB88320, matches zlib/PNG).
 * Returns true on success and writes the CRC to *out. Returns false on I/O error.
 *
 * Defined static here AND as a small named helper so the companion app can use
 * the same algorithm independently -- the algorithm itself is a public standard
 * (CRC-32/ISO-HDLC) so they cannot disagree as long as both use this polynomial.
 */
static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void initCrc32Table ()
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_ready = true;
}

bool fileCrc32 (const char *path, uint32_t *out_crc)
{
    if (!out_crc) return false;
    if (!crc32_table_ready) initCrc32Table();

    FILE *fp = fopen (path, "rb");
    if (!fp) return false;

    uint32_t crc = 0xFFFFFFFFu;
    unsigned char buf[64 * 1024];
    size_t n;
    while ((n = fread (buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < n; i++)
            crc = crc32_table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    }
    bool ok = !ferror(fp);
    fclose (fp);
    if (!ok) return false;

    *out_crc = crc ^ 0xFFFFFFFFu;
    return true;
}


/* Sanitize a value for writing into the INI format: replace any newline,
 * carriage return, or NUL with '?'. Returns a pointer to the input buffer
 * which is modified in place. v1 forbids newlines in values; this is the
 * belt-and-suspenders to enforce that even if a caller violates the rule.
 */
static char *sanitizeValue (char *s)
{
    if (!s) return s;
    for (char *p = s; *p; p++) {
        if (*p == '\n' || *p == '\r')
            *p = '?';
    }
    return s;
}


/* Read /etc/os-release for PRETTY_NAME on Linux, or sw_vers on macOS,
 * returning a best-effort short description in `out`. On any failure leaves
 * `out` empty -- this field is optional in the sidecar format.
 */
static void getOsDescription (char *out, size_t out_l)
{
    out[0] = '\0';

#if defined(_IS_LINUX)
    FILE *fp = fopen ("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets (line, sizeof(line), fp)) {
            // looking for PRETTY_NAME="..."
            if (strncmp (line, "PRETTY_NAME=", 12) == 0) {
                char *v = line + 12;
                // strip newline
                size_t n = strlen(v);
                while (n > 0 && (v[n-1] == '\n' || v[n-1] == '\r')) v[--n] = '\0';
                // strip surrounding quotes if present
                if (n >= 2 && v[0] == '"' && v[n-1] == '"') {
                    v[n-1] = '\0';
                    v++;
                }
                snprintf (out, out_l, "%s", v);
                break;
            }
        }
        fclose (fp);
    }
#elif defined(_IS_APPLE)
    /* Use sw_vers for a friendly string like "macOS 14.4.1". popen is
     * acceptable here because this runs once per saved config -- not in
     * any hot path -- and the binary is a system tool. */
    FILE *fp = popen ("sw_vers -productName 2>/dev/null", "r");
    char prod[64] = "";
    if (fp) {
        if (fgets (prod, sizeof(prod), fp)) {
            size_t n = strlen(prod);
            while (n > 0 && (prod[n-1] == '\n' || prod[n-1] == '\r')) prod[--n] = '\0';
        }
        pclose (fp);
    }
    fp = popen ("sw_vers -productVersion 2>/dev/null", "r");
    char ver[32] = "";
    if (fp) {
        if (fgets (ver, sizeof(ver), fp)) {
            size_t n = strlen(ver);
            while (n > 0 && (ver[n-1] == '\n' || ver[n-1] == '\r')) ver[--n] = '\0';
        }
        pclose (fp);
    }
    if (prod[0] && ver[0])
        snprintf (out, out_l, "%s %s", prod, ver);
    else if (prod[0])
        snprintf (out, out_l, "%s", prod);
#else
    (void) out_l;       // unused on other platforms
#endif
}


/* Write the sidecar file for the given config name.
 * The matching <name>.eeprom file must already exist on disk -- we read it
 * to compute the size and CRC32 fields. Any failure is logged and returns
 * without raising fatalError; sidecar creation is best-effort.
 */
void writeCfgInfo (const char *cfg_name)
{
    if (!cfg_name || !cfg_name[0]) return;

    // Locate the eeprom file the sidecar describes.
    // Mirror cfg2file() exactly: snprintf the prefix, strncpySubChar the name
    // (replacing spaces with underscores), then advance by strlen(cfg_name)
    // and append the suffix. The last argument to strncpySubChar is int per
    // HamClock's signature.
    char eeprom_path[2000];
    {
        // Strip trailing slash from our_dir if present
        std::string base = our_dir;
        if (!base.empty() && base.back() == '/') base.pop_back();
        int n = snprintf (eeprom_path, sizeof(eeprom_path), "%s/%s/",
                          base.c_str(), "configurations");
        if (n < 0 || n >= (int)sizeof(eeprom_path)) {
            Serial.printf ("CFG: sidecar path overflow for '%s'\n", cfg_name);
            return;
        }
        strncpySubChar (eeprom_path + n, cfg_name, '_', ' ', (int)(sizeof(eeprom_path) - n));
        n += (int)strlen (cfg_name);
        snprintf (eeprom_path + n, sizeof(eeprom_path) - n, "%s", HC_EEPROM_SUFFIX);
    }

    // Size from stat()
    struct stat st;
    if (stat (eeprom_path, &st) != 0) {
        Serial.printf ("CFG: sidecar stat(%s): %s\n", eeprom_path, strerror(errno));
        return;
    }
    unsigned long long eeprom_size = (unsigned long long) st.st_size;

    // CRC32 of the eeprom contents
    uint32_t crc = 0;
    bool crc_ok = fileCrc32 (eeprom_path, &crc);
    if (!crc_ok) {
        Serial.printf ("CFG: sidecar crc32(%s) failed\n", eeprom_path);
        // continue anyway -- we'll emit the sidecar without the crc line
    }

    // Hostname (truncate at first dot? -- no, keep FQDN if present)
    char host[256] = "";
    if (gethostname (host, sizeof(host)) != 0)
        host[0] = '\0';
    host[sizeof(host)-1] = '\0';

    // OS description (best-effort)
    char osdesc[256] = "";
    getOsDescription (osdesc, sizeof(osdesc));

    // Callsign (may be empty if user hasn't set one yet)
    const char *call = getCallsign();
    if (!call) call = "";

    // Timestamp in UTC ISO 8601
    char ts[32] = "";
    time_t now = time(NULL);
    struct tm tmv;
    if (gmtime_r (&now, &tmv) != NULL)
        strftime (ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);

    // Build the sidecar file path
    char sidecar_path[2100];
    infoPathFromEepromPath (eeprom_path, sidecar_path, sizeof(sidecar_path));

    // Write atomically via a temporary file in the same directory, then rename.
    char tmp_path[2200];
    snprintf (tmp_path, sizeof(tmp_path), "%s.tmp", sidecar_path);

    FILE *fp = fopen (tmp_path, "w");
    if (!fp) {
        Serial.printf ("CFG: sidecar fopen(%s): %s\n", tmp_path, strerror(errno));
        return;
    }

    // Set real owner (not fatal if can't) -- matches saveCfgFile's behavior.
    if (fchown (fileno(fp), getuid(), getgid()) < 0) {
        // silently ignore; not important
    }

    // Make mutable copies for sanitization
    char cfg_name_buf[256], host_buf[256], osdesc_buf[256], call_buf[64];
    snprintf (cfg_name_buf, sizeof(cfg_name_buf), "%s", cfg_name);
    snprintf (host_buf, sizeof(host_buf), "%s", host);
    snprintf (osdesc_buf, sizeof(osdesc_buf), "%s", osdesc);
    snprintf (call_buf, sizeof(call_buf), "%s", call);

    fprintf (fp, "[%s]\n", HC_INFO_SECTION);
    fprintf (fp, "%s=%d\n", HC_INFO_KEY_FORMAT_VER, HC_INFO_FORMAT_VERSION);
    fprintf (fp, "%s=%s\n", HC_INFO_KEY_CONFIG_NAME, sanitizeValue(cfg_name_buf));
    fprintf (fp, "%s=%s\n", HC_INFO_KEY_CREATED,     ts);
    fprintf (fp, "%s=%s\n", HC_INFO_KEY_HOSTNAME,    sanitizeValue(host_buf));
    fprintf (fp, "%s=%s\n", HC_INFO_KEY_PLATFORM,    platform);
    fprintf (fp, "%s=%s\n", HC_INFO_KEY_OS_DESC,     sanitizeValue(osdesc_buf));
    fprintf (fp, "%s=%s\n", HC_INFO_KEY_HC_VERSION,  hc_version);
    fprintf (fp, "%s=%llu\n", HC_INFO_KEY_EEPROM_SIZE, eeprom_size);
    if (crc_ok)
        fprintf (fp, "%s=%08X\n", HC_INFO_KEY_EEPROM_CRC32, crc);
    fprintf (fp, "%s=%s\n", HC_INFO_KEY_CALLSIGN,    sanitizeValue(call_buf));
    fprintf (fp, "%s=\n",   HC_INFO_KEY_USER_NOTE);     // reserved for future use

    bool write_ok = !ferror(fp);
    if (fclose (fp) != 0) write_ok = false;

    if (!write_ok) {
        Serial.printf ("CFG: sidecar write error for '%s'\n", sidecar_path);
        unlink (tmp_path);
        return;
    }

    if (rename (tmp_path, sidecar_path) != 0) {
        Serial.printf ("CFG: sidecar rename(%s,%s): %s\n",
                       tmp_path, sidecar_path, strerror(errno));
        unlink (tmp_path);
        return;
    }

    Serial.printf ("CFG: sidecar wrote '%s' (crc=%08X size=%llu)\n",
                   cfg_name, crc_ok ? crc : 0u, eeprom_size);
}


/* Delete the sidecar for the given config name. Silent on ENOENT
 * (orphan eeprom files without sidecars are fine).
 */
void deleteCfgInfo (const char *cfg_name)
{
    if (!cfg_name || !cfg_name[0]) return;

    char sidecar_path[2100];
    infoPathFromCfgName (cfg_name, sidecar_path, sizeof(sidecar_path));
    if (!sidecar_path[0]) return;

    if (unlink (sidecar_path) != 0) {
        if (errno != ENOENT)
            Serial.printf ("CFG: sidecar unlink(%s): %s\n", sidecar_path, strerror(errno));
    } else {
        Serial.printf ("CFG: sidecar deleted '%s'\n", cfg_name);
    }
}


/* Rename the sidecar from `from` to `to`, and update its internal
 * config_name= field. Silent on ENOENT (orphan eeprom files are fine).
 * Other fields are preserved verbatim -- created_utc, hostname, etc. remain
 * what they were on original creation.
 */
void renameCfgInfo (const char *from, const char *to)
{
    if (!from || !to || !from[0] || !to[0]) return;

    char from_path[2100], to_path[2100];
    infoPathFromCfgName (from, from_path, sizeof(from_path));
    infoPathFromCfgName (to,   to_path,   sizeof(to_path));
    if (!from_path[0] || !to_path[0]) return;

    // If the source sidecar doesn't exist, the eeprom was an orphan -- skip silently.
    FILE *fp_in = fopen (from_path, "r");
    if (!fp_in) {
        if (errno != ENOENT)
            Serial.printf ("CFG: sidecar rename open(%s): %s\n", from_path, strerror(errno));
        return;
    }

    // Read all lines, rewriting config_name= as we go.
    char tmp_path[2200];
    snprintf (tmp_path, sizeof(tmp_path), "%s.tmp", to_path);
    FILE *fp_out = fopen (tmp_path, "w");
    if (!fp_out) {
        Serial.printf ("CFG: sidecar rename open(%s): %s\n", tmp_path, strerror(errno));
        fclose (fp_in);
        return;
    }
    if (fchown (fileno(fp_out), getuid(), getgid()) < 0) {
        // silently ignore; not important
    }

    char to_name_buf[256];
    snprintf (to_name_buf, sizeof(to_name_buf), "%s", to);
    sanitizeValue (to_name_buf);

    char line[1024];
    bool ok = true;
    while (fgets (line, sizeof(line), fp_in)) {
        // crude prefix match for the config_name key at the start of a line
        const char *key_pfx = HC_INFO_KEY_CONFIG_NAME "=";
        size_t kpl = strlen (key_pfx);
        if (strncmp (line, key_pfx, kpl) == 0) {
            if (fprintf (fp_out, "%s%s\n", key_pfx, to_name_buf) < 0) { ok = false; break; }
        } else {
            if (fputs (line, fp_out) == EOF) { ok = false; break; }
        }
    }
    if (ferror(fp_in)) ok = false;
    fclose (fp_in);
    if (fclose (fp_out) != 0) ok = false;

    if (!ok) {
        Serial.printf ("CFG: sidecar rename write error for '%s'\n", tmp_path);
        unlink (tmp_path);
        return;
    }

    // Move tmp into place at the new name, then remove the old sidecar.
    if (rename (tmp_path, to_path) != 0) {
        Serial.printf ("CFG: sidecar rename(%s,%s): %s\n",
                       tmp_path, to_path, strerror(errno));
        unlink (tmp_path);
        return;
    }
    if (strcmp (from_path, to_path) != 0) {
        if (unlink (from_path) != 0 && errno != ENOENT) {
            Serial.printf ("CFG: sidecar rename unlink(%s): %s\n",
                           from_path, strerror(errno));
        }
    }

    Serial.printf ("CFG: sidecar renamed '%s' to '%s'\n", from, to);
}

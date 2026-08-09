/* iota.cpp -- IOTA group reference -> name lookup.
 *
 * Fed by fetchIOTA.py running on the backend server, which turns
 * iota-world.org's groups.json into a small REF,Name cache at /ONTA/iota.txt.
 * That file is a *reference database*, not a live spot feed -- there is no
 * "who's activating what right now" feed from iota-world.org -- so unlike
 * onta.txt this is used only to enrich ordinary DX Cluster spots whose
 * comment happens to mention an IOTA group, not to drive its own pane.
 */

#include "HamClock.h"

#include <unordered_map>
#include <string>
#include <cctype>
#include <cstring>
#include <sys/stat.h>

#include "iota.h"

static const char iota_page[] = "/ONTA/iota.txt";      // matches fetchIOTA.py deployment
static const char iota_file[] = "iota.txt";             // local cache file
#define IOTA_CACHE_INTERVAL   (24*3600)                  // group defs barely ever change

static std::unordered_map<std::string, std::string> iota_names;
static time_t iota_cache_mtime;                          // mtime of local file when last parsed

/* (re)load the REF -> Name cache if due. safe -- indeed intended -- to call on every DX Cluster
 * update cycle (every few seconds): openCachedFile() itself throttles the actual network check to
 * IOTA_CACHE_INTERVAL, and even the local reopen+stat it does every call is cheap. What must NOT
 * happen every call is rebuilding iota_names from scratch -- with ~1200 groups that's real CPU and
 * heap churn on an ESP32 if done every second forever -- so skip the rebuild unless the file's mtime
 * shows it's genuinely different from what we last parsed.
 */
void retrieveIOTACache (void)
{
    FILE *fp = openCachedFile (iota_file, iota_page, IOTA_CACHE_INTERVAL, 0);
    if (!fp)
        return;

    // has the file actually changed since we last parsed it?
    struct stat sbuf;
    if (fstat (fileno(fp), &sbuf) == 0 && sbuf.st_mtime == iota_cache_mtime) {
        fclose (fp);
        return;                                           // unchanged -- nothing to do
    }

    iota_names.clear();

    char line[80];
    while (fgets (line, sizeof(line), fp)) {
        chompString (line);
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char *comma = strchr (line, ',');
        if (!comma)
            continue;
        *comma = '\0';
        iota_names[line] = comma + 1;                   // ref -> rest of line is name
    }

    iota_cache_mtime = sbuf.st_mtime;
    fclose (fp);

    Serial.printf ("IOTA: read %d group names\n", (int)iota_names.size());
}

/* an IOTA group reference looks like AA-123: 2 letters, dash, 3 digits.
 * scan comment for the first match.
 */
bool findIOTARef (const char *comment, char *out, size_t out_len)
{
    if (!comment || out_len < 8)
        return (false);

    size_t len = strlen (comment);
    for (size_t i = 0; i + 6 <= len; i++) {
        const char *p = comment + i;
        if (isalpha ((unsigned char)p[0]) && isalpha ((unsigned char)p[1]) && p[2] == '-'
                    && isdigit ((unsigned char)p[3]) && isdigit ((unsigned char)p[4])
                    && isdigit ((unsigned char)p[5])
                    // must not be glued to more alnum chars on either side, else
                    // eg the middle of a longer token could false-match
                    && (i == 0 || !isalnum ((unsigned char)comment[i-1]))
                    && (i+6 == len || !isalnum ((unsigned char)p[6]))) {
            out[0] = toupper ((unsigned char)p[0]);
            out[1] = toupper ((unsigned char)p[1]);
            memcpy (out+2, p+2, 4);
            out[6] = '\0';
            return (true);
        }
    }
    return (false);
}

const char *lookupIOTAName (const char *ref)
{
    if (!ref || !ref[0])
        return (NULL);
    auto it = iota_names.find (ref);
    return (it == iota_names.end() ? NULL : it->second.c_str());
}

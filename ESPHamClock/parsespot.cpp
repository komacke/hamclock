/* parse several spot formats. all call dxcLog() to save error messages.
 */


#include "HamClock.h"
#include "iota.h"
#include "xota.h"



/*********************************************************************************************************
 *
 * generic cluster server
 *   DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z EL98
 *
 *********************************************************************************************************/


/* parse the given line into a new spot record.
 * if ok fill in spot and return true else dxcLog and return false.
 */
bool crackClusterSpot (char line[], DXSpot &spot)
{
    // fresh
    spot = {};

    // DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z EL98
    // N.B. match the "DX de " label case-insensitively: some AR-Cluster nodes (eg k1ttt.net)
    // print it as "DX de" for live pushed spots but "Dx de" for a show/dx backlog reply
    if (strncasecmp (line, "dx de ", 6) != 0
                        || sscanf (line+6, "%11[^ :]: %f %11s", spot.rx_call, &spot.kHz, spot.tx_call) != 3) {
        // already logged
        return (false);
    }

    // looks good so far, reach over to extract time and perform strict sanity check
    int hr = 10*(line[70]-'0') + (line[71]-'0');
    int mn = 10*(line[72]-'0') + (line[73]-'0');
    if (!isdigit(line[70]) || !isdigit(line[71]) || !isdigit(line[72]) || !isdigit(line[73])
                        || hr < 0 || hr > 59 || mn < 0 || mn > 60) {
        dxcLog ("bogus time in spot: '%4.4s'\n", &line[70]);
        return (false);
    }

    // spot does not include date so assume same as current
    tmElements_t tm;
    time_t now = myNow();
    breakTime (now, tm);
    tm.Hour = hr;
    tm.Minute = mn;
    spot.spotted = makeTime (tm);

    // spot does not include mode so try to set based on freq
    quietStrncpy (spot.mode, findHamMode (spot.kHz), sizeof(spot.mode));

    // comment is whatever text sits between the callsign and the time field at
    // line[70]; scan it for a recognizable IOTA group reference, and separately for
    // any of the "extra" xOTA programs (WCA/ARLHS/ILLW/SIOTA/WAB/WWBOTA) -- see xota.h
    // for why that one needs its own, stricter, label-anchored scan
    findIOTARef (line, spot.iota, sizeof(spot.iota));
    findXOTARef (line, spot.xota_org, sizeof(spot.xota_org), spot.xota_ref, sizeof(spot.xota_ref));

    // accommodate future from roundoff
    if (spot.spotted > now) {
        dxcLog ("%s %g: correcting future time %02d:%02d %ld > %ld\n", spot.tx_call, spot.kHz,
                        tm.Hour, tm.Minute, (long)spot.spotted, (long)(spot.spotted-SECSPERDAY));
        spot.spotted -= SECSPERDAY;
    }

    // find locations and grids
    bool ok = call2LL (spot.rx_call, spot.rx_ll) && call2LL (spot.tx_call, spot.tx_ll);
    if (ok) {
        ll2maidenhead (spot.rx_grid, spot.rx_ll);
        ll2maidenhead (spot.tx_grid, spot.tx_ll);
    }

    // find DXCC
    if (!call2DXCC (spot.tx_call, spot.tx_dxcc)) {
        dxcLog ("no DXCC for %s\n", spot.tx_call);
        return (false);
    }
    if (!call2DXCC (spot.rx_call, spot.rx_dxcc)) {
        dxcLog ("no DXCC for %s\n", spot.rx_call);
        return (false);
    }

    // return whether we had any luck
    return (ok);
}



/*********************************************************************************************************
 *
 * WSJT-X
 *   https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/Network/NetworkMessage.hpp
 *   We don't enforce the Status ID to be WSJT-X so this may also work for, say, JTCluster.
 *
 *********************************************************************************************************/

/* given address of pointer into a WSJT-X message, extract bool and advance pointer to next field.
 */
static bool wsjtx_bool (uint8_t **bpp)
{
    bool x = **bpp > 0;
    *bpp += 1;
    return (x);
}

/* given address of pointer into a WSJT-X message, extract uint32_t and advance pointer to next field.
 * bytes are big-endian order.
 */
static uint32_t wsjtx_quint32 (uint8_t **bpp)
{
    uint32_t x = ((*bpp)[0] << 24) | ((*bpp)[1] << 16) | ((*bpp)[2] << 8) | (*bpp)[3];
    *bpp += 4;
    return (x);
}

/* given address of pointer into a WSJT-X message, extract utf8 string and advance pointer to next field.
 * N.B. returned string points into message so will only be valid as long as message memory is valid.
 */
static char *wsjtx_utf8 (uint8_t **bpp)
{
    // save begining of this packet entry
    uint8_t *bp0 = *bpp;

    // decode length
    uint32_t len = wsjtx_quint32 (bpp);

    // check for flag meaning null length string same as 0 for our purposes
    if (len == 0xffffffff)
        len = 0;

    // advance packet pointer over contents
    *bpp += len;

    // copy contents to front, overlaying length, to make room to add EOS
    memmove (bp0, bp0+4, len);
    bp0[len] = '\0';

    // dxcLog ("utf8 %d '%s'\n", len, (char*)bp0);

    // return address of content now within packet
    return ((char *)bp0);
}

/* given address of pointer into a WSJT-X message, extract double and advance pointer to next field.
 */
static uint64_t wsjtx_quint64 (uint8_t **bpp)
{
    uint64_t x;

    x = ((uint64_t)(wsjtx_quint32(bpp))) << 32;
    x |= wsjtx_quint32 (bpp);

    return (x);
}

/* return whether the given packet contains a WSJT-X Status packet.
 * if true, leave with *bpp positioned just after ID.
 */
bool wsjtxIsStatusMsg (uint8_t **bpp)
{

    // crack magic header
    uint32_t magic = wsjtx_quint32 (bpp);
    // dxcLog ("magic 0x%x\n", magic);                     // RBF
    if (magic != 0xADBCCBDA) {
        // dxcLog ("packet received but wrong magic\n");   // RBF
        return (false);
    }

    // crack and ignore the max schema value
    (void) wsjtx_quint32 (bpp);                         // skip past max schema

    // crack message type. we only care about Status messages which are type 1
    uint32_t msgtype = wsjtx_quint32 (bpp);
    // dxcLog ("type %d\n", msgtype);
    if (msgtype != 1)
        return (false);

    // if we get this far assume packet is what we want.
    // crack ID but ignore to allow compatibility with clones.
    volatile char *id = wsjtx_utf8 (bpp);
    (void)id;           // lint
    // dxcLog ("id '%s'\n", id);
    // if (strcmp ("WSJT-X", id) != 0)
        // return (false);

    // ok!
    return (true);
}

/* parse and process WSJT-X message known to be of type Status.
 * if ok fill in spot and return true else dxcLog and return false.
 */
bool wsjtxParseStatusMsg (uint8_t *msg, DXSpot &spot)
{
    // dxcLog ("Parsing status\n");
    uint8_t **bpp = &msg;                               // walk along msg

    // crack remaining fields down to grid
    uint32_t hz = wsjtx_quint64 (bpp);                  // capture freq
    (void) wsjtx_utf8 (bpp);                            // skip over mode
    char *dx_call = wsjtx_utf8 (bpp);                   // capture DX call 
    (void) wsjtx_utf8 (bpp);                            // skip over report
    (void) wsjtx_utf8 (bpp);                            // skip over Tx mode
    (void) wsjtx_bool (bpp);                            // skip over Tx enabled flag
    (void) wsjtx_bool (bpp);                            // skip over transmitting flag
    (void) wsjtx_bool (bpp);                            // skip over decoding flag
    (void) wsjtx_quint32 (bpp);                         // skip over Rx DF -- not always correct
    (void) wsjtx_quint32 (bpp);                         // skip over Tx DF
    char *de_call = wsjtx_utf8 (bpp);                   // capture DE call 
    char *de_grid = wsjtx_utf8 (bpp);                   // capture DE grid
    char *dx_grid = wsjtx_utf8 (bpp);                   // capture DX grid

    // dxcLog ("WSJT: %7d %s %s %s %s\n", hz, de_call, de_grid, dx_call, dx_grid);

    // ignore if frequency is clearly bogus (which I have seen)
    if (hz == 0) {
        dxcLog ("%s invalid frequency: %u\n", de_call, hz);
        return (false);
    }

    // get each ll from grids
    LatLong ll_de, ll_dx;
    if (!maidenhead2ll (ll_de, de_grid)) {
        dxcLog ("%s invalid or missing DE grid: %s\n", de_call, de_grid);
        return (false);
    }
    if (!maidenhead2ll (ll_dx, dx_grid)) {
        dxcLog ("%s invalid or missing DX grid: %s\n", dx_call, dx_grid);
        return (false);
    }

    // looks good, create new record
    spot = {};
    strncpy (spot.tx_call, dx_call, sizeof(spot.tx_call)-1);        // preserve EOS
    strncpy (spot.rx_call, de_call, sizeof(spot.rx_call)-1);        // preserve EOS
    strncpy (spot.tx_grid, dx_grid, sizeof(spot.tx_grid)-1);        // preserve EOS
    strncpy (spot.rx_grid, de_grid, sizeof(spot.rx_grid)-1);        // preserve EOS
    spot.kHz = hz*1e-3F;
    spot.rx_ll = ll_de;
    spot.tx_ll = ll_dx;

    // time is now
    spot.spotted = myNow();

    // good!
    return (true);
}


/*********************************************************************************************************
 *
 * simple XML utilities.
 *
 *********************************************************************************************************/

/* find <key>content</key> within xml[].
 * return whether successful.
 */
static bool extractXMLElementContent (const char xml[], const char key[], char content[], size_t c_len)
{
    enum {
        XMLS_LOOK4_KEY,
        XMLS_IN_KEY,
        XMLS_IN_CONTENT,
    } state = XMLS_LOOK4_KEY;
    const char *key_candidate = NULL;
    const char *key_content = NULL;
    const size_t klen = strlen(key);

    for (; *xml != '\0'; xml++) {
        switch (state) {
        case XMLS_LOOK4_KEY:
            if (*xml == '<') {
                key_candidate = NULL;
                state = XMLS_IN_KEY;
            }
            break;
        case XMLS_IN_KEY:
            if (key_candidate == NULL)
                key_candidate = xml;     // first char
            if (*xml == '>') {
                if (strncasecmp (key, key_candidate, klen) == 0) {
                    key_content = NULL;
                    state = XMLS_IN_CONTENT;
                } else {
                    state = XMLS_LOOK4_KEY;
                }
            }
            break;
        case XMLS_IN_CONTENT:
            if (key_content == NULL)
                key_content = xml;       // first char
            if (*xml == '<') {
                int con_len = xml - key_content;
                snprintf (content, c_len, "%.*s", con_len, key_content);
                return (true);
            }
            break;
        }
    }

    return (false);
}



/*********************************************************************************************************
 *
 * XML such as N1MM or DXLog
 *   https://n1mmwp.hamdocs.com/appendices/external-udp-broadcasts/
 *   https://dxlog.net/docs/index.php/Additional_Information#Spot
 *
 *********************************************************************************************************/

bool crackXMLSpot (const char xml[], DXSpot &spot)
{
    // fresh
    spot = {};

    char buf[100];
    char *endptr;

    // must be a <spot> record action add
    bool have_spot = strcistr (xml,"<spot>") != NULL;           // not always closed??
    bool action_add = extractXMLElementContent (xml, "action", buf, sizeof(buf)) && !strcasecmp (buf, "add");
    if (!have_spot || !action_add)
        return (false);

    if (!extractXMLElementContent (xml, "spottercall", spot.rx_call, sizeof(spot.rx_call))) {
        dxcLog ("UDP: no spottercall\n");
        return (false);
    }

    if (!extractXMLElementContent (xml, "dxcall", spot.tx_call, sizeof(spot.tx_call))) {
        dxcLog ("UDP: no dxcall\n");
        return (false);
    }

    if (!extractXMLElementContent (xml, "frequency", buf, sizeof(buf))) {
        dxcLog ("UDP: no frequency for %s\n", spot.tx_call);
        return (false);
    }
    if ( ((spot.kHz = strtod (buf, &endptr)) == 0 && endptr == buf) || findHamBand(spot.kHz) == HAMBAND_NONE){
        dxcLog ("UDP: bogus frequency %s for %s\n", buf, spot.tx_call);
        return (false);
    }

    if (!extractXMLElementContent (xml, "mode", spot.mode, sizeof(spot.mode))) {
        quietStrncpy (spot.mode, findHamMode (spot.kHz), sizeof(spot.mode));
        dxcLog ("UDP: inferring mode for %s %s\n", spot.tx_call, spot.mode);
    }

    if (!extractXMLElementContent (xml, "timestamp", buf, sizeof(buf))) {
        dxcLog ("UDP: no timestamp for %s\n", spot.tx_call);
        return (false);
    }
    if ((spot.spotted = crackISO8601 (buf)) == 0) {
        dxcLog ("UDP: bogus timestamp %s for %s\n", buf, spot.tx_call);
        return (false);
    }


    if (!call2LL (spot.tx_call, spot.tx_ll)) {
        dxcLog ("UDP: could not find LL for %s\n", spot.tx_call);
        return (false);
    }
    ll2maidenhead (spot.tx_grid, spot.tx_ll);

    if (!call2DXCC (spot.tx_call, spot.tx_dxcc)) {
        dxcLog ("UDP: no DXCC for %s\n", spot.tx_call);
        return (false);
    }


    if (!call2LL (spot.rx_call, spot.rx_ll)) {
        dxcLog ("UDP: could find LL for %s\n", spot.rx_call);
        return (false);
    }
    ll2maidenhead (spot.rx_grid, spot.rx_ll);

    if (!call2DXCC (spot.rx_call, spot.rx_dxcc)) {
        dxcLog ("UDP: no DXCC for %s\n", spot.rx_call);
        return (false);
    }


    // yes!
    return (true);
}



/*********************************************************************************************************
 *
 * ADIF such as Log4OM
 *   https://www.log4om.com/l4ong/usermanual/Log4OMNG_ENU.pdf
 *
 *********************************************************************************************************/

/* crack the given string presumed to contain and ADIF message.
 * if ok fill in spot and return true else dxcLog and return false.
 */
bool crackADIFSpot (const char string[], DXSpot &spot)
{
    GenReader gr(string, strlen (string));
    DXSpot *spots = NULL;
    int n_bad;
    int n_good = readADIFFile (gr, spots, false, n_bad);
    bool ok = false;
    if (n_good > 0) {
        if (n_good > 1)
            dxcLog ("UDP: using only first of %d ADIF records\n", n_good);
        spot = spots[0];
        ok = true;
    } else {
        if (n_bad > 0)
            dxcLog ("UDP: packet contained %d bad ADIF records\n", n_bad);
    }
    if (spots)
        free (spots);
    return (ok);
}

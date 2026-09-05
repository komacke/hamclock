/* MOTD (Message of the Day) support.
 *
 * Periodically polls <backend_host>/ham/HamClock/motd. When the file is present,
 * displays a small white mailbox icon at the top of the map. Clicking the icon
 * pops up a modal with the message text and an OK button. The popup closes when
 * the user clicks OK (or anywhere in the popup) or after a timeout.
 *
 * When the file goes missing on the server, the icon is removed at the next poll.
 */

#include "HamClock.h"
#include <ctype.h>

// poll interval in seconds (how often we ask the server)
#define MOTD_POLL_INTERVAL      (5*60)

// popup geometry and behavior
//
// N.B. this used to hand-roll its own popup box (fixed size/position, custom draw, custom
// backing-store save/restore, custom OK button, custom input loop). That duplicated the
// generic modal dialog machinery in menu.cpp (MenuItem/MenuInfo/runMenu()), which every
// other confirmation/settings dialog in HamClock already uses -- including the near-identical
// "Open webpage?" confirmation in activenets.cpp. This version builds one MenuItem per
// wrapped line and calls runMenu(), which owns sizing, positioning, backing-store, the Ok
// button, and the 30s timeout (MENU_TO in HamClock.h) centrally.
#define MOTD_WRAP_W              400             // word-wrap target width, pixels (not a fixed box size)
#define MOTD_FONTW               6               // FAST_FONT character width, pixels
#define MOTD_MAXLINES            24              // max lines we'll render
#define MOTD_MAX_BYTES           8192            // max bytes we'll keep from the server
#define MOTD_MAX_URLS            4               // most http(s):// links we'll offer as "Open link" rows
#define MOTD_INDENT              2               // row indent, pixels -- matches other small menus

// filename to persist the hash of the last read message
#define MOTD_HASH_FN            "motd_hash.txt"

// icon geometry: small mailbox in the upper-right of clock_b, just left of the UTC button
#define MOTD_ICON_W             14
#define MOTD_ICON_H             14
#define MOTD_ICON_GAP           6               // gap between icon and UTC button

// clickable box for the icon (also where it gets drawn)
SBox motd_btn_b;

// module state
static char *motd_text;                         // malloced body, or NULL if no MOTD
static time_t motd_next_poll;                   // when to next check the server
static bool motd_is_new;                        // whether MOTD is new/unread


/* free any cached text and forget the MOTD
 */
static void motdForget()
{
    if (motd_text) {
        free (motd_text);
        motd_text = NULL;
    }
    motd_is_new = false;
}


/* fetch /ham/HamClock/motd. set motd_text on success (200), clear it on 404 or other failure.
 * caller is responsible for noticing state changes.
 */
static void motdFetch()
{
    WiFiClient client;

    Serial.printf ("MOTD: GET http://%s:%d/ham/HamClock/sysmsg.pl\n", backend_host, backend_port);

    if (!client.connect (backend_host, backend_port)) {
        Serial.printf ("MOTD: connect %s:%d failed\n", backend_host, backend_port);
        // network failure -- leave previous state alone
        return;
    }

    // GET /ham/HamClock/sysmsg.pl
    httpHCGET (client, backend_host, "/sysmsg.pl");

    // peek at the status line before consuming the header
    char status_line[200];
    if (!getTCPLine (client, status_line, sizeof(status_line), NULL)) {
        Serial.printf ("MOTD: no response line from %s\n", backend_host);
        client.stop();
        return;
    }

    Serial.printf ("MOTD: status: %s\n", status_line);

    // simple status check -- accept "HTTP/x.y 200 ..." as present, anything else as absent
    bool got_ok = (strstr (status_line, " 200 ") != NULL);

    if (!got_ok) {
        // file is missing or some other server-side error -- treat as no MOTD
        motdForget();
        client.stop();
        return;
    }

    // consume remaining headers
    if (!httpSkipHeader (client)) {
        Serial.printf ("MOTD: short header\n");
        client.stop();
        return;
    }

    // read body into a growing buffer, bounded by MOTD_MAX_BYTES
    char *buf = (char *) malloc (MOTD_MAX_BYTES + 1);
    if (!buf) {
        Serial.printf ("MOTD: out of memory\n");
        client.stop();
        return;
    }
    int used = 0;
    char line[512];
    while (getTCPLine (client, line, sizeof(line), NULL)) {
        int need = strlen(line) + 1;  // +1 for the newline we insert
        if (used + need >= MOTD_MAX_BYTES)
            break;
        memcpy (buf + used, line, need - 1);
        used += need - 1;
        buf[used++] = '\n';
    }
    buf[used] = '\0';
    client.stop();

    // trim trailing whitespace
    while (used > 0 && (buf[used-1] == '\n' || buf[used-1] == ' ' || buf[used-1] == '\t')) {
        buf[--used] = '\0';
    }

    if (used == 0) {
        // empty body -- treat as no MOTD
        free (buf);
        motdForget();
        return;
    }

    // install if changed
    if (motd_text == NULL || strcmp (motd_text, buf) != 0) {
        motdForget();
        motd_text = buf;

        // determine if this is really new to the user by comparing hash with persistent record
        uint32_t new_hash = stringHash (motd_text);
        uint32_t read_hash = 0;
        FILE *h_fp = fopenOurs (MOTD_HASH_FN, "r");
        if (h_fp) {
            if (fscanf (h_fp, "%u", &read_hash) != 1)
                read_hash = 0;
            fclose (h_fp);
        }
        motd_is_new = (new_hash != read_hash);

        if (motd_is_new)
            Serial.printf ("MOTD: stored %d bytes (new content), is_new=%d\n", used, (int)motd_is_new);
    } else {
        // same content as before
        free (buf);
    }
}


/* check the backend periodically. called from the main poll loop.
 * also called once at startup to do an initial fetch.
 */
void checkMOTD()
{
    if (myNow() < motd_next_poll)
        return;

    bool had_motd = (motd_text != NULL);
    bool was_new = motd_is_new;
    motdFetch();
    bool have_motd = (motd_text != NULL);
    bool is_new = motd_is_new;

    // if the present/absent state changed, or it is new content, redraw the icon
    // without waiting for some other event to trigger a clock-area redraw
    if (had_motd != have_motd || (is_new && !was_new)) {
        Serial.printf ("MOTD: state or novelty changed, redrawing icon\n");
        drawMOTDIcon();
    }

    motd_next_poll = myNow() + MOTD_POLL_INTERVAL;
}


/* return whether there is a MOTD to show
 */
bool motdIsPresent()
{
    return (motd_text != NULL);
}


/* draw the mailbox icon. should be called whenever the UTC button is drawn so
 * the icon stays fresh alongside it.
 *
 * The icon goes immediately to the left of the UTC button, occupying a slot of
 * the same width and height. If there is no MOTD on the server, the slot is
 * blanked so any stale icon is erased.
 */
void drawMOTDIcon()
{
    // mirror the UTC button geometry: width = MOTD_ICON_W, height = UTC_H
    // (see drawUTCButton() in clocks.cpp for reference).
    motd_btn_b.x = clock_b.x + clock_b.w - 2*MOTD_ICON_W - MOTD_ICON_GAP;
    motd_btn_b.y = clock_b.y;
    motd_btn_b.w = MOTD_ICON_W;
    motd_btn_b.h = MOTD_ICON_H;

    // always blank the slot first
    tft.fillRect (motd_btn_b.x, motd_btn_b.y, motd_btn_b.w, motd_btn_b.h, RA8875_BLACK);

    // red if unread MOTD is present; gray if read or if no MOTD
    uint16_t icon_col = (motdIsPresent() && motd_is_new) ? RA8875_RED : GRAY;

    // a simple envelope shape: a rectangle for the body with a "V" flap.
    const uint16_t bx = motd_btn_b.x;
    const uint16_t by = motd_btn_b.y;

    // envelope body: 12 px wide, 8 px tall
    const uint16_t env_x = bx + 1;
    const uint16_t env_y = by + 3;
    const uint16_t env_w = 12;
    const uint16_t env_h = 8;

    // outline
    tft.drawRect (env_x, env_y, env_w, env_h, icon_col);

    // flap
    tft.drawLine (env_x, env_y, env_x + env_w/2, env_y + env_h/2, icon_col);
    tft.drawLine (env_x + env_w - 1, env_y, env_x + env_w/2, env_y + env_h/2, icon_col);
}


/* render the MOTD text as rows in the shared modal dialog engine (menu.cpp). Any detected
 * http(s):// URL becomes its own "Open link" toggle row -- check it and press Ok to open it
 * via openURL(), same mechanism used throughout the rest of HamClock (a new browser tab if
 * this came from a live-web touch, else the local system browser). runMenu() owns sizing,
 * positioning, backing-store save/restore, and the timeout (MENU_TO, currently 30s).
 */
static void motdShowPopup()
{
    const char *text = (motd_text && motd_text[0]) ? motd_text : "No active messages at this time.";

    // save and restore font around our work
    FontWeight saved_fw;
    FontSize saved_fs;
    getFontStyle (&saved_fw, &saved_fs);
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // break the message into lines: existing newlines first, then word-wrap any line that's
    // too wide. Each finished line becomes one MENU_LABEL row below, so it must be its own
    // NUL-terminated string (MenuItem.label is const char *, not a length-bounded slice).
    const int max_chars_per_line = (MOTD_WRAP_W - 2*MOTD_INDENT) / MOTD_FONTW;
    static char line_bufs[MOTD_MAXLINES][MOTD_WRAP_W/MOTD_FONTW + 1];
    int n_lines = 0;

    const char *p = text;
    while (*p && n_lines < MOTD_MAXLINES) {
        // find end of this source line
        const char *eol = strchr (p, '\n');
        const char *line_end = eol ? eol : p + strlen(p);
        int line_len = line_end - p;

        // word-wrap if too wide
        while (line_len > max_chars_per_line && n_lines < MOTD_MAXLINES) {
            // find rightmost space within the limit
            int brk = max_chars_per_line;
            while (brk > 0 && p[brk] != ' ')
                brk--;
            if (brk == 0)
                brk = max_chars_per_line;  // no space found -- hard break
            snprintf (line_bufs[n_lines], sizeof(line_bufs[0]), "%.*s", brk, p);
            n_lines++;
            p += brk;
            line_len -= brk;
            // skip the breaking space
            if (*p == ' ') {
                p++;
                line_len--;
            }
        }

        if (n_lines < MOTD_MAXLINES) {
            snprintf (line_bufs[n_lines], sizeof(line_bufs[0]), "%.*s", line_len, p);
            n_lines++;
        }

        p = eol ? eol + 1 : line_end;
    }

    if (n_lines == 0) {
        selectFontStyle (saved_fw, saved_fs);
        return;
    }

    // find up to MOTD_MAX_URLS http(s):// links among the wrapped lines. Each becomes its
    // own toggle row below the message text, same idiom as e.g. the "Show web page" toggle
    // in contests.cpp: check it, press Ok, and we open it afterward.
    static char link_urls[MOTD_MAX_URLS][300];
    int n_links = 0;
    for (int i = 0; i < n_lines && n_links < MOTD_MAX_URLS; i++) {
        char *ls = line_bufs[i];
        int ll = strlen (ls);
        for (int c = 0; c < ll && n_links < MOTD_MAX_URLS; c++) {
            bool is_http  = (ll - c >= 7 && strncmp (ls+c, "http://", 7) == 0);
            bool is_https = (ll - c >= 8 && strncmp (ls+c, "https://", 8) == 0);
            if (is_http || is_https) {
                int ulen = 0;
                while (c+ulen < ll && !isspace((unsigned char)ls[c+ulen]))
                    ulen++;

                // trim trailing sentence punctuation that isn't really part of the URL,
                // e.g. the period ending "...check status at https://example.com/status."
                // -- otherwise it gets swept in since it's not whitespace.
                while (ulen > 0 && strchr (".,;:!?)]}'\"", ls[c+ulen-1]))
                    ulen--;

                // degenerate: trimming left nothing but the scheme itself (or less) --
                // not a usable link, skip it rather than register a bogus entry.
                int scheme_len = is_https ? 8 : 7;
                if (ulen <= scheme_len) {
                    c += ulen > 0 ? ulen - 1 : 0;
                    continue;
                }

                snprintf (link_urls[n_links], sizeof(link_urls[0]), "%.*s", ulen, ls+c);
                n_links++;
                c += ulen - 1;              // skip past what we just matched
            }
        }
    }

    // build a self-explanatory caption for each link's toggle row instead of just
    // repeating the raw URL text -- it already appears once in the message above,
    // so showing it again unlabeled just looks like a stray duplicate rather than
    // an action. This also makes the check-box-then-Ok action obvious at a glance.
    static char link_labels[MOTD_MAX_URLS][40];
    for (int i = 0; i < n_links; i++) {
        if (n_links == 1)
            snprintf (link_labels[i], sizeof(link_labels[0]), "Open link in browser");
        else
            snprintf (link_labels[i], sizeof(link_labels[0]), "Open link #%d in browser", i+1);
    }

    // build one insensitive label row per wrapped line, a blank separator if there are
    // any links, then one toggle row per detected link
    const int n_items = n_lines + (n_links > 0 ? 1 : 0) + n_links;
    static MenuItem mitems[MOTD_MAXLINES + 1 + MOTD_MAX_URLS];
    int mi = 0;
    for (int i = 0; i < n_lines; i++)
        mitems[mi++] = {MENU_LABEL, false, 0, MOTD_INDENT, line_bufs[i], NULL};
    if (n_links > 0)
        mitems[mi++] = {MENU_BLANK, false, 0, 0, NULL, NULL};
    const int link_item0 = mi;
    for (int i = 0; i < n_links; i++)
        mitems[mi++] = {MENU_TOGGLE, false, (uint8_t)(1+i), MOTD_INDENT, link_labels[i], NULL};

    // let runMenu() pick the exact size; just seed a reasonable starting position
    SBox menu_b;
    // Centered on the map (not the whole screen) so this reliably overlaps map_b --
    // that's what makes runMenu()'s boxesOverlap(menu_b, map_b) -> tft.drawPR() check
    // fire, a synchronous "flush and wait" that our previous side-panel position
    // likely never triggered at all. runMenu() still clamps back on-screen if needed.
    menu_b.x = map_b.x + (map_b.w - MOTD_WRAP_W) / 2;
    menu_b.y = map_b.y + map_b.h / 4;
    menu_b.w = 0;
    menu_b.h = 0;
    SBox ok_b;

    // Back to UF_CLOCKSOK: now that the popup is centered on the map (see menu_b setup
    // above) rather than the screen corner where the clock pane lives, they shouldn't
    // overlap, so there's no need to freeze the clock while this is up.
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_NOCANCEL, 1, n_items, mitems};
    if (runMenu (menu)) {
        for (int i = 0; i < n_links; i++) {
            if (mitems[link_item0+i].set) {
                Serial.printf ("MOTD: opening link %s\n", link_urls[i]);
                openURL (link_urls[i]);
            }
        }
    }

    selectFontStyle (saved_fw, saved_fs);
}



/* called by the central click dispatcher when the user taps the mailbox icon.
 */
void motdClicked()
{
    Serial.printf ("MOTD: motdClicked() entered, motdIsPresent=%d motd_text=%p\n",
                   motdIsPresent(), (void*)motd_text);

    // if was new, record hash as having been read
    if (motd_is_new && motd_text) {
        uint32_t current_hash = stringHash (motd_text);
        FILE *h_fp = fopenOurs (MOTD_HASH_FN, "w");
        if (h_fp) {
            fprintf (h_fp, "%u\n", current_hash);
            fclose (h_fp);
            Serial.printf ("MOTD: wrote hash %u to %s\n", current_hash, MOTD_HASH_FN);
        }
        motd_is_new = false;
    }

    drawMOTDIcon();

    Serial.printf ("MOTD: calling motdShowPopup() with %lu bytes of text\n",
                   (unsigned long)(motd_text ? strlen(motd_text) : 0));
    motdShowPopup();
    Serial.printf ("MOTD: motdShowPopup() returned\n");
}

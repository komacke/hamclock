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

// poll interval in seconds (how often we ask the server)
#define MOTD_POLL_INTERVAL      (5*60)

// popup geometry and behavior
#define MOTD_POPUP_W            400             // popup width, pixels
#define MOTD_POPUP_TIMEOUT      (30*1000)       // auto-close after this many ms
#define MOTD_BORDER             6               // popup border, pixels
#define MOTD_ROWH               14              // text row height, pixels
#define MOTD_FONTW              6               // FAST_FONT character width, pixels
#define MOTD_MAXLINES           24              // max lines we'll render
#define MOTD_MAX_BYTES          8192            // max bytes we'll keep from the server
#define MOTD_BG                 RGB565(40,40,200)  // popup background color
#define MOTD_FG                 RA8875_WHITE       // popup text and border color
#define MOTD_OK_W               40              // OK button width, pixels
#define MOTD_OK_H               16              // OK button height, pixels

// icon geometry: small mailbox in the upper-right of clock_b, just left of the UTC button
#define MOTD_ICON_W             14
#define MOTD_ICON_H             14
#define MOTD_ICON_GAP           2               // gap between icon and UTC button

// clickable box for the icon (also where it gets drawn)
SBox motd_btn_b;

// module state
static char *motd_text;                         // malloced body, or NULL if no MOTD
static time_t motd_next_poll;                   // when to next check the server


/* free any cached text and forget the MOTD
 */
static void motdForget()
{
    if (motd_text) {
        free (motd_text);
        motd_text = NULL;
    }
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

    // install
    motdForget();
    motd_text = buf;

    Serial.printf ("MOTD: stored %d bytes\n", used);
}


/* check the backend periodically. called from the main poll loop.
 * also called once at startup to do an initial fetch.
 */
void checkMOTD()
{
    if (myNow() < motd_next_poll)
        return;

    bool had_motd = (motd_text != NULL);
    motdFetch();
    bool have_motd = (motd_text != NULL);

    // if the present/absent state changed, redraw the icon so it appears or disappears
    // without waiting for some other event to trigger a clock-area redraw
    if (had_motd != have_motd) {
        Serial.printf ("MOTD: state changed, now %s -- redrawing icon\n",
                       have_motd ? "present" : "absent");
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

    // always blank the slot first so a stale icon is erased when the MOTD disappears
    tft.fillRect (motd_btn_b.x, motd_btn_b.y, motd_btn_b.w, motd_btn_b.h, RA8875_BLACK);

    if (!motdIsPresent())
        return;

    // a simple mailbox shape: a rectangle for the body, with an arched top suggested
    // by stair-step pixels at the corners, plus a vertical flag on the right side and
    // a small horizontal slot to indicate the door.
    const uint16_t bx = motd_btn_b.x;
    const uint16_t by = motd_btn_b.y;
    const uint16_t bw = motd_btn_b.w;

    // mailbox body: 10 px wide, 9 px tall, leaving room for flag on right
    const uint16_t body_x = bx + 1;
    const uint16_t body_y = by + 3;
    const uint16_t body_w = 9;
    const uint16_t body_h = 9;
    // outline
    tft.drawLine (body_x,           body_y+2,             body_x,           body_y+body_h-1,    MOTD_FG);  // left
    tft.drawLine (body_x+body_w-1,  body_y+2,             body_x+body_w-1,  body_y+body_h-1,    MOTD_FG);  // right
    tft.drawLine (body_x,           body_y+body_h-1,      body_x+body_w-1,  body_y+body_h-1,    MOTD_FG);  // bottom
    // arched top: a couple of stair-step pixels to suggest curvature
    tft.drawPixel (body_x+1,        body_y+1,             MOTD_FG);
    tft.drawPixel (body_x+2,        body_y,               MOTD_FG);
    tft.drawLine  (body_x+3,        body_y,               body_x+body_w-3,  body_y,             MOTD_FG);
    tft.drawPixel (body_x+body_w-3, body_y,               MOTD_FG);
    tft.drawPixel (body_x+body_w-2, body_y+1,             MOTD_FG);

    // door slot: short horizontal line near the middle
    tft.drawLine (body_x+2,         body_y+body_h-3,      body_x+body_w-3,  body_y+body_h-3,    MOTD_FG);

    // flag: a tall thin rectangle on the right side, raised to indicate new mail
    const uint16_t flag_x = bx + bw - 2;
    const uint16_t flag_y = by;
    tft.drawLine (flag_x,           flag_y,               flag_x,           flag_y+5,           MOTD_FG);  // pole
    tft.fillRect (flag_x-2,         flag_y,               2,                3,                  MOTD_FG);  // flag
}


/* render the MOTD text into a popup box. wait for the user to click anywhere in
 * the popup or for the timeout to elapse, then restore the underlying pixels.
 */
static void motdShowPopup()
{
    if (!motd_text)
        return;

    // save and restore font around our work
    FontWeight saved_fw;
    FontSize saved_fs;
    getFontStyle (&saved_fw, &saved_fs);

    // break the message into lines. break on existing newlines first, then word-wrap any
    // line that is too wide to fit in the popup.
    const int max_chars_per_line = (MOTD_POPUP_W - 2*MOTD_BORDER) / MOTD_FONTW;
    typedef struct {
        const char *start;
        int len;
    } LineRef;
    LineRef lines[MOTD_MAXLINES];
    int n_lines = 0;

    const char *p = motd_text;
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
            lines[n_lines].start = p;
            lines[n_lines].len = brk;
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
            lines[n_lines].start = p;
            lines[n_lines].len = line_len;
            n_lines++;
        }

        p = eol ? eol + 1 : line_end;
    }

    if (n_lines == 0) {
        selectFontStyle (saved_fw, saved_fs);
        return;
    }

    // compute popup box size and position (centered on screen)
    SBox popup_b;
    popup_b.w = MOTD_POPUP_W;
    popup_b.h = 2*MOTD_BORDER + n_lines*MOTD_ROWH + MOTD_BORDER + MOTD_OK_H + MOTD_BORDER;
    popup_b.x = (tft.width()  - popup_b.w) / 2;
    popup_b.y = (tft.height() - popup_b.h) / 2;

    // save what was on screen underneath
    uint8_t *backing_store = NULL;
    if (!tft.getBackingStore (backing_store, popup_b.x, popup_b.y, popup_b.w, popup_b.h)) {
        Serial.printf ("MOTD: failed to capture pixels for popup\n");
        selectFontStyle (saved_fw, saved_fs);
        return;
    }

    // draw popup background + border
    fillSBox (popup_b, MOTD_BG);
    drawSBox (popup_b, MOTD_FG);

    // draw text
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (MOTD_FG);
    uint16_t text_x = popup_b.x + MOTD_BORDER;
    uint16_t text_y = popup_b.y + MOTD_BORDER;
    for (int i = 0; i < n_lines; i++) {
        tft.setCursor (text_x, text_y);
        tft.printf ("%.*s", lines[i].len, lines[i].start);
        text_y += MOTD_ROWH;
    }

    // draw OK button centered horizontally at the bottom of the popup
    SBox ok_b;
    ok_b.w = MOTD_OK_W;
    ok_b.h = MOTD_OK_H;
    ok_b.x = popup_b.x + (popup_b.w - ok_b.w) / 2;
    ok_b.y = popup_b.y + popup_b.h - MOTD_BORDER - ok_b.h;
    fillSBox (ok_b, RA8875_BLACK);
    drawSBox (ok_b, MOTD_FG);
    static const char ok_str[] = "OK";
    uint16_t ok_str_w = getTextWidth (ok_str);
    tft.setCursor (ok_b.x + (ok_b.w - ok_str_w)/2, ok_b.y + (ok_b.h - 7)/2);
    tft.print (ok_str);

    // wait for any click in the popup, or timeout. inbox is the whole popup so a click
    // anywhere (including the OK button) will close. Tooltip uses the same idiom.
    UserInput ui = {
        popup_b,
        UI_UFuncNone,
        UF_UNUSED,
        MOTD_POPUP_TIMEOUT,
        UF_CLOCKSOK,
        {0,0}, TT_NONE, '\0', false, false
    };
    (void) waitForUser (ui);
    drainTouch();

    // restore screen contents underneath the popup
    if (!tft.setBackingStore (backing_store, popup_b.x, popup_b.y, popup_b.w, popup_b.h))
        Serial.printf ("MOTD: failed to restore pixels beneath popup\n");

    selectFontStyle (saved_fw, saved_fs);
}


/* called by the central click dispatcher when the user taps the mailbox icon.
 */
void motdClicked()
{
    Serial.printf ("MOTD: motdClicked() entered, motdIsPresent=%d motd_text=%p\n",
                   motdIsPresent(), (void*)motd_text);
    if (!motdIsPresent())
        return;
    Serial.printf ("MOTD: calling motdShowPopup() with %lu bytes of text\n",
                   (unsigned long)strlen(motd_text));
    motdShowPopup();
    Serial.printf ("MOTD: motdShowPopup() returned\n");
}

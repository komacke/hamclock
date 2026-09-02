/* simple means to ask for a password for a given category, set by -p
 */

#include "HamClock.h"

// global means to skip password, eg, for restful and demo commands.
bool bypass_pw;

// layout
#define FW      21                      // widest char width
#define FH      35                      // char height
#define TC      RA8875_WHITE            // title color
#define VC      RA8875_WHITE            // character color
#define CC      RA8875_GREEN            // active cursor color
#define UC      GRAY                    // idle cursor color
#define CG      4                       // cursor gap
#define HM      50                      // horizontal margin on each end of password
#define CD      9                       // cursor descent
#define TO      30000                   // timeout, millis
#define EYE_W   44                      // eye icon width
#define EYE_H   FH                      // eye icon height
#define EYE_GAP 12                      // gap between password text and eye icon
#define NP      ((800 - 2*HM - EYE_W - EYE_GAP)/FW) // max pw length, sans EOS
#define TY      (480/4)                 // title baseline y
#define PY      (2*480/3)               // password baseline y
#define NT      3                       // n tries

#define EYE_X   (HM + NP*FW + EYE_GAP)  // eye icon x placed right after password field
#define EYE_Y   (PY - FH + CD)          // eye icon y
#define HS_V    '*'                     // pw char when hiding

#define MX      200                     // message Y
#define MY      400                     // message Y
#define MW      (800-2*MX)              // message width
#define MC      RA8875_RED              // message color

/* draw c at position p
 */
static void drawChar (int p, char c)
{
    // find c width in order to center
    char s[2] = {c, '\0'};
    uint16_t c_w = getTextWidth(s);

    tft.setCursor (HM + p*FW + (FW - c_w)/2, PY);
    tft.setTextColor (VC);
    tft.print (c);
}

/* erase c at position p
 */
static void eraseChar (int p)
{
    tft.fillRect (HM + p*FW, PY - FH + CD, FW+1, FH, RA8875_BLACK);
}

/* draw an active cursor at position p
 */
static void drawActiveCursor (int p)
{
    uint16_t x0 = HM + p*FW;
    tft.drawLine (x0 + CG/2, PY + CD, x0 + FW - CG/2, PY + CD, CC);
}

/* draw an inactive cursor at position p
 */
static void drawIdleCursor (int p)
{
    uint16_t x0 = HM + p*FW;
    tft.drawLine (x0 + CG/2, PY + CD, x0 + FW - CG/2, PY + CD, UC);
}

/* erase cursor at position p
 */
static void eraseCursor (int p)
{
    uint16_t x0 = HM + p*FW;
    tft.drawLine (x0 + CG/2, PY + CD, x0 + FW - CG/2, PY + CD, RA8875_BLACK);
}

/* insert c at cursor position p in buf; increment len and p; show c or HS_V depending on hide.
 * N.B. we make no sanity checks
 */
static void insertChar (char buf[], int &len, int &p, bool hide, char c)
{
    // shift right to make room
    drawIdleCursor (p);
    for (int i = len; --i >= p; ) {
        buf[i+1] = buf[i];
        drawChar (i+1, hide ? HS_V : buf[i+1]);
        eraseChar (i);
    }

    // insert c at p
    buf[p] = c;
    drawChar (p, hide ? HS_V : c);

    // cursor moves right and buf is now 1 longer
    drawActiveCursor (++p);
    len += 1;
}

/* delete character at cursor position p-1 in buf; decrement len and p; show buf or HS_V depending on hide.
 * N.B. we make no sanity checks
 */
static void deleteChar (char buf[], int &len, int &p, bool hide)
{
    // don't leave a danging cursor at the far right
    if (p == NP)
        eraseCursor (p);
    else
        drawIdleCursor (p);

    // shift left to remove p-1
    for (int i = p-1; i < len-1; i++) {
        buf[i] = buf[i+1];
        eraseChar (i);
        drawChar (i, hide ? HS_V : buf[i]);
    }

    // p moves left and buf is now 1 shorter
    eraseChar (--len);
    drawActiveCursor (--p);
}



/* ask for a password for the given category.
 * return true if no such category, restful or password matches.
 */
bool askPasswd (const char *category, bool restore)
{
    // free pass if no such category or running a restful command
    if (!testPassword (category, NULL) || bypass_pw)
        return (true);

    // use entire screen. N.B. requires us to call tft.drawPR() to update
    eraseScreen();

    // draw title
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    char title[100];
    snprintf (title, sizeof(title), "Type password for %s:", category);
    uint16_t t_w = getTextWidth(title);
    uint16_t t_x = (tft.width() - t_w)/2;
    tft.setTextColor (TC);
    tft.setCursor (t_x, TY);
    tft.print (title);

    // set font for all subsequent text
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // draw instructions
    const char info[] = "Enter to accept, ESC to cancel, TAB to show/hide";
    uint16_t info_w = getTextWidth(info);
    uint16_t info_x = (tft.width() - info_w)/2;
    tft.setCursor (info_x, TY+FH);
    tft.print (info);

    // hide/show control
    SBox eye_b = {EYE_X, EYE_Y, EYE_W, EYE_H};
    bool hide = true;
    drawEyeIcon (eye_b, hide);

    // candidate password and initial cursor position. N.B. pw_buf does not NOT include EOS
    char pw_buf[NP+1];
    int cur_pos = 0;
    int pw_len = 0;

    // draw each cursor position, first one active
    drawActiveCursor (0);
    for (int i = 1; i < NP; i++)
        drawIdleCursor (i);

    // now user has up to NT tries to succeed or cancel.
    bool cancelled = false;
    bool pw_ok = false;
    for (int try_i = 1; !cancelled && !pw_ok && try_i <= NT; try_i++) {

        // show try number
        if (try_i > 1) {
            char try_buf[50];
            if (try_i == NT)
                snprintf (try_buf, sizeof(try_buf), "Incorrect. Last try.");
            else
                snprintf (try_buf, sizeof(try_buf), "Incorrect. Try %d of %d.", try_i, NT);
            tft.fillRect (MX, MY-FH, MW, FH+CD, RA8875_BLACK);
            tft.setCursor (MX, MY);
            tft.setTextColor (MC);
            tft.print (try_buf);
        }

        // configure waitForUser()
        bool enter = false;
        SBox all = {0, 0, tft.width(), tft.height()};   // full screen
        UserInput ui = { all, UI_UFuncNone, UF_UNUSED, TO, UF_NOCLOCKS,
                            {0, 0}, TT_NONE, '\0', false, false};

        // run until enter or cancel
        do {

            // reset
            enter = false;
            cancelled = false;

            // wait for user to do something or time out
            if (!waitForUser(ui)) {
                Serial.print ("PW: timed out\n");
                cancelled = true;
                continue;
            }

            if (ui.kb_char != CHAR_NONE) {

                switch (ui.kb_char) {

                case CHAR_NL:           // fallthru
                case CHAR_CR:
                    enter = true;       // finished with this attempt
                    break;

                case CHAR_ESC:
                    cancelled = true;   // bale out
                    break;

                case CHAR_BS:           // fallthru
                case CHAR_DEL:
                    if (cur_pos > 0)
                        deleteChar (pw_buf, pw_len, cur_pos, hide);
                    break;

                case CHAR_TAB:
                    // handled below
                    break;

                case CHAR_LEFT:
                    if (cur_pos > 0) {
                        // don't leave a danging cursor at the far right
                        if (cur_pos == NP)
                            eraseCursor (cur_pos);
                        else
                            drawIdleCursor (cur_pos);
                        drawActiveCursor (--cur_pos);
                    }
                    break;

                case CHAR_RIGHT:
                    if (cur_pos < pw_len) {
                        drawIdleCursor (cur_pos);
                        drawActiveCursor (++cur_pos);
                    }
                    break;

                default:
                    if (isprint (ui.kb_char) && pw_len < NP)
                        insertChar (pw_buf, pw_len, cur_pos, hide, ui.kb_char);
                    break;
                }
            }

            if (ui.kb_char == CHAR_TAB || inBox (ui.tap, eye_b)) {

                // toggle and redraw
                hide = !hide;

                drawEyeIcon (eye_b, hide);
                for (int i = 0; i < pw_len; i++) {
                    eraseChar (i);
                    drawChar (i, hide ? HS_V : pw_buf[i]);
                }
            }

        } while (!cancelled && !enter);

        if (enter) {
            pw_buf[pw_len] = '\0';
            pw_ok = testPassword (category, pw_buf);
        }     
    }

    if (restore)
        initScreen();

    Serial.printf ("PW for %s %s\n", category, pw_ok ? "ok" : "failed");

    return (pw_ok);
}

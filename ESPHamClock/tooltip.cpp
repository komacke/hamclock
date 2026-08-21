/* provide simple text box as a tool tip
 */

#include "HamClock.h"

#define TT_MAXLL        50                              // max line length, characters
#define TT_BORDER       4                               // border size, all 4 sides, pixels
#define TT_TIMEOUT      (6*1000)                        // timeout, millis
#define TT_ROWH         12                              // row spacing, pixels
#define TT_FG           RA8875_WHITE                    // text and border color
#define TT_BG           RGB565(40,40,200)               // background color
#define TT_MAXLINES     10                              // max n lines in a tip, more are ignored
#define TT_FONTW        6                               // font width, pixels

/* show the given text in a box near the given location.
 * stay up until time out or any user input occurs.
 */
void tooltip (const SCoord &s, const char *tip)
{
    // save and restore font
    FontWeight fw;
    FontSize fs;
    getFontStyle (&fw, &fs);

    // break into roughly same length lines at white space
    typedef struct {
        int start, count;                               // starting index and length of line
    } OneLine;
    OneLine tip_lines[TT_MAXLINES];                     // each line
    const size_t tip_len = strlen (tip);
    int n_lines = tip_len / TT_MAXLL + 1;               // number of lines
    int nom_ll = tip_len / n_lines;                     // nominal length of each line
    int max_ll = 0;                                     // actual longest line


    // cut tip into each line
    int start = 0;
    for (int i = 0; i < n_lines; i++) {
        OneLine &ol = tip_lines[i];
        ol.start = start;
        // find right-most blank after start + nom_ll. N.B. beware EOS on last line
        for (ol.count = 0; tip[start+ol.count] != '\0'; ol.count++)
            if (tip[start+ol.count] == ' ' && ol.count > nom_ll)
                break;
        if (ol.count > max_ll)
            max_ll = ol.count;
        start += ol.count + 1;                          // +1 to skip blank
    }

    // determine tip box size
    SBox tip_b;
    tip_b.w = max_ll * TT_FONTW + 2*TT_BORDER;
    tip_b.h = n_lines * TT_ROWH + TT_BORDER;

    // set location at s but beware right and bottom edges
    tip_b.x = s.x + tip_b.w >= tft.width() ? tft.width() - tip_b.w : s.x;
    tip_b.y = s.y + tip_b.h >= tft.height() ? tft.height() - tip_b.h : s.y;

    // get current pixels
    uint8_t *backing_store;
    if (!tft.getBackingStore (backing_store, tip_b.x, tip_b.y, tip_b.w, tip_b.h))
        fatalError ("failed to capture pixels beneath %d x %d tooltip", tip_b.w, tip_b.h);

    // start box
    fillSBox (tip_b, TT_BG);
    drawSBox (tip_b, TT_FG);

    // draw each text line
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(TT_FG);
    uint16_t text_x = tip_b.x + TT_BORDER;
    uint16_t text_y = tip_b.y + TT_BORDER;
    for (int i = 0; i < n_lines; i++) {
        tft.setCursor (text_x, text_y);
        tft.printf ("%.*s", tip_lines[i].count, &tip[tip_lines[i].start]);
        text_y += TT_ROWH;
    }

    // wait -- UF_CLOCKSOK so the main clock(s) keep ticking while the tooltip is up; only the
    // small backing-store region under the tooltip itself is frozen/restored, so this doesn't
    // disturb the tooltip's own contents.
    UserInput ui = {
        tip_b,
        UI_UFuncNone,
        UF_UNUSED,
        TT_TIMEOUT,
        UF_CLOCKSOK,
        {0,0}, TT_NONE, '\0', false, false
    };

    (void) waitForUser (ui);
    drainTouch();

    // finished -- restore contents and font
    if (!tft.setBackingStore (backing_store, tip_b.x, tip_b.y, tip_b.w, tip_b.h))
        fatalError ("failed to restore pixels beneath %d x %d tooltip", tip_b.w, tip_b.h);
    selectFontStyle (fw, fs);
}

/* this code manages the call sign and ON THE AIR experience.
 */


#include "HamClock.h"

CallsignInfo cs_info;                                   // state, global for setup.cpp::call[] and webserver


#define DEFCALL_FG      RA8875_WHITE                    // default callsign fg color
#define DEFCALL_BG      RA8875_BLACK                    // default callsign bg color
#define DEFTITLE_FG     RA8875_WHITE                    // default title fg color
#define DEFTITLE_BG     RA8875_BLUE                     // default title bg color
#define DEFONAIR_FG     RA8875_WHITE                    // default ON AIR fg color
#define DEFONAIR_BG     RA8875_RED                      // default ON AIR bg color


// persistent sw and hw ON AIR states for setOnAir*
static bool onair_sw, onair_hw;


/* draw full spectrum in the given box.
 */
static void drawRainbow (SBox &box)
{
    // sweep full range of hue starting at random x within box
    uint8_t x0 = random(box.w);
    for (uint16_t x = box.x; x < box.x+box.w; x++) {
        uint8_t h = 255*((x+x0-box.x)%box.w)/box.w;
        tft.fillRect (x, box.y, 1, box.h, HSV_2_RGB565(h,255,255));
    }
}

/* draw the given string centered in the given box using the current font and given color.
 * if it fits in one line, set y to box.y + l1_dy.
 * if fits as two lines draw set their y to box.y + l12_dy and l22_dy.
 * if latter two are 0 then don't even try 2 lines.
 * if it won't fit in 2 lines and anyway is set, draw as much as possible.
 * if shadow then draw a black background shadow.
 * return whether it all fit some way.
 */
static bool drawBoxText (bool anyway, const SBox &box, const char *str, uint16_t color,
uint16_t l1_dy, uint16_t l12_dy, uint16_t l22_dy, bool shadow)
{
    // try as one line
    uint16_t w = getTextWidth (str);
    if (w < box.w) {
        shadowString (str, shadow, color, box.x + (box.w-w)/2, box.y + l1_dy);
        return (true);
    }

    // bale if don't even want to try 2 lines
    if (l12_dy == 0 || l22_dy == 0)
        return (false);

    // try splitting into 2 lines
    StackMalloc str_copy0(str);
    char *s0 = (char *) str_copy0.getMem();
    for (char *space = strrchr (s0,' '); space; space = strrchr (s0,' ')) {
        *space = '\0';
        uint16_t w0 = getTextWidth (s0);
        if (w0 < box.w) {
            char *s1 = space + 1;
            strcpy (s1, str + (s1 - s0));               // restore zerod spaces
            uint16_t w1 = getTextWidth (s1);
            if (w1 < box.w) {
                // 2 lines fit
                shadowString (s0, shadow, color, box.x + (box.w - w0)/2, box.y + l12_dy);
                shadowString (s1, shadow, color, box.x + (box.w - w1)/2, box.y + l22_dy);
                return (true);
            } else if (anyway) {
                // print 1st line and as AMAP of 2nd
                shadowString (s0, shadow, color, box.x + (box.w - w0)/2, box.y + l12_dy);
                w1 = maxStringW (s1, box.w);
                shadowString (s1, shadow, color, box.x + (box.w - w1)/2, box.y + l22_dy);
                return (false);
            }
        }
    }

    // no way
    return (false);
}


/* return next color after current in basic series of primary colors that is nicely different from contrast.
 */
static uint16_t getNextColor (uint16_t current, uint16_t contrast)
{
    static uint16_t colors[] = {
        RA8875_RED, RA8875_GREEN, RA8875_BLUE, RA8875_CYAN,
        RA8875_MAGENTA, RA8875_YELLOW, RA8875_WHITE, RA8875_BLACK,
        DE_COLOR
    };
    #define NCOLORS NARRAY(colors)
    
    // find index of current color, ok if not found
    unsigned current_i;
    for (current_i = 0; current_i < NCOLORS; current_i++)
        if (colors[current_i] == current)
            break;

    // scan forward from current until find one nicely different from contrast
    for (unsigned cdiff_i = 1; cdiff_i < NCOLORS; cdiff_i++) {
        uint16_t next_color = colors[(current_i + cdiff_i) % NCOLORS];

        // certainly doesn't work if same as contrast
        if (next_color == contrast)
            continue;

        // continue scanning if bad combination
        switch (next_color) {
        case RA8875_RED:
            if (contrast == RA8875_MAGENTA || contrast == DE_COLOR)
                continue;
            break;
        case RA8875_GREEN:      // fallthru
        case RA8875_BLUE:
            if (contrast == RA8875_CYAN)
                continue;
            break;
        case RA8875_CYAN:
            if (contrast == RA8875_GREEN || contrast == RA8875_BLUE)
                continue;
            break;
        case RA8875_MAGENTA:
            if (contrast == RA8875_RED || contrast == DE_COLOR)
                continue;
            break;
        case RA8875_YELLOW:
            if (contrast == RA8875_WHITE)
                continue;
            break;
        case RA8875_WHITE:
            if (contrast == RA8875_YELLOW)
                continue;
            break;
        case RA8875_BLACK:
            // black goes with anything
            break;
        case DE_COLOR:
            if (contrast == RA8875_RED || contrast == RA8875_MAGENTA)
                continue;
            break;
        }

        // no complaints
        return (next_color);
    }

    // default 
    return (colors[0]);
}

/* set colors for the given system, if set.
 */
static void setCallColors (CallColors_t &cu, uint16_t *fg, uint16_t *bg, uint8_t *rainbow)
{
    if (fg)
        cu.fg = *fg;
    if (rainbow)
        cu.rainbow = *rainbow;
    else if (bg) {
        cu.bg = *bg;
        cu.rainbow = 0;
    }
}

/* draw callsign using cs_info.
 * draw everything if all, else just fg text as when just changing text color.
 */
static void drawCallsign (bool all)
{
    uint16_t fg_c = 0, bg_c = 0;
    bool rainbow = false;
    const char *text = NULL;
    switch (cs_info.now_showing) {
    case CT_CALL:
        fg_c = cs_info.call_col.fg;
        bg_c = cs_info.call_col.bg;
        rainbow = cs_info.call_col.rainbow != 0;
        text = cs_info.call;
        break;
    case CT_TITLE:
        fg_c = cs_info.title_col.fg;
        bg_c = cs_info.title_col.bg;
        rainbow = cs_info.title_col.rainbow != 0;
        text = cs_info.title;
        break;
    case CT_ONAIR:
        fg_c = cs_info.onair_col.fg;
        bg_c = cs_info.onair_col.bg;
        rainbow = cs_info.onair_col.rainbow != 0;
        text = cs_info.onair;
        break;
    default:
        fatalError ("Bug! drawCallsign(%d) %d\n", all, (int)cs_info.now_showing);
        break;  // lint
    }

    // start with background iff all
    if (all) {
        if (rainbow)
            drawRainbow (cs_info.box);
        else
            fillSBox (cs_info.box, bg_c);
    }

    // set text color
    tft.setTextColor(fg_c);

    // copy text to slash0 with each '0' replaced with DEL to use slashed-0 hacked into BOLD/LARGE
    StackMalloc call_slash0(text);
    char *slash0 = (char *) call_slash0.getMem();
    for (char *z = slash0; *z != '\0' ; z++) {
        if (*z == '0')
            *z = 127;   // del
    }

    // start with largest font and keep shrinking and trying 2 lines until fits
    const SBox &box = cs_info.box;              // handy
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    if (!drawBoxText (false, box, slash0, fg_c, box.h/2+20, 0, 0, rainbow)) {
        // try smaller font
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        if (!drawBoxText (false, box, text, fg_c, box.h/2+10, 0, 0, rainbow)) {
            // try smaller font
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            if (!drawBoxText (false, box, text, fg_c, box.h/2+10, 0, 0, rainbow)) {
                // try all upper case to allow 2 lines without regard to descenders
                StackMalloc call_uc(text);
                char *uc = (char *) call_uc.getMem();
                for (char *z = uc; *z != '\0' ; z++)
                    *z = toupper(*z);
                if (!drawBoxText (false, box, uc, fg_c, box.h/2+10, box.h/2-2, box.h-3, rainbow)) {
                    // try smallest font
                    selectFontStyle (LIGHT_FONT, FAST_FONT);
                    (void) drawBoxText (true, box, text, fg_c, box.h/2-10, box.h/2-14, box.h/2+4, rainbow);
                }
            }
        }
    }
}

/* common test for setOnAirHW() and setOnAirSW(), latch state to avoid repetitive drawing.
 */
static void setOnAir(void)
{
    if (onair_hw || onair_sw) {
        // set on-air if not already
        if (cs_info.now_showing != CT_ONAIR) {
            cs_info.now_showing = CT_ONAIR;
            Serial.printf ("CALL: turn on-air On\n");
            drawCallsign (true);
        }
    } else if (cs_info.now_showing == CT_ONAIR) {
        // restore to prefer or just pick one if BOTH
        cs_info.now_showing = cs_info.ct_prefer == CT_BOTH ? CT_CALL : cs_info.ct_prefer;
        Serial.printf ("CALL: turn on-air Off\n");
        drawCallsign (true);
    }
}

/* run a menu overlaying the callsign area to allow editing title and ON AIR message.
 */
static void runCallsignMenu (void)
{
    // check whether PTT is a potentional option
    bool ptt = getFlrig(NULL,NULL) || getRigctld(NULL,NULL) || GPIOOk();

    // set up the ONAIR field
    MenuText onair_mt;
    memset (&onair_mt, 0, sizeof(onair_mt));
    char new_onair[NV_ONAIR_LEN];
    char ptt_label[] = "PTT:  ";
    memcpy (new_onair, cs_info.onair, sizeof(new_onair));
    onair_mt.text = new_onair;
    onair_mt.t_mem = sizeof(new_onair);
    onair_mt.label = ptt_label;
    onair_mt.l_mem = sizeof(ptt_label);
    onair_mt.to_upper = false;
    onair_mt.c_pos = 0;
    onair_mt.w_pos = 0;
    onair_mt.text_fp = NULL;                            // accept any message
    onair_mt.label_fp = NULL;

    // set up the title field
    MenuText title_mt;
    memset (&title_mt, 0, sizeof(title_mt));
    char title_label[] = "Title:";
    char new_title[NV_TITLE_LEN];
    memcpy (new_title, cs_info.title, sizeof(new_title));
    title_mt.text = new_title;
    title_mt.t_mem = sizeof(new_title);
    title_mt.label = title_label;
    title_mt.l_mem = sizeof(title_label);
    title_mt.to_upper = false;
    title_mt.c_pos = 0;
    title_mt.w_pos = 0;
    title_mt.text_fp = NULL;                            // accept any message
    title_mt.label_fp = NULL;

    MenuItem mitems[] = {
        {MENU_LABEL,                        false, 0, 2, "Show: ", 0},                  // 0
        {MENU_1OFN, cs_info.ct_prefer == CT_CALL,  1, 2, "call", 0},                    // 1
        {MENU_1OFN, cs_info.ct_prefer == CT_TITLE, 1, 2, "title", 0},                   // 2
        {MENU_1OFN, cs_info.ct_prefer == CT_BOTH,  1, 2, "rotate", 0},                  // 3
        {MENU_TEXT,                         false, 2, 2, title_mt.label, &title_mt},    // 4
        {ptt ? MENU_TEXT : MENU_IGNORE,     false, 3, 2, onair_mt.label, &onair_mt},    // 5
    };

    SBox menu_b = cs_info.box;                          // copy, not ref!
    menu_b.x += 20;
    menu_b.y += 2;
    menu_b.w = 0;
    menu_b.h = 0;

    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 4, NARRAY(mitems), mitems};
    if (runMenu (menu)) {

        // save new messages
        if (ptt && strcmp (new_onair, cs_info.onair)) {
            (void) strTrimAll (new_onair);
            Serial.printf ("CALL: new onair: %s\n", new_onair);
            memcpy (cs_info.onair, new_onair, NV_ONAIR_LEN);
            NVWriteString (NV_ONAIR_MSG, cs_info.onair);
        }
        if (strcmp (new_title, cs_info.title)) {
            (void) strTrimAll (new_title);
            Serial.printf ("CALL: new title: %s\n", new_title);
            memcpy (cs_info.title, new_title, NV_TITLE_LEN);
            NVWriteString (NV_TITLE, cs_info.title);
        }

        // set preferred type
        if (mitems[1].set)
            cs_info.ct_prefer = CT_CALL;
        else if (mitems[2].set)
            cs_info.ct_prefer = CT_TITLE;
        else
            cs_info.ct_prefer = CT_BOTH;
        NVWriteUInt8 (NV_CALLPREF, (uint8_t)cs_info.ct_prefer);

        // update what's now showing unless showing PTT
        if (cs_info.now_showing != CT_ONAIR) {
            // set to prefer or let updateCallsign rotate if BOTH
            if (cs_info.ct_prefer != CT_BOTH)
                cs_info.now_showing = cs_info.ct_prefer;
        }

        // full redraw to show changes
        updateCallsign (true);
    }
}

/* called periodically to update callsign.
 */
void updateCallsign (bool force_draw)
{
    // check for call/title rotation unless on-air
    if (cs_info.ct_prefer == CT_BOTH && cs_info.now_showing != CT_ONAIR) {
        time_t now = myNow();
        if (now > cs_info.next_update || force_draw) {
            cs_info.now_showing = cs_info.now_showing == CT_CALL ? CT_TITLE : CT_CALL;  // toggle
            cs_info.next_update = now + ROTATION_INTERVAL;                              // same rate as panes
            force_draw = true;
        }
    }

    // draw if desired
    if (force_draw)
        drawCallsign (true);
}

/* load cs_info color settings from NV and set default ON AIR text
 */
void initCallsignInfo()
{
    // init call colors -- call itself is handled in setup.cpp
    if (!NVReadUInt16 (NV_CALL_FG, &cs_info.call_col.fg)) {
        cs_info.call_col.fg = DEFCALL_FG;
        NVWriteUInt16 (NV_CALL_FG, cs_info.call_col.fg);
    }
    if (!NVReadUInt16 (NV_CALL_BG, &cs_info.call_col.bg)) {
        cs_info.call_col.bg = DEFCALL_BG;
        NVWriteUInt16 (NV_CALL_BG, cs_info.call_col.bg);
    }
    if (!NVReadUInt8 (NV_CALL_RAINBOW, &cs_info.call_col.rainbow)) {
        cs_info.call_col.rainbow = 0;
        NVWriteUInt8 (NV_CALL_RAINBOW, cs_info.call_col.rainbow);
    }


    // init onair colors and text
    if (!NVReadUInt16 (NV_ONAIR_FG, &cs_info.onair_col.fg)) {
        cs_info.onair_col.fg = DEFONAIR_FG;
        NVWriteUInt16 (NV_ONAIR_FG, cs_info.onair_col.fg);
    }
    if (!NVReadUInt16 (NV_ONAIR_BG, &cs_info.onair_col.bg)) {
        cs_info.onair_col.bg = DEFONAIR_BG;
        NVWriteUInt16 (NV_ONAIR_BG, cs_info.onair_col.bg);
    }
    if (!NVReadUInt8 (NV_ONAIR_RAINBOW, &cs_info.onair_col.rainbow)) {
        cs_info.onair_col.rainbow = 0;
        NVWriteUInt8 (NV_ONAIR_RAINBOW, cs_info.onair_col.rainbow);
    }
    if (!NVReadString (NV_ONAIR_MSG, cs_info.onair)) {
        strcpy (cs_info.onair, "ON THE AIR");
        NVWriteString (NV_ONAIR_MSG, cs_info.onair);
    }


    // init title colors and text
    if (!NVReadUInt16 (NV_TITLE_FG, &cs_info.title_col.fg)) {
        cs_info.title_col.fg = DEFTITLE_FG;
        NVWriteUInt16 (NV_TITLE_FG, cs_info.title_col.fg);
    }
    if (!NVReadUInt16 (NV_TITLE_BG, &cs_info.title_col.bg)) {
        cs_info.title_col.bg = DEFTITLE_BG;
        NVWriteUInt16 (NV_TITLE_BG, cs_info.title_col.bg);
    }
    if (!NVReadUInt8 (NV_TITLE_RAINBOW, &cs_info.title_col.rainbow)) {
        cs_info.title_col.rainbow = 0;
        NVWriteUInt8 (NV_TITLE_RAINBOW, cs_info.title_col.rainbow);
    }
    if (!NVReadString (NV_TITLE, cs_info.title)) {
        cs_info.title[0] = '\0';
        NVWriteString (NV_TITLE, cs_info.title);
    }


    // get preferred and current display when not PTT
    uint8_t pref;
    if (!NVReadUInt8 (NV_CALLPREF, &pref)) {
        pref = CT_CALL;
        NVWriteUInt8 (NV_CALLPREF, pref);
    }
    cs_info.ct_prefer = (Call_t)pref;
    if (cs_info.ct_prefer == CT_CALL || cs_info.ct_prefer == CT_TITLE)
        cs_info.now_showing = cs_info.ct_prefer;
    else
        cs_info.now_showing = CT_CALL;
}

/* set and save the given callsign usage info.
 * N.B. this can NOT be used to change the real call sign.
 */
void setCallsignInfo (Call_t t, const char *text, uint16_t *fg, uint16_t *bg, uint8_t *rainbow)
{
    switch (t) {

    case CT_CALL:

        // just set colors, ignore text
        setCallColors (cs_info.call_col, fg, bg, rainbow);
        NVWriteUInt16 (NV_CALL_FG, cs_info.call_col.fg);
        NVWriteUInt16 (NV_CALL_BG, cs_info.call_col.bg);
        NVWriteUInt8 (NV_CALL_RAINBOW, cs_info.call_col.rainbow);

        // change to call view unless showing ONAIR
        if (cs_info.now_showing != CT_ONAIR)
            cs_info.now_showing = CT_CALL;

        // save as preferred display
        cs_info.ct_prefer = CT_CALL;

        break;

    case CT_TITLE:

        // just change colors if text is empty
        if (text && strlen(text) > 0) {
            quietStrncpy (cs_info.title, text, sizeof(cs_info.title));
            NVWriteString (NV_TITLE, cs_info.title);
        }

        setCallColors (cs_info.title_col, fg, bg, rainbow);
        NVWriteUInt16 (NV_TITLE_FG, cs_info.title_col.fg);
        NVWriteUInt16 (NV_TITLE_BG, cs_info.title_col.bg);
        NVWriteUInt8 (NV_TITLE_RAINBOW, cs_info.title_col.rainbow);

        // change to title view unless showing ONAIR
        if (cs_info.now_showing != CT_ONAIR)
            cs_info.now_showing = CT_TITLE;

        // save as preferred display
        cs_info.ct_prefer = CT_TITLE;

        break;

    case CT_ONAIR:

        // just change colors if text is empty
        if (text && strlen(text) > 0) {
            quietStrncpy (cs_info.onair, text, sizeof(cs_info.onair));
            NVWriteString (NV_ONAIR_MSG, cs_info.onair);
        }

        setCallColors (cs_info.onair_col, fg, bg, rainbow);
        NVWriteUInt16 (NV_ONAIR_FG, cs_info.onair_col.fg);
        NVWriteUInt16 (NV_ONAIR_BG, cs_info.onair_col.bg);
        NVWriteUInt8 (NV_ONAIR_RAINBOW, cs_info.onair_col.rainbow);

        // just make changes without forcing type, still only shows when really PTT

        break;

    default:
        fatalError ("Bug! setCallsignInfo(%d)", (int)t);

    }
}

/* set ON AIR message state from hardware input.
 *   -- cooperates with setOnAirSW()
 *   -- harmless if set repeatedly to the same state
 */
void setOnAirHW (bool on)
{
    onair_hw = on;
    setOnAir();
}

/* set ON AIR message state from software input.
 *   -- cooperates with setOnAirHW()
 *   -- harmless if set repeatedly to the same state
 */
void setOnAirSW (bool on)
{
    onair_sw = on;
    setOnAir();
}

/* rotate cu to next fg color
 */
static void cycleFGColor (NV_Name fg_e, CallColors_t &cu)
{
    // choose foreground as if over a black background when really over rainbow
    uint16_t bg = cu.rainbow ? RA8875_BLACK : cu.bg;
    cu.fg = getNextColor (cu.fg, bg);

    if (fg_e != NV_NONE)
        NVWriteUInt16 (fg_e, cu.fg);
}

/* rotate cu to next bg color
 */
static void cycleBGColor (NV_Name bg_e, NV_Name rainbow_e, CallColors_t &cu)
{
    // cycle through rainbow when current bg is white
    if (cu.rainbow) {
        cu.rainbow = 0;
        cu.bg = getNextColor (cu.bg, cu.fg);
    } else if (cu.bg == RA8875_WHITE) {
        cu.rainbow = 1;
        // leave cu.bg in order to resume color scan when rainbow is turned off
    } else {
        cu.bg = getNextColor (cu.bg, cu.fg);
    }

    if (bg_e != NV_NONE)
        NVWriteUInt16 (bg_e, cu.bg);
    if (rainbow_e != NV_NONE)
        NVWriteUInt8 (rainbow_e, cu.rainbow);
}

/* given a touch location within cs_info.box perform appropriate action, which might include
 * changes to cs_info.
 */
void doCallsignTouch (const SCoord &s)
{
    if (s.x < cs_info.box.x + cs_info.box.w/3) {

        // left third: fg

        switch (cs_info.now_showing) {
        case CT_CALL:
            cycleFGColor (NV_CALL_FG, cs_info.call_col);
            break;
        case CT_TITLE:
            cycleFGColor (NV_TITLE_FG, cs_info.title_col);
            break;
        case CT_ONAIR:
            cycleFGColor (NV_ONAIR_FG, cs_info.onair_col);
            break;
        default:
            fatalError ("Bug! doCallsignTouch() left %d", cs_info.now_showing);
        }
        drawCallsign (false);   // just foreground

    } else if (s.x < cs_info.box.x + 2*cs_info.box.w/3) {

        // center third: menu

        runCallsignMenu();

    } else {

        // right third: background

        switch (cs_info.now_showing) {
        case CT_CALL:
            cycleBGColor (NV_CALL_BG, NV_CALL_RAINBOW, cs_info.call_col);
            break;
        case CT_TITLE:
            cycleBGColor (NV_TITLE_BG, NV_TITLE_RAINBOW, cs_info.title_col);
            break;
        case CT_ONAIR:
            cycleBGColor (NV_ONAIR_BG, NV_ONAIR_RAINBOW, cs_info.onair_col);
            break;
        default:
            fatalError ("Bug! doCallsignTouch() right %d", cs_info.now_showing);
        }
        drawCallsign (true);    // fg and bg

    }
}

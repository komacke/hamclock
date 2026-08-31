/* generic modal dialog
 */

#include "HamClock.h"

/* MENU_TEXT editing is a bear. These sketches help:
 *
 *   window len = 8
 *                          l  c  w  action                                 l  c  w
 *     -----------------                               -----------------
 *   |a|b|c|d|e|f|g|h| |    8  8  1  CHAR_LEFT         |a|b|c|d|e|f|g|h|    8  7  0
 *     -----------------                               -----------------
 *                    _                                               _
 *
 *
 *     -----------------                               -----------------
 *   |a|b|c|d|e|f|g|h| |    8  8  1  CHAR_DEL          |a|b|c|d|e|f|g| |    7  7  0
 *     -----------------                               -----------------
 *                    _                                               _
 *  
 *     -----------------                               -----------------
 *   |a|b|c|d|e|f|g|h| |    8  8  1  type x        |a|b|c|d|e|f|g|h|x| |  9  9  2
 *     -----------------                               -----------------
 *                    _                                               _
 *  
 *     -----------------                               -----------------
 *   |a|b|c|d|e|f|g|h| |    8  7  1  type y          |a|b|c|d|e|f|g|y|h|  9  8  2
 *     -----------------                               -----------------
 *                  _                                                 _
 *
 */


// basic parameters
// allow setting some/all these in Menu?
#define MENU_TBM        5               // top and bottom margin -- was 2px, which read as
                                        // cramped once longer item lists (eg pane choice, now
                                        // 25 items) made menus tall enough for the tight margin
                                        // to be visually obvious. Deliberately a small, uniform
                                        // bump applied to every menu rather than special-cased
                                        // to one -- adds ~6px total height (top+bottom) against
                                        // boxes already well over 100px tall, and doesn't touch
                                        // width at all, unlike the column-count approach that
                                        // caused this menu to overlap neighboring panes.
#define MENU_RM         2               // right margin
#define MENU_RH         11              // row height, includes room for MENU_TEXT cursor
#define MENU_IS         6               // indicator size
#define MENU_BB         5               // ok/cancel button horizontal border
#define MENU_BDX        2               // ok/cancel button text horizontal offset
#define MENU_BG         2               // ok/cancel button text vertical gap
#define MENU_BDROP      2               // text cursor drop below top of box
#define MENU_FW         6               // MENU_TEXT font width
#define MENU_CD         2               // cursor rows below text baseline
#define MENU_TIMEOUT    MENU_TO         // timeout, millis
#define MENU_FGC        RA8875_WHITE    // normal foreground color
#define MENU_BGC        RA8875_BLACK    // normal background color
#define MENU_ERRC       RA8875_RED      // error color
#define MENU_BSYC       RA8875_YELLOW   // busy color
#define MENU_FOCC       RA8875_GREEN    // focus color

/* pick box for a Menutext is partitioned as follows:
 *             | label             | text field |
 * | mi.indent | (l_mem-1)*MENU_FW | text_width | MENU_RM |
 *             <- pb.x
 *             <- pb.w ------------------------->
 *
 * N.B. must agree with pb's created in runMenu()
 */
#define MENU_LX(pb)     (pb.x)                                          // MenuText label x, pixels
#define MENU_LW(mi)     ((uint16_t)((mi.textf->l_mem-1)*MENU_FW))       // MenuText label width, pixels
#define MENU_TX(pb,mi)  (MENU_LX(pb) + MENU_LW(mi))                     // MenuText text x, pixels
#define MENU_TW(pb,mi)  (pb.w - MENU_LW(mi))                            // MenuText text width, pixels


static const char ok_label[] = "Ok";
static const char cancel_label[] = "Cancel";

/* draw text and optionally the label and/or cursor for the given MenuItem in the given box.
 * N.B. mi is assumed to be of type MENU_TEXT.
 */
static void drawMenuText (const MenuItem &mi, const SBox &pb, bool draw_label, bool draw_cursor)
{
    // require
    if (mi.type != MENU_TEXT)
        fatalError ("drawMenuText not %d: %d", (int)MENU_TEXT, (int)mi.type);

    // handy
    MenuText *tfp = mi.textf;
    const size_t t_len = strlen(tfp->text);

    // draw fresh label if desired
    if (draw_label) {
        tft.fillRect (MENU_LX(pb), pb.y, MENU_LW(mi), pb.h, MENU_BGC);

        if (debugLevel (DEBUG_MENUS, 1))
            tft.drawRect (MENU_LX(pb), pb.y, MENU_LW(mi), pb.h, RA8875_GREEN);

        tft.setCursor (MENU_LX(pb), pb.y + MENU_BDROP);
        tft.print (tfp->label);
    }

    // erase all text, including cursor
    tft.fillRect (MENU_TX(pb,mi), pb.y, MENU_TW(pb,mi), pb.h, MENU_BGC);

    if (debugLevel (DEBUG_MENUS, 1)) {
        drawSBox (pb, RA8875_GREEN);
        tft.drawRect (MENU_TX(pb,mi), pb.y, MENU_TW(pb,mi), pb.h, RA8875_RED);
    }

    // check left end
    if (tfp->c_pos < tfp->w_pos)
        tfp->w_pos = tfp->c_pos;

    // check right end: insure cursor is both within the string and within the text window
    int t_n = MENU_TW(pb,mi)/MENU_FW;
    if (tfp->c_pos > t_len)
        tfp->c_pos = t_len;
    if (tfp->c_pos > tfp->w_pos + t_n - 1)
        tfp->w_pos = tfp->c_pos - t_n + 1;

    // print starting at w_pos
    tft.setCursor (MENU_TX(pb,mi), pb.y + MENU_BDROP);
    tft.printf ("%.*s", t_n, tfp->text + tfp->w_pos);

    // draw cursor if desired (already erased above)
    if (draw_cursor) {
        uint16_t c_x = MENU_TX(pb,mi) + MENU_FW*(tfp->c_pos-tfp->w_pos);
        uint16_t c_y = pb.y + pb.h - MENU_CD;
        tft.drawLine (c_x, c_y, c_x + MENU_FW, c_y, MENU_FOCC);
    }
}

/* briefly show the given message over the given MENU_TEXT
 */
static void drawMenuTextMsg (const MenuItem &mi, const SBox &pb, const char *msg)
{
    // overlay the given msg
    tft.fillRect (MENU_TX(pb,mi), pb.y, MENU_TW(pb,mi), pb.h, MENU_BGC);
    tft.setCursor (MENU_TX(pb,mi), pb.y + MENU_BDROP);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(MENU_ERRC);
    tft.print (msg);

    // delay then restore
    wdDelay (3000);
    tft.setTextColor (MENU_FGC);
    drawMenuText (mi, pb, false, true);

    // dont keep doing this
    drainTouch();
}

/* draw selector symbol and label for the given menu item in the given pick box.
 * kb_focus indicates this item has the keyboard focus.
 */
void menuDrawItem (const MenuItem &mi, const SBox &pb, bool draw_label, bool kb_focus)
{
    // N.B. updateClocks can change font!
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (MENU_FGC);

    // prepare a copy of the label without underscores if drawing
    char *no__copy = NULL;
    if (draw_label && mi.label) {       // label will be NULL for IGNORE and BLANK
        no__copy = strdup (mi.label);
        strncpySubChar (no__copy, mi.label, ' ', '_', strlen(mi.label)+1);      // len including EOS
    }

    // draw depending on type

    switch (mi.type) {

    case MENU_BLANK:    // fallthru
    case MENU_IGNORE:
        break;

    case MENU_LABEL:
        if (draw_label) {
            tft.setCursor (pb.x + mi.indent, pb.y + MENU_BDROP);
            tft.print (no__copy);
        }
        drawSBox (pb, kb_focus ? MENU_FOCC : MENU_BGC);
        break;

    case MENU_0OFN:     // fallthru
    case MENU_01OFN:    // fallthru
    case MENU_1OFN:
        if (mi.type == MENU_1OFN && mi.submenu) {
            // outline down-pointing triangle (U+25BD look-alike) instead of the round radio
            // dot: this row navigates into another menu rather than making a final choice,
            // so it needs a visibly different indicator than a plain single-pick list item.
            uint16_t tx0 = pb.x + mi.indent, ty0 = pb.y + (MENU_RH-MENU_IS)/2;
            uint16_t tx1 = tx0 + MENU_IS,    ty1 = ty0;
            uint16_t tx2 = tx0 + MENU_IS/2,  ty2 = ty0 + MENU_IS;
            if (mi.set)
                tft.fillTriangle (tx0, ty0, tx1, ty1, tx2, ty2, MENU_FGC);
            else {
                tft.fillTriangle (tx0, ty0, tx1, ty1, tx2, ty2, MENU_BGC);
                tft.drawTriangle (tx0, ty0, tx1, ty1, tx2, ty2, MENU_FGC);
            }
        } else if (mi.set)
            tft.fillCircle (pb.x + mi.indent + MENU_IS/2, pb.y + MENU_RH/2, MENU_IS/2, MENU_FGC);
        else {
            tft.fillCircle (pb.x + mi.indent + MENU_IS/2, pb.y + MENU_RH/2, MENU_IS/2, MENU_BGC);
            tft.drawCircle (pb.x + mi.indent + MENU_IS/2, pb.y + MENU_RH/2, MENU_IS/2, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (pb.x + mi.indent + MENU_IS + MENU_IS/2, pb.y + MENU_BDROP);
            tft.print (no__copy);
        }
        drawSBox (pb, kb_focus ? MENU_FOCC : MENU_BGC);
        break;

    case MENU_AL1OFN:   // fallthru
    case MENU_TOGGLE:
        if (mi.set)
            tft.fillRect (pb.x + mi.indent, pb.y + (MENU_RH-MENU_IS)/2, MENU_IS, MENU_IS, MENU_FGC);
        else {
            tft.fillRect (pb.x + mi.indent, pb.y + (MENU_RH-MENU_IS)/2, MENU_IS, MENU_IS, MENU_BGC);
            tft.drawRect (pb.x + mi.indent, pb.y + (MENU_RH-MENU_IS)/2, MENU_IS, MENU_IS, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (pb.x + mi.indent + MENU_IS + MENU_IS/2, pb.y + MENU_BDROP);
            tft.print (no__copy);
        }
        drawSBox (pb, kb_focus ? MENU_FOCC : MENU_BGC);
        break;

    case MENU_TEXT:
        // this type is complex enough deserves its own drawing code
        drawMenuText (mi, pb, draw_label, kb_focus);
        break;
    }

    // clean up
    free ((void*)no__copy);

    if (debugLevel (DEBUG_MENUS, 1))
        drawSBox (pb, RA8875_RED);

    // draw now if menu is over map_b
    if (!draw_label && boxesOverlap (pb, map_b))
        tft.drawPR();
}

/* count how many items in the same group and type as ii are set
 */
static int menuCountItemsSet (MenuInfo &menu, int ii)
{
    MenuItem &menu_ii = menu.items[ii];
    int n_set = 0;

    for (int i = 0; i < menu.n_items; i++) {
        if (menu.items[i].type == MENU_IGNORE)
            continue;
        if (menu.items[i].type != menu_ii.type)
            continue;
        if (menu.items[i].group != menu_ii.group)
            continue;
        if (menu.items[i].set)
            n_set++;
    }

    return (n_set);
}

/* turn off MENU_TEXT and all items that are in the same group and type as item ii.
 */
static void menuItemsAllOff (MenuInfo &menu, SBox *pick_boxes, int ii)
{
    MenuItem &menu_ii = menu.items[ii];

    for (int i = 0; i < menu.n_items; i++) {
        MenuItem &mi = menu.items[i];
        if (mi.type != MENU_IGNORE && mi.set
                && (mi.type == MENU_TEXT || (mi.type == menu_ii.type && mi.group == menu_ii.group))) {
            mi.set = false;
            menuDrawItem (mi, pick_boxes[i], false, false);
        }
    }
}



/* draw (or redraw) the optional footer status line, if menu.footer_text is set. Recomputes
 * its own layout position fresh each call (cheap: just a loop over menu.items[]) rather than
 * needing n_tblrows/n_mt threaded in from runMenu()'s own computation of those, so this can
 * be called after every toggle, not just once at initial draw.
 *
 * Two modes, selected by menu.footer_live_count:
 *   false (default) -- print footer_text exactly as given. Original, unchanged behavior;
 *     right for a footer describing something other than this menu's own items (eg the
 *     pane-choice category picker in plotmgmnt.cpp, whose footer reports a running total
 *     over a master array that lives outside this particular runMenu() call entirely).
 *   true -- treat footer_text as a label prefix and append a live "(selected/total)" count
 *     computed over this menu's own MENU_TOGGLE/MENU_AL1OFN/MENU_0OFN/MENU_01OFN/MENU_1OFN
 *     items (MENU_LABEL/MENU_BLANK/MENU_IGNORE/MENU_TEXT don't have a meaningful .set concept
 *     and don't count toward either number). Right for a footer describing this menu's own
 *     checklist, eg the pane-choice per-category sub-menu.
 */
static void drawMenuFooter (MenuInfo &menu)
{
    if (!menu.footer_text)
        return;

    // recompute table-row layout fresh -- same logic runMenu() used to size the box,
    // needed here too so the footer lands on the right row regardless of who's calling
    int n_table = 0, n_mt = 0;
    for (int i = 0; i < menu.n_items; i++) {
        MenuFieldType t = menu.items[i].type;
        if (t == MENU_IGNORE)
            continue;
        if (t == MENU_TEXT)
            n_mt++;
        else
            n_table++;
    }
    int n_tblrows = (n_table + menu.n_cols - 1)/menu.n_cols;

    char buf[64];
    if (menu.footer_live_count) {
        int selected = 0, total = 0;
        for (int i = 0; i < menu.n_items; i++) {
            MenuFieldType t = menu.items[i].type;
            if (t == MENU_TOGGLE || t == MENU_AL1OFN || t == MENU_0OFN
                        || t == MENU_01OFN || t == MENU_1OFN) {
                total++;
                if (menu.items[i].set)
                    selected++;
            }
        }
        snprintf (buf, sizeof(buf), "%s (%d/%d)", menu.footer_text, selected, total);
    } else {
        snprintf (buf, sizeof(buf), "%s", menu.footer_text);
    }

    uint16_t fy = menu.menu_b.y + MENU_TBM + (n_tblrows + n_mt)*MENU_RH;
    // inset 1px each side -- drawSBox(menu.menu_b,...) draws a 1px outline right at these
    // same x/x+w edges; without the inset this fill overwrites that outline at the footer's
    // row specifically, erasing the box border there until something else happens to redraw it
    SBox fbox = {(uint16_t)(menu.menu_b.x+1), fy, (uint16_t)(menu.menu_b.w-2), (uint16_t)MENU_RH};
    fillSBox (fbox, MENU_BGC);         // erase whatever was there before -- the new count
                                        // may be shorter than the old one, would otherwise
                                        // leave stale trailing characters behind
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (menu.footer_color ? menu.footer_color : MENU_FGC);
    tft.setCursor (fbox.x + MENU_RM, fy + MENU_BDROP);
    tft.print (buf);
    tft.setTextColor (MENU_FGC);       // restore default for whatever draws next
}

/* engage an action at the specified pick index.
 * kb_focus indicates whether to highlight the new focus item for keyboard navigation.
 */
static void updateMenu (MenuInfo &menu, SBox *pick_boxes, int pick_i, bool kb_focus)
{
    SBox &pb = pick_boxes[pick_i];
    MenuItem &mi = menu.items[pick_i];

    switch (mi.type) {
    case MENU_LABEL:        // fallthru
    case MENU_BLANK:        // fallthru
    case MENU_IGNORE:
        break;

    case MENU_1OFN:
        if (!mi.set) {
            menuItemsAllOff (menu, pick_boxes, pick_i);
            mi.set = true;
            menuDrawItem (mi, pb, false, kb_focus);
        }
        break;

    case MENU_01OFN:
        // turn off if set, else turn this one on and all others in this group off
        if (mi.set) {
            mi.set = false;
            menuDrawItem (mi, pb, false, kb_focus);
        } else {
            menuItemsAllOff (menu, pick_boxes, pick_i);
            mi.set = true;
            menuDrawItem (mi, pb, false, kb_focus);
        }
        break;

    case MENU_AL1OFN:
        // allow toggle, but Ok is disabled if none are set
        mi.set = !mi.set;
        menuDrawItem (mi, pb, false, kb_focus);
        break;

    case MENU_0OFN:     // fallthru
    case MENU_TOGGLE:
        // unconditional toggle
        mi.set = !mi.set;
        menuDrawItem (mi, pb, false, kb_focus);
        break;

    case MENU_TEXT:
        menuDrawItem (mi, pb, true, kb_focus);
        break;
    }

    // live footer redraw, if this menu wants one -- cheap no-op otherwise (footer_text NULL)
    drawMenuFooter (menu);
}


/* move to a different field depending on arrow keyin kb_char.
 * m_index is index of current menu item, return new item.
 */
static int kbNavigation (MenuInfo &menu, SBox *pick_boxes, int m_index, const char kb_char)
{

    // prep search state
    SBox &pm = pick_boxes[m_index];
    int mind = 100000;
    int candidate = -1;

    // search in desired direction
    switch (kb_char) {

    case CHAR_LEFT:
        // next field left closest in y, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            int d = abs ((int)pii.x - (int)pm.x) + abs ((int)pii.y - (int)pm.y);
            if (MENU_ACTIVE(mii.type) && pii.x < pm.x && d < mind) {
                candidate = ii;
                mind = d;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case CHAR_DOWN:
        // next field down closest to x, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            int d = abs ((int)pii.x - (int)pm.x) + abs ((int)pii.y - (int)pm.y);
            if (MENU_ACTIVE(mii.type) && pii.y > pm.y && d < mind) {
                candidate = ii;
                mind = d;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case CHAR_UP:
        // next field up closest to x, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            uint16_t d = abs ((int)pii.x - (int)pm.x) + abs ((int)pii.y - (int)pm.y);
            if (MENU_ACTIVE(mii.type) && pii.y < pm.y && d < mind) {
                candidate = ii;
                mind = d;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case CHAR_RIGHT:
        // next field right closest in y, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            int d = abs ((int)pii.x - (int)pm.x) + abs ((int)pii.y - (int)pm.y);
            if (MENU_ACTIVE(mii.type) && pii.x > pm.x && d < mind) {
                candidate = ii;
                mind = d;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case CHAR_SPACE:
        // activate this location
        updateMenu (menu, pick_boxes, m_index, true);
        break;

    default:
        break;
    }

    return (m_index);
}

/* given a MENU_TEXT text field index textf_i perform the editing actions of the given character.
 * if char is up or down arrow, defer to kbNavigation and return its index, else return the same index.
 */
static int textFieldEdit (MenuInfo &menu, SBox *pick_boxes, int textf_i, const char kb_char)
{
    // handy
    MenuItem &mi = menu.items[textf_i];
    if (mi.type != MENU_TEXT)
        fatalError ("textFieldEdit called with type %d", (int)mi.type);
    SBox &pb = pick_boxes[textf_i];
    MenuText *tfp = mi.textf;
    size_t t_len = strlen (tfp->text);

    if (debugLevel (DEBUG_MENUS, 2))
        printf ("TFE l= %2d c= %2d w= %2d %2d='%-*s' ... ",  MENU_TW(pb,mi)/MENU_FW, tfp->c_pos, tfp->w_pos,
                                (int)t_len, (int)tfp->t_mem, tfp->text);

    switch (kb_char) {
    case CHAR_UP:
    case CHAR_DOWN:
        return (kbNavigation (menu, pick_boxes, textf_i, kb_char));

    case CHAR_LEFT:
        if (debugLevel (DEBUG_MENUS, 2))
            printf (" LEFT ");
        if (tfp->c_pos > 0) {
            tfp->c_pos -= 1;
            menuDrawItem (mi, pb, false, true);
        }
        break;

    case CHAR_RIGHT:
        if (debugLevel (DEBUG_MENUS, 2))
            printf (" RIGT ");
        tfp->c_pos += 1;
        menuDrawItem (mi, pb, false, true);
        break;

    case CHAR_BS:
    case CHAR_DEL:
        if (debugLevel (DEBUG_MENUS, 2))
            printf (" DEL  ");
        // delete char left of cursor, if any
        if (tfp->c_pos > 0) {
            memmove (&tfp->text[tfp->c_pos-1], &tfp->text[tfp->c_pos], t_len - tfp->c_pos + 1);  // w/EOS
            tfp->c_pos -= 1;
            menuDrawItem (mi, pb, false, true);
        }
        break;

    default:
        // insert kb_char at cursor pos if printable and more room left
        if (isprint(kb_char)) {
            if (t_len < tfp->t_mem-1) {
                if (debugLevel (DEBUG_MENUS, 2))
                    printf (" IN %c ", kb_char);
                memmove (&tfp->text[tfp->c_pos+1], &tfp->text[tfp->c_pos], t_len - tfp->c_pos + 1); // w/EOS
                tfp->text[tfp->c_pos] = tfp->to_upper ? toupper(kb_char) : kb_char;
                tfp->c_pos += 1;
                menuDrawItem (mi, pb, false, true);
            } else {
                if (debugLevel (DEBUG_MENUS, 2))
                    printf (" FULL ");
                drawMenuTextMsg (mi, pb, "full");
            }
        }
        break;
    }

    // final check
    t_len = strlen (tfp->text);
    if (t_len > tfp->t_mem - 1)
        fatalError ("textFieldEdit t_len %d > t_mem-1 %d", (int)t_len, (int)tfp->t_mem-1);

    if (debugLevel (DEBUG_MENUS, 2))
        printf ("  %2d %2d %2d='%-*s'\n", tfp->c_pos, tfp->w_pos, (int)strlen(tfp->text),
                                (int)tfp->t_mem, tfp->text);

    return (textf_i);
}

/* update menu from the given tap.
 * m_index is index of current menu item, return new item or same if tap not in any item.
 */
static int tapNavigation (MenuInfo &menu, SBox *pick_boxes, int m_index, const SCoord &tap)
{
    for (int i = 0; i < menu.n_items; i++) {

        MenuItem &mi = menu.items[i];
        SBox &pb = pick_boxes[i];

        if (mi.type != MENU_IGNORE && inBox (tap, pb)) {

            if (mi.type == MENU_TEXT) {

                // tap could be in either the label or text areas
                MenuText *tfp = mi.textf;

                // get start of text area -- N.B. must match menuDrawItem() position
                uint16_t text_x = MENU_TX(pb,mi);

                if (tap.x < text_x) {
                    // call label updater, if any
                    if (tfp->label_fp)
                        (*tfp->label_fp) (tfp);
                } else {
                    // set cursor at tap location but not passed end
                    unsigned c_pos = tfp->w_pos + (tap.x - text_x)/MENU_FW;
                    if (c_pos > strlen(tfp->text))
                        c_pos = strlen(tfp->text);
                    tfp->c_pos = c_pos;
                }
            }

            // erase current location
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);

            // implement each type of behavior
            updateMenu (menu, pick_boxes, i, mi.type == MENU_TEXT);

            // tap found
            return (i);
        }
    }

    return (m_index);
}

/* return whether the menu is in a valid state to be closed with Ok.
 * Currently this means all groups that include at least one MENU_AL1OFN have at least one item set.
 */
static bool menuStateOk (MenuInfo &menu)
{
    // check each group that contains at least one MENU_AL1OFN
    for (int i = 0; i < menu.n_items; i++) {
        if (menu.items[i].type == MENU_AL1OFN) {

            // only mandatory if parent item is set, if any
            int parent_i = -1;
            for (int j = i - 1; j >= 0; j--) {
                if (MENU_ACTIVE(menu.items[j].type) && menu.items[j].indent < menu.items[i].indent) {
                    parent_i = j;
                    break;
                }
            }
            if (parent_i >= 0 && !menu.items[parent_i].set)
                continue;

            if (menuCountItemsSet (menu, i) == 0)
                return (false);
        }
    }
    return (true);
}

/* operate the given menu until ok, cancel or timeout.
 * caller passes a box we use for ok so they can use it later with menuRedrawOk if needed.
 * return true if op clicked ok or CR/NL else false for all other cases.
 * N.B. menu.menu_b.x/y are required but we may adjust to prevent edge spill.
 * N.B. incoming menu.menu_b.w is only grown to fit; thus calling with 0 will shrink wrap.
 * N.B. incoming menu.menu_b.h is ignored, we always shrink wrap h.
 * N.B. menu box is restored before returning.
 * N.B. all MENU_TEXT, if any, must be last
 */
bool runMenu (MenuInfo &menu)
{
    // font
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (MENU_FGC);

    // find number of non-ignore items and find widest item
    int n_table = 0;                                    // n menu.items not including MENU_TEXT
    int n_mt = 0;                                       // n MENU_TEXT
    int widest = 0;                                     // widest item, not including MENU_TEXT
    for (int i = 0; i < menu.n_items; i++) {
        MenuItem &mi = menu.items[i];
        if (mi.type == MENU_IGNORE)
            continue;
        if (mi.type == MENU_TEXT) {
            // enforce being at the bottom -- width is always full width
            if (i < menu.n_items-1) {
                MenuFieldType next_mft = menu.items[i+1].type;
                if (next_mft != MENU_TEXT && next_mft != MENU_IGNORE)
                fatalError ("all MENU_TEXT must be last");
            }
            n_mt++;
        } else {
            // set width in accord with how menuDrawItem() draws the item
            int w = mi.label ? getTextWidth(mi.label) + mi.indent + 3*MENU_IS/2 : 0;
            if (w > widest)
                widest = w;
            n_table++;                                  // MENU_TEXT is not part of table
        }
    }

    // number of rows in each table column
    int n_tblrows = (n_table + menu.n_cols - 1)/menu.n_cols;

    // one more row if there's a footer status line -- 0 for every caller that doesn't set one
    int n_footer = menu.footer_text ? 1 : 0;

    // set menu height, +1 for each MENU_TEXT, +1 for an optional footer, +1 for ok/cancel
    menu.menu_b.h = MENU_TBM + (n_tblrows + n_mt + n_footer + 1)*MENU_RH + MENU_TBM + MENU_BG;

    // set ok button size, don't know position yet
    uint16_t min_ok_w = getTextWidth (ok_label) + MENU_BDX*2;
    menu.ok_b.h = MENU_RH;

    // create cancel button, set size but don't know position yet
    SBox cancel_b;
    uint16_t min_cancel_w = getTextWidth (cancel_label) + MENU_BDX*2;
    cancel_b.h = MENU_RH;

    // width is duplicated for each column plus add a bit of right margin
    if (menu.menu_b.w < widest * menu.n_cols + MENU_RM)
        menu.menu_b.w = widest * menu.n_cols + MENU_RM;

    // insure menu width accommodates ok and/or cancel buttons
    if (menu.cancel == M_NOCANCEL) {
        if (menu.menu_b.w < MENU_BB + min_ok_w + MENU_BB)
            menu.menu_b.w = MENU_BB + min_ok_w + MENU_BB;
    } else {
        uint16_t min_btn_w = min_cancel_w > min_ok_w ? min_cancel_w : min_ok_w;
        if (menu.menu_b.w < 2 * min_btn_w + 3 * MENU_BB)
            menu.menu_b.w = 2 * min_btn_w + 3 * MENU_BB;
    }


    // now we know the size of menu_b

    // reposition box if needed to avoid spillage
    if (menu.menu_b.x + menu.menu_b.w >= tft.width())
        menu.menu_b.x = tft.width() - menu.menu_b.w - 1;
    if (menu.menu_b.y + menu.menu_b.h >= tft.height())
        menu.menu_b.y = tft.height() - menu.menu_b.h - 1;

    // now we can set ok and cancel button positions within the menu box
    if (menu.cancel == M_NOCANCEL) {
        menu.ok_b.w = menu.menu_b.w - 2*MENU_BB;
        menu.ok_b.x = menu.menu_b.x + MENU_BB;
        menu.ok_b.y = menu.menu_b.y + menu.menu_b.h - MENU_TBM - menu.ok_b.h;
        cancel_b.x = 0;
        cancel_b.y = 0;
        cancel_b.w = 0;
        cancel_b.h = 0;
    } else {
        uint16_t btn_w = (menu.menu_b.w - 3*MENU_BB)/2;
        menu.ok_b.w = btn_w;
        menu.ok_b.x = menu.menu_b.x + MENU_BB;
        menu.ok_b.y = menu.menu_b.y + menu.menu_b.h - MENU_TBM - menu.ok_b.h;
        cancel_b.w = btn_w;
        cancel_b.x = menu.ok_b.x + btn_w + MENU_BB;
        cancel_b.y = menu.ok_b.y;
    }

    // menu_b is now properly positioned

    // capture what we are about to clobber
    uint8_t *backing_store;
    if (!tft.getBackingStore (backing_store, menu.menu_b.x, menu.menu_b.y, menu.menu_b.w, menu.menu_b.h))
        fatalError ("failed to capture pixels beneath %d x %d menu", menu.menu_b.w, menu.menu_b.h);

    // can now prepare new menu box
    fillSBox (menu.menu_b, MENU_BGC);
    drawSBox (menu.menu_b, MENU_FGC);

    // display ok/cancel buttons
    fillSBox (menu.ok_b, MENU_BGC);
    drawSBox (menu.ok_b, MENU_FGC);
    uint16_t ok_txt_w = getTextWidth (ok_label);
    uint16_t ok_tx = menu.ok_b.x + (menu.ok_b.w > ok_txt_w ? (menu.ok_b.w - ok_txt_w)/2 : MENU_BDX);
    tft.setCursor (ok_tx, menu.ok_b.y + MENU_BDROP);
    tft.print (ok_label);
    if (menu.cancel == M_CANCELOK) {
        fillSBox (cancel_b, MENU_BGC);
        drawSBox (cancel_b, MENU_FGC);
        uint16_t cancel_txt_w = getTextWidth (cancel_label);
        uint16_t cancel_tx = cancel_b.x + (cancel_b.w > cancel_txt_w ? (cancel_b.w - cancel_txt_w)/2 : MENU_BDX);
        tft.setCursor (cancel_tx, cancel_b.y + MENU_BDROP);
        tft.print (cancel_label);
    }

    // display each table item in its own "pick box", all MENU_TEXT will be last and not within table proper
    StackMalloc pbox_mem(menu.n_items*sizeof(SBox));
    SBox *pick_boxes = (SBox *) pbox_mem.getMem();
    uint16_t col_w = (menu.menu_b.w - MENU_RM)/menu.n_cols;
    int tblcell_i = 0;                  // table cell, only incremented for non-IGNORE/MENU_TEXT items
    int mt_i = 0;                       // separate index for MENU_TEXTs
    for (int i = 0; i < menu.n_items; i++) {

        MenuItem &mi = menu.items[i];
        SBox &pb = pick_boxes[i];

        // assign item next location and draw unless to be ignored
        if (mi.type == MENU_TEXT) {
            // N.B. this assumes all MENU_TEXT are full width below the table
            // N.B. see MENU_T* and MENU_L* macros
            pb.x = menu.menu_b.x + mi.indent;
            pb.y = menu.menu_b.y + MENU_TBM + (n_tblrows + mt_i)*MENU_RH;
            pb.w = menu.menu_b.w - (mi.indent + MENU_RM);
            pb.h = MENU_RH;
            menuDrawItem (mi, pb, true, false);
            mt_i++;
        } else if (mi.type != MENU_IGNORE) {
            pb.x = menu.menu_b.x + 1 + (tblcell_i/n_tblrows)*col_w;
            pb.y = menu.menu_b.y + MENU_TBM + (tblcell_i%n_tblrows)*MENU_RH;
            pb.w = col_w;
            pb.h = MENU_RH;
            menuDrawItem (mi, pb, true, false);
            tblcell_i++;
        }
    }

    // optional footer status line -- see drawMenuFooter() for the two modes. Drawn here for
    // the initial paint; updateMenu() calls the same function again after every toggle so a
    // live-count footer stays current without needing to be part of the per-item redraw path.
    drawMenuFooter (menu);

    // immediate draw if menu is over map
    if (boxesOverlap (menu.menu_b, map_b))
        tft.drawPR();

    // initial Ok button state
    menuRedrawOk (menu.ok_b, menuStateOk(menu) ? MENU_OK_OK : MENU_OK_DISABLE);

    // set kb focus to first member of first active group, if any
    int focus_idx = -1;
    for (int i = 0; i < menu.n_items; i++) {
        if (MENU_ACTIVE(menu.items[i].type)) {
            focus_idx = i;
            break;
        }
    }

    // operations are severely restricted if there are no active menu items
    // N.B. code below will CRASH if focus < 0.
    bool is_active_menu = (focus_idx >= 0);

    UserInput ui = {
        menu.menu_b,
        UI_UFuncNone,
        UF_UNUSED,
        MENU_TIMEOUT,
        menu.update_clocks,
        {0, 0}, TT_NONE, '\0', false, false
    };

    // run
    bool ok = false;
    while (waitForUser (ui)) {

        // OK if check for Enter or tap in ok
        if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || inBox (ui.tap, menu.ok_b)) {

            if (!menuStateOk (menu))
                continue;

            // done unless a text field reports an error
            ok = true;

            // must also check MenuText field test function if used
            for (int i = 0; ok && i < menu.n_items; i++) {
                if (menu.items[i].type == MENU_TEXT) {
                    MenuItem &mi = menu.items[i];
                    MenuText *tfp = mi.textf;
                    Message ynot;
                    if (tfp->text_fp && !(tfp->text_fp)(tfp, ynot)) {
                        SBox &pb = pick_boxes[i];
                        drawMenuTextMsg (mi, pb, ynot.get());
                        ok = false;
                    }
                }
            }

            // finished if still ok
            if (ok)
                break;
        }

        // finished w/o ok if type ESC or tap cancel box or tap anywhere outside the menu box
        if (ui.kb_char == CHAR_ESC                                                      // type ESC
                        || (menu.cancel == M_CANCELOK && inBox (ui.tap, cancel_b))      // tap cancel
                        || (ui.kb_char == CHAR_NONE && !inBox (ui.tap, menu.menu_b))    // tap outside
                    ) {
            break;
        }

        // check for kb or tap control
        if (is_active_menu) {
            if (ui.kb_char) {
                if (menu.items[focus_idx].type == MENU_TEXT)
                    focus_idx = textFieldEdit (menu, pick_boxes, focus_idx, ui.kb_char);
                else
                    focus_idx = kbNavigation (menu, pick_boxes, focus_idx, ui.kb_char);
            } else {
                focus_idx = tapNavigation (menu, pick_boxes, focus_idx, ui.tap);

                // opt-in: let a tap that selects a MENU_1OFN item act immediately, skipping
                // the separate Ok confirmation most menus require. inBox() check rules out a
                // tap that missed every item (tapNavigation() leaves focus_idx unchanged in
                // that case) coincidentally leaving focus_idx pointed at an item that was
                // already selected from an earlier tap -- without it, a stray tap on blank
                // space inside the menu could wrongly trigger an exit.
                if (menu.instant_1ofn && menu.items[focus_idx].type == MENU_1OFN
                            && menu.items[focus_idx].set
                            && inBox (ui.tap, pick_boxes[focus_idx])
                            && menuStateOk (menu)) {
                    ok = true;
                    break;
                }
            }

            // refresh Ok button state
            menuRedrawOk (menu.ok_b, menuStateOk(menu) ? MENU_OK_OK : MENU_OK_DISABLE);
        }
    }

    // done
    drainTouch();

    // restore contents
    if (!tft.setBackingStore (backing_store, menu.menu_b.x, menu.menu_b.y, menu.menu_b.w, menu.menu_b.h))
        fatalError ("mem pixel restore failed %d x %d", menu.menu_b.w, menu.menu_b.h);

    // If the menu was over the map, publish the restored pixels immediately.
    // On lazy redraw paths, otherwise the browser can still be looking at a dead menu.
    if (boxesOverlap (menu.menu_b, map_b))
        tft.drawPR();

    // record settings
    Serial.printf ("Menu result after %s:\n", ok ? "Ok" : "Cancel");
    for (int i = 0; i < menu.n_items; i++) {
        MenuItem &mi = menu.items[i];
        if (MENU_ACTIVE(mi.type))
            Serial.printf ("  %-15s g%d s%d\n", mi.label ? mi.label : "", mi.group, mi.set);
    }

    return (ok);
}

/* show a brief message in the given color in the given box, generally a box that had been used for a menu
 */
void menuMsg (const SBox &box, uint16_t color, const char *msg)
{
    uint8_t *backing_store;
    if (!tft.getBackingStore (backing_store, box.x, box.y, box.w, box.h))
        fatalError ("menuMsg '%s' bad pixels capture beneath %d+%d-%dx%d menu", msg, box.x,box.y,box.w,box.h);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (box.x + (box.w - getTextWidth(msg))/2, box.y + box.h/3);
    tft.setTextColor (color);
    fillSBox (box, RA8875_BLACK);
    drawSBox (box, RA8875_WHITE);
    tft.print (msg);
    wdDelay(2000);

    if (!tft.setBackingStore (backing_store, box.x, box.y, box.w, box.h))
        fatalError ("menuMsg '%s' bad pixels restore beneath %d+%d-%dx%d menu", msg, box.x,box.y,box.w,box.h);
}


/* redraw the given ok box in the given visual state.
 * used to allow caller to provide busy or error feedback.
 * N.B. we assume ok_b is same as passed to runMenu and remains unchanged since its return.
 */
void menuRedrawOk (SBox &ok_b, MenuOkState oks)
{
    switch (oks) {
    case MENU_OK_OK:
        tft.setTextColor (MENU_FGC);
        fillSBox (ok_b, MENU_BGC);
        drawSBox (ok_b, MENU_FGC);
        break;
    case MENU_OK_BUSY:
        tft.setTextColor (MENU_BGC);
        fillSBox (ok_b, MENU_BSYC);
        drawSBox (ok_b, MENU_FGC);
        break;
    case MENU_OK_ERR:
        tft.setTextColor (MENU_BGC);
        fillSBox (ok_b, MENU_ERRC);
        drawSBox (ok_b, MENU_FGC);
        break;
    case MENU_OK_DISABLE:
        tft.setTextColor (GRAY);
        fillSBox (ok_b, MENU_BGC);
        drawSBox (ok_b, GRAY);
        break;
    }

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t ok_txt_w = getTextWidth (ok_label);
    uint16_t ok_tx = ok_b.x + (ok_b.w > ok_txt_w ? (ok_b.w - ok_txt_w)/2 : MENU_BDX);
    tft.setCursor (ok_tx, ok_b.y + MENU_BDROP);
    tft.print (ok_label);

    // immediate draw if over map
    if (boxesOverlap (ok_b, map_b))
        tft.drawPR();
}

/* wait until:
 *   a tap occurs:                                   set tap location and type and return true.
 *   a char is typed:                                set kb_char/ctrl/shift and return true.
 *   (*fp)() (IFF fp != NULL) returns true:          set fp_true to true and return false.
 *   to_ms is > 0 and nothing happens for that long: return false.
 * while waiting we optionally update clocks and allow some web server commands.
 * return true if user typed or tapped, else false if timed out or ui.fp return true.
 */
bool waitForUser (UserInput &ui)
{
    // initial timeout
    uint32_t t0 = millis();

    // reset all actions until they happen here
    ui.kb_ctrl = ui.kb_shift = false;
    ui.kb_char = CHAR_NONE;
    ui.tap = {0, 0};
    ui.tt = TT_NONE;

    for(;;) {

        if ((ui.tt = readCalTouchWS(ui.tap)) != TT_NONE)
            return(true);

        ui.kb_char = tft.getChar (&ui.kb_ctrl, &ui.kb_shift);
        if (ui.kb_char != CHAR_NONE)
            return (true);

        if (ui.to_ms != UI_NOTIMEOUT && timesUp (&t0, ui.to_ms))
            return (false);

        if (ui.fp != UI_UFuncNone && (*ui.fp)()) {
            ui.fp_true = UF_TRUE;
            return (false);
        }

        if (ui.update_clocks == UF_CLOCKSOK)
            updateClocks(false);

        wdDelay (10);

        // refresh protected region in case X11 window is moved
        if (boxesOverlap (ui.inbox, map_b))
            tft.drawPR();
    }
}

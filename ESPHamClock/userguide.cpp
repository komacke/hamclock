/* User Guide QR code modal and book icon.
 *
 * Displays an open-book icon underneath the MOTD icon next to the UTC button.
 * When clicked, shows a popup modal with a QR code linking to the User Guide PDF,
 * an "Open in Browser" button for desktop/web users, and a "Close" button.
 */

#include "HamClock.h"
#include "qrcodegen.h"

#define USER_GUIDE_URL "https://ohb.hamclock.app/ham/HamClock/HamClockUserGuide.pdf"

// Icon geometry: 14x14 box, positioned underneath the MOTD mailbox icon
#define UG_ICON_W 14
#define UG_ICON_H 14

SBox userguide_btn_b;

/* Draw the open-book user guide icon below the MOTD mailbox.
 */
void drawUserGuideIcon (void)
{
    // Position directly under motd_btn_b
    userguide_btn_b.x = clock_b.x + clock_b.w - 2 * UG_ICON_W - 6;
    userguide_btn_b.y = clock_b.y + 18;
    userguide_btn_b.w = UG_ICON_W;
    userguide_btn_b.h = UG_ICON_H;

    // Blank the icon slot first
    tft.fillRect (userguide_btn_b.x, userguide_btn_b.y, userguide_btn_b.w, userguide_btn_b.h, RA8875_BLACK);

    uint16_t icon_col = GRAY;
    uint16_t x = userguide_btn_b.x;
    uint16_t y = userguide_btn_b.y;

    // Open book drawing:
    // Center vertical spine
    tft.drawLine (x + 7, y + 2, x + 7, y + 10, icon_col);

    // Left page: top slope up-left, left edge down, bottom slope down-right
    tft.drawLine (x + 7, y + 2, x + 2, y + 4, icon_col);
    tft.drawLine (x + 2, y + 4, x + 2, y + 10, icon_col);
    tft.drawLine (x + 2, y + 11, x + 7, y + 10, icon_col);

    // Right page: top slope up-right, right edge down, bottom slope down-left
    tft.drawLine (x + 7, y + 2, x + 12, y + 4, icon_col);
    tft.drawLine (x + 12, y + 4, x + 12, y + 10, icon_col);
    tft.drawLine (x + 12, y + 11, x + 7, y + 10, icon_col);

    // Internal page detail horizontal lines
    tft.drawLine (x + 4, y + 6, x + 5, y + 6, icon_col);
    tft.drawLine (x + 4, y + 8, x + 5, y + 8, icon_col);
    tft.drawLine (x + 8, y + 6, x + 9, y + 6, icon_col);
    tft.drawLine (x + 8, y + 8, x + 9, y + 8, icon_col);
}

/* Modal popup for the User Guide QR Code.
 */
static void userGuideShowModal (void)
{
    // Generate QR code
    uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText (USER_GUIDE_URL, tempBuffer, qr0,
                                    qrcodegen_Ecc_LOW,
                                    qrcodegen_VERSION_MIN, 10,
                                    qrcodegen_Mask_AUTO, true);
    if (!ok) {
        Serial.printf ("UserGuide: failed to encode QR code for %s\n", USER_GUIDE_URL);
        return;
    }

    int qr_size = qrcodegen_getSize (qr0); // number of modules per side
    int scale = 3;                         // 3 app pixels per module (tft auto-scales by SCALESZ)
    int border = 8;                        // quiet zone in pixels
    int qr_px = qr_size * scale;
    int qr_box_w = qr_px + 2 * border;

    // Dialog layout:
    int dlg_w = qr_box_w + 30;
    if (dlg_w < 200) dlg_w = 200;

    int title_h = 16;
    int sub_h = 12;
    int btn_h = 18;
    int pad = 8;

    int dlg_h = pad + title_h + pad/2 + sub_h + pad + qr_box_w + pad + btn_h + pad;

    SBox dlg_b;
    dlg_b.w = dlg_w;
    dlg_b.h = dlg_h;
    // Center dialog on the map so tft.drawPR() flushes properly
    dlg_b.x = map_b.x + (map_b.w - dlg_w) / 2;
    dlg_b.y = map_b.y + (map_b.h - dlg_h) / 2;

    // Save backing store to restore pixels underneath
    uint8_t *backing_store = NULL;
    if (!tft.getBackingStore (backing_store, dlg_b.x, dlg_b.y, dlg_b.w, dlg_b.h)) {
        Serial.printf ("UserGuide: failed to get backing store\n");
        return;
    }

    // Save font style
    FontWeight saved_fw;
    FontSize saved_fs;
    getFontStyle (&saved_fw, &saved_fs);

    // Draw dialog card background & border
    fillSBox (dlg_b, RGB565(20, 20, 20));
    drawSBox (dlg_b, GRAY);

    // Title: "HamClock User Guide"
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    const char *title = "HamClock User Guide";
    uint16_t tw = getTextWidth (title);
    tft.setCursor (dlg_b.x + (dlg_w - tw) / 2, dlg_b.y + pad + 2);
    tft.print (title);

    // Subtitle: "Scan with phone or open below"
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (GRAY);
    const char *sub = "Scan with phone or open below";
    uint16_t sw = getTextWidth (sub);
    tft.setCursor (dlg_b.x + (dlg_w - sw) / 2, dlg_b.y + pad + title_h + pad/2);
    tft.print (sub);

    // QR Code container (white quiet zone background)
    int qr_x0 = dlg_b.x + (dlg_w - qr_box_w) / 2;
    int qr_y0 = dlg_b.y + pad + title_h + pad/2 + sub_h + pad;
    tft.fillRect (qr_x0, qr_y0, qr_box_w, qr_box_w, RA8875_WHITE);

    // Draw QR modules
    for (int y = 0; y < qr_size; y++) {
        for (int x = 0; x < qr_size; x++) {
            if (qrcodegen_getModule (qr0, x, y)) {
                tft.fillRect (qr_x0 + border + x * scale,
                              qr_y0 + border + y * scale,
                              scale, scale, RA8875_BLACK);
            }
        }
    }

    // Buttons
    // Left button: "Open in Browser", Right button: "Close"
    int btn_margin = 10;
    int btn_gap = 8;
    int avail_w = dlg_w - 2 * btn_margin - btn_gap;
    int open_btn_w = (avail_w * 65) / 100;
    int close_btn_w = avail_w - open_btn_w;
    int btn_y = qr_y0 + qr_box_w + pad;

    SBox open_b;
    open_b.x = dlg_b.x + btn_margin;
    open_b.y = btn_y;
    open_b.w = open_btn_w;
    open_b.h = btn_h;

    SBox close_b;
    close_b.x = open_b.x + open_b.w + btn_gap;
    close_b.y = btn_y;
    close_b.w = close_btn_w;
    close_b.h = btn_h;

    fillSBox (open_b, RGB565(50, 50, 50));
    drawSBox (open_b, GRAY);
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    const char *open_lbl = "Open in Browser";
    uint16_t ow = getTextWidth (open_lbl);
    tft.setCursor (open_b.x + (open_b.w - ow) / 2, open_b.y + (btn_h - 8) / 2);
    tft.print (open_lbl);

    fillSBox (close_b, RGB565(30, 80, 150));
    drawSBox (close_b, RA8875_WHITE);
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    const char *close_lbl = "Close";
    uint16_t cw = getTextWidth (close_lbl);
    tft.setCursor (close_b.x + (close_b.w - cw) / 2, close_b.y + (btn_h - 8) / 2);
    tft.print (close_lbl);

    // If over map, flush to web/display
    if (boxesOverlap (dlg_b, map_b))
        tft.drawPR();

    // User input loop
    UserInput ui = {
        dlg_b,
        UI_UFuncNone,
        UF_UNUSED,
        30000,          // 30 second timeout
        UF_CLOCKSOK,
        {0,0}, TT_NONE, '\0', false, false
    };

    bool open_browser = false;
    while (waitForUser (ui)) {
        if (ui.kb_char == CHAR_ESC || ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL) {
            break;
        }
        if (inBox (ui.tap, open_b)) {
            open_browser = true;
            break;
        }
        if (inBox (ui.tap, close_b) || !inBox (ui.tap, dlg_b)) {
            break;
        }
    }
    drainTouch();

    // Restore screen pixels
    if (!tft.setBackingStore (backing_store, dlg_b.x, dlg_b.y, dlg_b.w, dlg_b.h))
        fatalError ("failed to restore pixels beneath user guide dialog");

    selectFontStyle (saved_fw, saved_fs);

    if (boxesOverlap (dlg_b, map_b))
        tft.drawPR();

    if (open_browser) {
        Serial.printf ("UserGuide: opening %s\n", USER_GUIDE_URL);
        openURLPopup (USER_GUIDE_URL);
    }
}

/* return whether touch event at s is on the user guide icon.
 */
bool checkUserGuideTouch (SCoord &s)
{
    #define UG_HIT_PAD 4
    if (userguide_btn_b.w == 0) {
        userguide_btn_b.x = clock_b.x + clock_b.w - 2 * UG_ICON_W - 6;
        userguide_btn_b.y = clock_b.y + 18;
        userguide_btn_b.w = UG_ICON_W;
        userguide_btn_b.h = UG_ICON_H;
    }
    SBox pb = userguide_btn_b;
    pb.x -= UG_HIT_PAD;
    pb.y -= UG_HIT_PAD;
    pb.w += 2 * UG_HIT_PAD;
    pb.h += 2 * UG_HIT_PAD;

    Serial.printf ("UserGuide: checkUserGuideTouch s=(%d,%d) userguide_btn_b=(%d,%d %dx%d)\n",
                   s.x, s.y, userguide_btn_b.x, userguide_btn_b.y, userguide_btn_b.w, userguide_btn_b.h);

    if (inBox (s, pb)) {
        userGuideShowModal();
        return (true);
    }
    return (false);
}

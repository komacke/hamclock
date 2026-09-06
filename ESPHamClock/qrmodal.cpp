/*
 * General-purpose QR Code modal dialog.
 *
// Encodes the given URL into a QR Code using qrcodegen, displays a centered card
// with title, optional subtitle, quiet zone, and two action buttons:
// "Open" (default / highlighted) and "Close".
//
// Supports keyboard navigation:
//   - Left / Right / Tab: toggle button focus
//   - Enter / Space: activate focused button
//   - ESC: dismiss without action
//
// Returns true if the user confirmed/opened, false if dismissed.
 */

#include "HamClock.h"
#include "qrcodegen.h"

bool showQRCodeModal (const char *url, const char *title, const char *subtitle, const char *open_lbl)
{
    if (!url || !title)
        return (false);

    if (!open_lbl)
        open_lbl = "Open Link";

    // Generate QR code
    uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText (url, tempBuffer, qr0,
                                     qrcodegen_Ecc_LOW,
                                     qrcodegen_VERSION_MIN, 10,
                                     qrcodegen_Mask_AUTO, true);
    if (!ok) {
        Serial.printf ("QRModal: failed to encode QR code for %s\n", url);
        return (false);
    }

    int qr_size = qrcodegen_getSize (qr0); // number of modules per side
    int scale = 3;                          // 3 app pixels per module (tft auto-scales by SCALESZ)
    int border = 8;                        // quiet zone in pixels
    int qr_px = qr_size * scale;
    int qr_box_w = qr_px + 2 * border;
    // Check if browser opening is supported
    bool is_live_session = isLiveWebTouch();
    bool can_open_browser = true;
#if defined(_USE_FB0)
    // On standalone fb0 there is no window manager or browser unless accessed via Live Web
    if (!is_live_session)
        can_open_browser = false;
#endif

    const char *display_subtitle = subtitle;
    if (!can_open_browser && subtitle && strstr(subtitle, "open below"))
        display_subtitle = "Scan with phone or tablet";

    // Font metrics for sizing
    selectFontStyle (BOLD_FONT, FAST_FONT);
    uint16_t title_w = getTextWidth ((char*)title);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t sub_w = display_subtitle ? getTextWidth ((char*)display_subtitle) : 0;
    // Dialog layout: accommodate QR code and text widths
    int dlg_w = qr_box_w + 30;
    if (dlg_w < title_w + 24)
        dlg_w = title_w + 24;
    if (subtitle && dlg_w < sub_w + 24)
        dlg_w = sub_w + 24;
    if (dlg_w < 200)
        dlg_w = 200;

    int title_h = 16;
    int sub_h = subtitle ? 12 : 0;
    int btn_h = 18;
    int pad = 8;

    int dlg_h = pad + title_h + (subtitle ? (pad/2 + sub_h) : 0) + pad + qr_box_w + pad + btn_h + pad;

    SBox dlg_b;
    dlg_b.w = dlg_w;
    dlg_b.h = dlg_h;
    // Center dialog on the map so tft.drawPR() flushes properly
    dlg_b.x = map_b.x + (map_b.w - dlg_w) / 2;
    dlg_b.y = map_b.y + (map_b.h - dlg_h) / 2;

    // Save backing store to restore pixels underneath
    uint8_t *backing_store = NULL;
    if (!tft.getBackingStore (backing_store, dlg_b.x, dlg_b.y, dlg_b.w, dlg_b.h)) {
        Serial.printf ("QRModal: failed to get backing store\n");
        return (false);
    }

    // Save font style
    FontWeight saved_fw;
    FontSize saved_fs;
    getFontStyle (&saved_fw, &saved_fs);

    // Draw dialog card background & border
    fillSBox (dlg_b, RGB565(20, 20, 20));
    drawSBox (dlg_b, GRAY);

    // Title
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (dlg_b.x + (dlg_w - title_w) / 2, dlg_b.y + pad + 2);
    tft.print (title);

    // Subtitle (if provided)
    if (display_subtitle) {
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (GRAY);
        tft.setCursor (dlg_b.x + (dlg_w - sub_w) / 2, dlg_b.y + pad + title_h + pad/2);
        tft.print (display_subtitle);
    }

    // QR Code container (white quiet zone background)
    int qr_x0 = dlg_b.x + (dlg_w - qr_box_w) / 2;
    int qr_y0 = dlg_b.y + pad + title_h + (subtitle ? (pad/2 + sub_h) : 0) + pad;
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

    // Buttons:
    // If can_open_browser is false (e.g. standalone fb0 without Live Web), display a single centered "Close" button.
    // Otherwise, display Left button (Open action) and Right button ("Close").
    int btn_margin = 10;
    int btn_y = qr_y0 + qr_box_w + pad;

    SBox open_b = {0, 0, 0, 0};
    SBox close_b = {0, 0, 0, 0};

    if (can_open_browser) {
        int btn_gap = 8;
        int avail_w = dlg_w - 2 * btn_margin - btn_gap;
        int open_btn_w = avail_w / 2;
        int close_btn_w = avail_w - open_btn_w;

        open_b.x = dlg_b.x + btn_margin;
        open_b.y = btn_y;
        open_b.w = open_btn_w;
        open_b.h = btn_h;

        close_b.x = open_b.x + open_b.w + btn_gap;
        close_b.y = btn_y;
        close_b.w = close_btn_w;
        close_b.h = btn_h;
    } else {
        // Single centered Close button
        int close_btn_w = dlg_w - 2 * btn_margin;
        if (close_btn_w > 120)
            close_btn_w = 120;

        close_b.x = dlg_b.x + (dlg_w - close_btn_w) / 2;
        close_b.y = btn_y;
        close_b.w = close_btn_w;
        close_b.h = btn_h;
    }

    auto drawModalButtons = [&] (bool open_active) {
        if (can_open_browser) {
            // Open/action button
            fillSBox (open_b, open_active ? RGB565(30, 80, 150) : RGB565(40, 40, 40));
            drawSBox (open_b, open_active ? RA8875_WHITE : GRAY);
            selectFontStyle (BOLD_FONT, FAST_FONT);
            tft.setTextColor (RA8875_WHITE);
            uint16_t ow = getTextWidth ((char*)open_lbl);
            tft.setCursor (open_b.x + (open_b.w - ow) / 2, open_b.y + (btn_h - 8) / 2);
            tft.print (open_lbl);

            // Close button
            fillSBox (close_b, !open_active ? RGB565(30, 80, 150) : RGB565(40, 40, 40));
            drawSBox (close_b, !open_active ? RA8875_WHITE : GRAY);
            selectFontStyle (BOLD_FONT, FAST_FONT);
            tft.setTextColor (RA8875_WHITE);
            const char *close_lbl = "Close";
            uint16_t cw = getTextWidth ((char*)close_lbl);
            tft.setCursor (close_b.x + (close_b.w - cw) / 2, close_b.y + (btn_h - 8) / 2);
            tft.print (close_lbl);
        } else {
            // Single Close button, styled highlighted/active
            fillSBox (close_b, RGB565(30, 80, 150));
            drawSBox (close_b, RA8875_WHITE);
            selectFontStyle (BOLD_FONT, FAST_FONT);
            tft.setTextColor (RA8875_WHITE);
            const char *close_lbl = "Close";
            uint16_t cw = getTextWidth ((char*)close_lbl);
            tft.setCursor (close_b.x + (close_b.w - cw) / 2, close_b.y + (btn_h - 8) / 2);
            tft.print (close_lbl);
        }

        if (boxesOverlap (dlg_b, map_b))
            tft.drawPR();
    };

    bool open_focused = can_open_browser;
    drawModalButtons (open_focused);

    // User input loop
    UserInput ui = {
        dlg_b,
        UI_UFuncNone,
        UF_UNUSED,
        30000,          // 30 second timeout
        UF_CLOCKSOK,
        {0,0}, TT_NONE, '\0', false, false
    };

    bool open_confirmed = false;
    while (waitForUser (ui)) {
        if (isLiveWebTouch())
            is_live_session = true;
        if (ui.kb_char == CHAR_TAB || ui.kb_char == CHAR_LEFT || ui.kb_char == CHAR_RIGHT) {
            if (can_open_browser) {
                open_focused = !open_focused;
                drawModalButtons (open_focused);
            }
            continue;
        }
        if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == ' ') {
            // Select currently highlighted choice
            if (can_open_browser)
                open_confirmed = open_focused;
            else
                open_confirmed = false;     // Only "Close" button exists, so selecting it closes
            break;
        }
        if (ui.kb_char == CHAR_ESC) {
            open_confirmed = false;
            break;
        }
        if (can_open_browser && inBox (ui.tap, open_b)) {
            open_confirmed = true;
            break;
        }
        if (inBox (ui.tap, close_b) || !inBox (ui.tap, dlg_b)) {
            open_confirmed = false;
            break;
        }
    }
    drainTouch();

    // Restore screen pixels
    if (!tft.setBackingStore (backing_store, dlg_b.x, dlg_b.y, dlg_b.w, dlg_b.h))
        fatalError ("failed to restore pixels beneath QR code dialog");

    selectFontStyle (saved_fw, saved_fs);

    if (boxesOverlap (dlg_b, map_b))
        tft.drawPR();

    if (open_confirmed) {
        Serial.printf ("QRModal: opening %s (is_live=%d)\n", url, is_live_session);
        if (is_live_session)
            cur_touch_live = true;
        openURLPopup (url);
    }

    return (open_confirmed);
}

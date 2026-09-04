/*
 * User Guide QR code modal and book icon.
 *
 * Displays an open-book icon underneath the MOTD icon next to the UTC button.
 * When clicked, shows a popup modal with a QR code linking to the User Guide PDF,
 * an "Open Guide" button for desktop/web users, and a "Close" button.
 */

#include "HamClock.h"

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

    if (inBox (s, pb)) {
        showQRCodeModal (USER_GUIDE_URL, "HamClock User Guide", "Scan with phone or open below", "Open Guide");
        return (true);
    }
    return (false);
}

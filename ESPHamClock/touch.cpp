/* handle the touch screen
 */



#include "HamClock.h"


/* read keyboard char and check for warp cursor if hjkl or engage cr/lf/space
 * N.B. ignore multiple rapid engages
 */
TouchType checkKBWarp (SCoord &s)
{
    TouchType tt = TT_NONE;
    s.x = s.y = 0;

#if !defined(_WEB_ONLY)

    // ignore if don't want warping
    if (!want_kbcursor)
        return (TT_NONE);

    bool control, shift;
    char c = tft.getChar (&control, &shift);
    if (c) {

        switch (c) {

        case CHAR_LEFT: case CHAR_DOWN: case CHAR_UP: case CHAR_RIGHT:
            // warp
            {
                unsigned n = 1;
                if (shift)
                    n *= 2;
                if (control)
                    n *= 4;
                int x, y;
                if (tft.warpCursor (c, n, &x, &y)) {
                    s.x = x;
                    s.y = y;
                }
            }
            break;

        case CHAR_CR: case CHAR_NL: case CHAR_SPACE:
            // engage
            {
                static uint32_t prev_engage_ms;
                static int n_fast_engages;
                uint32_t engage_ms = millis();
                bool engage_rate_ok = engage_ms - prev_engage_ms > 1000;
                bool engage_ok = engage_rate_ok || ++n_fast_engages < 10;

                if (engage_ok && tft.getMouse (&s.x, &s.y))
                    tt = TT_TAP;
                if (engage_rate_ok)
                    n_fast_engages = 0;
                else if (!engage_ok)
                    Serial.printf ("Keyboard functions are too fast\n");

                prev_engage_ms = engage_ms;
            }
            break;

        default:
            // ignore all other chars
            break;

        }
    }
#endif // !_WEB_ONLY

    return (tt);
}

#define TOUCH_LONGPRESS_MS   500       // ms to trigger secondary tap (matches live web)
#define TOUCH_JITTER_DIST    10        // max app pixel drift to still consider a hold

static bool touch_down;                // whether a touch is currently active
static uint32_t touch_t0;              // millis when touch started
static SCoord touch_s0;                // initial touch location
static bool touch_hold_fired;          // whether long-press already fired for this touch

/* reset touch hold tracking
 */
static void resetTouchHold()
{
    touch_down = false;
    touch_hold_fired = false;
}

/* read the touch screen or mouse.
 * pass back calibrated screen coordinate and return a TouchType.
 * button 1 touches held for TOUCH_LONGPRESS_MS yield TT_TAP_BX (secondary tap).
 */
TouchType readCalTouch (SCoord &s)
{
    TouchType tt = TT_NONE;

    if (tft.touched()) {
        int mb = 1;
        SCoord cur_s;
        while (tft.touched()) {
            tft.touchRead (&cur_s.x, &cur_s.y, &mb);
        }

        // Hardware mouse alternate button (right/middle click) always acts immediately
        if (mb != 1) {
            resetTouchHold();
            s = cur_s;
            tt = TT_TAP_BX;
        } else {
            // Button 1 (touchscreen or left click)
            uint32_t now = millis();
            if (!touch_down) {
                touch_down = true;
                touch_t0 = now;
                touch_s0 = cur_s;
                touch_hold_fired = false;
            } else {
                // If moved too far, reset hold starting point/timer
                int dx = (int)cur_s.x - (int)touch_s0.x;
                int dy = (int)cur_s.y - (int)touch_s0.y;
                if (dx*dx + dy*dy > TOUCH_JITTER_DIST * TOUCH_JITTER_DIST) {
                    touch_t0 = now;
                    touch_s0 = cur_s;
                    touch_hold_fired = false;
                } else if (!touch_hold_fired && (now - touch_t0 >= TOUCH_LONGPRESS_MS)) {
                    // Fire secondary tap on long-press
                    touch_hold_fired = true;
                    s = touch_s0;
                    tt = TT_TAP_BX;
                }
            }
        }
    } else {
        // Not touched now: check if just released
        if (touch_down) {
            if (!touch_hold_fired) {
                // Released before long-press threshold: emit regular tap
                s = touch_s0;
                tt = TT_TAP;
            }
            resetTouchHold();
        }
    }

    if (tt != TT_NONE)
        Serial.printf("Touch: \t%4d %4d\ttype %d\n", s.x, s.y, (int)tt);

    // return tap type
    return (tt);
}


/* drain pending touch and kb
 */
void drainTouch()
{
    resetTouchHold();

    uint16_t tx, ty;
    while (tft.touched())
        tft.touchRead (&tx, &ty, NULL);

    wifi_tt = TT_NONE;
    wifi_tt_s = {0, 0};
    wifi_tt_live = false;

    bool control, shift;
    while (tft.getChar (&control, &shift) != CHAR_NONE)
        continue;
}

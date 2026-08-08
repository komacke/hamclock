/* plot management
 * each PlotPane is in one of PlotChoice state at any given time, all must be different.
 * each pane rotates through the set of bits in its rotset.
 */

// must come before HamClock.h: ArduinoLib/Wire.h #defines byte as uint8_t, which corrupts
// <functional>'s internal std::byte declaration if <functional> is parsed after that macro
// exists. Including it first lets it fully expand under its header guard before that happens.
#include <functional>
#include <algorithm>
#include "HamClock.h"


const SBox plot_b[PANE_N] = {
    {0,   148, PLOTBOX0_W,   PLOTBOX0_H},
    {235, 0,   PLOTBOX123_W, PLOTBOX123_H},
    {405, 0,   PLOTBOX123_W, PLOTBOX123_H},
    {575, 0,   PLOTBOX123_W, PLOTBOX123_H},
};
PlotChoice plot_ch[PANE_N];
PlotMask plot_rotset[PANE_N];
PlotMask plot_rothold;
PlotPane menu_open_for_pane = PANE_NONE;   // see HamClock.h

/* true if choice pc is in pane pp's current rotation set. PLOT_CH_NONE has no bit. */
static bool paneHasChoice (PlotPane pp, PlotChoice pc)
{
    return ((plot_rotset[pp] & PLOTBIT(pc)) != 0);
}

#define X(a,b)  b,                      // expand names while preserving the NONE hole at value 32
const char *plot_names[PLOT_CH_N] = {
    PLOTNAMES_LOW
    "NONE",
    PLOTNAMES_HIGH
};
#undef X

/* retrieve the plot choice for the given pane from NV, if set and valid
 */
static bool getPlotChoiceNV (PlotPane new_pp, PlotChoice *new_pc)
{
    bool ok = false;
    uint8_t pc;

    switch (new_pp) {
    case PANE_0:
        // only Pane 0 can be NONE
        ok = NVReadUInt8 (NV_PLOT_0, &pc) && (PLOT_CH_IS_REAL(pc) || pc == PLOT_CH_NONE);
        break;
    case PANE_1:
        ok = NVReadUInt8 (NV_PLOT_1, &pc) && PLOT_CH_IS_REAL(pc);
        break;
    case PANE_2:
        ok = NVReadUInt8 (NV_PLOT_2, &pc) && PLOT_CH_IS_REAL(pc);
        break;
    case PANE_3:
        ok = NVReadUInt8 (NV_PLOT_3, &pc) && PLOT_CH_IS_REAL(pc);
        break;
    case PANE_N:
        break;

    // default: no default in order to check coverage at compile time

    }

    if (ok)
        *new_pc = (PlotChoice)pc;

    return (ok);
}

/* set the current choice for the given pane to any one of rotset, or a default if none.
 */
static void setDefaultPaneChoice (PlotPane pp)
{
    // check rotset first
    if (plot_rotset[pp]) {
        for (int i = 0; i < PLOT_CH_N; i++) {
            if (plot_rotset[pp] & PLOTBIT(i)) {
                plot_ch[pp] = (PlotChoice) i;
                break;
            }
        }
    } else {
        // default for PANE_0 is PLOT_CH_NONE, others are from a standard set
        if (pp == PANE_0) {
            plot_ch[pp] = PLOT_CH_NONE;
            Serial.println ("PANE: Setting pane 0 to default NONE");
        } else {
            const PlotChoice ch_defaults[PANE_N-1] = {PLOT_CH_SSN, PLOT_CH_XRAY, PLOT_CH_SDO};
            plot_ch[pp] = ch_defaults[pp-1];
            plot_rotset[pp] = PLOTBIT(plot_ch[pp]);
            Serial.printf ("PANE: Setting pane %d to default %s\n", (int)pp, plot_names[plot_ch[pp]]);
        }
    }
}

/* qsort-style function to compare pointers to two MenuItems by their string names
 */
static int menuChoiceQS (const void *p1, const void *p2)
{
    return (strcmp (((MenuItem*)p1)->label, ((MenuItem*)p2)->label));
}

/* return whether the given choice is currently physically available on this platform.
 * N.B. does not consider whether in use by panes -- for that use findPaneForChoice()
 */
bool plotChoiceIsAvailable (PlotChoice pc)
{
    switch (pc) {

    case PLOT_CH_DXCLUSTER:     return (useDXCluster());
    case PLOT_CH_GIMBAL:        return (haveGimbal());
    case PLOT_CH_TEMPERATURE:   return (getNBMEConnected() > 0);
    case PLOT_CH_PRESSURE:      return (getNBMEConnected() > 0);
    case PLOT_CH_HUMIDITY:      return (getNBMEConnected() > 0);
    case PLOT_CH_DEWPOINT:      return (getNBMEConnected() > 0);
    case PLOT_CH_COUNTDOWN:     return (getSWEngineState(NULL,NULL) == SWE_COUNTDOWN);
    case PLOT_CH_DEWX:          return ((brb_rotset & (1u << BRB_SHOW_DEWX)) == 0);
    case PLOT_CH_DXWX:          return ((brb_rotset & (1u << BRB_SHOW_DXWX)) == 0);
    case PLOT_CH_ADIF:          return (getADIFilename() != NULL);

    // the remaining pane type are always available

    case PLOT_CH_BC:            // fallthru
    case PLOT_CH_FLUX:          // fallthru
    case PLOT_CH_KP:            // fallthru
    case PLOT_CH_MOON:          // fallthru
    case PLOT_CH_NOAASPW:       // fallthru
    case PLOT_CH_SSN:           // fallthru
    case PLOT_CH_XRAY:          // fallthru
    case PLOT_CH_SDO:           // fallthru
    case PLOT_CH_SOLWIND:       // fallthru
    case PLOT_CH_DRAP:          // fallthru
    case PLOT_CH_CONTESTS:      // fallthru
    case PLOT_CH_PSK:           // fallthru
    case PLOT_CH_BZBT:          // fallthru
    case PLOT_CH_ONTA:          // fallthru
    case PLOT_CH_AURORA:        // fallthru
    case PLOT_CH_DXPEDS:        // fallthru
    case PLOT_CH_DST:           // fallthru
    case PLOT_CH_STORMS:        // fallthru
    case PLOT_CH_ACTIVENETS:    // fallthru
    case PLOT_CH_LAUNCHES:	// fallthru
    case PLOT_CH_HFCOND:        // fallthru
    case PLOT_CH_VHFCOND:       // fallthru
    case PLOT_CH_SATACT:        // fallthru
    case PLOT_CH_MESHTASTIC:    // fallthru
        return (true);

    case PLOT_CH_NONE:         // fallthru
    case PLOT_CH_N:
        break;                  // lint
    }

    return (false);

}

/* log the rotation set for the given pain, tag PlotChoice if in the set.
 */
void logPaneRotSet (PlotPane pp, PlotChoice pc)
{
    Serial.printf ("Pane %d choices:\n", (int)pp);
    for (int i = 0; i < PLOT_CH_N; i++)
        if (plot_rotset[pp] & PLOTBIT(i))
            Serial.printf ("    %c%s\n", i == pc ? '*' : ' ', plot_names[i]);
}

/* log the BRB rotation set
 */
void logBRBRotSet()
{
    Serial.printf ("BRB: choices:\n");
    for (int i = 0; i < BRB_N; i++)
        if (brb_rotset & (1u << i))
            Serial.printf ("    %c%s\n", i == brb_mode ? '*' : ' ', brb_names[i]);
    Serial.printf ("BRB: now mode %d\n", brb_mode);
}

/* if the given rotset include PLOT_CH_COUNTDOWN and more, show message in box and return true.
 * else return false.
 */
bool enforceCDownAlone (const SBox &box, PlotMask rotset)
{
    if ((rotset & PLOTBIT(PLOT_CH_COUNTDOWN)) && (rotset & ~PLOTBIT(PLOT_CH_COUNTDOWN))) {
        plotMessage (box, RA8875_RED, "Countdown may not be combined with other data panes");
        wdDelay(5000);
        return (true);
    }
    return (false);
}

/* show a table of suitable plot choices in and for the given pane and allow user to choose one or more.
 * always return a selection even if it's the current selection again, never PLOT_CH_NONE.
 * N.B. do not use this for PANE_0
 */
/* category grouping for askPaneChoice()'s pane-choice picker -- keeps each individual
 * checklist short and fixed-size regardless of how many PLOT_CH choices exist in total,
 * rather than one ever-growing list whose box has to keep growing to match (which is what
 * caused the original cramped-then-overlapping problems this replaces).
 */
static const char *pane_categories[] = {
    "Space Wx",
    "DX & Contest",
    "Weather",
    "Sky & Space",
    "Station & Sensors",
    "Other",
};
#define N_PANE_CATEGORIES  NARRAY(pane_categories)

/* which category a given PlotChoice belongs to, for the picker above.
 * N.B. deliberately falls through to "Other" (category 5) via default: rather than
 * enumerating every single PlotChoice explicitly -- a future PLOT_CH_* added here without
 * being explicitly categorized still shows up (in Other) instead of silently vanishing
 * from the menu entirely, which would be a much worse failure mode than just being in the
 * wrong bucket until someone gives it a real category.
 */
static int categoryOfChoice (PlotChoice pc)
{
    switch (pc) {
    case PLOT_CH_AURORA: case PLOT_CH_BZBT: case PLOT_CH_DST: case PLOT_CH_DRAP:
    case PLOT_CH_NOAASPW: case PLOT_CH_KP: case PLOT_CH_FLUX: case PLOT_CH_SOLWIND:
    case PLOT_CH_SSN: case PLOT_CH_XRAY:
        return 0;   // Space Weather

    case PLOT_CH_CONTESTS: case PLOT_CH_DXCLUSTER: case PLOT_CH_DXPEDS: case PLOT_CH_HFCOND:
    case PLOT_CH_PSK: case PLOT_CH_ONTA: case PLOT_CH_VHFCOND: case PLOT_CH_BC:
        return 1;   // DX & Contest

    case PLOT_CH_DEWX: case PLOT_CH_DXWX: case PLOT_CH_STORMS:
        return 2;   // Weather

    case PLOT_CH_MOON: case PLOT_CH_SDO: case PLOT_CH_SATACT:
        return 3;   // Sky & Space

    case PLOT_CH_GIMBAL: case PLOT_CH_TEMPERATURE: case PLOT_CH_PRESSURE:
    case PLOT_CH_HUMIDITY: case PLOT_CH_DEWPOINT: case PLOT_CH_ADIF:
        return 4;   // Station & Sensors

    default:
        return 5;   // Other -- Nets, Mesh_Mon, Launches, Countdown, and anything new
                     // that hasn't been explicitly categorized yet
    }
}

/* reverse lookup: MenuItem.label (a pointer into plot_names[]) back to its PlotChoice,
 * or PLOT_CH_NONE if not found. Same match-by-label technique askPaneChoice()'s commit
 * step already used inline; factored out since the category loop below needs it too.
 */
static PlotChoice labelToChoice (const char *label)
{
    for (int j = 0; j < PLOT_CH_N; j++)
        if (PLOT_CH_IS_REAL(j) && strcmp (plot_names[j], label) == 0)
            return (PlotChoice) j;
    return PLOT_CH_NONE;
}

// accordion layout constants -- duplicated from (and must stay in sync with) the private
// layout constants in menu.cpp, since those aren't exported. This accordion draws its rows
// with menuDrawItem(), so rows need to land in boxes sized the way menuDrawItem() expects.
#define ACC_TBM         5               // top and bottom margin, matches menu.cpp MENU_TBM
#define ACC_RM          2               // right margin, matches menu.cpp MENU_RM
#define ACC_LM          2               // left margin for row content -- menuDrawItem() ends
                                         // every row with its own drawSBox() outline around
                                         // that row's full pb rectangle; with pb.x sitting
                                         // exactly on our own box's left border column, each
                                         // row's outline was overdrawing a black tick over our
                                         // border there, row by row -- reading as a dotted left
                                         // edge. This keeps every row's pb clear of that column.
#define ACC_RH          11              // row height, matches menu.cpp MENU_RH
#define ACC_IS          6               // indicator size, matches menu.cpp MENU_IS
#define ACC_BB          5               // ok/cancel button horizontal border, matches MENU_BB
#define ACC_BDX         2               // ok/cancel button text horizontal offset, matches MENU_BDX
#define ACC_BDROP       2               // text vertical drop, matches menu.cpp MENU_BDROP
#define ACC_BG          2               // bottom gap, matches menu.cpp MENU_BG
#define ACC_CHILD_INDENT 10             // extra indent for a leaf item nested under its header
#define ACC_HEADER_INDENT 6             // left padding for a header row's triangle+label,
                                         // clear of the box's own left edge -- 2px alone (the
                                         // old plain MenuItem default) sat close enough to that
                                         // edge to look like it was getting clipped
#define ACC_SIDE_INSET 0                // how far the visible border+content sits in from the
                                         // pane's true left/right edges. Was 5, to dodge a
                                         // border-flicker bug -- that's now fixed at its actual
                                         // source (see menu_open_for_pane / showRotatingBorder()),
                                         // and the margin itself, now that it's static rather than
                                         // flickering, reads as a plain black band cutting into
                                         // the pane instead. 0 keeps the vis_box/cur_box split in
                                         // place (harmless) in case a small inset is wanted again.
#define ACC_FGC         RA8875_WHITE
#define ACC_BGC         RA8875_BLACK
#define ACC_FOOTC       RA8875_YELLOW

static const char acc_ok_label[] = "Ok";
static const char acc_cancel_label[] = "Cancel";

/* accordion-style category picker: every category header is always visible; tapping one
 * expands its checklist directly beneath it, collapsing whatever else was open, rather than
 * opening a second pop-up menu on top of the first -- categories behave like folders the user
 * opens and closes in place. There is exactly one Ok/Cancel pair for the whole operation; the
 * "Total Selections (N/M)" footer and the Ok/Cancel row always sit directly beneath whatever
 * is currently expanded, so they move up/down as categories open and close. Returns true (with
 * mitems[].set updated) if the user pressed Ok, else false (Cancel/ESC/tap-outside/timeout),
 * in which case mitems[] is left exactly as it was on entry.
 */
/* work out how to lay out one expanded category's checklist -- how many columns its
 * children need, how many of them are shown, and whether every OTHER header has to be
 * hidden to make room -- so the whole picker fits within budget_h (the pane's own height).
 * Tries, in order: every header + 1-column children; every header + 2-column children;
 * only the open header + 1-column children; only the open header + 2-column children;
 * and finally only the open header + 2-column children truncated to whatever fits, as a
 * last resort for a pane too small or a category too big for any of the above. Each tier
 * is tried only because the one before it didn't fit -- most categories never get past the
 * first.
 */
static void fitAccordionLayout (int n_cats, int n_children, int budget_h,
                                 bool &hide_siblings, int &n_ccols, int &shown_children, int &cur_h)
{
    auto calc_h = [](int visible_rows) {
        return ACC_TBM + (visible_rows + 2)*ACC_RH + ACC_TBM + ACC_BG;    // +2: footer + ok/cancel
    };

    int rows_1col = n_cats + n_children;
    int h_1col = calc_h (rows_1col);
    if (h_1col <= budget_h) {
        hide_siblings = false; n_ccols = 1; shown_children = n_children; cur_h = h_1col;
        return;
    }

    int rows_2col_children = (n_children + 1)/2;
    int h_2col = calc_h (n_cats + rows_2col_children);
    if (h_2col <= budget_h) {
        hide_siblings = false; n_ccols = 2; shown_children = n_children; cur_h = h_2col;
        return;
    }

    int h_hide_1col = calc_h (1 + n_children);
    if (h_hide_1col <= budget_h) {
        hide_siblings = true; n_ccols = 1; shown_children = n_children; cur_h = h_hide_1col;
        return;
    }

    int h_hide_2col = calc_h (1 + rows_2col_children);
    if (h_hide_2col <= budget_h) {
        hide_siblings = true; n_ccols = 2; shown_children = n_children; cur_h = h_hide_2col;
        return;
    }

    // last resort: only the open header, 2 columns, truncated to whatever fits
    hide_siblings = true;
    n_ccols = 2;
    int max_child_rows = (budget_h - 2*ACC_TBM - ACC_BG)/ACC_RH - 2 - 1;  // - footer,ok,the header
    if (max_child_rows < 0)
        max_child_rows = 0;
    shown_children = max_child_rows * 2;
    if (shown_children > n_children)
        shown_children = n_children;
    cur_h = calc_h (1 + max_child_rows);
}

// which pane's picker is currently up is tracked in the shared global menu_open_for_pane (see
// HamClock.h) -- both so accordionServiceOtherPanes() below knows which one NOT to touch, and
// so showRotatingBorder() leaves its border alone no matter who calls it.

// re-draws whatever the picker's current frame looks like, set fresh each loop iteration in
// askPaneCategoryAccordion() below; nullptr the rest of the time. The periodic background tick
// calls this right after servicing other panes/the map, to reclaim any pixels that work may
// have just drawn over -- our picker's own box is otherwise only ever repainted in response to
// a tap, so without this, a stray background redraw would sit there until the next tap.
static std::function<void()> acc_redraw_fn = nullptr;

/* UserInput.fp callback for askPaneCategoryAccordion()'s wait loop: services every OTHER
 * pane's normal rotation/updates (network fetches, redraws, the lot) at a throttled rate while
 * the picker sits waiting for a tap, so opening one pane's picker doesn't stall every other
 * pane's rotation for as long as it's left open. Also services the map overlays (DX Cluster
 * spots and the like) and their underlying data collection, since those are normally driven
 * from the main loop() rather than updateWiFi() and would otherwise sit frozen the whole time
 * a picker is open. Always returns false so waitForUser() just keeps waiting for the next tap
 * -- this is a side-effecting tick, not a wait-ending condition.
 */
static bool accordionServiceOtherPanes (void)
{
    static uint32_t last_ms;
    uint32_t now_ms = millis();
    if (now_ms - last_ms < 1000)               // once a second is plenty; this work can be heavy
        return false;
    last_ms = now_ms;

    updateWiFi (menu_open_for_pane);
    checkDXCluster();
    drawAllSymbols();

    // publish whatever that work just drew (map overlays etc), then reclaim our own box in
    // case any of it happened to land underneath the picker, and publish that too
    tft.drawPR();
    if (acc_redraw_fn)
        acc_redraw_fn();

    return false;
}

/* a few plot names run long enough to crowd a 2-column child row -- shorten just for that
 * display, never the underlying label itself (which is also the persisted/API name used to
 * match choices elsewhere, eg labelToChoice()).
 */
static const char *accordionAbbrev (const char *label)
{
    if (!strcmp (label, "Solar_Flux")) return "Sol_Flux";
    if (!strcmp (label, "Solar_Wind")) return "Sol_Wind";
    if (!strcmp (label, "Planetary_K")) return "Kp";
    if (!strcmp (label, "Sunspot_N")) return "SSN";
    if (!strcmp (label, "Disturbance")) return "Dst";     // Dst = the geomagnetic Disturbance
                                                           // storm time index this plots, and
                                                           // the name hams/space-wx sites use
    if (!strcmp (label, "DX_Cluster")) return "DXClstr";       // "DX Cluster" ran past the
                                                                // column edge; even "DXCluster"
                                                                // was too close for comfort
    return label;
}

/* a handful of categories have a deliberately chosen display order rather than the default
 * alphabetical one -- eg so a commonly-used item lands in a more convenient spot. Absent from
 * this table, an item just keeps its normal (alphabetical) position. Add entries here as
 * requested; the two-column layout fills column 0 top-to-bottom, then column 1.
 */
static int accordionOrderKey (const char *label)
{
    static const struct { const char *label; int key; } order[] = {
        // DX & Contest, reordered from the default alphabetical layout
        { "On_The_Air",  0 },
        { "DXPeditions", 1 },
        { "VOACAP_DEDX", 2 },
        { "Live_Spots",  3 },
        { "Contests",    4 },
        { "VHF_Cond",    5 },
        { "HF_Bands",    6 },
    };
    for (unsigned i = 0; i < NARRAY(order); i++)
        if (!strcmp (label, order[i].label))
            return (order[i].key);
    return (-1);        // no override -- sort below keeps it in its natural position
}

static bool askPaneCategoryAccordion (PlotPane pp, SBox box, MenuItem *mitems, int n_mitems)
{
    // one header per non-empty category, in display order; cat_total[] doesn't change once
    // the picker opens (membership is fixed), only how many of each are selected does
    int cat_of_row[N_PANE_CATEGORIES];
    int cat_total[N_PANE_CATEGORIES] = {0};
    int n_cats = 0;
    for (int i = 0; i < n_mitems; i++) {
        PlotChoice pc = labelToChoice (mitems[i].label);
        if (pc != PLOT_CH_NONE)
            cat_total[categoryOfChoice(pc)]++;
    }
    for (int c = 0; c < (int)N_PANE_CATEGORIES; c++)
        if (cat_total[c] > 0)
            cat_of_row[n_cats++] = c;

    // box width is fixed for the life of the picker -- the pane's own width, widened only if
    // that's somehow not enough for Ok/Cancel once the side inset (below) is accounted for.
    int ok_w = getTextWidth(acc_ok_label) + ACC_BDX*2;
    int cancel_w = getTextWidth(acc_cancel_label) + ACC_BDX*2;
    int btn_w = ACC_BB + ok_w + ACC_BB + cancel_w + ACC_BB;
    if (box.w < btn_w + 2*ACC_SIDE_INSET)
        box.w = btn_w + 2*ACC_SIDE_INSET;
    if (box.x + box.w >= tft.width())
        box.x = tft.width() - box.w - 1;

    // budget_h is the real target: the pane's own height, so an expanded category's checklist
    // -- via fitAccordionLayout()'s column/hide/truncate cascade -- stays within the pane
    // instead of spilling down over the map. box.h on entry is the pane's own height; clamped
    // against the bottom of the screen too, purely as a last-ditch safety net.
    int budget_h = box.h;
    if (box.y + budget_h >= tft.height())
        budget_h = tft.height() - box.y - 2;
    box.h = budget_h;

    uint8_t *backing_store;
    if (!tft.getBackingStore (backing_store, box.x, box.y, box.w, box.h))
        fatalError ("failed to capture pixels beneath %d x %d pane accordion", box.w, box.h);

    int expanded_cat = -1;             // -1 == every category collapsed
    int last_h = 0;                    // box height actually drawn last frame, for erase-sizing
    bool ok = false;

    menu_open_for_pane = pp;            // let showRotatingBorder()/accordionServiceOtherPanes()
                                         // know which pane to leave alone
    for (;;) {

        // children of whichever category is currently expanded, in mitems[] order, then
        // reordered per accordionOrderKey() for any category that has a custom display order
        int exp_children[64];
        int n_exp = 0;
        if (expanded_cat >= 0) {
            for (int i = 0; i < n_mitems; i++) {
                PlotChoice pc = labelToChoice (mitems[i].label);
                if (pc != PLOT_CH_NONE && categoryOfChoice(pc) == expanded_cat)
                    exp_children[n_exp++] = i;
            }
            std::stable_sort (exp_children, exp_children + n_exp, [mitems](int a, int b) {
                int ka = accordionOrderKey (mitems[a].label);
                int kb = accordionOrderKey (mitems[b].label);
                if (ka < 0 && kb < 0)
                    return false;              // neither overridden -- stable_sort keeps order
                if (ka < 0)
                    return false;              // unoverridden items sort after overridden ones
                if (kb < 0)
                    return true;
                return ka < kb;
            });
        }

        bool hide_siblings = false;
        int n_ccols = 1;
        int shown_children = n_exp;
        int cur_h;
        if (expanded_cat < 0)
            cur_h = ACC_TBM + (n_cats + 2)*ACC_RH + ACC_TBM + ACC_BG;
        else
            fitAccordionLayout (n_cats, n_exp, budget_h, hide_siblings, n_ccols, shown_children, cur_h);
        SBox cur_box = {box.x, box.y, box.w, (uint16_t)cur_h};        // full pane width -- what
                                                                       // we fill/manage/restore
                                                                       // every frame
        SBox vis_box = {(uint16_t)(box.x + ACC_SIDE_INSET), box.y,
                         (uint16_t)(box.w - 2*ACC_SIDE_INSET), (uint16_t)cur_h};   // where the
                                                                       // actual border+content
                                                                       // draws -- inset off the
                                                                       // pane's true edges so our
                                                                       // border doesn't sit on the
                                                                       // exact column something
                                                                       // else keeps redrawing

        // header and leaf rows, and the Ok/Cancel buttons, as drawn this iteration -- read by
        // the tap-handling code below once we return from waitForUser()
        SBox row_boxes[128];
        bool row_is_header[128];
        int row_data[128];                     // category index if header, else mitems[] index
        int n_row_boxes = 0;
        SBox ok_b, cancel_b;
        bool enable_ok = false;

        // draws exactly this iteration's frame: headers (or just the expanded one), its
        // children, the footer, and Ok/Cancel, then publishes if it overlaps the map. Safe to
        // call more than once per iteration -- cur_h/hide_siblings/n_ccols/shown_children are
        // fixed for the whole iteration, so a repeat call just repaints the same thing, which
        // is exactly what's needed to reclaim pixels that background pane activity (see
        // accordionServiceOtherPanes()) may have drawn over in the meantime.
        auto redrawFrame = [&]() {

            // if this frame is shrinking, put back the real pixels (map or whatever else was
            // there) for the strip we're giving up -- not a plain fill, which would otherwise
            // leave a dead black rectangle sitting on the map below the picker until something
            // else happens to redraw over it
            if (cur_h < last_h)
                tft.restoreBackingRegion (backing_store, box.x, box.y, box.w, box.h, cur_h, last_h-cur_h);

            // (re)paint just the currently visible extent
            fillSBox (cur_box, ACC_BGC);
            drawSBox (vis_box, ACC_FGC);
            int erase_h = cur_h > last_h ? cur_h : last_h;    // for the map-publish check below
            last_h = cur_h;

            // draw headers -- all of them, or just the expanded one if siblings are hidden
            // this frame -- and, immediately after the expanded one, as many of its children
            // as fit
            n_row_boxes = 0;
            int row = 0;
            int total_selected = 0;
            for (int i = 0; i < n_mitems; i++)
                if (mitems[i].set)
                    total_selected++;
            for (int r = 0; r < n_cats; r++) {
                int c = cat_of_row[r];

                if (hide_siblings && c != expanded_cat)
                    continue;               // hidden this frame -- didn't all fit

                int selected = 0;
                for (int i = 0; i < n_mitems; i++) {
                    PlotChoice pc = labelToChoice (mitems[i].label);
                    if (pc != PLOT_CH_NONE && categoryOfChoice(pc) == c && mitems[i].set)
                        selected++;
                }
                char label[64];
                snprintf (label, sizeof(label), "%s (%d/%d)", pane_categories[c], selected, cat_total[c]);

                MenuItem hmi;
                hmi.type = MENU_1OFN;
                hmi.submenu = true;            // draw with the folder-style triangle, not a radio dot
                hmi.set = (c == expanded_cat);  // filled == open, outline == closed
                hmi.indent = ACC_HEADER_INDENT;
                hmi.group = 1;
                hmi.label = label;

                hmi.textf = NULL;

                SBox hb = {(uint16_t)(vis_box.x + ACC_LM), (uint16_t)(vis_box.y + ACC_TBM + row*ACC_RH),
                           (uint16_t)(vis_box.w - ACC_LM - ACC_RM), ACC_RH};
                menuDrawItem (hmi, hb, true, false);
                row_boxes[n_row_boxes] = hb;
                row_is_header[n_row_boxes] = true;
                row_data[n_row_boxes] = c;
                n_row_boxes++;
                row++;

                if (c == expanded_cat) {
                    int rows_per_col = n_ccols > 1 ? (shown_children + n_ccols - 1)/n_ccols
                                                    : shown_children;
                    int col_w = (vis_box.w - ACC_LM - ACC_RM)/n_ccols;
                    for (int j = 0; j < shown_children; j++) {
                        MenuItem cmi = mitems[exp_children[j]];
                        cmi.label = accordionAbbrev (cmi.label);
                        cmi.indent = ACC_HEADER_INDENT + ACC_CHILD_INDENT;    // nested clear of
                                                                               // the header above it
                        int col = n_ccols > 1 ? j/rows_per_col : 0;
                        int row_in_col = n_ccols > 1 ? j%rows_per_col : j;
                        SBox cb = {(uint16_t)(vis_box.x + ACC_LM + col*col_w),
                                   (uint16_t)(vis_box.y + ACC_TBM + (row+row_in_col)*ACC_RH),
                                   (uint16_t)col_w, ACC_RH};
                        menuDrawItem (cmi, cb, true, false);
                        row_boxes[n_row_boxes] = cb;
                        row_is_header[n_row_boxes] = false;
                        row_data[n_row_boxes] = exp_children[j];
                        n_row_boxes++;
                    }
                    row += rows_per_col;
                }
            }

            // footer: live running total, directly beneath whatever's currently visible
            SBox footer_b = {(uint16_t)(vis_box.x + ACC_LM), (uint16_t)(vis_box.y + ACC_TBM + row*ACC_RH),
                              (uint16_t)(vis_box.w - ACC_LM - ACC_RM), ACC_RH};
            char footer[64];
            snprintf (footer, sizeof(footer), "Total Selections (%d/%d)", total_selected, n_mitems);
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (ACC_FOOTC);
            tft.setCursor (footer_b.x + 2, footer_b.y + ACC_BDROP);
            tft.print (footer);
            tft.setTextColor (ACC_FGC);
            row++;

            // Ok/Cancel row, also directly beneath whatever's currently visible
            ok_b.w = ok_w;
            ok_b.h = ACC_RH;
            ok_b.x = vis_box.x + ACC_BB;
            ok_b.y = vis_box.y + ACC_TBM + row*ACC_RH;
            cancel_b.w = cancel_w;
            cancel_b.h = ACC_RH;
            cancel_b.x = vis_box.x + vis_box.w - cancel_w - ACC_BB;
            cancel_b.y = ok_b.y;

            enable_ok = total_selected > 0;
            drawSBox (ok_b, enable_ok ? ACC_FGC : GRAY);
            tft.setTextColor (enable_ok ? ACC_FGC : GRAY);
            tft.setCursor (ok_b.x + ACC_BDX, ok_b.y + ACC_BDROP);
            tft.print (acc_ok_label);
            drawSBox (cancel_b, ACC_FGC);
            tft.setTextColor (ACC_FGC);
            tft.setCursor (cancel_b.x + ACC_BDX, cancel_b.y + ACC_BDROP);
            tft.print (acc_cancel_label);

            // publish immediately if anything we touched this frame -- newly drawn or newly
            // restored -- overlaps the map, not just the currently visible box, so a shrinking
            // frame's restored strip shows up right away too
            SBox touched_b = {box.x, box.y, box.w, (uint16_t)erase_h};
            if (boxesOverlap (touched_b, map_b))
                tft.drawPR();
        };

        redrawFrame();
        acc_redraw_fn = redrawFrame;   // let the periodic background tick re-assert us if needed

        // wait for the next tap -- fp services every other pane's rotation/updates while we
        // sit here, so this pane's picker being open doesn't stall the rest of the display
        UserInput ui = {
            box, accordionServiceOtherPanes, UF_UNUSED, MENU_TO, UF_CLOCKSOK,
            {0, 0}, TT_NONE, '\0', false, false
        };
        if (!waitForUser (ui)) {
            ok = false;                        // timed out
            break;
        }
        if (ui.kb_char == CHAR_ESC) {
            ok = false;
            break;
        }
        if (ui.kb_char != CHAR_NONE)
            continue;                          // ignore other keys, this picker is tap-only

        if (inBox (ui.tap, ok_b)) {
            if (enable_ok) {
                ok = true;
                break;
            }
            menuMsg (cur_box, RA8875_RED, "Select an item");
            continue;
        }
        if (inBox (ui.tap, cancel_b)) {
            ok = false;
            break;
        }
        if (!inBox (ui.tap, cur_box)) {
            // tap outside the currently visible extent -- same as Cancel, there being only
            // one Ok/Cancel for the whole picker now
            ok = false;
            break;
        }

        // find which visible row, if any, was tapped
        int tapped_row = -1;
        for (int i = 0; i < n_row_boxes; i++)
            if (inBox (ui.tap, row_boxes[i])) {
                tapped_row = i;
                break;
            }
        if (tapped_row < 0)
            continue;

        if (row_is_header[tapped_row]) {
            int tapped_cat = row_data[tapped_row];
            expanded_cat = (tapped_cat == expanded_cat) ? -1 : tapped_cat;
        } else {
            int mi_idx = row_data[tapped_row];
            mitems[mi_idx].set = !mitems[mi_idx].set;
        }
    }

    menu_open_for_pane = PANE_NONE;
    acc_redraw_fn = nullptr;
    drainTouch();

    if (!tft.setBackingStore (backing_store, box.x, box.y, box.w, box.h))
        fatalError ("mem pixel restore failed %d x %d", box.w, box.h);
    if (boxesOverlap (box, map_b))
        tft.drawPR();

    return (ok);
}

static PlotChoice askPaneChoice (PlotPane pp)
{
    // not for use for PANE_0
    if (pp == PANE_0)
        fatalError ("askPaneChoice called with pane 0");

    // set this temporarily to show all choices, just for testing worst-case layout
    #define ASKP_SHOWALL 0                      // RBF

    // build items from all candidates suitable for this pane -- unchanged from before;
    // categorization only affects how this list gets displayed/edited below, not what's
    // tracked. mitems[].set is the running truth for the whole operation throughout.
    MenuItem *mitems = NULL;
    int n_mitems = 0;
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice pc = (PlotChoice) i;
        if (!PLOT_CH_IS_REAL(pc))
            continue;
        PlotPane pp_ch = findPaneForChoice (pc);

        // otherwise use if not used elsewhere and available or already assigned to this pane
        if ( (pp_ch == PANE_NONE && plotChoiceIsAvailable(pc)) || pp_ch == pp || ASKP_SHOWALL) {
            // set up next menu item
            mitems = (MenuItem *) realloc (mitems, (n_mitems+1)*sizeof(MenuItem));
            if (!mitems)
                fatalError ("pane alloc: %d", n_mitems);
            MenuItem &mi = mitems[n_mitems++];
            // MENU_TOGGLE, not MENU_AL1OFN: renders identically (same checkbox fallthrough
            // in menuDrawItem()) but isn't subject to menuStateOk()'s "Ok disabled if none
            // set" rule. That rule made sense for the original single flat list (a pane needs
            // >=1 choice overall) but once split into per-category sub-menus it was firing
            // per category independently, blocking Ok whenever any one category was reduced
            // to zero even with plenty selected elsewhere. The overall "at least one total"
            // guarantee is now enforced once, explicitly, at the picker's finish point below.
            mi.type = MENU_TOGGLE;
            mi.set = paneHasChoice (pp, pc) ? true : false;
            mi.label = plot_names[pc];
            mi.indent = 2;
            mi.group = 1;
        }
    }

    // nice sort by label
    qsort (mitems, n_mitems, sizeof(MenuItem), menuChoiceQS);

    // show the accordion category picker: headers always visible, tapping one expands its
    // checklist in place (collapsing any other), letting the user touch items across several
    // categories in one operation, all under the picker's single Ok/Cancel pair.
    SBox box = plot_b[pp];       // copy -- pristine, never mutated; askPaneCategoryAccordion()
                                  // takes its own copy to size/position, this one is untouched
    bool proceed = askPaneCategoryAccordion (pp, box, mitems, n_mitems);

    // return current choice by default
    PlotChoice return_ch = plot_ch[pp];

    if (proceed) {

        // build the complete 64-bit rotation set. Choices after the fixed NONE sentinel are
        // represented in the high word, so Sat Alerts can rotate with any other pane choice.
        PlotMask new_rotset = 0;
        for (int i = 0; i < n_mitems; i++) {
            if (mitems[i].set) {
                // find which choice this refers to by matching labels
                for (int j = 0; j < PLOT_CH_N; j++) {
                    if (PLOT_CH_IS_REAL(j) && strcmp (plot_names[j], mitems[i].label) == 0) {
                        new_rotset |= PLOTBIT(j);
                        break;
                    }
                }
            }
        }

        // enforce a few panes that do not work well with rotation
        PlotMask new_sets[PANE_N];
        memcpy (new_sets, plot_rotset, sizeof(new_sets));
        new_sets[pp] = new_rotset;
        if (!enforceCDownAlone (box, new_rotset)) {

            plot_rotset[pp] = new_rotset;
            savePlotOps();

            // return current choice if still in rotset, else just pick one
            if (!(plot_rotset[pp] & PLOTBIT(return_ch))) {
                for (int i = 0; i < PLOT_CH_N; i++) {
                    if (plot_rotset[pp] & PLOTBIT(i)) {
                        return_ch = (PlotChoice)i;
                        break;
                    }
                }
            }
        }
    }

    // finished with menu. labels were static.
    free ((void*)mitems);

    // report
    logPaneRotSet(pp, return_ch);

    // done
    return (return_ch);
}

/* return which pane _is currently showing_ the given choice, else PANE_NONE
 */
PlotPane findPaneChoiceNow (PlotChoice pc)
{
    for (int i = PANE_0; i < PANE_N; i++)
        if (plot_ch[i] == pc)
            return ((PlotPane)i);
    return (PANE_NONE);
}

/* return which pane has the given choice in its rotation set _even if not currently visible_, else PANE_NONE
 */
PlotPane findPaneForChoice (PlotChoice pc)
{
    for (int i = PANE_0; i < PANE_N; i++)
        if ( paneHasChoice ((PlotPane)i, pc) )
            return ((PlotPane)i);
    return (PANE_NONE);
}

/* given a current choice, select the next rotation plot choice for the given pane.
 * if not rotating return the same choice.
 */
PlotChoice getNextRotationChoice (PlotPane pp, PlotChoice pc)
{
    if (isPaneRotating (pp)) {
        for (int i = 1; i < PLOT_CH_N; i++) {
            int j = (pc + i) % PLOT_CH_N;
            if (plot_rotset[pp] & PLOTBIT(j)) {
                // don't rotate into the Storms pane when there are no active storms; skip it so the
                // pane only appears once a storm exists. If Storms is the sole choice, isPaneRotating()
                // is false and we never get here, so it always shows in that case.
                if (j == PLOT_CH_STORMS && !stormsActive())
                    continue;
                return ((PlotChoice)j);
            }
        }
        // only an (empty) Storms choice remained besides us -- stay on the current choice
        return (pc);
    } else
        return (pc);
}

/* same as getNextRotationChoice() but walks the rotation set backwards -- used to implement
 * manual reverse pane advancement.
 */
PlotChoice getPrevRotationChoice (PlotPane pp, PlotChoice pc)
{
    if (isPaneRotating (pp)) {
        for (int i = 1; i < PLOT_CH_N; i++) {
            int j = (pc - i + PLOT_CH_N) % PLOT_CH_N;
            if (plot_rotset[pp] & PLOTBIT(j)) {
                if (j == PLOT_CH_STORMS && !stormsActive())
                    continue;
                return ((PlotChoice)j);
            }
        }
        return (pc);
    } else
        return (pc);
}

/* return any available unassigned plot choice
 */
PlotChoice getAnyAvailableChoice()
{
    int s = random (PLOT_CH_N);
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice pc = (PlotChoice)((s + i) % PLOT_CH_N);
        if (plotChoiceIsAvailable (pc)) {
            bool inuse = false;
            for (int j = 0; !inuse && j < PANE_N; j++) {
                if (plot_ch[j] == pc || (plot_rotset[j] & PLOTBIT(pc))) {
                    inuse = true;
                }
            }
            if (!inuse)
                return (pc);
        }
    }
    fatalError ("getAnyAvailableChoice() no available pane choices");

    // never get here, just for lint
    return (PLOT_CH_FLUX);
}

/* return any available unassigned plot choice suitable on PANE_0, might be PLOT_CH_NONE
 */
PlotChoice getAnyAvailablePane0Choice()
{
    // build a collection of available choices
    PlotChoice available[PLOT_CH_N];
    int n_available = 0;
    for (int pc = 0; pc < PLOT_CH_N; pc++) {
        if ((PLOTBIT(pc) & PANE_0_CH_MASK)
                && plotChoiceIsAvailable((PlotChoice)pc) && findPaneForChoice((PlotChoice)pc) == PANE_NONE) {
            available[n_available] = (PlotChoice)pc;
            n_available++;
        }
    }
    if (n_available == 0)
        return (PLOT_CH_NONE);
    else
        return (available[random(n_available)]);
}


/* remove any PLOT_CH_COUNTDOWN from rotset if stopwatch engine not SWE_COUNTDOWN,
 * and if it is currently visible replace with an alternative.
 * N.B. PANE_0 can never be PLOT_CH_COUNTDOWN
 */
void insureCountdownPaneSensible()
{
    if (getSWEngineState(NULL,NULL) != SWE_COUNTDOWN) {
        for (int i = PANE_1; i < PANE_N; i++) {
            if (plot_rotset[i] & PLOTBIT(PLOT_CH_COUNTDOWN)) {
                plot_rotset[i] &= ~PLOTBIT(PLOT_CH_COUNTDOWN);
                if (plot_ch[i] == PLOT_CH_COUNTDOWN) {
                    setDefaultPaneChoice((PlotPane)i);
                    if (!setPlotChoice ((PlotPane)i, plot_ch[i])) {
                        fatalError ("can not replace Countdown pain %d with %s",
                                    i, plot_names[plot_ch[i]]);
                    }
                }
            }
        }
    }
}

/* check for touch in the given pane, return whether ours.
 * N.B. accommodate a few choices that have their own touch features.
 * N.B. TT_TAP_BX means to force rotation now
 */
bool checkPlotTouch (TouchType tt, const SCoord &s, PlotPane pp)
{
    // ignore taps in this pane while reverting
    if (pp == ignorePaneTouch())
        return (false);

    // for sure not ours if not even in this box
    const SBox &box = plot_b[pp];
    if (!inBox (s, box))
        return (false);

    // reserve top portion for bringing up choice menu or forcing rotation.
    // left half forces reverse rotation, right half forces forward rotation.
    bool in_top = s.y < box.y + PANETITLE_H;

    if (in_top && tt == TT_TAP_BX) {
        if (s.x < box.x + box.w/2)
            forcePaneRotationPrev (pp);
        else
            forcePaneRotation (pp);
        return (true);
    }

    // check the choices that have their own active areas
    switch (plot_ch[pp]) {
    case PLOT_CH_DXCLUSTER:
        if (checkDXClusterTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_BC:
        if (checkBCTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_CONTESTS:
        if (checkContestsTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_ACTIVENETS:
        if (checkActiveNetsTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_SDO:
        if (checkSDOTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_GIMBAL:
        if (checkGimbalTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_COUNTDOWN:
        if (!in_top) {
            checkCountdownTouch();
            return (true);
        }
        break;
    case PLOT_CH_MOON:
        if (checkMoonTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_SSN:
        if (!in_top) {
            plotServerFile ("/ssn/ssn-history.txt", "SIDC Sunspot History", "Year");
            return(true);
        }
        break;

    case PLOT_CH_NOAASPW:
        if (!in_top) {
            plotRSGHistory();
            return(true);
        }
        break;
    case PLOT_CH_FLUX:
        if (!in_top) {
            plotServerFile ("/solar-flux/solarflux-history.txt", "10.7 cm Solar Flux History", "Year");
            return(true);
        }
        break;
    case PLOT_CH_PSK:
        if (checkPSKTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_ONTA:
        if (checkOnTheAirTouch (tt, s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_SATACT:
        if (checkHamsatTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_MESHTASTIC:
        if (checkMeshtasticTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_ADIF:
        if (checkADIFTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_DXPEDS:
        if (checkDXPedsTouch (s, box))
            return (true);
        in_top = true;
        break;

    case PLOT_CH_STORMS:
        if (checkStormsTouch (s, box))
            return (true);
        in_top = true;
        break;

    case PLOT_CH_LAUNCHES:
        if (checkLaunchesTouch (s, box))
            return (true);
        in_top = true;
        break;

    // tapping a BME below top rotates just among other BME and disables auto rotate.
    // try all possibilities because they might be on other panes.
    case PLOT_CH_TEMPERATURE:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_HUMIDITY)
                            || setPlotChoice (pp, PLOT_CH_DEWPOINT)
                            || setPlotChoice (pp, PLOT_CH_PRESSURE)) {
                plot_rotset[pp] = PLOTBIT(plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_PRESSURE:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_TEMPERATURE)
                            || setPlotChoice (pp, PLOT_CH_HUMIDITY)
                            || setPlotChoice (pp, PLOT_CH_DEWPOINT)) {
                plot_rotset[pp] = PLOTBIT(plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_HUMIDITY:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_DEWPOINT)
                            || setPlotChoice (pp, PLOT_CH_PRESSURE)
                            || setPlotChoice (pp, PLOT_CH_TEMPERATURE)) {
                plot_rotset[pp] = PLOTBIT(plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_DEWPOINT:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_PRESSURE)
                            || setPlotChoice (pp, PLOT_CH_TEMPERATURE)
                            || setPlotChoice (pp, PLOT_CH_HUMIDITY)) {
                plot_rotset[pp] = PLOTBIT(plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;

    default:
        break;
    }

    if (!in_top)
        return (false);

    // draw menu with choices for this pane
    if (pp == PANE_0) {
        drawDEFormatMenu();
    } else {

        // ask for new set, engage if change current
        PlotChoice pc = askPaneChoice(pp);
        if (pc != plot_ch[pp] && !setPlotChoice (pp, pc))
            fatalError ("checkPlotTouch bad choice %d pane %d", (int)pc, (int)pp);
    }

    // it was ours
    return (true);
}

/* called once to init plot info from NV and insure legal and consistent values.
 * N.B. PANE_0 is the only pane allowed to be PLOT_CH_NONE
 */
void initPlotPanes()
{
    // Retrieve the historic low words from their original EEPROM locations and combine them with
    // the newly appended high words. A missing high-word cookie is the normal upgrade case.
    static const NV_Name rot_lo_nv[PANE_N] = {
        NV_PANE0ROTSET, NV_PANE1ROTSET, NV_PANE2ROTSET, NV_PANE3ROTSET
    };
    static const NV_Name rot_hi_nv[PANE_N] = {
        NV_PANE0ROTSET_HI, NV_PANE1ROTSET_HI, NV_PANE2ROTSET_HI, NV_PANE3ROTSET_HI
    };
    memset (plot_rotset, 0, sizeof(plot_rotset));
    for (int i = PANE_0; i < PANE_N; i++) {
        uint32_t lo = 0;
        uint32_t hi = 0;
        NVReadUInt32 (rot_lo_nv[i], &lo);
        NVReadUInt32 (rot_hi_nv[i], &hi);
        plot_rotset[i] = ((PlotMask)hi << 32) | lo;
    }

    // NB. since NV_PANE0ROTSET repurposes a prior NV it might contain invalid bits, 0 all if find any
    if (plot_rotset[PANE_0] & ~PANE_0_CH_MASK) {

        Serial.printf ("PANE: Resetting bogus Pane 0 rot set: 0x%llx\n",
                        (unsigned long long)plot_rotset[PANE_0]);
        plot_rotset[PANE_0] = 0;
        plot_ch[PANE_0] = PLOT_CH_NONE;

        // save scrubbed values
        NVWriteUInt32 (NV_PANE0ROTSET, 0);
        NVWriteUInt32 (NV_PANE0ROTSET_HI, 0);
        NVWriteUInt8 (NV_PLOT_0, plot_ch[PANE_0]);
    }


    // remove bits beyond the real choices. PLOT_CH_NONE consumes no bit.
    const unsigned n_plot_bits = PLOT_CH_N - 1;
    const PlotMask all_panes = n_plot_bits >= 64 ? ~UINT64_C(0) : ((UINT64_C(1) << n_plot_bits) - 1);
    for (int i = PANE_0; i < PANE_N; i++) {
        plot_rotset[i] &= all_panes;                     // reset any bits too high
        for (int j = 0; j < PLOT_CH_N; j++) {
            if (PLOT_CH_IS_REAL(j) && (plot_rotset[i] & PLOTBIT(j))) {
                if (!plotChoiceIsAvailable ((PlotChoice)j)) {
                    plot_rotset[i] &= ~PLOTBIT(j);
                    Serial.printf ("PANE: Removing %s from pane %d: not available\n", plot_names[j],i);
                }
            }
        }
    }

    // if current selection not yet defined or not in rotset pick one from rotset or set a default
    for (int i = PANE_0; i < PANE_N; i++) {
        if (!getPlotChoiceNV ((PlotPane)i, &plot_ch[i]) || !paneHasChoice ((PlotPane)i, plot_ch[i]))
            setDefaultPaneChoice ((PlotPane)i);
    }

    // insure same choice not in more than 1 pane
    for (int i = PANE_0; i < PANE_N; i++) {
        for (int j = i+1; j < PANE_N; j++) {
            if (plot_ch[i] == plot_ch[j]) {
                // found dup -- replace with some other unused choice
                for (int k = 0; k < PLOT_CH_N; k++) {
                    PlotChoice new_pc = (PlotChoice)k;
                    if (PLOT_CH_IS_REAL(new_pc) && plotChoiceIsAvailable(new_pc)
                            && findPaneChoiceNow(new_pc) == PANE_NONE) {
                        Serial.printf ("PANE: Reassigning dup pane %d from %s to %s\n", j,
                                        plot_names[plot_ch[j]], plot_names[new_pc]);
                        // remove dup from rotation set then replace with new choice
                        plot_rotset[j] &= ~PLOTBIT(plot_ch[j]);
                        plot_rotset[j] |= PLOTBIT(new_pc);
                        plot_ch[j] = new_pc;
                        break;
                    }
                }
            }
        }
    }

    // one last bit of paranoia: insure each pane choice is in its rotation set unless empty
    for (int i = PANE_0; i < PANE_N; i++)
        if (plot_ch[i] != PLOT_CH_NONE)
            plot_rotset[i] |= PLOTBIT(plot_ch[i]);

    // log and save final arrangement, including the raw current choices
    Serial.printf ("PANE: raw plot_ch[] = %d %d %d %d (%s %s %s %s)\n",
                    (int)plot_ch[PANE_0], (int)plot_ch[PANE_1], (int)plot_ch[PANE_2], (int)plot_ch[PANE_3],
                    plot_ch[PANE_0]==PLOT_CH_NONE ? "NONE" : plot_names[plot_ch[PANE_0]],
                    plot_ch[PANE_1]==PLOT_CH_NONE ? "NONE" : plot_names[plot_ch[PANE_1]],
                    plot_ch[PANE_2]==PLOT_CH_NONE ? "NONE" : plot_names[plot_ch[PANE_2]],
                    plot_ch[PANE_3]==PLOT_CH_NONE ? "NONE" : plot_names[plot_ch[PANE_3]]);
    for (int i = PANE_0; i < PANE_N; i++)
        logPaneRotSet ((PlotPane)i, plot_ch[i]);
    savePlotOps();
}

/* update the original low rotation words, appended high words and current choices.
 */
void savePlotOps()
{
    static const NV_Name rot_lo_nv[PANE_N] = {
        NV_PANE0ROTSET, NV_PANE1ROTSET, NV_PANE2ROTSET, NV_PANE3ROTSET
    };
    static const NV_Name rot_hi_nv[PANE_N] = {
        NV_PANE0ROTSET_HI, NV_PANE1ROTSET_HI, NV_PANE2ROTSET_HI, NV_PANE3ROTSET_HI
    };
    static const NV_Name plot_nv[PANE_N] = {NV_PLOT_0, NV_PLOT_1, NV_PLOT_2, NV_PLOT_3};

    for (int i = PANE_0; i < PANE_N; i++) {
        NVWriteUInt32 (rot_lo_nv[i], (uint32_t)plot_rotset[i]);
        NVWriteUInt32 (rot_hi_nv[i], (uint32_t)(plot_rotset[i] >> 32));
        NVWriteUInt8 (plot_nv[i], plot_ch[i]);
    }
}

/* flash plot and NCDXF_b borders that are nearly ready to change
 * unless rotating pretty fast.
 */
void showRotatingBorder (PlotPane skip_pp)
{
    time_t t0 = myNow();

    // just leave it white if rotation period is 10 s or less
    const int min_rot = 10;
    uint16_t c = RA8875_WHITE;

    // check plot panes
    for (int pp = 0; pp < PANE_N; pp++) {
        if (pp == skip_pp || pp == menu_open_for_pane)
            continue;               // its own picker is up right now -- leave it alone. Most
                                     // callers (eg updateClocks(), many times a second) have no
                                     // idea a picker might be open and never pass skip_pp
                                     // themselves, hence checking the global too, unconditionally.
        if (ROTHOLD_TST(plot_ch[pp])) {
            // mark when pane rotation is holding
            drawSBox (plot_b[pp], RA8875_RED);
        } else if (isPaneRotating((PlotPane)pp) || isSpecialPaneRotating((PlotPane)pp)) {
            // this pane is rotating among other pane choices or SDO is rotating its images
            if (getPaneRotationPeriod() > min_rot)
                c = ((nextPaneRotation((PlotPane)pp) > t0 + PLOT_ROTWARN_DT) || (t0&1) == 1)
                                ? RA8875_WHITE : GRAY;
            drawSBox (plot_b[pp], c);
        } else {
            drawSBox (plot_b[pp], GRAY);
        }
    }

    // check BRB
    if (BRBIsRotating()) {
        if (getPaneRotationPeriod() > min_rot)
            c = ((brb_next_update > t0 + PLOT_ROTWARN_DT) || (t0&1) == 1) ? RA8875_WHITE : GRAY;
        drawSBox (NCDXF_b, c);
    } else
        drawSBox (NCDXF_b, GRAY);

}

/* given min and max and an approximate number of divisions desired,
 * fill in ticks[] with nicely spaced values and return how many.
 * N.B. return value, and hence number of entries to ticks[], might be as
 *   much as 2 more than numdiv.
 */
int tickmarks (float min, float max, int numdiv, float ticks[])
{
    static int factor[] = { 1, 2, 5 };
    #define NFACTOR    NARRAY(factor)
    float minscale;
    float delta;
    float lo;
    float v;
    int n;

    minscale = fabsf (max - min);

    if (minscale == 0) {
        /* null range: return ticks in range min-1 .. min+1 */
        for (n = 0; n < numdiv; n++)
            ticks[n] = min - 1.0 + n*2.0/numdiv;
        return (numdiv);
    }

    delta = minscale/numdiv;
    for (n=0; n < (int)NFACTOR; n++) {
        float scale;
        float x = delta/factor[n];
        if ((scale = (powf(10.0F, ceilf(log10f(x)))*factor[n])) < minscale)
            minscale = scale;
    }
    delta = minscale;

    lo = floor(min/delta);
    for (n = 0; (v = delta*(lo+n)) < max+delta; )
        ticks[n++] = v;

    return (n);
}

/* return whether this pane is currently rotating to other panes
 */
bool isPaneRotating (PlotPane pp)
{
    // beware plot choices not yet defined
    PlotChoice pc = plot_ch[pp];
    if (!PLOT_CH_IS_REAL(pc))
        return (false);

    bool on_hold = ROTHOLD_TST(pc);
    bool just_us = (plot_rotset[pp] & ~PLOTBIT(pc)) == 0;
    return (!on_hold && !just_us);
}

/* return whether this pane has its own special rotating ability engaged.
 */
bool isSpecialPaneRotating (PlotPane pp)
{
    bool sdo_rot = isSDORotating() && findPaneForChoice (PLOT_CH_SDO) == pp;
    bool onta_rot = isONTARotating() && findPaneForChoice (PLOT_CH_ONTA) == pp;
    return (sdo_rot || onta_rot);
}

/* restore normal PANE_0
 */
void restoreNormPANE0(void)
{
    plot_ch[PANE_0] = PLOT_CH_NONE;
    plot_rotset[PANE_0] = 0;
    savePlotOps();

    drawOneTimeDE();
    drawDEInfo();
    drawOneTimeDX();
    drawDXInfo();
}

/* return whether s is over a plot pane currently displaying a hover-capable plot choice.
 */
bool overHoverPane (const SCoord &s)
{
    for (int i = PANE_0; i < PANE_N; i++) {
        if (inBox (s, plot_b[i])) {
            PlotChoice ch = plot_ch[i];
            if (ch == PLOT_CH_ACTIVENETS || ch == PLOT_CH_LAUNCHES || ch == PLOT_CH_STORMS ||
                ch == PLOT_CH_DXCLUSTER || ch == PLOT_CH_PSK || ch == PLOT_CH_ONTA ||
                ch == PLOT_CH_ADIF || ch == PLOT_CH_DXPEDS || ch == PLOT_CH_SDO ||
                ch == PLOT_CH_MOON || ch == PLOT_CH_SATACT || ch == PLOT_CH_SATACT) {
                return (true);
            }
        }
    }
    return (false);
}

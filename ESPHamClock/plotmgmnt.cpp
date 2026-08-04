/* plot management
 * each PlotPane is in one of PlotChoice state at any given time, all must be different.
 * each pane rotates through the set of bits in its rotset.
 */

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

    // category loop: repeatedly show a small category picker, drill into whichever one is
    // picked (its own short checklist, editing mitems[].set in place), then loop back --
    // so the user can still touch items across multiple categories in one operation, the
    // same thing the old single giant checklist allowed, just one category at a time.
    // Cancelling the picker itself abandons everything (same meaning Cancel has everywhere
    // else); picking the synthetic "Total Selections" entry (or pressing Ok with nothing
    // picked -- see below for why that's possible) stops browsing and applies mitems[] as-is.
    SBox box = plot_b[pp];       // copy -- pristine, never mutated; every runMenu() call
                                  // below gets its own fresh copy of this, not a shared/
                                  // reused SBox, since runMenu() resizes menu_b/ok_b in place
                                  // and a reused box could carry over a previous call's size
    bool proceed = false;

    for (;;) {

        // per-category selected/total counts, for the summary shown on each picker row --
        // a bounded "(N/M)" suffix rather than listing selected item names, which is the
        // right call specifically because it can't grow unwieldy: the format is the same
        // width whether a category has 1 selected item or 20, unlike a name list would be
        int cat_total[N_PANE_CATEGORIES] = {0};
        int cat_selected[N_PANE_CATEGORIES] = {0};
        for (int i = 0; i < n_mitems; i++) {
            PlotChoice pc = labelToChoice (mitems[i].label);
            if (pc != PLOT_CH_NONE) {
                int c = categoryOfChoice(pc);
                cat_total[c]++;
                if (mitems[i].set)
                    cat_selected[c]++;
            }
        }
        int total_selected = 0;
        for (int i = 0; i < n_mitems; i++)
            if (mitems[i].set)
                total_selected++;

        // build the picker: one radio row per non-empty category. No synthetic "done" row --
        // pressing Ok with no category picked (menuStateOk() doesn't gate MENU_1OFN groups,
        // confirmed earlier) already means "finish", same as Ok always means "confirm and
        // proceed" in every other menu in the app; a running total is shown as a footer
        // status line instead of a fake selectable row.
        // cat_labels[] holds the "Name (N/M)" strings runMenu() reads from -- must stay
        // alive through the runMenu() call below, hence declared here, not in a helper
        char cat_labels[N_PANE_CATEGORIES][64];
        MenuItem cat_items[N_PANE_CATEGORIES];
        int cat_of_row[N_PANE_CATEGORIES];     // picker row -> category index
        int n_cat_items = 0;
        for (int c = 0; c < (int)N_PANE_CATEGORIES; c++) {
            if (cat_total[c] == 0)
                continue;
            MenuItem &mi = cat_items[n_cat_items];
            mi.type = MENU_1OFN;
            mi.set = false;
            snprintf (cat_labels[n_cat_items], sizeof(cat_labels[0]), "%s (%d/%d)",
                      pane_categories[c], cat_selected[c], cat_total[c]);
            mi.label = cat_labels[n_cat_items];
            mi.indent = 2;
            mi.group = 1;
            cat_of_row[n_cat_items] = c;
            n_cat_items++;
        }

        char footer[64];
        snprintf (footer, sizeof(footer), "Total Selections (%d/%d)", total_selected, n_mitems);

        SBox cat_box = box;            // fresh copy for this call
        SBox cat_ok_b;
        MenuInfo cat_menu = {cat_box, cat_ok_b, UF_CLOCKSOK, M_CANCELOK, 1, n_cat_items, cat_items,
                              footer, RA8875_YELLOW, true};
        bool cat_ok = runMenu (cat_menu);

        if (!cat_ok) {
            // Cancel at the picker -- abandon the whole operation
            proceed = false;
            break;
        }

        // which category did they pick, if any? Ok with nothing picked means finish.
        int picked = -1;
        for (int i = 0; i < n_cat_items; i++) {
            if (cat_items[i].set) {
                picked = cat_of_row[i];
                break;
            }
        }

        if (picked == -1) {
            if (total_selected == 0) {
                // don't allow finishing with nothing selected anywhere -- the same overall
                // guarantee MENU_AL1OFN used to provide automatically, now enforced once
                // here instead of per-category (see the MENU_TOGGLE comment above for why)
                menuMsg (cat_box, RA8875_RED, "Select an item");
                continue;
            }
            // finished browsing categories -- apply mitems[] as-is
            proceed = true;
            break;
        }

        // drill into the picked category: build its own short checklist from the matching
        // subset of mitems[], preserving current .set state, run it, write any changes back
        int cat = picked;
        MenuItem *sub_items = NULL;
        int *sub_to_master = NULL;     // sub-menu row -> index into mitems[]
        int n_sub = 0;
        for (int i = 0; i < n_mitems; i++) {
            PlotChoice pc = labelToChoice (mitems[i].label);
            if (pc != PLOT_CH_NONE && categoryOfChoice(pc) == cat) {
                sub_items = (MenuItem *) realloc (sub_items, (n_sub+1)*sizeof(MenuItem));
                sub_to_master = (int *) realloc (sub_to_master, (n_sub+1)*sizeof(int));
                if (!sub_items || !sub_to_master)
                    fatalError ("pane category alloc: %d", n_sub);
                sub_items[n_sub] = mitems[i];      // copies current .set state too
                sub_to_master[n_sub] = i;
                n_sub++;
            }
        }

        SBox sub_box = box;            // fresh copy for this call
        SBox sub_ok_b;
        // footer_live_count=true: runMenu() itself recomputes and redraws "<category> (N/M)"
        // after every toggle, using pane_categories[cat] as the label prefix -- genuinely
        // live now, not a snapshot of the count as of when this sub-menu opened
        MenuInfo sub_menu = {sub_box, sub_ok_b, UF_CLOCKSOK, M_CANCELOK, 2, n_sub, sub_items,
                              pane_categories[cat], RA8875_YELLOW, false, true};
        bool sub_ok = runMenu (sub_menu);

        if (sub_ok) {
            for (int i = 0; i < n_sub; i++)
                mitems[sub_to_master[i]].set = sub_items[i].set;
        }
        // else: Cancel within a category discards just that category's edits this pass;
        // loop back to the picker with every other category's state untouched

        free (sub_items);
        free (sub_to_master);
    }

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
void showRotatingBorder ()
{
    time_t t0 = myNow();

    // just leave it white if rotation period is 10 s or less
    const int min_rot = 10;
    uint16_t c = RA8875_WHITE;

    // check plot panes
    for (int pp = 0; pp < PANE_N; pp++) {
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

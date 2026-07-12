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
uint32_t plot_rotset[PANE_N];
uint32_t plot_rothold;

#define X(a,b)  b,                      // expands PLOTNAMES to name and comma
const char *plot_names[PLOT_CH_N] = {
    PLOTNAMES
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
        ok = NVReadUInt8 (NV_PLOT_0, &pc) && (pc < PLOT_CH_N || pc == PLOT_CH_NONE);
        break;
    case PANE_1:
        ok = NVReadUInt8 (NV_PLOT_1, &pc) && (pc < PLOT_CH_N);
        break;
    case PANE_2:
        ok = NVReadUInt8 (NV_PLOT_2, &pc) && (pc < PLOT_CH_N);
        break;
    case PANE_3:
        ok = NVReadUInt8 (NV_PLOT_3, &pc) && (pc < PLOT_CH_N);
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
            if (plot_rotset[pp] & (1u << i)) {
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
            const PlotChoice ch_defaults[PANE_N] = {PLOT_CH_SSN, PLOT_CH_XRAY, PLOT_CH_SDO};
            plot_ch[pp] = ch_defaults[pp];
            plot_rotset[pp] = (1u << plot_ch[pp]);
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
        return (true);

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
        if (plot_rotset[pp] & (1u << i))
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
bool enforceCDownAlone (const SBox &box, uint32_t rotset)
{
    if ((rotset & (1<<PLOT_CH_COUNTDOWN)) && (rotset & ~(1<<PLOT_CH_COUNTDOWN))) {
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
static PlotChoice askPaneChoice (PlotPane pp)
{
    // not for use for PANE_0
    if (pp == PANE_0)
        fatalError ("askPaneChoice called with pane 0");

    // set this temporarily to show all choices, just for testing worst-case layout
    #define ASKP_SHOWALL 0                      // RBF

    // build items from all candidates suitable for this pane
    MenuItem *mitems = NULL;
    int n_mitems = 0;
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice pc = (PlotChoice) i;
        PlotPane pp_ch = findPaneForChoice (pc);

        // otherwise use if not used elsewhere and available or already assigned to this pane
        if ( (pp_ch == PANE_NONE && plotChoiceIsAvailable(pc)) || pp_ch == pp || ASKP_SHOWALL) {
            // set up next menu item
            mitems = (MenuItem *) realloc (mitems, (n_mitems+1)*sizeof(MenuItem));
            if (!mitems)
                fatalError ("pane alloc: %d", n_mitems);
            MenuItem &mi = mitems[n_mitems++];
            mi.type = MENU_AL1OFN;
            mi.set = (plot_rotset[pp] & (1u << pc)) ? true : false;
            mi.label = plot_names[pc];
            mi.indent = 2;
            mi.group = 1;
        }
    }

    // nice sort by label
    qsort (mitems, n_mitems, sizeof(MenuItem), menuChoiceQS);

    // run
    SBox box = plot_b[pp];       // copy
    SBox ok_b;
    MenuInfo menu = {box, ok_b, UF_CLOCKSOK, M_CANCELOK, 2, n_mitems, mitems};
    bool menu_ok = runMenu (menu);

    // return current choice by default
    PlotChoice return_ch = plot_ch[pp];

    if (menu_ok) {

        // find new rotset for this pane
        uint32_t new_rotset = 0;
        for (int i = 0; i < n_mitems; i++) {
            if (mitems[i].set) {
                // find which choice this refers to by matching labels
                for (int j = 0; j < PLOT_CH_N; j++) {
                    if (strcmp (plot_names[j], mitems[i].label) == 0) {
                        new_rotset |= (1u << j);
                        break;
                    }
                }
            }
        }

        // enforce a few panes that do not work well with rotation
        uint32_t new_sets[PANE_N];
        memcpy (new_sets, plot_rotset, sizeof(new_sets));
        new_sets[pp] = new_rotset;
        if (!enforceCDownAlone (box, new_rotset)) {

            plot_rotset[pp] = new_rotset;
            savePlotOps();

            // return current choice if still in rotset, else just pick one
            if (!(plot_rotset[pp] & (1u << return_ch))) {
                for (int i = 0; i < PLOT_CH_N; i++) {
                    if (plot_rotset[pp] & (1u << i)) {
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
        if ( (plot_rotset[i] & (1<<pc)) )
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
            if (plot_rotset[pp] & (1u << j)) {
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
                if (plot_ch[j] == pc || (plot_rotset[j] & (1u << pc))) {
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
        if (((1<<pc) & PANE_0_CH_MASK)
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
            if (plot_rotset[i] & (1u << PLOT_CH_COUNTDOWN)) {
                plot_rotset[i] &= ~(1u << PLOT_CH_COUNTDOWN);
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

    // reserve top portion for bringing up choice menu or forcing rotation
    bool in_top = s.y < box.y + PANETITLE_H;

    if (in_top && tt == TT_TAP_BX) {
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
        if (checkOnTheAirTouch (s, box))
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
                plot_rotset[pp] = (1u << plot_ch[pp]);   // no auto rotation
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
                plot_rotset[pp] = (1u << plot_ch[pp]);   // no auto rotation
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
                plot_rotset[pp] = (1u << plot_ch[pp]);   // no auto rotation
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
                plot_rotset[pp] = (1u << plot_ch[pp]);   // no auto rotation
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
    // retrieve rotation sets -- ok to leave 0 for now if not yet defined
    memset (plot_rotset, 0, sizeof(plot_rotset));
    NVReadUInt32 (NV_PANE0ROTSET, &plot_rotset[PANE_0]);
    NVReadUInt32 (NV_PANE1ROTSET, &plot_rotset[PANE_1]);
    NVReadUInt32 (NV_PANE2ROTSET, &plot_rotset[PANE_2]);
    NVReadUInt32 (NV_PANE3ROTSET, &plot_rotset[PANE_3]);

    // NB. since NV_PANE0ROTSET repurposes a prior NV it might contain invalid bits, 0 all if find any
    if (plot_rotset[PANE_0] & ~PANE_0_CH_MASK) {

        Serial.printf ("PANE: Resetting bogus Pane 0 rot set: 0x%x\n", plot_rotset[PANE_0]);
        plot_rotset[PANE_0] = 0;
        plot_ch[PANE_0] = PLOT_CH_NONE;

        // save scrubbed values
        NVWriteUInt32 (NV_PANE0ROTSET, plot_rotset[PANE_0]);
        NVWriteUInt8 (NV_PLOT_0, plot_ch[PANE_0]);
    }


    // rm any choice not available
    // all-panes mask, valid even when PLOT_CH_N == 32 (1u<<32 would be UB)
    const uint32_t all_panes = (PLOT_CH_N >= 32) ? 0xFFFFFFFFu : ((1u << PLOT_CH_N) - 1);
    for (int i = PANE_0; i < PANE_N; i++) {
        plot_rotset[i] &= all_panes;                     // reset any bits too high
        for (int j = 0; j < PLOT_CH_N; j++) {
            if (plot_rotset[i] & (1u << j)) {
                if (!plotChoiceIsAvailable ((PlotChoice)j)) {
                    plot_rotset[i] &= ~(1u << j);
                    Serial.printf ("PANE: Removing %s from pane %d: not available\n", plot_names[j],i);
                }
            }
        }
    }

    // if current selection not yet defined or not in rotset pick one from rotset or set a default
    for (int i = PANE_0; i < PANE_N; i++) {
        if (!getPlotChoiceNV ((PlotPane)i, &plot_ch[i]) || !(plot_rotset[i] & (1u << plot_ch[i])))
            setDefaultPaneChoice ((PlotPane)i);
    }

    // insure same choice not in more than 1 pane
    for (int i = PANE_0; i < PANE_N; i++) {
        for (int j = i+1; j < PANE_N; j++) {
            if (plot_ch[i] == plot_ch[j]) {
                // found dup -- replace with some other unused choice
                for (int k = 0; k < PLOT_CH_N; k++) {
                    PlotChoice new_pc = (PlotChoice)k;
                    if (plotChoiceIsAvailable(new_pc) && findPaneChoiceNow(new_pc) == PANE_NONE) {
                        Serial.printf ("PANE: Reassigning dup pane %d from %s to %s\n", j,
                                        plot_names[plot_ch[j]], plot_names[new_pc]);
                        // remove dup from rotation set then replace with new choice
                        plot_rotset[j] &= ~(1u << plot_ch[j]);
                        plot_rotset[j] |= (1u << new_pc);
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
            plot_rotset[i] |= (1u << plot_ch[i]);

    // log and save final arrangement
    for (int i = PANE_0; i < PANE_N; i++)
        logPaneRotSet ((PlotPane)i, plot_ch[i]);
    savePlotOps();
}

/* update NV_PANE?CH from plot_rotset[] and NV_PLOT_? from plot_ch[]
 */
void savePlotOps()
{
    NVWriteUInt32 (NV_PANE0ROTSET, plot_rotset[PANE_0]);
    NVWriteUInt32 (NV_PANE1ROTSET, plot_rotset[PANE_1]);
    NVWriteUInt32 (NV_PANE2ROTSET, plot_rotset[PANE_2]);
    NVWriteUInt32 (NV_PANE3ROTSET, plot_rotset[PANE_3]);

    NVWriteUInt8 (NV_PLOT_0, plot_ch[PANE_0]);
    NVWriteUInt8 (NV_PLOT_1, plot_ch[PANE_1]);
    NVWriteUInt8 (NV_PLOT_2, plot_ch[PANE_2]);
    NVWriteUInt8 (NV_PLOT_3, plot_ch[PANE_3]);
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
    if (pc == PLOT_CH_N)
        return (false);

    bool on_hold = ROTHOLD_TST(pc);
    bool just_us = (plot_rotset[pp] & ~(1u << pc)) == 0;
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

    drawOneTimeDE();
    drawDEInfo();
    drawOneTimeDX();
    drawDXInfo();
}

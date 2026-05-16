/* manage configuration files.
 * allow current config to be saved, and offer restore/update/delete/rename of existing configurations.
 */

#include "HamClock.h"
#include "cfg_info_format.h"

// directory and suffix for more eeprom files
static const char cfg_dir[] = "configurations";
static const char cfg_suffix[] = ".eeprom";


// layout
#define TB_SET_CLR  RA8875_GREEN                // most toggle button selected color
#define TB_DEL_CLR  RA8875_RED                  // Delete toggle button selected color
#define TB_OFF_CLR  RA8875_BLACK                // toggle button idle color
#define TB_B_CLR    RA8875_WHITE                // toggle button border color
#define TL_CLR      RGB565(255,200,100)         // titles color
#define NAME_CCLR   RA8875_GREEN                // name cursor color
#define TX_CLR      RA8875_WHITE                // text color
#define SCROLL_CLR  TL_CLR                      // scroller color
#define TITLE_Y     35                          // title y text baseline
#define TB_SZ       20                          // square toggle button size
#define ROW_DY      34                          // table row spacing
#define TBL_HY      160                         // table headings text baseline y
#define TBL_TBX     40                          // x of left side of first table column TB
#define TBL_Y       180                         // y of top of first table row TB
#define TBL_LX      15                          // x of left side of first table column title
#define TBL_DX      82                          // dx to next table column 
#define COK_Y       425                         // Cancel and Ok button top y
#define COK_W       100                         // " width
#define COK_H       38                          // " height
#define NAME_CW     14                          // Name cursor width
#define NAME_CDY    9                           // " cursor drop below baseline
#define NAME_H      (FONT_DY+NAME_CDY)          // " name field height
#define NAME_W      430                         // " name field width
#define SAVE_TX     TBL_TBX                     // Save TB x
#define SAVE_LX     (SAVE_TX+45)                // " label x
#define SAVE_NX     (SAVE_LX+110)               // " name field x
#define SAVE_Y      80                          // " text baseline Y
#define FONT_DY     24                          // " distance from top to baseline
#define MAX_NAMLEN  50                          // max chars in save_name, w/EOS, but clipped to SAVE_NW
#define RESET_TX    SAVE_TX                     // Reset TB x
#define RESET_LX    SAVE_LX                     // " label x
#define RESET_Y     (SAVE_Y+ROW_DY)             // " text baseline Y
#define MAX_VIS     ((COK_Y-TBL_Y)/ROW_DY)      // max N display rows
#define SCR_W       16                          // scrollbar width
#define SCR_X       8                           // " left edge x
#define SCR_Y       TBL_Y                       // " top y
#define SCR_H       ((MAX_VIS-1)*ROW_DY+TB_SZ)  // " height

#define ERR_DWELL   2000                        // dwell time for err msg, millis

// table columns
typedef enum {
    USE_CIDX,                                   // "use" column index
    UPD_CIDX,                                   // "update" column index
    DEL_CIDX,                                   // "delete" column index
    REN_CIDX,                                   // "rename" column index
    N_CIDX,                                     // n columns
} ColumnType;

#define OTHER_CIDX N_CIDX                       // handy pseudo-type for one-off live Save and Reset

// toggle button controller
typedef struct {
    SBox box;                                   // where
    ColumnType type;                            // usage
    bool on;                                    // state
} TBControl;

// editable name info
typedef struct {
    char name[MAX_NAMLEN];                      // name string, always with EOS
    unsigned cursor;                            // cursor location, index into name[]
    SBox box;                                   // name display box
} CfgName;

// one config entry
typedef struct {
    char *orig_name;                            // original name .. N.B. malloced but don't free
    char edit_name[MAX_NAMLEN];                 // edited name string, use only if on[REN_CIDX]
    bool on[N_CIDX];                            // the states pertaining to this name
} CfgInfo;

typedef struct {
    CfgInfo *cfg;                               // malloced collection, sorted alphabetically with "a" at [0]
    int n_cfg;                                  // n total cfg[], visible or not
    int top_cfg;                                // cfg[] index currently at top of display

    TBControl save_tb;                          // Save TB 
    CfgName save_name;                          // Save text field

    TBControl reset_tb;                         // Reset TB

    TBControl tbl_tb[MAX_VIS][N_CIDX];          // toggle button table, drawn with [0] on top
    CfgName tbl_name[MAX_VIS];                  // editable name for each table row

    ScrollBar sb;                               // scrollable if too many to show all
} CfgTable;


// handy conversions between table row and CfgInfo index
#define T2C(ct,tr)  ((tr) + (ct).top_cfg)
#define C2T(ct,ci)  ((ci) - (ct).top_cfg)

// handy whether cfg[ci] or tbl_row is visible
#define CVIS(ct,ci) ((ci) >= (ct).top_cfg && (ci) < (ct).n_cfg && C2T((ct),(ci)) < MAX_VIS)
#define TVIS(ct,tr) (CVIS((ct),T2C((ct),(tr))))




/* draw the given TB in its current state
 */
static void drawTBControl (const TBControl &tb)
{
    if (tb.on) {
        // color depends on column
        uint16_t color = tb.type == DEL_CIDX ? TB_DEL_CLR : TB_SET_CLR;   // show delete column in red
        fillSBox (tb.box, color);
        drawSBox (tb.box, TB_B_CLR);
    } else {
        fillSBox (tb.box, TB_OFF_CLR);
        drawSBox (tb.box, TB_B_CLR);
    }
}

/* erase the cursor used with the given cfg name
 */
static void eraseCfgNameCursor (const CfgName &cn)
{
    // erase entire line so we don't care where it was before
    const SBox &b = cn.box;
    uint16_t cy = b.y + FONT_DY + NAME_CDY;
    tft.drawLine (b.x, cy, b.x+b.w, cy, RA8875_BLACK);
}

/* draw the given name's cursor
 */
static void drawCfgNameCursor (const CfgName &cn)
{
    // erase old
    eraseCfgNameCursor (cn);

    // find x position by finding width of string chopped at cursor
    char copy[sizeof(cn.name)];
    strcpy (copy, cn.name);
    copy[cn.cursor] = '\0';
    const SBox &b = cn.box;
    uint16_t cx = b.x + getTextWidth (copy);
    uint16_t cy = b.y + FONT_DY + NAME_CDY;
    tft.drawLine (cx, cy, cx+NAME_CW, cy, NAME_CCLR);
}

/* erase the given config name and cursor
 */
static void eraseCfgName (const CfgName &cn)
{
    fillSBox (cn.box, RA8875_BLACK);
    eraseCfgNameCursor (cn);
}

/* draw the given config name
 * N.B. text only, not the cursor
 */
static void drawCfgName (const CfgName &cn)
{
    eraseCfgName (cn);

    tft.setTextColor (TX_CLR);
    tft.setCursor (cn.box.x, cn.box.y + FONT_DY);
    tft.print (cn.name);

    // drawSBox (cn.box, RA8875_RED);   // RBF
}


/* draw either the edited or original name, with or without cursor
 */
static void drawCfgEditName (CfgName &cn, const CfgInfo &ci, bool editing, bool cursor)
{
    quietStrncpy (cn.name, editing ? ci.edit_name : ci.orig_name, sizeof(cn.name));
    drawCfgName (cn);

    if (editing && cursor)
        drawCfgNameCursor (cn);
    else
        eraseCfgNameCursor (cn);
}

/* given row/col of one of the ctbl.tbl_tb[] that has just toggled on or off:
 *   draw and save its state in the corresponding ctbl.cfg.
 *   go through all the other TB states and update any that conflict.
 */
static void updateCfgChoices (CfgTable &ctbl, int tbl_row, int tbl_col)
{
    // draw
    TBControl &tb = ctbl.tbl_tb[tbl_row][tbl_col];
    drawTBControl (tb);

    // set the cfg[] state corresponding to tbl_row to match
    const int cfg_row = T2C(ctbl,tbl_row);
    ctbl.cfg[cfg_row].on[tbl_col] = tb.on;

    // off never conflicts
    if (tb.on) {

        // columns other than tbl_col in tbl_row must be off
        for (int c = 0; c < N_CIDX; c++) {
            if (c != tbl_col && ctbl.cfg[cfg_row].on[c]) {
                ctbl.cfg[cfg_row].on[c] = false;                        // turn off in master list
                ctbl.tbl_tb[tbl_row][c].on = false;                     // turn off TB
                drawTBControl (ctbl.tbl_tb[tbl_row][c]);
            }
        }

        // check those columns that require at most one to be on
        if (tbl_col == USE_CIDX || tbl_col == UPD_CIDX) {
            for (int cr = 0; cr < ctbl.n_cfg; cr++) {                   // all cfg entries
                if (cr != cfg_row && ctbl.cfg[cr].on[tbl_col]) {        // other than this row on?
                    ctbl.cfg[cr].on[tbl_col] = false;                   // turn off in master list
                    if (CVIS(ctbl,cr)) {                                // TB visible?
                        int dsp_row = C2T(ctbl,cr);                     // TB row
                        ctbl.tbl_tb[dsp_row][tbl_col].on = false;       // turn off TB
                        drawTBControl (ctbl.tbl_tb[dsp_row][tbl_col]);
                    }
                }
            }
        }

        // turn off Save but retain name
        ctbl.save_tb.on = false;
        drawTBControl (ctbl.save_tb);
        eraseCfgName (ctbl.save_name);

        // and Reset
        ctbl.reset_tb.on = false;
        drawTBControl (ctbl.reset_tb);
    }

    // now check the rename fields, insuring at most tbl_row shows cursor
    for (int tr = 0; tr < MAX_VIS; tr++) {
        if (TVIS(ctbl,tr)) {
            CfgName &cn = ctbl.tbl_name[tr];
            CfgInfo &ci = ctbl.cfg[T2C(ctbl,tr)];
            bool tb_on = ctbl.tbl_tb[tr][REN_CIDX].on;
            drawCfgEditName (cn, ci, tb_on, tr == tbl_row);
        }
    }
}



/* draw all table controls and names based on current scroller.
 */
static void drawCfgTable (CfgTable &ctbl)
{
    // prep for text
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (TX_CLR);

    // draw each table row even if just erasing it
    for (int tbl_row = 0; tbl_row < MAX_VIS; tbl_row++) {

        // get corresponding master row
        int cfg_row = T2C(ctbl,tbl_row);
        CfgInfo &ci = ctbl.cfg[cfg_row];

        if (TVIS(ctbl,tbl_row)) {

            // update TBs
            for (int c = 0; c < N_CIDX; c++) {
                ctbl.tbl_tb[tbl_row][c].on = ci.on[c];
                drawTBControl (ctbl.tbl_tb[tbl_row][c]);
            }

            // show edited name if REN_CIDX is on else original
            drawCfgEditName (ctbl.tbl_name[tbl_row], ci, ctbl.tbl_tb[tbl_row][REN_CIDX].on, false);

        } else {

            // no TBs
            for (int c = 0; c < N_CIDX; c++)
                fillSBox (ctbl.tbl_tb[tbl_row][c].box, TB_OFF_CLR);

            // no names
            eraseCfgName (ctbl.tbl_name[tbl_row]);

        }
    }
}

/* reset all matrix controls.
 */
static void setAllCfgTableOff (CfgTable &ctbl)
{
    for (int i = 0; i < ctbl.n_cfg; i++)
        for (int j = 0; j < N_CIDX; j++)
            ctbl.cfg[i].on[j] = false;
    drawCfgTable (ctbl);
}

/* convert the given config name to an actual full file name
 */
static void cfg2file (const char *cfg_name, char fn[], size_t fn_l)
{
    int n_pref = snprintf (fn, fn_l, "%s/%s/", our_dir.c_str(), cfg_dir);
    strncpySubChar (fn+n_pref, cfg_name, '_', ' ', fn_l - n_pref);
    n_pref += strlen(cfg_name);
    snprintf (fn+n_pref, fn_l-n_pref, "%s", cfg_suffix);
}

/* convert the given file name to a config name.
 * return whether file name and contents meet basic requirements.
 */
static bool file2cfg (const char *fn, char cfg[], size_t cfg_l)
{
    // sidecar files live alongside .eeprom files; skip them silently
    if (strstr (fn, HC_INFO_SUFFIX)) return (false);

    const char *suffix = strstr (fn, cfg_suffix);
    if (suffix) {
        size_t basename_l = suffix - fn;
        if (basename_l > 0 && basename_l < cfg_l) {
            // file name looks promising, check basic contents
            char path[2000];
            snprintf (path, sizeof(path), "%s/%s/%s", our_dir.c_str(), cfg_dir, fn);
            FILE *fp = fopen (path, "r");
            if (fp) {
                if (fgets(path,sizeof(path),fp) && !feof(fp) && !ferror(fp) && !strcmp(path,"00000000 00\n")){
                    // looks plausible!
                    strncpySubChar (cfg, fn, ' ', '_', basename_l+1);   // include room for EOS
                    return (true);
                }
                fclose (fp);
            }
        }
    }

    // nope
    if (strcmp(fn,".") != 0 && strcmp (fn,"..") != 0)   // log other than the dot files
        Serial.printf ("CFG: bogus config file: '%s'\n", fn);
    return (false);
}

/* set caller's list pointer to malloced array of malloced configuration names and return count.
 */
static int getCfgFiles (const char *dir, char ***cfg_names)
{
    // open the directory of configuration files
    DIR *dirp = opendir (dir);
    if (dirp == NULL)
        fatalError ("%s: %s", dir, strerror(errno));

    // fill user's array with config names which are derived from file names with '-' changed to ' '
    char **&nm_list = *cfg_names;                       // handy reference to caller's list location
    nm_list = NULL;
    struct dirent *dp;
    int n_cfgs = 0;
    while ((dp = readdir(dirp)) != NULL) {
        nm_list = (char **) realloc (nm_list, (n_cfgs + 1) * sizeof(char *));
        if (nm_list == NULL)
            fatalError ("No memory for %d config files", n_cfgs+1);
        char cfg_name[MAX_NAMLEN];
        if (file2cfg (dp->d_name, cfg_name, sizeof(cfg_name)))
            nm_list[n_cfgs++] = strdup (cfg_name);
    }

    // finished with directory
    closedir (dirp);

    // nice alpha order, "a" at [0]
    qsort (nm_list, n_cfgs, sizeof(char*), qsAString);

    // return count
    return (n_cfgs);
}

/* copy the given config name as the new default eeprom
 */
static void engageCfgFile (const char *cfg_name)
{
    // open for reading
    char buf[2000];
    cfg2file (cfg_name, buf, sizeof(buf));
    FILE *from_fp = fopen (buf, "r");
    if (!from_fp)
        fatalError ("%s: %s", buf, strerror(errno));

    // overwrite existing
    const char *eeprom = EEPROM.getFilename();
    FILE *to_fp = fopen (eeprom, "w");
    if (!to_fp)
        fatalError ("%s: %s", eeprom, strerror(errno));

    // set real owner, not fatal if can't
    if (fchown (fileno(to_fp), getuid(), getgid()) < 0)
        Serial.printf ("CFG: chown(%s,%d,%d): %s\n", eeprom, getuid(), getgid(), strerror(errno));

    // copy
    size_t n_r;
    while ((n_r = fread (buf, 1, sizeof(buf), from_fp)) > 0) {
        if (fwrite (buf, 1, n_r, to_fp) != n_r)
            fatalError ("%s: %s\n", eeprom, strerror(errno));
    }

    // finished
    fclose (to_fp);
    fclose (from_fp);

    Serial.printf ("CFG: engage '%s'\n", cfg_name);
}

/* save the current configuration to a file based on the given name.
 */
static void saveCfgFile (const char *cfg_name)
{
    // create new
    char buf[2000];
    cfg2file (cfg_name, buf, sizeof(buf));
    FILE *to_fp = fopen (buf, "w");
    if (!to_fp)
        fatalError ("%s: %s", buf, strerror(errno));

    // set real owner, not fatal if can't
    if (fchown (fileno(to_fp), getuid(), getgid()) < 0)
        Serial.printf ("CFG: chown(%s,%d,%d): %s\n", buf, getuid(), getgid(), strerror(errno));

    // read existing
    const char *eeprom = EEPROM.getFilename();
    FILE *from_fp = fopen (eeprom, "r");
    if (!from_fp)
        fatalError ("%s: %s", eeprom, strerror(errno));

    // copy
    size_t n_r;
    while ((n_r = fread (buf, 1, sizeof(buf), from_fp)) > 0) {
        if (fwrite (buf, 1, n_r, to_fp) != n_r)
            fatalError ("%s: %s\n", eeprom, strerror(errno));
    }

    // finished
    fclose (to_fp);
    fclose (from_fp);

    Serial.printf ("CFG: save '%s'\n", cfg_name);
    writeCfgInfo (cfg_name);
}

/* delete the config file based on the given name
 */
static void deleteCfgFile (const char *cfg_name)
{
    char buf[2000];
    cfg2file (cfg_name, buf, sizeof(buf));
    if (unlink (buf) < 0)
        fatalError ("unlink(%s): %s", buf, strerror(errno));
    Serial.printf ("CFG: delete '%s'\n", cfg_name);
    deleteCfgInfo (cfg_name);
}

/* rename config file from to to
 */
static void renameCfgFile (const char *from, const char *to)
{
    char f_buf[2000], t_buf[2000];
    cfg2file (from, f_buf, sizeof(f_buf));
    cfg2file (to, t_buf, sizeof(t_buf));
    if (rename (f_buf, t_buf) < 0)
        fatalError ("rename(%s,%s): %s", f_buf, t_buf, strerror(errno));
    Serial.printf ("CFG: rename '%s' to '%s'\n", from, to);
    renameCfgInfo (from, to);
}

/* free a string list and its contents
 */
static void freeCfgNames (char **cfg_names, int n_cfg)
{
    for (int i = 0; i < n_cfg; i++)
        free ((void*)cfg_names[i]);
    free ((void*)cfg_names);
}

/* free any memory allocated for CfgTable
 */
static void freeCfgTable (CfgTable &ctbl)
{
    // N.B. do NOT free the cfg.name's, these are malloc'd elsewhere

    free ((void*)ctbl.cfg);
}

/* decide whether the candidate name is acceptable.
 * if so return true, else briefly show reason return false.
 */
static bool isCfgNameOk (CfgName &cn)
{
    // clean ends
    strTrimAll (cn.name);

    // can't be empty
    if (strlen (cn.name) == 0) {
        // show error message
        fillSBox (cn.box, RA8875_BLACK);
        eraseCfgNameCursor (cn);
        tft.setCursor (cn.box.x, cn.box.y + FONT_DY);
        tft.setTextColor (RA8875_RED);
        tft.print ("Empty");
        wdDelay(ERR_DWELL);

        // restore message
        drawCfgName (cn);
        return (false);
    }

    // can't already exist
    char buf[2000];
    cfg2file (cn.name, buf, sizeof(buf));
    FILE *test_fp = fopen (buf, "r");
    if (test_fp) {
        // done with fp
        fclose (test_fp);

        // show error message
        fillSBox (cn.box, RA8875_BLACK);
        eraseCfgNameCursor (cn);
        tft.setCursor (cn.box.x, cn.box.y + FONT_DY);
        tft.setTextColor (RA8875_RED);
        tft.print ("Exists");
        wdDelay(ERR_DWELL);

        // restore message and turn off TB
        drawCfgName (cn);
        return (false);
    }

    // name looks reasonable
    return (true);
}

/* given a tap and/or keyboard char known to be within the save box, update cn
 */
static void editCfgName (const SCoord &s, const char kbc, CfgName &cn)
{
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (TX_CLR);

    if (inBox (s, cn.box)) {
        // set cursor based on tap position
        uint16_t sdx = s.x - cn.box.x;
        char copy[sizeof(cn.name)];
        strcpy (copy, cn.name);
        (void) maxStringW (copy, sdx);
        cn.cursor = strlen (copy);
        drawCfgNameCursor (cn);

    } else if (kbc == CHAR_RIGHT) {
        if (cn.cursor < strlen(cn.name)) {
            cn.cursor += 1;
            drawCfgNameCursor (cn);
        }

    } else if (kbc == CHAR_LEFT) {
        if (cn.cursor > 0) {
            cn.cursor -= 1;
            drawCfgNameCursor (cn);
        }

    } else if (kbc == CHAR_BS || kbc == CHAR_DEL) {
        if (cn.cursor > 0) {
            // remove char to left of cursor
            memmove (cn.name+cn.cursor-1, cn.name+cn.cursor,
                                                            sizeof(cn.name)-cn.cursor+1); // w/EOS
            cn.cursor -= 1;
            drawCfgName (cn);
            drawCfgNameCursor (cn);
        }

    } else if (isprint (kbc)) {
        size_t sl = strlen(cn.name);
        if (sl < sizeof(cn.name)-1 && getTextWidth(cn.name) < cn.box.w - 2*NAME_CW) {
            // open a gap to insert kbc at cursor
            memmove (cn.name+cn.cursor+1, cn.name+cn.cursor,
                                                            sizeof(cn.name)-cn.cursor+1); // w/EOS
            cn.name[cn.cursor] = kbc;
            cn.cursor += 1;
            drawCfgName (cn);
            drawCfgNameCursor (cn);
        }
    }
}


/* offer user the means to reset, save, delete, rename or restore from a set of existing configuration names.
 * return whether a reboot is required and, if so, whether to restore all defaults
 */
static bool runConfigMenu (char **names, int n_cfgs, bool &restore_def)
{
    // master table
    CfgTable ctbl;

    // begin scrolling with first cfg[] on top
    ctbl.top_cfg = 0;

    // init the save controls
    ctbl.save_tb = {{SAVE_TX, SAVE_Y-TB_SZ, TB_SZ, TB_SZ}, OTHER_CIDX, false};
    memset (ctbl.save_name.name, 0, sizeof(ctbl.save_name.name));
    ctbl.save_name.cursor = 0;
    ctbl.save_name.box = {SAVE_NX, SAVE_Y-FONT_DY, NAME_W, NAME_H};
    drawTBControl (ctbl.save_tb);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (TL_CLR);
    tft.setCursor (SAVE_LX, SAVE_Y);
    tft.print ("Save new:");

    // alloc and init one CfgInfo for each names[]
    ctbl.n_cfg = n_cfgs;
    ctbl.cfg = (CfgInfo *) calloc (ctbl.n_cfg, sizeof(CfgInfo));
    if (n_cfgs > 0 && ctbl.cfg == NULL)
        fatalError ("no memory for %d configuration files", ctbl.n_cfg);
    for (int i = 0; i < ctbl.n_cfg; i++) {
        ctbl.cfg[i].orig_name = names[i];                       // not another malloc!
        memcpy (ctbl.cfg[i].edit_name, names[i], sizeof(ctbl.cfg[i].edit_name));
    }

    // init display matrix row and column
    for (int tbl_row = 0; tbl_row < MAX_VIS; tbl_row++) {

        // text baseline
        uint16_t y = TBL_Y + tbl_row*ROW_DY + TB_SZ;

        // TB matrix
        for (int cidx = 0; cidx < N_CIDX; cidx++) {
            TBControl &t = ctbl.tbl_tb[tbl_row][cidx];
            t.type = (ColumnType) cidx;
            t.on = false;
            SBox &b = t.box;
            b.x = TBL_TBX + cidx*TBL_DX;
            b.y = y - TB_SZ;
            b.w = b.h = TB_SZ;
        }

        // name
        CfgName &cn = ctbl.tbl_name[tbl_row];
        cn.box = {(uint16_t)(TBL_LX + 4*TBL_DX), (uint16_t)(y - FONT_DY), NAME_W, NAME_H};
        cn.cursor = 0;
        memset (&cn.name, 0, sizeof(cn.name));
    }

    // position the ok and cancel buttons
    SBox ok_b = {200, COK_Y, COK_W, COK_H};
    SBox cancel_b = {500, COK_Y, COK_W, COK_H};

    // postion reset button and its label
    ctbl.reset_tb = {{RESET_TX, RESET_Y-TB_SZ, TB_SZ, TB_SZ}, OTHER_CIDX, false};
    drawTBControl (ctbl.reset_tb);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (TL_CLR);
    tft.setCursor (RESET_LX, RESET_Y);
    tft.print ("Reset to default configuration (restarts)");

    // prep scroller
    SBox scroll_b = {SCR_X, SCR_Y, SCR_W, SCR_H};
    ctbl.sb.init (MAX_VIS, ctbl.n_cfg, scroll_b);

    // draw title
    static const char title[] = "Manage Configurations";
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor (TL_CLR);
    tft.setCursor ((800-getTextWidth(title))/2, TITLE_Y);
    tft.print (title);

    // draw column headings
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (TL_CLR);
    tft.setCursor (TBL_LX + 0*TBL_DX, TBL_HY);
    tft.print ("Restore");
    tft.setCursor (TBL_LX + 1*TBL_DX, TBL_HY);
    tft.print ("Update");
    tft.setCursor (TBL_LX + 2*TBL_DX, TBL_HY);
    tft.print ("Delete");
    tft.setCursor (TBL_LX + 3*TBL_DX - 10, TBL_HY);
    tft.print ("Rename");
    tft.setCursor (TBL_LX + 4*TBL_DX, TBL_HY);
    tft.print ("Name:");

    // add small restart reminder below Restore
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (TBL_LX+10, TBL_HY+4);
    tft.print ("(restarts)");

    // draw cancel and ok
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drawStringInBox ("Ok", ok_b, false, RA8875_WHITE);
    drawStringInBox ("Cancel", cancel_b, false, RA8875_WHITE);

    // draw full table
    drawCfgTable (ctbl);

    // prep run
    SBox screen_b = {0, 0, tft.width(), tft.height()};
    drawSBox (screen_b, GRAY);
    UserInput ui = {
        screen_b,
        UI_UFuncNone, 
        UF_UNUSED,
        UI_NOTIMEOUT,
        UF_NOCLOCKS,
        {0, 0}, TT_NONE, '\0', false, false
    }; 

    // note where editing, tbl_row or -1 for Save else -2
    #define EDITING_SAVE (-1)
    #define EDITING_NONE (-2)
    int editing = EDITING_NONE;
    
    // run
    bool user_ok = false;
    restore_def = false;
    while (waitForUser(ui)) {

        // done when ESC or Cancel
        if (ui.kb_char == CHAR_ESC || inBox (ui.tap, cancel_b)) {
            drawStringInBox ("Cancel", cancel_b, true, RA8875_WHITE);
            wdDelay(500);
            break;
        }

        // engage when CR or Ok but beware blank save
        if (ui.kb_char == CHAR_NL || ui.kb_char == CHAR_CR || inBox (ui.tap, ok_b)) {
            if (!ctbl.save_tb.on || isCfgNameOk (ctbl.save_name)) {
                drawStringInBox ("Ok", ok_b, true, RA8875_WHITE);
                wdDelay(500);
                user_ok = true;
                break;
            }
        }

        // steer to appropriate control, if any
        if (ctbl.sb.checkTouch (ui.kb_char, ui.tap)) {
            ctbl.top_cfg = ctbl.sb.getTop();
            drawCfgTable (ctbl);

        } else if (inBox (ui.tap, ctbl.save_name.box)) {
            drawCfgNameCursor (ctbl.save_name);
            setAllCfgTableOff (ctbl);
            editing = EDITING_SAVE;

        } else if (inBox (ui.tap, ctbl.save_tb.box)) {
            ctbl.save_tb.on = !ctbl.save_tb.on;
            drawTBControl (ctbl.save_tb);
            editing = ctbl.save_tb.on ? EDITING_SAVE : EDITING_NONE;
            if (ctbl.save_tb.on) {
                drawCfgNameCursor (ctbl.save_name);
                setAllCfgTableOff (ctbl);
            } else
                eraseCfgNameCursor (ctbl.save_name);

        } else if (inBox (ui.tap, ctbl.reset_tb.box)) {
            restore_def = ctbl.reset_tb.on = !ctbl.reset_tb.on;
            drawTBControl (ctbl.reset_tb);
            if (ctbl.reset_tb.on)
                setAllCfgTableOff (ctbl);

        } else {
            // find table entry, if any
            bool found = false;
            for (int tbl_row = 0; !found && tbl_row < MAX_VIS && TVIS(ctbl,tbl_row); tbl_row++) {

                // check TB matrix
                for (int cidx = 0; !found && cidx < N_CIDX; cidx++) {
                    TBControl &tb = ctbl.tbl_tb[tbl_row][cidx];
                    if (inBox (ui.tap, tb.box)) {

                        // toggle
                        tb.on = !tb.on;
                        updateCfgChoices (ctbl, tbl_row, cidx);

                        // note whether editing
                        if (cidx == REN_CIDX)
                            editing = tb.on ? tbl_row : EDITING_NONE;

                        // multilevel goto here not allowed in std=c++17
                        found = true;
                    }
                }

                // check clicking in name text field
                if (!found && inBox (ui.tap, ctbl.tbl_name[tbl_row].box)) {
                    // friendly turn on matching TB
                    ctbl.tbl_tb[tbl_row][REN_CIDX].on = true;
                    updateCfgChoices (ctbl, tbl_row, REN_CIDX);
                    // note we are now editing this row
                    editing = tbl_row;
                    found = true;
                }
            }
        }

        // handle editing
        if (editing == EDITING_SAVE)
            editCfgName (ui.tap, ui.kb_char, ctbl.save_name);
        else if (editing != EDITING_NONE && TVIS(ctbl,editing)) {
            // handle edit and store change back into master table
            CfgName &cn = ctbl.tbl_name[editing];
            editCfgName (ui.tap, ui.kb_char, cn);
            quietStrncpy (ctbl.cfg[T2C(ctbl,editing)].edit_name, cn.name, MAX_NAMLEN);
        }
    }

    // engage all specified changes, noting if any require restarting
    // N.B. we assume only sensible combinations have already been established
    bool restart = false;
    if (user_ok) {
        if (ctbl.save_tb.on)
            saveCfgFile (ctbl.save_name.name);                  // copy eeprom to save_name
        for (int r = 0; r < n_cfgs; r++) {
            CfgInfo &ci = ctbl.cfg[r];
            if (ci.on[UPD_CIDX])                                // N.B. UPD_CIDX must be done before others
                saveCfgFile (ci.orig_name);                     // copy eeprom to cfg name
            if (ci.on[USE_CIDX]) {
                engageCfgFile (ci.orig_name);                   // copy cfg name to eeprom
                restart = true;                                 // restart to engage
            }
            if (ci.on[DEL_CIDX])
                deleteCfgFile (ci.orig_name);                   // delete cfg name
            if (ci.on[REN_CIDX] && strcmp (ci.orig_name, ci.edit_name))
                renameCfgFile (ci.orig_name, ci.edit_name);     // rename cfg name
        }
        if (restore_def)
            restart = true;                                     // restart to restore def
    }

    // clean up
    freeCfgTable (ctbl);

    // return whether current configuration has changed
    return (restart);
}

/* take over the entire screen to offer saving and restoring config files.
 */
void runConfigManagement(void)
{
    // all ours
    eraseScreen();

    // directory of config files to manage, create if not already
    char dir[1000];
    snprintf (dir, sizeof(dir), "%s/%s", our_dir.c_str(), cfg_dir);
    if (mkdir (dir, 0755) == 0) {
        if (chown (dir, getuid(), getgid()) < 0)                // try to set real owner, not fatal
            Serial.printf ("CFG: chown(%d,%d): %s\n", getuid(), getgid(), strerror(errno));
    } else if (errno != EEXIST) {
        // ok if already exists
        fatalError ("%s: %s", dir, strerror(errno));
    }

    // gather collection of existing saved files
    char **cfg_names;
    int n_cfgs = getCfgFiles (dir, &cfg_names);

    // run menu noting whether to engage a new config
    bool restore_def;
    bool restart = runConfigMenu (cfg_names, n_cfgs, restore_def);

    // clean up
    freeCfgNames (cfg_names, n_cfgs);

    // act
    if (restart)
        doReboot (true, restore_def);
    else
        initScreen();
}

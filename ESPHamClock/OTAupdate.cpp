/* handle remote firmware updating
 */

#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>


#include "HamClock.h"

// server path to script that returns the newest version available
static const char v_page[] = "/version.pl";

// query layout
#define ASK_TO          60                              // ask timeout, secs
#define Q_Y             40                              // question y
#define C_Y             80                              // controls y
#define LH              30                              // line height
#define FD              7                               // font descent
#define LINDENT         10                              // list indent
#define INFO_Y          150                             // first list y
#define YNBOX_W         120                             // Y/N box width
#define YNBOX_H         40                              // Y/N box height
#define YNBOX_GAP       200                             // Y/N boxs gap
#define NBOX_X          50                              // no box x
#define NBOX_Y          C_Y                             // no box y
#define YBOX_X          (800-NBOX_X-YNBOX_W)            // yes box x
#define YBOX_Y          C_Y                             // yes box y
#define SCR_W           30                              // scroll width
#define SCR_M           5                               // scroll LR margin
#define SCR_X           (800-SCR_M-SCR_W-5)             // scroll x
#define SCR_Y           INFO_Y                          // scroll y
#define SCR_H           (480-10-SCR_Y)                  // scroll height

// install layout
#define PROG_Y0         100                             // progress text y
#define PROG_DY         45                              // progress text line spacing
#define PBAR_INDENT     30                              // left and right progress bar indent
#define PBAR_Y0         200                             // progress bar top
#define PBAR_H          30                              // progress bar height
#define PBAR_W          (800-2*PBAR_INDENT)             // progress bar width

/* called by ESPhttpUpdate during download with bytes so far and total.
 */
static void onProgressCB (int sofar, int total)
{
    tft.drawRect (PBAR_INDENT, PBAR_Y0, PBAR_W, PBAR_H, RA8875_WHITE);
    tft.fillRect (PBAR_INDENT, PBAR_Y0, sofar*PBAR_W/total, PBAR_H, RA8875_WHITE);
    checkWebServer (true);
}



/* return whether a new version is available.
 * if so pass back the name in new_ver[new_verl]
 * default no if error.
 */
bool newVersionIsAvailable (char *new_ver, uint16_t new_verl)
{
    WiFiClient v_client;
    char line[100];
    bool found_newer = false;
    bool connected;
    Serial.printf ("%s/%s\n", software_host, v_page);
 if (version_https) {
     Serial.printf ("via https\n");
 }
 else {
     Serial.printf ("via port %d\n",backend_port);
 }

    if (version_https) {
        connected = connecthttpsHCGET (v_client, software_host, v_page);
    }
    else {
        connected = v_client.connect (backend_host, backend_port);
    }

    if (connected) {
        if (version_https == false) {
            // query page
            httpHCGET (v_client, backend_host, v_page);


            if (!httpSkipHeader (v_client)) {
                Serial.println ("Version query header is short");
                goto out;
            }
        }

        // next line is new version number
        if (!getTCPLine (v_client, line, sizeof(line), NULL)) {
            Serial.println ("Version query timed out");
            goto out;
        }

        // non-beta accepts only newer non-beta; beta accepts anything newer
        Serial.printf ("found version %s\n", line);
        float our_v = atof(hc_version);
        float new_v = atof(line);
        bool we_are_beta = strchr (hc_version, 'b') != NULL;
        bool new_is_beta = strchr (line, 'b') != NULL;
        if (we_are_beta) {
            if (new_is_beta) {
                int our_beta_v = atoi (strchr(hc_version,'b') + 1);
                int new_beta_v = atoi (strchr(line,'b') + 1);
                if (new_beta_v > our_beta_v) {
                    found_newer = true;
                    strncpy (new_ver, line, new_verl);
                }
            } else {
                if (new_v >= our_v) {
                    found_newer = true;
                    strncpy (new_ver, line, new_verl);
                }
            }
        } else {
            if (!new_is_beta && new_v > our_v) {
                found_newer = true;
                strncpy (new_ver, line, new_verl);
            }
        }

        // just log next few lines for debug
        // for (int i = 0; i < 2 && getTCPLine (v_client, line, sizeof(line), NULL); i++)
            // Serial.printf ("  %s\n", line);
    }

out:

    // finished with connection
    v_client.stop();

    return (found_newer);
}

/* check if a beta version exists by spoofing next version in the User-Agent.
 * return whether a beta exists, and if so copy its name to beta_ver.
 */
static bool getBetaVersion (char *beta_ver, uint16_t beta_verl)
{
    // check if we are on a stable version (no 'b')
    if (strchr (hc_version, 'b') != NULL)
        return false;

    // construct spoof version
    int major, minor;
    if (sscanf (hc_version, "%d.%d", &major, &minor) != 2)
        return false;

    static char spoof_ver[32];
    snprintf (spoof_ver, sizeof(spoof_ver), "%d.%db00", major, minor + 1);

    // save current version pointer
    const char *orig_version = hc_version;
    hc_version = spoof_ver;

    // query version.pl
    WiFiClient v_client;
    char line[100];
    bool found_beta = false;
    bool connected;

    if (version_https) {
        connected = connecthttpsHCGET (v_client, software_host, v_page);
    }
    else {
        connected = v_client.connect (backend_host, backend_port);
    }

    if (connected) {
        if (version_https == false) {
            httpHCGET (v_client, backend_host, v_page);
            if (!httpSkipHeader (v_client)) {
                goto out;
            }
        }

        if (getTCPLine (v_client, line, sizeof(line), NULL)) {
            // Check if the returned version contains 'b'
            if (strchr (line, 'b') != NULL) {
                // Also verify it starts with major.minor+1 (e.g. 4.27)
                char expected_prefix[32];
                snprintf (expected_prefix, sizeof(expected_prefix), "%d.%d", major, minor + 1);
                if (strncmp (line, expected_prefix, strlen(expected_prefix)) == 0) {
                    found_beta = true;
                    strncpy (beta_ver, line, beta_verl);
                }
            }
        }
    }

out:
    v_client.stop();
    hc_version = orig_version; // restore
    return found_beta;
}

/* fetch release notes for the version corresponding to the spoofed UA version (or normal version if spoof_ua_ver is NULL)
 */
static bool fetchReleaseNotes (char ***lines_ptr, int *n_lines_ptr, const char *spoof_ua_ver)
{
    // free existing lines
    char **lines = *lines_ptr;
    int n_lines = *n_lines_ptr;
    while (--n_lines >= 0)
        free (lines[n_lines]);
    free (lines);
    *lines_ptr = NULL;
    *n_lines_ptr = 0;

    const char *orig_version = hc_version;
    if (spoof_ua_ver)
        hc_version = spoof_ua_ver;

    WiFiClient v_client;
    char line[128];
    bool connected = false;
    bool ok = false;

    if (version_https) {
        connected = connecthttpsHCGET (v_client, software_host, v_page);
    }
    else {
        connected = v_client.connect (backend_host, backend_port);
    }

    if (connected) {
        if (version_https == false) {
            // query page
            httpHCGET (v_client, backend_host, v_page);
            if (!httpSkipHeader (v_client)) {
                Serial.println ("Version query header is short");
                goto out;
            }
        }

        // skip next line which is new version number
        if (!getTCPLine (v_client, line, sizeof(line), NULL)) {
            Serial.println ("Info timed out");
            goto out;
        }

        // remaining lines are changes, add to lines[]
        char **new_lines = NULL;
        int count = 0;
        while (getTCPLine (v_client, line, sizeof(line), NULL)) {
            maxStringW (line, SCR_X-SCR_M-LINDENT-1);       // insure fit
            new_lines = (char **) realloc (new_lines, (count+1) * sizeof(char*));
            if (!new_lines)
                fatalError ("no memory for %d version changes", count+1);
            new_lines[count++] = strdup (line);
        }
        *lines_ptr = new_lines;
        *n_lines_ptr = count;
        ok = true;
    }
  out:
    v_client.stop();
    hc_version = orig_version;
    return ok;
}


/* draw as many of the given lines starting with top_line as will fit
 */
static void drawChangeList (char **line, int top_line, int n_lines)
{
    uint16_t line_y = INFO_Y;

    // erase over to scroll bar
    tft.fillRect (0, line_y, SCR_X-SCR_M-1, tft.height() - line_y, RA8875_BLACK);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    for (int i = top_line; i < n_lines && (line_y += LH) < tft.height() - FD; i++) {
        tft.setCursor (LINDENT, line_y);
        tft.print (line[i]);
    }
}

/* show release notes for current or pending release.
 * if pending:
 *    ask and return whether to install the given version using the given default answer
 * else
 *    always return false.
 */
bool askOTAupdate(char *new_ver, bool show_pending, bool def_yes)
{
    // prep
    eraseScreen();
    hideClocks();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    char line[128];

    // title
    tft.setCursor (LINDENT, Q_Y);
    if (show_pending)
        snprintf (line, sizeof(line), "New version %s is available. Update now?  ... ", new_ver);
    else
        snprintf (line, sizeof(line), "You're up to date with the following changes ... ");
    tft.print (line);

    // get cursor location for count down
    uint16_t count_x = tft.getCursorX();
    uint16_t count_y = tft.getCursorY();
    int count_s = ASK_TO;
    tft.print(count_s);

    // draw button boxes, no is just Ok if not pending
    SBox no_b =  {NBOX_X, NBOX_Y, YNBOX_W, YNBOX_H};
    SBox yes_b = {YBOX_X, YBOX_Y, YNBOX_W, YNBOX_H};
    bool active_yes = def_yes;
    if (show_pending) {
        drawStringInBox ("No", no_b, !active_yes, RA8875_WHITE);
        drawStringInBox ("Yes", yes_b, active_yes, RA8875_WHITE);
    } else {
        drawStringInBox ("Ok", no_b, false, RA8875_WHITE);
    }

    // Check if beta version exists
    char beta_ver[32];
    bool beta_exists = false;
    SBox beta_b = {650, C_Y, 100, YNBOX_H};
    if (!show_pending && strchr(hc_version, 'b') == NULL) {
        beta_exists = getBetaVersion (beta_ver, sizeof(beta_ver));
        if (beta_exists) {
            drawStringInBox ("Beta", beta_b, false, RA8875_WHITE);
        }
    }

    // prep for potentially long wait
    closeGimbal();
    closeDXCluster();

    // read list of changes
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    char **lines = NULL;                        // malloced list of malloced strings -- N.B. free!
    int n_lines = 0;
    fetchReleaseNotes (&lines, &n_lines, NULL);

    // how many will fit
    const int max_lines = (tft.height() - FD - INFO_Y)/LH;

    // prep first display of changes
    drawChangeList (lines, 0, n_lines);

    // scrollbar
    SBox sb_b = {SCR_X, SCR_Y, SCR_W, SCR_H};
    ScrollBar sb;
    sb.init (max_lines, n_lines, sb_b);

    // prep for user input
    SBox screen_b = {0, 0, tft.width(), tft.height()};
    UserInput ui = {
        screen_b,
        UI_UFuncNone,
        UF_UNUSED,
        1000,
        UF_NOCLOCKS,
        {0, 0}, TT_NONE, '\0', false, false
    };

    // wait for response or time out
    drainTouch();
    Serial.println ("Waiting for update y/n ...");
    bool finished = false;
    bool is_beta_flow = false;
    while (!finished && count_s > 0) {

        // wait for any user action
        if (waitForUser(ui)) {

            // reset counter
            count_s = ASK_TO;

            if (sb.checkTouch (ui.kb_char, ui.tap)) {

                drawChangeList (lines, sb.getTop(), n_lines);

            } else {

                switch (ui.kb_char) {
                case CHAR_TAB:
                case CHAR_LEFT:
                case CHAR_RIGHT:
                    if (show_pending) {
                        active_yes = !active_yes;
                        drawStringInBox ("Yes", yes_b, active_yes, RA8875_WHITE);
                        drawStringInBox ("No", no_b, !active_yes, RA8875_WHITE);
                    }
                    break;
                case CHAR_ESC:
                    finished = true;
                    active_yes = false;
                    break;
                case CHAR_CR:
                case CHAR_NL:
                    finished = true;
                    break;

                case CHAR_NONE:
                    // click?
                    if (!show_pending && beta_exists && inBox (ui.tap, beta_b)) {
                        // Switch to beta upgrade flow!
                        show_pending = true;
                        active_yes = true;
                        is_beta_flow = true;
                        strncpy (new_ver, beta_ver, 20);

                        int major, minor;
                        char spoof_ver[32];
                        if (sscanf (hc_version, "%d.%d", &major, &minor) == 2) {
                            snprintf (spoof_ver, sizeof(spoof_ver), "%d.%db00", major, minor + 1);
                            fetchReleaseNotes (&lines, &n_lines, spoof_ver);
                        }

                        tft.fillRect (0, 0, tft.width(), INFO_Y, RA8875_BLACK);
                        tft.setTextColor (RA8875_WHITE);
                        selectFontStyle (BOLD_FONT, SMALL_FONT);
                        tft.setCursor (LINDENT, Q_Y);
                        snprintf (line, sizeof(line), "New version %s is available. Update now?  ... ", new_ver);
                        tft.print (line);

                        count_x = tft.getCursorX();
                        count_y = tft.getCursorY();
                        count_s = ASK_TO;
                        tft.print(count_s);

                        drawStringInBox ("No", no_b, !active_yes, RA8875_WHITE);
                        drawStringInBox ("Yes", yes_b, active_yes, RA8875_WHITE);

                        drawChangeList (lines, 0, n_lines);
                        sb.init (max_lines, n_lines, sb_b);
                        break;
                    }
                    if (show_pending && inBox (ui.tap, yes_b)) {
                        drawStringInBox ("Yes", yes_b, true, RA8875_WHITE);
                        drawStringInBox ("No", no_b, false, RA8875_WHITE);
                        wdDelay(200);
                        finished = true;
                        active_yes = true;
                    }
                    if (inBox (ui.tap, no_b)) {
                        if (show_pending) {
                            drawStringInBox ("No", no_b, true, RA8875_WHITE);
                            drawStringInBox ("Yes", yes_b, false, RA8875_WHITE);
                        } else {
                            drawStringInBox ("Ok", no_b, true, RA8875_WHITE);
                        }
                        wdDelay(200);
                        finished = true;
                        active_yes = false;
                    }
                    break;
                }
            }
        }

        // update countdown
        tft.setTextColor (RA8875_WHITE);
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        tft.fillRect (count_x, count_y-30, 60, 40, RA8875_BLACK);
        tft.setCursor (count_x, count_y);
        tft.print(--count_s);

    }

    if (count_s == 0 && is_beta_flow) {
        active_yes = false;
    }

    // clean up
    while (--n_lines >= 0)
        free (lines[n_lines]);
    free (lines);

    // return result
    Serial.printf ("... update answer %d\n", active_yes);
    return (active_yes);
}

/* reload HamClock with the given version.
 * we never return regardless of success or fail.
 */
void doOTAupdate(const char *newver)
{
    Serial.printf ("Begin download version %s\n", newver);

    // inform user
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (0, PROG_Y0);
    tft.printf ("  Performing remote update to V%s...", newver);
    tft.setCursor (0, PROG_Y0 + PROG_DY);
    tft.print ("  Do not interrupt power or network during this process.");

    // connect progress callback
    ESPhttpUpdate.onProgress (onProgressCB);

    // build url
    WiFiClient client;
    char url[400];
    if (strchr(newver, 'b'))
        snprintf (url, sizeof(url), "https://%s/ham/HamClock/ESPHamClock-V%s.zip", software_host, newver);
    else
        snprintf (url, sizeof(url), "https://%s/ham/HamClock/ESPHamClock.zip", software_host);

    // go
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, url);

    // show error message and exit
    switch (ret) {
    case HTTP_UPDATE_FAILED:
        fatalError ("Update failed: Error %d\n%s\n",
                        ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

    case HTTP_UPDATE_NO_UPDATES:
        fatalError ("No updates found after all??");
        break;

    case HTTP_UPDATE_OK:
        fatalError ("Update Ok??");
        break;

    default:
        fatalError ("Unknown failure code: %d", (int)ret);
        break;
    }
}

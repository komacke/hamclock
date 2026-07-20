/* manage the Meshtastic "Mesh Net" Pane.
 *
 * Fetches node info and neighbour ("who heard whom") data from Liam Cottle's public
 * Meshtastic Map API at https://meshtastic.liamcottle.net/api/v1 and displays it as a
 * scrollable list plus node dots + connecting lines on the map.
 *
 * IMPORTANT SCOPE NOTE: the public API has no geographic filter on its node list, and that
 * list runs into the tens of thousands of entries worldwide, so we deliberately never fetch
 * it in bulk -- that would mean a multi-megabyte JSON response against a fixed-size buffer,
 * and it would be rude to hammer someone's free public server that hard on a timer besides.
 * Instead this pane is driven entirely by a short user-configured watch list of specific
 * node IDs (setup.cpp field NV_MESHWATCHLIST, decimal node IDs, comma-separated): for each
 * watched node we fetch its own info plus its neighbour list, and only fetch a neighbour's
 * info (to resolve its position for the map) the first time we see it. Total requests per
 * refresh stay proportional to the watch list size, not the size of the global network.
 *
 * N.B. the exact field names used by the /neighbours endpoint below (node_id,
 * neighbour_node_id, snr) are inferred from the project's documentation rather than a
 * captured live response -- if they don't match, watch the Serial log (DEBUG_WEB) for the
 * raw JSON and adjust jsonGetUInt32()/jsonGetFloat() calls below accordingly.
 *
 * No JSON library is linked into HamClock (ESP32 build has none available), so this file
 * includes the same small dependency-free JSON scanner used by hamsat.cpp, tailored here to
 * this API's node/neighbour schema.
 */

#include "HamClock.h"


// title/accent color
#define MESH_COLOR      RGB565(180,140,255)
#define MESH_LINK_COLOR RGB565(120,90,200)          // map link line color -- not a ham band, so
                                                     // deliberately not reusing getBandColor()

// layout
#define MESH_DY         LISTING_DY                  // one link per row

// remote endpoint
static const char mesh_host[] = "meshtastic.liamcottle.net";
#define MESH_TO_MS      15000                        // curl timeout, ms
#define MESH_MAXAGE     300                          // refresh no more often than this, secs --
                                                       // conservative since this hits someone's
                                                       // free public server on a timer
#define MESH_MAX_WATCH  10                            // max node IDs honored from the watch list
#define MESH_MAX_NODES  80                            // cap on cached node info (watched + neighbours)
#define MESH_MAX_LINKS  120                           // cap on drawn neighbour links


/* one cached node's info, keyed by node_id
 */
typedef struct {
    uint32_t node_id;
    char short_name[8];
    char long_name[40];
    LatLong ll;
    bool has_pos;
    time_t updated_at;
    int battery_level;                               // 0-100%, 101 = powered/no battery, -1 = unknown
    float voltage;                                    // volts, 0 = unknown
} MeshNode;

static MeshNode mesh_nodes[MESH_MAX_NODES];
static int n_mesh_nodes;

static DXSpot *mesh_links;                           // malloced list of neighbour relationships
static int n_mesh_links;
static ScrollState mesh_ss;
static SBox mesh_url_b;                               // tappable region over the subtitle text

static uint32_t mesh_watch_ids[MESH_MAX_WATCH];
static int n_mesh_watch;


/* *********************************************************************************************
 * minimal JSON helpers -- same technique as hamsat.cpp, tailored to this API's schema.
 */

static const char *jsonSkipWS (const char *p)
{
    while (*p && isspace ((unsigned char)*p))
        p++;
    return (p);
}

static const char *jsonSkipBalanced (const char *p)
{
    p = jsonSkipWS (p);
    char open = *p;
    char close = (open == '{') ? '}' : (open == '[') ? ']' : 0;
    if (!close)
        return (NULL);

    int depth = 0;
    bool in_str = false;
    for (; *p; p++) {
        char c = *p;
        if (in_str) {
            if (c == '\\' && p[1])
                p++;
            else if (c == '"')
                in_str = false;
        } else {
            if (c == '"')
                in_str = true;
            else if (c == open)
                depth++;
            else if (c == close) {
                depth--;
                if (depth == 0)
                    return (p+1);
            }
        }
    }
    return (NULL);
}

static const char *jsonFindValue (const char *obj, const char *obj_end, const char *key)
{
    char pat[40];
    snprintf (pat, sizeof(pat), "\"%s\"", key);
    size_t patlen = strlen(pat);
    for (const char *s = obj; s < obj_end - patlen; s++) {
        if (memcmp (s, pat, patlen) == 0) {
            const char *p = jsonSkipWS (s + patlen);
            if (*p == ':')
                return (jsonSkipWS (p+1));
        }
    }
    return (NULL);
}

static void jsonGetString (const char *obj, const char *obj_end, const char *key,
                            char *buf, size_t buflen)
{
    buf[0] = '\0';
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v || *v != '"')
        return;
    v++;
    size_t bi = 0;
    while (*v && *v != '"' && bi < buflen-1) {
        if (*v == '\\' && v[1]) {
            v++;
            char c = *v;
            buf[bi++] = (c == 'n') ? '\n' : (c == 't') ? '\t' : c;
        } else
            buf[bi++] = *v;
        v++;
    }
    buf[bi] = '\0';
}

/* this API quotes many numeric fields as JSON strings (e.g. "voltage":"4.209..."), not just
 * BigInt ids -- tolerate an optional leading quote here too.
 */
static float jsonGetFloat (const char *obj, const char *obj_end, const char *key, float dflt)
{
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v || *v == 'n' /* null */)
        return (dflt);
    if (*v == '"')
        v++;
    return ((float) atof (v));
}

/* node IDs can exceed INT32_MAX (they're derived from a MAC address), so use strtoul not atoi.
 * also: this API serializes BigInt fields (node_id, neighbour_node_id) as JSON STRINGS, e.g.
 * "node_id":"744971138", not bare numbers -- skip a leading quote if present.
 */
static uint32_t jsonGetUInt32 (const char *obj, const char *obj_end, const char *key, uint32_t dflt)
{
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v || *v == 'n' /* null */)
        return (dflt);
    if (*v == '"')
        v++;
    return ((uint32_t) strtoul (v, NULL, 10));
}


/* *********************************************************************************************
 * networking
 */

/* issue an HTTPS GET for the given path on mesh_host, slurp the whole response into buf.
 * mirrors connectHamsatHTTPS()/retrieveHamsat()'s curl-pipe technique in hamsat.cpp.
 */
static bool fetchMeshJSON (const char *path, char *buf, size_t buflen)
{
    WiFiClient client;
    char cmd[300];
    snprintf (cmd, sizeof(cmd), "curl -A \"%s/%s\" --max-time 15 --silent https://%s%s",
                platform, hc_version, mesh_host, path);

    Serial.printf ("MESH: %s\n", cmd);
    if (!client.connectCommand (cmd)) {
        Serial.println ("MESH: connect failed");
        return (false);
    }

    size_t bi = 0;
    char c;
    while (bi < buflen-1 && getTCPChar (client, &c))
        buf[bi++] = c;
    buf[bi] = '\0';
    client.stop();

    if (bi < 2) {
        Serial.println ("MESH: response too short");
        return (false);
    }

    return (true);
}

/* parse a single node JSON object (as returned by both /nodes and /nodes/:id) into n
 */
static void parseNodeObject (const char *obj, const char *obj_end, MeshNode &n)
{
    n = MeshNode{};

    n.node_id = jsonGetUInt32 (obj, obj_end, "node_id", 0);
    jsonGetString (obj, obj_end, "short_name", n.short_name, sizeof(n.short_name));
    jsonGetString (obj, obj_end, "long_name", n.long_name, sizeof(n.long_name));

    // API returns these as integers scaled by 1e7 (matches Meshtastic's internal
    // latitude_i/longitude_i protobuf convention), e.g. 283410432 -> 28.3410432 degrees.
    // a genuinely absent position comes through as JSON null, which jsonGetFloat's dflt=0
    // already handles, so a raw 0 here means "no position" either way.
    float lat_i = jsonGetFloat (obj, obj_end, "latitude", 0);
    float lng_i = jsonGetFloat (obj, obj_end, "longitude", 0);
    if (lat_i != 0 || lng_i != 0) {
        n.ll = LatLong (lat_i/1e7F, lng_i/1e7F);
        n.has_pos = true;
    }

    char ts[32];
    jsonGetString (obj, obj_end, "updated_at", ts, sizeof(ts));
    n.updated_at = ts[0] ? crackISO8601 (ts) : 0;

    // battery_level: 0-100%, 101 means "powered externally, no battery reading". voltage is
    // one of this API's many numeric-fields-quoted-as-strings (see jsonGetFloat), same as snr.
    n.battery_level = (int) jsonGetFloat (obj, obj_end, "battery_level", -1);
    n.voltage = jsonGetFloat (obj, obj_end, "voltage", 0);
}

/* find a cached node by id, else fetch /nodes/:id and add it to the cache.
 * returns NULL if not found/fetchable or the cache is full.
 */
static MeshNode *findOrFetchNode (uint32_t node_id)
{
    for (int i = 0; i < n_mesh_nodes; i++)
        if (mesh_nodes[i].node_id == node_id)
            return (&mesh_nodes[i]);

    if (n_mesh_nodes >= MESH_MAX_NODES)
        return (NULL);

    char path[60];
    snprintf (path, sizeof(path), "/api/v1/nodes/%lu", (unsigned long)node_id);

    StackMalloc buf_mem(4000);
    char *buf = (char *) buf_mem.getMem();
    if (!fetchMeshJSON (path, buf, buf_mem.getSize()))
        return (NULL);

    const char *obj_end = jsonSkipBalanced (buf);
    if (!obj_end)
        return (NULL);

    MeshNode &n = mesh_nodes[n_mesh_nodes];
    parseNodeObject (buf, obj_end, n);
    if (n.node_id == 0)
        return (NULL);                              // didn't actually parse a node
    n_mesh_nodes++;
    return (&n);
}

/* parse the watch list NV string (decimal node IDs, comma-separated) into mesh_watch_ids[]
 */
static void loadMeshWatchList (void)
{
    n_mesh_watch = 0;

    char wlstr[NV_MESHWATCHLIST_LEN];
    if (!NVReadString (NV_MESHWATCHLIST, wlstr))
        wlstr[0] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r (wlstr, ",", &saveptr);
    while (tok && n_mesh_watch < MESH_MAX_WATCH) {
        while (isspace((unsigned char)*tok))
            tok++;
        uint32_t id = (uint32_t) strtoul (tok, NULL, 10);
        if (id != 0)
            mesh_watch_ids[n_mesh_watch++] = id;
        tok = strtok_r (NULL, ",", &saveptr);
    }
}

/* fetch neighbours for the given watched node and append any newly-resolvable links to
 * mesh_links[]/mesh_link_snr[]. dedups so a bidirectional pair isn't drawn twice.
 */
static void retrieveNeighboursFor (uint32_t node_id)
{
    char path[70];
    snprintf (path, sizeof(path), "/api/v1/nodes/%lu/neighbours", (unsigned long)node_id);

    StackMalloc buf_mem(8000);
    char *buf = (char *) buf_mem.getMem();
    if (!fetchMeshJSON (path, buf, buf_mem.getSize()))
        return;

    // response is a bare JSON array of neighbour relationship objects
    const char *p = jsonSkipWS (buf);
    if (*p != '[')
        return;
    p = jsonSkipWS (p+1);

    while (*p && *p != ']') {
        if (*p != '{') {
            p++;
            continue;
        }
        const char *obj_end = jsonSkipBalanced (p);
        if (!obj_end)
            break;

        uint32_t a_id = jsonGetUInt32 (p, obj_end, "node_id", 0);
        uint32_t b_id = jsonGetUInt32 (p, obj_end, "neighbour_node_id", 0);
        float snr = jsonGetFloat (p, obj_end, "snr", 0);

        if (a_id && b_id && n_mesh_links < MESH_MAX_LINKS) {

            // dedup regardless of direction. N.B. repurposing DXSpot's tx_dxcc/rx_dxcc (int) to
            // stash node ids here since DXSpot has no spare uint32_t field -- node ids can in
            // theory exceed INT32_MAX (they're derived from a MAC address), which would wrap
            // negative here. Not seen in practice on real hardware ids, but worth knowing if a
            // node's "Open page" link ever opens the wrong node.
            bool dup = false;
            for (int i = 0; i < n_mesh_links && !dup; i++) {
                uint32_t la = (uint32_t) mesh_links[i].tx_dxcc;    // stash node ids here, see below
                uint32_t lb = (uint32_t) mesh_links[i].rx_dxcc;
                if ((la == a_id && lb == b_id) || (la == b_id && lb == a_id))
                    dup = true;
            }

            if (!dup) {
                MeshNode *na = findOrFetchNode (a_id);
                MeshNode *nb = findOrFetchNode (b_id);
                if (na && nb && na->has_pos && nb->has_pos) {
                    DXSpot &link = mesh_links[n_mesh_links];
                    link = DXSpot{};
                    quietStrncpy (link.tx_call, na->short_name[0] ? na->short_name : "?",
                                  sizeof(link.tx_call));
                    quietStrncpy (link.rx_call, nb->short_name[0] ? nb->short_name : "?",
                                  sizeof(link.rx_call));
                    link.tx_ll = na->ll;
                    link.rx_ll = nb->ll;
                    link.tx_dxcc = (int) a_id;                     // repurposed: node id, not DXCC
                    link.rx_dxcc = (int) b_id;                     // repurposed: node id, not DXCC
                    link.snr = snr;
                    link.spotted = myNow();
                    n_mesh_links++;
                }
            }
        }

        p = jsonSkipWS (obj_end);
        if (*p == ',')
            p = jsonSkipWS (p+1);
    }
}

/* full refresh: reload watch list, fetch each watched node + its neighbours
 */
static bool retrieveMeshtastic (void)
{
    n_mesh_nodes = 0;
    n_mesh_links = 0;
    free (mesh_links);
    mesh_links = (DXSpot *) calloc (MESH_MAX_LINKS, sizeof(DXSpot));
    if (!mesh_links)
        fatalError ("No room for mesh links");

    loadMeshWatchList();

    if (n_mesh_watch == 0)
        return (true);                              // nothing configured -- not an error

    for (int i = 0; i < n_mesh_watch; i++) {
        findOrFetchNode (mesh_watch_ids[i]);         // make sure the watched node itself is cached
        retrieveNeighboursFor (mesh_watch_ids[i]);
    }

    return (true);
}


/* *********************************************************************************************
 * drawing
 */

/* count how many mesh_links entries involve the given node id, for display in the node list
 */
static int countLinksFor (uint32_t node_id)
{
    int n = 0;
    for (int i = 0; i < n_mesh_links; i++)
        if ((uint32_t)mesh_links[i].tx_dxcc == node_id || (uint32_t)mesh_links[i].rx_dxcc == node_id)
            n++;
    return (n);
}

static void drawMeshVisNodes (const SBox &box)
{
    uint16_t x = box.x + 1;
    uint16_t y0 = box.y + LISTING_Y0;
    tft.fillRect (box.x+1, y0-LISTING_OS, box.w-2, box.h - (LISTING_Y0-LISTING_OS+1), RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    if (n_mesh_watch == 0) {
        tft.setTextColor (RGB565(180,140,60));
        tft.setCursor (x+2, y0);
        tft.print ("No watch list -- see Setup");
        mesh_ss.drawScrollUpControl (box, MESH_COLOR, MESH_COLOR);
        mesh_ss.drawScrollDownControl (box, MESH_COLOR, MESH_COLOR);
        return;
    }

    if (n_mesh_nodes == 0) {
        tft.setTextColor (RGB565(180,140,60));
        tft.setCursor (x+2, y0);
        tft.print ("No response -- see Serial log");
        mesh_ss.drawScrollUpControl (box, MESH_COLOR, MESH_COLOR);
        mesh_ss.drawScrollDownControl (box, MESH_COLOR, MESH_COLOR);
        return;
    }

    time_t now = myNow();
    int min_i, max_i;
    if (mesh_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            const MeshNode &n = mesh_nodes[i];
            uint16_t y = y0 + mesh_ss.getDisplayRow(i) * MESH_DY;

            char age[24];
            long dt = now - n.updated_at;
            if (!n.updated_at)
                strcpy (age, "?");
            else if (dt < 60)
                snprintf (age, sizeof(age), "%lds", dt);
            else if (dt < 3600)
                snprintf (age, sizeof(age), "%ldm", dt/60);
            else if (dt < 86400)
                snprintf (age, sizeof(age), "%ldh", dt/3600);
            else
                snprintf (age, sizeof(age), "%ldd", dt/86400);

            char batt[6];
            if (n.battery_level < 0)
                strcpy (batt, "?");
            else if (n.battery_level >= 101)
                strcpy (batt, "PWR");
            else
                snprintf (batt, sizeof(batt), "%d%%", n.battery_level);

            uint16_t batt_color;
            if (n.battery_level < 0)
                batt_color = RGB565(150,150,150);              // unknown
            else if (n.battery_level >= 90)
                batt_color = RA8875_GREEN;
            else if (n.battery_level >= 70)
                batt_color = RA8875_YELLOW;
            else
                batt_color = RA8875_RED;                       // covers < 70%, PWR excluded above

            int nlinks = countLinksFor (n.node_id);

            // print as separate segments (not one long string) so the battery figure can have
            // its own color, and so each field stays a fixed pixel width -- this pane is only
            // ~160px wide (PLOTBOX123_W), too narrow for a single long combined string.
            #define MESH_CHW        6            // FAST_FONT fixed advance, pixels/char
            uint16_t cx = x + 1;
            tft.setTextColor (nlinks > 0 ? RA8875_WHITE : RGB565(150,150,150));
            tft.setCursor (cx, y);
            char namebuf[13];
            snprintf (namebuf, sizeof(namebuf), "%-5.5s%-6.6s ", n.short_name[0] ? n.short_name : "?",
                        n.long_name);
            tft.print (namebuf);
            cx += 12*MESH_CHW;

            tft.setTextColor (batt_color);
            tft.setCursor (cx, y);
            char battbuf[5];
            snprintf (battbuf, sizeof(battbuf), "%-4.4s", batt);
            tft.print (battbuf);
            cx += 4*MESH_CHW;

            tft.setTextColor (nlinks > 0 ? RA8875_WHITE : RGB565(150,150,150));
            tft.setCursor (cx, y);
            char restbuf[16];
            snprintf (restbuf, sizeof(restbuf), "%2dL %s", nlinks, age);
            tft.print (restbuf);
        }
    }

    mesh_ss.drawScrollUpControl (box, MESH_COLOR, MESH_COLOR);
    mesh_ss.drawScrollDownControl (box, MESH_COLOR, MESH_COLOR);
}

static void drawMeshtasticPane (const SBox &box)
{
    prepPlotBox (box);

    const char *title = "Mesh Mon";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (MESH_COLOR);
    uint16_t pw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-pw)/2, box.y + PANETITLE_H);
    tft.print (title);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    char sub[40];
    snprintf (sub, sizeof(sub), "%d watched, %d links", n_mesh_watch, n_mesh_links);
    uint16_t sw = maxStringW (sub, box.w-2);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (box.x + (box.w-sw)/2, box.y + SUBTITLE_Y0);
    tft.print (sub);

    mesh_url_b.x = box.x + (box.w-sw)/2;
    mesh_url_b.y = box.y + SUBTITLE_Y0 - 2;
    mesh_url_b.w = sw;
    mesh_url_b.h = 12;

    drawMeshVisNodes (box);
}


/* *********************************************************************************************
 * public entry points -- declared extern in HamClock.h, dispatched from wifi.cpp / plotmgmnt.cpp
 * exactly like updateHamsat()/checkHamsatTouch().
 */

static void resetMeshtasticStorage (const SBox &box)
{
    n_mesh_nodes = 0;
    n_mesh_links = 0;
    free (mesh_links);
    mesh_links = NULL;
    mesh_ss.init ((box.h - LISTING_Y0)/MESH_DY, 0, 0, mesh_ss.DIR_TOPDOWN);
}

bool updateMeshtastic (const SBox &box, bool fresh)
{
    if (fresh) {
        ROTHOLD_CLR(PLOT_CH_MESHTASTIC);
        resetMeshtasticStorage (box);
    }

    bool ok = retrieveMeshtastic ();
    if (ok) {
        mesh_ss.init ((box.h - LISTING_Y0)/MESH_DY, mesh_ss.top_vis, n_mesh_nodes, mesh_ss.DIR_TOPDOWN);
        if (fresh)
            mesh_ss.scrollToNewest();
        drawMeshtasticPane (box);
    } else {
        plotMessage (box, RA8875_RED, "meshtastic.liamcottle.net download error");
    }

    return (ok);
}

bool checkMeshtasticTouch (const SCoord &s, const SBox &box)
{
    if (s.y < box.y + PANETITLE_H) {

        if (mesh_ss.checkScrollUpTouch (s, box)) {
            if (mesh_ss.okToScrollUp()) {
                mesh_ss.scrollUp ();
                drawMeshVisNodes (box);
            }
            return (true);
        }

        if (mesh_ss.checkScrollDownTouch (s, box)) {
            if (mesh_ss.okToScrollDown()) {
                mesh_ss.scrollDown ();
                drawMeshVisNodes (box);
            }
            return (true);
        }

        if (ROTHOLD_TST(PLOT_CH_MESHTASTIC))
            return (true);

        return (false);
    }

    if (inBox (s, mesh_url_b)) {

        typedef enum {
            UEX_PAGE,
            UEX_N
        } UrlExInfo;

        MenuItem mitems[UEX_N];
        mitems[UEX_PAGE] = {MENU_TOGGLE, false, 1, 2, "Open webpage?", 0};

        SBox menu_b = {s.x, s.y, 0, 0};
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, UEX_N, mitems};
        if (runMenu (menu) && mitems[UEX_PAGE].set)
            openURL ("https://meshtastic.liamcottle.net");

        return (true);
    }

    int row = (s.y - (box.y + LISTING_Y0)) / MESH_DY;
    int array_index;
    if (mesh_ss.findDataIndex (row, array_index) && array_index < n_mesh_nodes) {
        const MeshNode &n = mesh_nodes[array_index];

        typedef enum {
            NEX_NAME,
            NEX_BATT,
            NEX_PAGE,
            NEX_N
        } NodeExInfo;

        char title[20];
        snprintf (title, sizeof(title), "%s, %d links",
                    n.short_name[0] ? n.short_name : "?", countLinksFor(n.node_id));

        char battinfo[20];
        if (n.battery_level < 0)
            snprintf (battinfo, sizeof(battinfo), "Batt: unknown");
        else if (n.battery_level >= 101)
            snprintf (battinfo, sizeof(battinfo), "PWR, %.2fV", n.voltage);
        else
            snprintf (battinfo, sizeof(battinfo), "%d%%, %.2fV", n.battery_level, n.voltage);

        MenuItem mitems[NEX_N];
        mitems[NEX_NAME] = {MENU_LABEL,  false, 0, 2, title, 0};
        mitems[NEX_BATT] = {MENU_LABEL,  false, 0, 2, battinfo, 0};
        mitems[NEX_PAGE] = {MENU_TOGGLE, false, 1, 2, "Open node page", 0};

        const uint16_t menu_x = box.x + 20;
        const uint16_t menu_h = 90;
        const uint16_t menu_max_y = box.y + box.h - menu_h - 5;
        const uint16_t menu_y = s.y < menu_max_y ? s.y : menu_max_y;
        SBox menu_b = {menu_x, menu_y, 0, 0};
        SBox ok_b;

        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, NEX_N, mitems};
        if (runMenu (menu) && mitems[NEX_PAGE].set) {
            char url[70];
            snprintf (url, sizeof(url), "https://meshtastic.liamcottle.net/?node_id=%lu",
                        (unsigned long)n.node_id);
            openURL (url);
        }

        return (true);
    }

    return (false);
}

/* draw each cached node as a labeled dot, and a connecting line for each neighbour link.
 * called once per main map refresh cycle, same convention as drawActiveNetsOnMap().
 */
void drawMeshtasticOnMap (void)
{
    if (findPaneChoiceNow (PLOT_CH_MESHTASTIC) == PANE_NONE)
        return;

    // links: draw the connecting line first so node dots/labels layer on top
    for (int i = 0; i < n_mesh_links; i++) {
        DXSpot &link = mesh_links[i];

        // straight-line-ish great circle path with a fixed color -- deliberately not
        // drawSpotPathOnMap(), which looks up color/width/dash by ham band; these are LoRa
        // mesh links, not ham band spots.
        float slat = sinf (link.rx_ll.lat);
        float clat = cosf (link.rx_ll.lat);
        float dist, bear;
        propPath (false, link.rx_ll, slat, clat, link.tx_ll, &dist, &bear);
        const int raw_pw = 1;
        const int n_step = ((int)ceilf(dist/deg2rad(PATH_SEGLEN))) | 1;
        const float step = dist/n_step;
        SCoord prev_s = {0, 0};
        for (int j = 0; j <= n_step; j++) {
            float r = j*step;
            float ca, B;
            SCoord sc;
            solveSphere (bear, r, slat, clat, &ca, &B);
            ll2sRaw (asinf(ca), fmodf(link.rx_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, sc, raw_pw);
            if (prev_s.x > 0) {
                if (segmentSpanOkRaw (prev_s, sc, raw_pw))
                    tft.drawLineRaw (prev_s.x, prev_s.y, sc.x, sc.y, raw_pw, MESH_LINK_COLOR);
                else
                    sc.x = 0;
            }
            prev_s = sc;
        }
    }

    // nodes: a small fixed dot plus short-name label at each unique cached position.
    // deliberately not drawSpotLabelOnMap(): it sizes/colors via getRawBandSpotRadius()/
    // getBandColor(), both keyed by spot.kHz -- since a mesh node has no ham-band frequency,
    // kHz stays 0, which those functions treat as "no matching band" and fall back to
    // degenerate values. Bounds check is a plain single-point overMap(), matching the
    // established pattern used by drawIB_MapMarker() and drawSpotLabelOnMap() -- an earlier
    // extent-box variant here was an unnecessary over-correction for a bug that turned out to
    // be stale EEPROM state, not a real geometry problem.
    #define MESH_DOT_R      3                       // fixed dot radius, raw pixels
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    for (int i = 0; i < n_mesh_nodes; i++) {
        const MeshNode &n = mesh_nodes[i];
        if (!n.has_pos)
            continue;

        SCoord s;
        ll2s (n.ll, s, MESH_DOT_R);
        if (!overMap(s))
            continue;

        SCoord s_raw;
        ll2sRaw (n.ll, s_raw, MESH_DOT_R);
        drawSpotDot (s_raw.x, s_raw.y, MESH_DOT_R, LOME_TXEND, MESH_COLOR);

        const char *label = n.short_name[0] ? n.short_name : "?";
        uint16_t lbl_w = getTextWidth (label);
        uint16_t lbl_x = s.x + MESH_DOT_R + 2;
        uint16_t lbl_y = s.y - 4;
        tft.fillRect (lbl_x-1, lbl_y-1, lbl_w+2, 9, RA8875_BLACK);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (lbl_x, lbl_y);
        tft.print (label);
    }
}

/* fill a MeshInfo from a cached node, for hover info popups
 */
static void meshFillInfo (const MeshNode &n, MeshInfo *info)
{
    quietStrncpy (info->name, n.long_name, sizeof(info->name));
    quietStrncpy (info->short_name, n.short_name, sizeof(info->short_name));
    info->battery_level = n.battery_level;
    info->voltage = n.voltage;
    info->n_links = countLinksFor (n.node_id);
    info->ll = n.ll;
}

/* if from_ll is within MAX_CSR_DIST of a cached node, return that node's mark location and
 * popup info. Used for hovering the dot on the map. Same pattern as getClosestActiveNet().
 */
bool getClosestMeshtasticNode (LatLong &from_ll, LatLong *mark_ll, MeshInfo *info)
{
    if (findPaneChoiceNow (PLOT_CH_MESHTASTIC) == PANE_NONE)
        return (false);

    int best = -1;
    float best_d = 1e10F;
    for (int i = 0; i < n_mesh_nodes; i++) {
        if (!mesh_nodes[i].has_pos)
            continue;
        float d = mesh_nodes[i].ll.GSD (from_ll);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }

    if (best < 0 || best_d*ERAD_M >= MAX_CSR_DIST)
        return (false);

    *mark_ll = mesh_nodes[best].ll;
    meshFillInfo (mesh_nodes[best], info);
    return (true);
}

/* if ms is hovering over a node row in the Mesh Mon pane, return that node's mark location and
 * popup info. Used for hovering a row in the list. Same pattern as getActiveNetsPaneInfo().
 */
bool getMeshtasticPaneInfo (const SCoord &ms, LatLong *mark_ll, MeshInfo *info)
{
    if (n_mesh_nodes == 0)
        return (false);

    PlotPane pp = findPaneChoiceNow (PLOT_CH_MESHTASTIC);
    if (pp == PANE_NONE)
        return (false);
    const SBox &box = plot_b[pp];
    if (!inBox (ms, box) || ms.y < box.y + LISTING_Y0)
        return (false);

    int vis_row = (ms.y - (box.y + LISTING_Y0)) / MESH_DY;
    int node_i;
    if (!mesh_ss.findDataIndex (vis_row, node_i) || node_i < 0 || node_i >= n_mesh_nodes)
        return (false);
    if (!mesh_nodes[node_i].has_pos)
        return (false);                          // nothing to ring/pop without a location

    *mark_ll = mesh_nodes[node_i].ll;
    meshFillInfo (mesh_nodes[node_i], info);
    return (true);
}

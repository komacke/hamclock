/* this is the html that runs in a browser showing a live hamclock connection.
 * the basic idea is to start with a complete image then continuously poll for incremental changes.
 * this page is loaded first with all subsequent communication via a websocket.
 * ESP is far too slow reading pixels to make this practical.
 */

#include "HamClock.h"

#if defined (_IS_UNIX)

char live_html[] =  R"_raw_html_(
<!DOCTYPE html>
<html>

<head>

    <!-- this might help iOS safari run full screen -->
    <meta name="apple-mobile-web-app-capable" content="yes" />
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent" />

    <title>
        HamClock Live!
    </title>
    
    <style>

        #hamclock-cvs {
            touch-action: pinch-zoom; /* allow both 1-finger moves and multi-touch p-z */
        }

    </style>

    <script>

        // config
        const UPDATE_MS = 100;          // update interval
        const MOUSE_JITTER = 5;         // allow this much mouse motion for a touch
        const LONGPRESS_MS = 500;       // press and hold duration in ms to trigger tooltip
        const APP_W = 800;              // app coord system width
        const nonan_chars =             // supported non-alnum chars
          ['Tab', 'Enter', 'Space', 'Escape', 'Backspace', 'ArrowLeft', 'ArrowDown', 'ArrowUp', 'ArrowRight'];
        const RELOAD_KEY = "reload";    // sessionStorage key to manage reloads

        // state
        var ws;                         // Websocket
        var ws_abdata = 0;              // ws onmessage header capture
        var drawing_verbose = 0;        // > 0 for more info about drawing
        var ws_verbose = 0;             // > 0 for more info about websocket commands
        var event_verbose = 0;          // > 0 for more info about keyboard or pointer activity
        var prev_regnhdr;               // for erasing if drawing_verbose > 1
        var app_scale = 0;              // size factor -- set for real when get first whole image
        var pointerdown_x = 0;          // location of pointerdown event
        var pointerdown_y = 0;          // location of pointerdown event
        var pointermove_ms = 0;         // Date.now when pointermove event
        var longpress_timer = null;     // timer for long-press detection
        var longpress_fired = false;    // whether long-press triggered tooltip
        var want_fs, tried_fs;          // whether user wants full screen and has succeeded once
        var wsclose_reload = 1;         // whether to reload if lose ws connection
        var cvs, ctx;                   // handy

        // define functions, onLoad follows near the bottom

        // request a new full image
        function getFullImage() {
            sendWSMsg ("get_live.png?");
        }

        // request an image update
        function getUpdate() {
            sendWSMsg ("get_live.bin?");
        }
        

        // given inherent hamclock build size, set canvas size and configure to stay centered
        function initCanvas (hc_w, hc_h) {
            if (drawing_verbose) {
                console.log("document.documentElement.clientWidth = " + document.documentElement.clientWidth);
                console.log("window.innerWidth = " + window.innerWidth);
                console.log ("hamclock is " +  hc_w + " x " +  hc_h);
            }

            // pixels to draw on always match the real clock size
            cvs.width =  hc_w;
            cvs.height =  hc_h;

            // get window area dimensions
            let win_w = document.documentElement.clientWidth || window.innerWidth;
            let win_h = document.documentElement.clientHeight || window.innerHeight;

            // center if HC is smaller else shrink to fit
            if (hc_w < win_w && hc_h < win_h) {

                // hc is smaller -- center in full screen
                cvs.style.width = hc_w + "px";
                cvs.style.height = hc_h + "px";

            } else {

                // hc is larger -- shrink to fit preserving aspect
                if (win_w*hc_h > win_h*hc_w) {
                    hc_w = hc_w*win_h/hc_h;
                    hc_h = win_h;
                } else {
                    hc_h = hc_h*win_w/hc_w;
                    hc_w = win_w;
                }
                cvs.style.width = hc_w + "px";
                cvs.style.height = hc_h + "px";
            }

            // center 
            cvs.style.position = 'absolute';
            cvs.style.top = "50%";
            cvs.style.left = "50%";
            cvs.style.margin = (-hc_h/2) + "px" + " 0 0 " + (-hc_w/2) + "px"; // trbl
            app_scale = hc_w/APP_W;

            if (drawing_verbose)
                console.log ("canvas is " + hc_w + " x " + hc_h + " app_scale " + app_scale);
        }

        // display the given full png uint8
        function drawFullImage (png8) {

            var pngbl = new Blob ([png8], {type:"image/png"});
            createImageBitmap (pngbl)
            .then(function(ibm) {
                if (drawing_verbose)
                    console.log ("drawFullImage size " + ibm.width + " x " + ibm.height);
                initCanvas(ibm.width, ibm.height);
                ctx.drawImage(ibm, 0, 0);
            })
            .catch(function(err){
                console.log("full image promise err: " + err);
            });
        }

        // update given header and png sprites image -- see liveweb.cpp::updateExistingClient()
        function drawUpdate (hdr8, png8) {

            // extract 4-byte header preamble
            const blok_w = hdr8[0];                         // block width, pixels
            const blok_h = hdr8[1];                         // block width, pixels
            const n_regn = (hdr8[2] << 8) | hdr8[3];        // total n blocks wide, MSB LSB

            if (drawing_verbose > 1) {
                // erase, or at least unmark, previous marked regions
                if (prev_regnhdr) {
                    ctx.strokeStyle = "black";
                    ctx.beginPath();
                    for (let i = 0; i < prev_regnhdr.length; i++) {
                        const cvs_x = prev_regnhdr[4+3*i] * blok_w;
                        const cvs_y = prev_regnhdr[5+3*i] * blok_h;
                        const cvs_w = prev_regnhdr[6+3*i] * blok_w;
                        ctx.rect (cvs_x, cvs_y, cvs_w, blok_h);
                    }
                    ctx.stroke();
                }
                // save for next time
                prev_regnhdr = hdr8.slice(0,4+3*n_regn);
            }

            // walk down remainder of header and draw each region
            if (n_regn > 0) {
                // png8 is one image blok_h hi of n_regns contiguous regions each variable width
                let pngbl = new Blob ([png8], {type:"image/png"});
                createImageBitmap (pngbl)
                .then(function(ibm) {
                    // render each region.
                    let regn_x = 0;                         // walk region x along img
                    let n_draw = 0;                         // count n drawn regions just for stat
                    for (let i = 0; i < n_regn; i++) {
                        const cvs_x = hdr8[4+3*i] * blok_w; // ul corner x in canvas pixels
                        const cvs_y = hdr8[5+3*i] * blok_h; // ul corner y in canvas pixels
                        const n_long = hdr8[6+3*i];         // n regions long
                        const cvs_w = n_long * blok_w;      // total region width in canvas pixels
                        ctx.drawImage (ibm, regn_x, 0, cvs_w, blok_h, cvs_x, cvs_y, cvs_w, blok_h);

                        if (drawing_verbose > 1) {
                            // mark updated regions
                            if (drawing_verbose > 2)
                                console.log (regn_x + " : " + cvs_x + "," + cvs_y + " "
                                                    + cvs_w + "x" + blok_h);
                            ctx.strokeStyle = "red";
                            ctx.beginPath();
                            ctx.rect (cvs_x, cvs_y, cvs_w, blok_h);
                            ctx.stroke();
                        }

                        regn_x += cvs_w;                    // next region
                        n_draw += n_long;
                    }
                    if (drawing_verbose)
                        console.log ("  drawUpdate " + hdr8.byteLength + "B " +
                                    n_regn + "/" + n_draw + " of " + blok_w + " x " + blok_h);

                })
                .catch(function(err) {
                    console.log("update promise err: ", err);
                    runSoon (getFullImage);
                });
            }

        }

        // schedule func() soon
        var upd_tid = 0;                            // update pacing timer id
        function runSoon (func) {

            // insure no nested requests
            if (upd_tid) {
                if (ws_verbose)
                    console.log ("cancel pending timer");
                clearTimeout(upd_tid);
            }

            // register callback
            upd_tid = setTimeout (function(){upd_tid = 0; func();}, UPDATE_MS);
            if (ws_verbose)
                console.log ("setting timer for " + func.name + " in " + UPDATE_MS + " ms");
        }

        // given any pointer event return coords with respect to canvas scaled to application.
        // returns undefined if scaling factor is not yet known.
        function getAppCoords (event) {

            if (app_scale) {
                const rect = cvs.getBoundingClientRect();
                const x = Math.round((event.clientX - rect.left)/app_scale);
                const y = Math.round((event.clientY - rect.top)/app_scale);
                return ({x, y});
            }
        }

        // send the given key and optionl control and shift modifier codes to the hamclock
        function sendKey (k, c, s) {

            // a real space would send 'char= ' which doesn't parse so we invent Space name
            if (k === ' ')
                k = 'Space';

            // accept only certain non-alphanumeric keys
            if ((k.length == 1 && (k.charCodeAt(0) < 33 || k.charCodeAt(0) > 126))
                            || (k.length > 1 && !nonan_chars.find (e => { if (e == k) return true; }))) {
                if (event_verbose)
                    console.log('ignoring ' + k);
                return;
            }
            
            // package up and send
            var msg = 'set_char?char=' + k + '&mod=';
            if (c)
                msg += 'C';
            if (s)
                msg += 'S';
            if (event_verbose)
                console.log('sending ' + msg);
            sendWSMsg (msg);
        }


        // connect keydown to send character to hamclock, beware ctrl keys and browser interactions
        window.addEventListener('keydown', function(event) {

            if (event_verbose)
                console.log('keydown: ', event);

            // now that user has done something check if they want to go full screen
            checkFullScreen();

            // handy
            const key = event.key;

            // ignore meta
            if (event.metaKey) {
                if (event_verbose)
                    console.log('ignoring ' + key);
                return;
            }

            // don't let browser see tab
            if (key === "Tab") {
                if (event_verbose)
                    console.log ("stopping tab");
                event.preventDefault();
            }

            sendKey (key, event.ctrlKey, event.shiftKey);
        });

        // respond to mobile device being rotated. resize seems to work better than orientationchange
        // window.addEventListener("orientationchange", function(event) {
        window.addEventListener("resize", function(event) {
            if (event_verbose)
                console.log ("resize event");
            // get full image to establish new screen size
            runSoon (getFullImage);
        });

        // show _one_ simple stand-alone message
        var msg_drawn;
        function drawMsgOnce (msg) {
            if (!msg_drawn) {
                console.log (msg);
                ctx.fillStyle = "black";
                ctx.fillRect (0, 0, 10000, 10000);
                ctx.fillStyle = "orange";
                ctx.font = "25px sans-serif";
                ctx.fillText (msg, 50, 50);
                msg_drawn = 1;
            }
        }


        // reload this page a few times, presumably hamclock was restarted but don't try forever
        function reloadThisPage() {

            // use sessionStorage to detect reloading
            var s = sessionStorage.getItem (RELOAD_KEY);

            if (s) {

                // already loaded once, just report without restart but remove key to allow manual reloading
                sessionStorage.removeItem (RELOAD_KEY);
                drawMsgOnce ("No connection");

            } else {

                // record key so we can detect subsequent reload
                sessionStorage.setItem (RELOAD_KEY, "one");

                console.log ('* reloading');
                setTimeout (function() {
                    try {
                        window.location.reload(true);
                    } catch(err) {
                        console.log('* reload err: ' + err);
                    }
                }, 3000);               // setup waits for 10 seconds

            }
        }



        // send the given message over the websocket
        function sendWSMsg (msg) {
            if (ws.readyState != 1) {
                setTimeout (sendWSMsg, 500, msg);
            } else {
                if (ws_verbose)
                    console.log ('sendWSMsg: ' + msg);
                ws.send (msg);
            }
        }

        // try once to engage full screen if desired.
        // N.B. must be called from a user action
        function checkFullScreen() {
            if (want_fs && !tried_fs) {
                console.log ("engaging FS");
                document.documentElement.requestFullscreen()
                tried_fs = true;
            }
        }

        // called one time after page has loaded
        function onLoad() {

            // create websocket back to same location replacing last component of our path with "live-ws"
            let ws_proto = (location.protocol === "https:") ? "wss://" : "ws://";       // tnx WK2X
            let ws_host = location.host + location.pathname.replace(/\/[^\/]*$/, "/live-ws");
            ws = new WebSocket ( ws_proto + ws_host);
            ws.binaryType = "arraybuffer";
            ws.onopen = function () {
                console.log('WS connection established.');
            };
            ws.onclose = function () {
                console.log('WS connection closed.');
                // reload on server die but not intentional actions
                if (wsclose_reload)
                    reloadThisPage();
            };
            ws.onerror = function (e) {
                console.log('WS connection failed: ', e);
                reloadThisPage();
            };


            // respond to hamclock messages
            ws.onmessage = function (e) {
                if (ws_verbose > 1)
                    console.log ('ws onmessage length ' + e.data.byteLength);

                if (e.data instanceof ArrayBuffer) {
                    // received whole or update image

                    var data8 = new Uint8Array (e.data);
                    if (data8[0] == 137 && data8[1] == 80 && data8[2] == 78 && data8[3] == 71) {
                        // this is a PNG image -- show whole if alone else assume its part of an update
                        if (ws_abdata) {
                            drawUpdate (new Uint8Array(ws_abdata), data8);
                            ws_abdata = 0;
                        } else {
                            drawFullImage (data8);
                            // allow restart
                            sessionStorage.removeItem (RELOAD_KEY);
                        }
                        // ask for updates regardless
                        runSoon (getUpdate);
                    } else {
                        // this is an update patch collection
                        ws_abdata = e.data;
                    }

                } else if (typeof e.data === 'string') {
                    // received text message

                    if (ws_verbose)
                        console.log ('rxWSMsg: ', e.data);

                    if (e.data === 'full-screen') {
                        // record user wants full screen, won't actually happen until they click.
                        // message arrives continuously because client can't tell if user reloaded page.
                        if (!want_fs) {
                            console.log ("user wants FS");
                            tried_fs = false;
                            want_fs = true;
                        }
                    }

                    else if (e.data.substring(0,5) == 'open ') {
                        // try to open a url in a tab
                        var url = e.data.substring(5);
                        console.log('opening ' + url);
                        if (!window.open(url, "HamClockTab")) {         // naming the tab allows reuse
                            console.log ("Failed to open ", url);
                            alert ("Failed to open " + url + ". \nYou may have popups blocked");
                        }
                    }

                    else if (e.data === 'Too many connections') {       // N.B. string must match liveweb.cpp
                        // close and don't reload
                        drawMsgOnce (e.data);
                        wsclose_reload = 0;
                    }

                    else if (e.data === 'Session timed out') {          // N.B. string must match liveweb.cpp
                        // close and don't reload
                        drawMsgOnce (e.data);
                        wsclose_reload = 0;
                    }


                    else
                        drawMsgOnce (e.data);

                } else {
                    console.log ("Unknown WS data: ", e.data);
                }
            }

            
            // handy access to canvas and drawing context
            cvs = document.getElementById('hamclock-cvs');
            ctx = cvs.getContext('2d', { alpha: false });       // faster w/o alpha
            ctx.translate(0.5, 0.5);                            // a tiny bit less blurry?

            function cancelLongPress() {
                if (longpress_timer !== null) {
                    clearTimeout (longpress_timer);
                    longpress_timer = null;
                }
            }

            // pointerdown: record time and position, start long-press timer
            cvs.addEventListener ('pointerdown', function(event) {
                // check if user wants to go full screen
                checkFullScreen();

                // all ours
                event.preventDefault();

                const m = getAppCoords (event);
                if (!m) {
                    console.log("pointerdown: don't know app_scale yet");
                    return;
                }

                cancelLongPress();
                longpress_fired = false;

                pointermove_ms = Date.now();
                pointerdown_x = m.x;
                pointerdown_y = m.y;
                if (event_verbose)
                    console.log ('pointer down');

                // start long-press timer for primary button/touch without modifiers
                var mods = event.ctrlKey || event.metaKey;
                if (event.button === 0 && !mods) {
                    longpress_timer = setTimeout (function() {
                        longpress_timer = null;
                        longpress_fired = true;
                        if (event_verbose)
                            console.log ('long press at ' + m.x + ',' + m.y);
                        // trigger tooltip / secondary tap (button 1)
                        sendWSMsg ('set_touch?x=' + m.x + '&y=' + m.y + '&button=1');
                    }, LONGPRESS_MS);
                }
            });

            // pointerleave: cancel long-press and send illegal mouse location 
            cvs.addEventListener ('pointerleave', function(event) {
                // all ours
                event.preventDefault();
                cancelLongPress();

                // send location well outside app
                sendWSMsg ('set_mouse?x=-1&y=-1');

            });

            // pointercancel: cancel long-press
            cvs.addEventListener ('pointercancel', function(event) {
                event.preventDefault();
                cancelLongPress();
            });

            // pointerup: send set_touch unless long-press already handled
            cvs.addEventListener ('pointerup', function(event) {
                // all ours
                event.preventDefault();
                cancelLongPress();

                if (longpress_fired) {
                    longpress_fired = false;
                    return;
                }

                // extract application coords
                const m = getAppCoords (event);
                if (!m) {
                    console.log("pointerup: don't know app_scale yet");
                    return;
                }

                // ignore if pointer moved so moves don't end with a tap
                if (Math.abs(m.x-pointerdown_x) > MOUSE_JITTER || Math.abs(m.y-pointerdown_y) > MOUSE_JITTER){
                    if (event_verbose)
                        console.log ('cancel pointerup because pointer moved');
                    return;
                }

                // code button0+mods, button1 (middle), or button2 (right) as button 1, else button 0.
                // N.B. event.button 0 means button 1 !
                var mods = event.ctrlKey || event.metaKey;
                var button = ((event.button == 0 && mods) || event.button == 1 || event.button == 2) ? 1 : 0;
                console.log ("button " + event.button + " + " + mods + " -> " + button);

                // if tapping on virtual keyboard Paste button in non-Android browser, read browser clipboard
                if (m.x >= 16 && m.x <= 155 && m.y >= 404 && m.y <= 470 && !window.AndroidApp) {
                    if (navigator.clipboard && navigator.clipboard.readText) {
                        navigator.clipboard.readText().then(text => {
                            if (text)
                                pasteString(text);
                        }).catch(err => {
                            console.log("clipboard read denied: ", err);
                            let text = prompt("Paste text here (Ctrl+V):", "");
                            if (text)
                                pasteString(text);
                        });
                    } else {
                        let text = prompt("Paste text here (Ctrl+V):", "");
                        if (text)
                            pasteString(text);
                    }
                }

                // compose and send
                let msg = 'set_touch?x=' + m.x + '&y=' + m.y + '&button=' + button;
                sendWSMsg (msg);
            });

            // helper to paste string into HamClock
            function pasteString(str) {
                if (!str) return;
                for (let i = 0; i < str.length; i++) {
                    sendKey(str[i]);
                }
            }
            window.pasteString = pasteString;

            // suppress the browser's native right-click context menu on the canvas so a right-click
            // reaches pointerup above (as an 'other button' tap) instead of popping the OS/browser menu
            cvs.addEventListener ('contextmenu', function(event) {
                event.preventDefault();
            });


            // pointermove: send set_mouse
            cvs.addEventListener ('pointermove', function(event) {
                // all ours
                event.preventDefault();

                // cancel long press if pointer moves beyond jitter
                const m = getAppCoords (event);
                if (longpress_timer !== null && m) {
                    if (Math.abs(m.x-pointerdown_x) > MOUSE_JITTER || Math.abs(m.y-pointerdown_y) > MOUSE_JITTER)
                        cancelLongPress();
                }

                // not crazy fast
                let now = Date.now();
                if (pointermove_ms + UPDATE_MS > now)
                    return;
                pointermove_ms = now;

                if (!m) {
                    console.log("pointermove: don't know app_scale yet");
                    return;
                }

                // compose and send
                let msg = 'set_mouse?x=' + m.x + '&y=' + m.y;
                sendWSMsg (msg);
            });

            document.addEventListener('paste', e => {
                let str = e.clipboardData.getData('text/plain');
                pasteString(str);
            });


            // all set. start things off with the full image, repeats from then on with updates forever.
            getFullImage();

        }

    </script>

</head>

<body onload='onLoad()' bgcolor='black' >

    <!-- page is a single canvas, size will be set based on hamclock build size -->
    <canvas id='hamclock-cvs'></canvas>

</body>
</html> 

)_raw_html_";

#endif

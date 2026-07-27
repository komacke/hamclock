# HamClock Client RESTful API

The HamClock Client provides a RESTful API scripting interface, with which it can be controlled and queried over a network. These commands can be sent with command line tools such as curl or wget or with a browser. The list of commands below is followed by several examples.

By default, the RESTful API interface uses port 8080 but this can be changed with the -e command line argument. For example, to change to port 9000, run hamclock as follows:

    hamclock -e 9000

To disable the RESTful API interface, use `-e -1`.

## Base Command Set

The following commands form the base command set defined as of v4.22.
All implementations that intended to be fully compatible with the HamClock Client RESTful API must implement these commands.

      get_capture.bmp       get live screen shot in bmp format
      get_config.txt        get current display settings
      get_contests.txt      get current list of contests
      get_de.txt            get DE info
      get_dx.txt            get DX info
      get_dxpeds.txt        get current list of DXpeditions
      get_dxspots.txt       get DX spots
      get_gpio?             pin=MCP&latched=[true,false]
      get_livespots.txt     get live spots list
      get_livestats.txt     get live spots statistics
      get_ontheair.txt      get POTA/SOTA activators
      get_satellite.txt     get current sat info
      get_satellites.txt    get list of all sats
      get_sensors.txt       get sensor data
      get_spacewx.txt       get space weather info
      get_sys.txt           get system stats
      get_time.txt          get current time
      get_voacap.txt        get current band conditions matrix
      set_adif?             pane=[0123] (POST)
      set_alarm?            state=off|armed&time=HR:MN&utc=yes|no
      set_auxtime?          format=[one_from_menu]
      set_bmp?              pane=[1,2,3,map]&fit=[resize,crop,fill][&off] (POST)
      set_cluster?          host=xxx&port=yyy
      set_debug?            name=xxx&level=n
      set_defmt?            fmt=[one_from_menu]&atin=RSAtAt|RSInAgo
      set_displayOnOff?     on|off
      set_displayTimes?     on=HR:MN&off=HR:MN&day=[Sun..Sat]&idle=mins
      set_gpio?             pin=MCP&level=[hi,lo]&blink=hz
      set_livespots?        (see error message)
      set_mapcenter?        lng=X
      set_mapcolor?         setup=name&color=R,G,B
      set_mapview?          Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off
      set_newde?            grid=AB12&lat=X&lng=Y&call=AA0XYZ
      set_newdx?            grid=AB12&lat=X&lng=Y
      set_once_alarm?       state=off|armed&time=YYYY-MM-DDTHR:MN&tz=DE|UTC
      set_pane?             Pane[0123]=X,Y,Z... any from:
         VOACAP_DEDX DE_Wx DX_Wx Solar_Flux Planetary_K Moon NOAA_SpcWx Sunspot_N
         X-Ray SDO Solar_Wind DRAP Contests Live_Spots Bz_Bt On_The_Air Aurora
         DXPeditions Disturbance
      set_panzoom?          pan_x=X&pan_y=Y&pan_dx=dX&pan_dy=dY&zoom=Z
      set_rotator?          state=[un]stop|[un]auto&az=X&el=X
      set_rss?              reset|add=X|network|interval=secs|on|off|file (POST)
      set_satname?          abc|none
      set_sattle?           name=abc&t1=line1&t2=line2
      set_screenlock?       lock=on|off
      set_senscorr?         sensor=76|77&dTemp=X&dPres=Y
      set_stopwatch?        reset|run|stop|lap|countdown=mins
      set_time?             change=delta_seconds
      set_time?             ISO=YYYY-MM-DDTHH:MM:SS
      set_time?             Now
      set_time?             unix=secs_since_1970
      set_title?            call|title|onair=[text]&fg=R,G,B&bg=R,G,B|rainbow
      set_touch?            x=X&y=Y
      set_voacap?           band=X&power=W&tz=DE|UTC&mode=X&map=X&TOA=X
      exit                  exit HamClock
      postDiags             post diagnostic logs and configuration settings
      restart               restart HamClock
      updateVersion         update to latest version

Any command not recognized will show API help.

## Example Usage

In the examples below, change the IP to what HamClock reports as its L-IP.

Get the current clock UTC time:

        curl 'http://192.168.7.101:8080/get_time.txt'

Set display to turn on Wednesday at 8 AM and off at 10 PM, DE time, with 10 minutes idle time:

        curl 'http://192.168.7.101:8080/set_displayTimes?on=8:00&off=22:00&day=Wed&idle=10'

Set a new DE location from latitude and longitude:

        curl 'http://192.168.7.101:8080/set_newde?lat=40.7&lng=-74'

Set DE grid to center of EM13:

        curl 'http://localhost:8080/set_newde?grid=EM13'

Save the current display to a file named hcscreen.bmp:

        curl 'http://192.168.7.101:8080/get_capture.bmp' > hcscreen.bmp

Specify an earth satellite with its TLE:
Note 1: This TLE will be out of date by the time you read this, it is shown just as an example of proper syntax.
Note 2: This elaborate curl syntax is required whenever the payload includes spaces. See User Contrib #13 for a small script that helps with this curl syntax.

        curl --get 'http://192.168.7.101:8080/set_sattle' --data-urlencode 'name=ISS&t1=1 25544U 98067A   21320.06051688  .00001570  00000+0  37172-4 0  9998&t2=2 25544  51.6441 311.7573 0004586 201.9414 260.8823 15.48581357312130'

Set satellite to ISS and report current ephemeris with respect to DE:

        curl 'http://192.168.7.101:8080/set_satname?ISS'

Set Live Spots to report spots made by DE call using wspr on 40, 30 and 20m

        curl 'http://192.168.7.101:8080/set_livespots?spot=by&what=call&data=wspr&bands=40,30,20

Set Pane 3 to rotate through NOAA Space weather, X-Ray flux and DRAP trend:

        curl 'http://192.168.7.101:8080/set_pane?Pane3=Space_Wx,X-Ray,DRAP'

Toggle the screen lock padlock:

        curl 'http://192.168.7.101:8080/set_touch?x=224&y=132'

Change call sign to say "Welcome Visitors" white on red (requires expanded curl because of embedded blank):

        curl --get 'http://192.168.7.101:8080/set_title' --data-urlencode 'title=Welcome Visitors&fg=255,255,255&bg=red'

... then restore normal call sign:

        curl 'http://192.168.7.101:8080/set_title?call='

Change map to Azimuthal projection with Countries style and RSS on, leaving Grid unchanged:

        curl 'http://192.168.7.101:8080/set_mapview?RSS=on&Projection=Azimuthal&Style=Countries'

Turn off RSS network feed and clear the local title list:

        curl 'http://192.168.7.101:8080/set_rss?reset'

Turn off RSS network feed and add one RSS title to local list:

        curl --get 'http://192.168.7.101:8080/set_rss' --data-urlencode 'add=This is a new RSS title'

Turn off RSS network feed and load a file containing up to 15 lines into the RSS title list:

        curl --data-binary '@mytitles.txt' 'http://192.168.7.101:8080/set_rss?file'

Restore normal network RSS operation:

        curl 'http://192.168.7.101:8080/set_rss?network'

Load the ADIF file named myfile.adi and select ADIF in data pane 2 (center pane).

        curl --data-binary '@myfile.adi' 'http://192.168.7.101:8080/set_adif?pane=3'

Load the BMP file named myimage.bmp in data pane 3 (right pane), cropping to center if necessary:

        curl --data-binary '@myimage.bmp' 'http://192.168.7.101:8080/set_bmp?pane=3&fit=crop'

Same but in the main map area:

        curl --data-binary '@myimage.bmp' 'http://192.168.7.101:8080/set_bmp?pane=map&fit=resize'

* Once loaded, the image will be added to the map style menu as User. It may be combined with other styles for rotation or zoomed in the usual manner. If you don't see it immediately, some other style is probably in rotation. The higher the resolution of the loaded file, the better zooming will look.
* All map overlays are disabled while a User image is displayed.
* The image can be displayed with any projection, but other than Mercator will look warped or wrapped.
* There are three options for how the image is made to fit the target box:
* resize will expand the image to fill the box, but will maintain the aspect ratio using black gaps if necessary.
* fill will expand the image to fill the entire box, changing the aspect ratio if necessary.
* crop will extract the center portion of the image to fill the box, making no attempt to resize.
* To remove the User image, run set_bmp with off: `set_bmp?pane=map&off`
* There are several popular tools available to create bmp files from a variety of formats. Here are some examples that create myimage.bmp from myimage.png and the Debian package to be installed for its use:

        sudo apt install ffmpeg
        ffmpeg -i myimage.png -v error -c:a copy -pix_fmt rgb565 -y myimage.bmp

        sudo apt install netpbm
        pngtopam myimage.png | ppmtobmp -bpp 24 -windows > myimage.bmp

        sudo apt install imagemagick
        convert myimage.png bmp3:myimage.bmp

Set map grid color to yellow:

        curl 'http://192.168.7.101:8080/set_mapcolor?setup=Map_grid&color=yellow'

set_gpio and get_gpio use the following pin assignments. MCP refers to the MCP23017 I2C IO chip. Pins used by HamClock are shown for reference only. It is not currently possible to use get_gpio to read back a previously commanded set_gpio value.

        Pin       MCP    RPi GPIO      RPi Header   HamClock
         0         A0       13            33        out - countdown red
         1         A1       19            35        out - countdown green
         2         A2       26            37        in - countdown reset
         3         A3        6            31        out - alarm state
         4         A4        5            29        in - alarm off
         5         A5       20            38        out - satellite state
         6         A6       21            40        in - on air
         7         A7       17            11        reserved
         8         B0       27            13
         9         B1       22            15
        10         B2       23            16
        11         B3       24            18
        12         B4       12            32
        13         B5       16            36
        14         B6        7            26
        15         B7        4             7

Start RPi header pin 36 blinking at 1 Hz:

        curl 'http://192.168.7.101:8080/set_gpio?pin=13&blink=1'

Retrieve current level of MCP B3:

        curl 'http://192.168.7.101:8080/get_gpio?pin=11&latch=false'

Retrieve level of MCP B3 after change following previous latched retrieval, if any:

        curl 'http://192.168.7.101:8080/get_gpio?pin=11&latch=true'

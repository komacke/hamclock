# User Contributions

All contributed scripts referred to below are available in [../hamclock-contrib/](../hamclock-contrib/)

## 1. Tips for Executing and Displaying HamClock on Windows 10

Contributed by Joeri van Dooren, ON3URE

* In windows download <https://sourceforge.net/projects/vcxsrv>
* Install; choose single windows
* In Windows install WSL2 with ubuntu.
* Start wsl.exe

        sudo apt update
        sudo apt -y install g++ libx11-dev wget
        wget https://www.clearskyinstitute.com/ham/HamClock/ESPHamClock.zip
        unzip ESPHamClock.zip
        cd ESPHamClock
        make hamclock-800x480
        sudo make install
        export DISPLAY=:0
        /usr/local/bin/hamclock

## 2. More tips for Executing and Displaying HamClock on Windows 10 and 11

Install Ubuntu from the Microsoft Store, for example the one at this link: Ubuntu 20.04.6 LTS. Follow the prompts to create your user name and password.

At the terminal command prompt run the following commands:

    sudo apt-get update
    sudo apt-get upgrade
    sudo apt install curl make g++ xorg-dev libx11-dev zip
    cd
    rm -fr ESPHamClock
    curl -O https://www.clearskyinstitute.com/ham/HamClock/ESPHamClock.zip
    unzip ESPHamClock.zip
    cd ESPHamClock

Now, enter make to get a list of the resolutions and options available. The following targets are available (choose as appropriate for your system)

    hamclock-800x480 X11 GUI desktop version, AKA hamclock
    hamclock-1600x60 X11 GUI desktop version, larger, AKA hamclock-big
    hamclock-2400x1440 X11 GUI desktop version, larger yet
    hamclock-3200x1920 X11 GUI desktop version, huge
    hamclock-web-800x480 web server only (no display)
    hamclock-web-1600x960 web server only (no display), larger
    hamclock-web-2400x1440 web server only (no display), larger yet
    hamclock-web-3200x1920 web server only (no display), huge

In this example, suppose we want to use a web browser to access Hamclock with a resolution of 1600x960, then we type the following:

    make -j 4 hamclock-web-2400x1440
    sudo make install

All set: to run hamclock as a service just type hamclock & and press enter.

## 3. Automating qrz page from spots

Contributed by Hans Klausmann, DL5RAZ

see dl5raz.py.

## 4. Script to simplify sending web commands from command line

Script by Barry, N0NZ

Documentation [here](../hamclock-contrib/hcmd.py.docx).

## 5. Script to read and write the eeprom config file

Contributed by Elwood Downey, <wb0oew@arrl.net>

Someone asked if they could edit the eeprom config file. Normally no, but since I enjoy writing perl, I couldn't resist writing hceeprom.pl to do exactly that. You can download it within the contrib collection here.

Save the script anywhere in your PATH and make it executable as usual. The script needs to know the directory containing the source files and the path to the eeprom file. By default these are assumed to be the current directory and ~/.hamclock/eeprom, respectively, but these can be changed with the -s and -e options. Run the script with no args or --help for a complete usage summary.

Note well: you are hacking a file not intended for human consumption, so you can easily mess things up. There's no validation checking whatsoever, so you could go right ahead and set preposperous values such as a longitude of 10000 or inconsistent values such as a lat/long that doesn't match the grid square sending HamClock into unexplored territory. Save a copy of eeprom before starting, or remove it then run HamClock so it will build a new one containing all defaults. Never edit the file while HamClock is running because HamClock often updates it, but only reads it once when starting up. You have been warned.

What ever, let's try it! Here are some examples:

Read the call sign:

    hceeprom.pl NV_CALLSIGN
    NV_CALLSIGN = WB0OEW

Set a new call sign:

    hceeprom.pl NV_CALLSIGN AB1XYZ
    NV_CALLSIGN = AB1XYZ

Set the DX cluster host:

    hceeprom.pl NV_DXHOST usdx.w1nr.net
    NV_DXHOST = usdx.w1nr.net

Set DX spots to show as full call sign

    hceeprom.pl NV_MAPSPOTS 2
    NV_MAPSPOTS = 2

Set the BME280 at I2C address 77 temperature correction:

    hceeprom.pl NV_TEMPCORR2 -0.23
    NV_TEMPCORR2 = -0.23

## 6. Script to query for any space weather datum

Contributed by Elwood Downey, <wb0oew@arrl.net>

This script hcgetsw.sh queries HamClock for the most recent value of any space weather data it provides. Run the script with -help for a complete list.

## 7. Script to send any RSS feed to HamClock

Contributed by Elwood Downey, <wb0oew@arrl.net>

HamClock does not allow setting individual RSS feeds, see FAQ 20 for the reason why. But this script uses the web-based RSS title loading facility set_rss to read any RSS feed and load its titles into HamClock.

To use the script, just give it the host name (or IP) of your HamClock followed by the full URL of the RSS to load. For example:

    hcrss.pl 192.168.7.117 https://www.arrl.org/news/rss

## 8. Instructions for macOS dual screen

Contributed by AL7CR.

These settings allow a full screen Hamclock with no menu or borders on one display and a fully useable Mac desktop on the other. Tested on XQuartx 2.7.11 under High Sierra 10.13.6. The steps are:

* Set "Displays have separate spaces" otherwise running HamClock will cause the second screen to be black whether or not it is set to full screen mode.
* Set "Assign to Display" to the desired display. This is done by right clicking the XQuartz icon in the dock and selecting the desired display. This option only appears if you have multiple displays.
* Autohide Dock and Menubar as for a single screen. Note that this setting is system wide and not per screen as you might expect.
* And the key point, in XQuartz preferences disable full screen mode. If this is not done Hamclock will disappear when it's window loses the system focus. Command-Option-A will make it reappear however you will not be able to work in one screen with Hamclock in the other unless XQuartz full screen is disabled.

## 9. Arch User Repository

Contributed by KJ7RRV.

HamClock is available in the Arch User Repository here. The package names are:

* hamclock (800x480)
* hamclock-big (1600x960)
* hamclock-bigger (2400x1440)
* hamclock-huge (3200x1920)

## 10. Running HamClock on ProxMox

Contributed by NI2O.

This [pdf](../hamclock-contrib/HamClock%20on%20ProxMox.pdf) shows how to configure HamClock to operate in an LXC container on a ProxMox server.

## 11. Compile HamClock statically so it will run on e.g. TrueNAS

Contributed by M1JKL.

[TrueNAS](https://www.truenas.com/) allows you to run Docker containers and VMs of all sorts; in my case, all I wanted to do was to run Hamclock as a service and hence it seemed a massive overkill to go down the VM/Docker route - hundreds of MB of installed space used up for a simple executable.

So this is what I did:

* edit the Makefile to say CXX = g++ -static
* run make with the hamclock configuration of your choice e.g.

        make -j 4 hamclock-web-1600x960

* copy the resulting hamclock executable to your truenas directory of choice, maybe in your home and make it executable
* go to ~/ and check it runs

        ./hamclock

* Truenas natively contains tmux which is a terminal multiplexer, so running something like

        tmux ./hamclock

    allows you to close the truenas login session whilst detaching from the still-running hamclock.

I did this a few weeks ago so the description above is from memory. Anyone with a bit of linux knowledge (I have almost none!) will work out a more elegant approach to establish it as a true service by using things like init.d or /etc/rc.local.

## 12. Web proxy to control multiple independent web hamclocks from the same URL

Contributed by WB0OEW.

Suppose you wanted to provide one URL for club members to access HamClock from anywhere they could access a browser. You could start one HamClock and publish its live web connection directly but as soon as more than one person connected they would interfere and things will get confusing and frustrating pretty quickly.

This little program solves that by functioning as a multiplexing proxy. It listens to the same port 8081 as a real HamClock, but runs a separate instance of hamclock for each IP reported by connecting browsers.

To give it a try, extract the program from the contrib collection mentioned at the top of this tab. The entire program is self-contained in a single file named hcwebproxy.cpp. Also, since the hamclocks that the proxy will run are only useful on the web, it is recommended to build a web-only hamclock just for use by the hcwebproxy. If this is how you normally build hamclock anyway then you can continue to use that one.

Putting it all together, assuming

* you have already downloaded and installed your first HamClock
* you want to use a different hamclock just for hcwebproxy

then the procedure might go something like this:

        cd ~
        curl -O 'https://clearskyinstitute.com/ham/HamClock/hamclock-contrib.zip'
        unzip hamclock-contrib.zip
        cd hamclock-contrib
        make hcwebproxy
        sudo make install
        cd ~/ESPHamClock
        make hamclock-web-1600x960
        sudo mv hamclock-web-1600x960 /usr/local/bin
        sudo chown root /usr/local/bin/hamclock-web-1600x960
        sudo chmod u+s /usr/local/bin/hamclock-web-1600x960
        hcwebproxy -d ./hcwebproxy-hamclocks -h hamclock-web-1600x960 -i 240 -m 3 -v -- -t 50 > x.hcwp &

The main reason for escalating to root privilege is to allow remote updates. See FAQ 19 for more information.

Now surf to the usual URL such as <http://your-host:8081/live.html> and you should get a new HamClock.

Try these additional tests to understand different use cases:

* Open another tab on the same browser to the same URL and you will share the same HamClock because the browser is reporting the same IP. Any changes in one tab will be immediately reflected in the other. You can have as many connections as desired open at the same time.
* Now close all connection tabs then open a new connection to the same URL. You will be reconnected to the same instance already running because the browser reports the same IP.
* Open the URL from a different computer and you will get a completely new HamClock with default settings, different from the instance from the first computer. Opening additional connections will connect to the same instance. Also see the note below regarding NAT routers.

Further notes:

* This requires running HamClock version 2.91 or newer.
* Remember that all computers behind a NAT router connecting to a public IP will report as coming from the same public IP of the NAT. Thus, they will all share the same hamclock instance.
* Many simultaneous running hamclocks can be a large system load, so consider using the -m option to limit the maximum number accordingly. The default is 10 processes. Attempts to connect when this maximum has been reached will result in a brief informative message. Also see -i, next.
* If the -m limit is reached when a new connection is attempted, hamclock processes that have been idle longer than -i hours are eligible for reassignment to handle the new connection. The default is 24 hours.
* Another way to mitigate high system load is to throttle back the maximum CPU usage for each HamClock. This is controlled by HamClock's own -t option which you can set using hcwebproxy's -- option. Note well that this will reduce the responsiveness of each hamclock so use wisely. For example, to limit each hamclock to no more than 20% CPU:

        hcwebproxy -h hamclock-web-1600x960 -- -t 20 &

* Since more than one HamClock may well be running on the same host, note well that the features that use external hardware should only be activated on at most one at a time. This refers to everything on the last page of User Guide. HamClock itself tries to enforce single access but beware of this limitation.
* I actually find this so useful that I now always use this to access HamClock running on my RPi even if I just run one hamclock. Reason being that the hamclock processes are sleeping when I am not connected and the Pi runs much cooler most of the time. Plus it's super handy to check propagation conditions and my WSPR reports using my iPhone when away from the shack.
* If something goes terribly wrong, hcwebproxy will try to kill all the hamclock processes it knows about before it exits. To investigate why, run hcwebproxy again with -v for debug info and try to reproduce the problem. Send me the log if it seems like a bug.
* Even if hcwebproxy dies and all hamclocks get killed, their settings will be restored after a restart because all hamclock working directories are stored in separate directories named by the connection IP. The directory in which these are created can be set with the -d option, the default is $HOME/hcwebproxy-hamclocks.
* Since all hamclock instances share the same program file, when any one of them performs a remote update, all subsequent hamclocks will use the newer version as well. Thus, after one hcwebproxy client does an update, merely restarting the others will update them also without requiring an actual update.
* Normally hcwebproxy disables the RESTful API ports on the hamclocks it starts because it is awkward to find the one you want. Nevertheless, if this is important for your use case, you can use the -a option to assign each hamclock its own RESTful API port. The port numbers are assigned sequentially starting with the value given with -a. No attempt is made to avoid ports already in use by other applications so choose wisely. The problem remains how to know which hamclock is using which port; this is left as an exercise for the reader.
* You can see what hcwebproxy is doing with a shell command such as:

        ps ax | egrep 'hamclock|hcwebproxy' | grep -v egrep

    The processes with status T are sTopped.

## 13. Script to simplify running curl

Contributed by WB0OEW.

Copy/paste the following into a file named hccurl and use it to send commands to hamclock that require embedded spaces.

    #!/bin/bash
    # run curl with spaces

    if [ $# -lt 3 ] || [ $# -gt 4 ] ; then
        echo Purpose: curl wrapper for sending hamclock commands with or without embedded spaces
        echo Usage: hccurl host port command '[value]'
        echo Example set: hccurl 192.168.7.101 8080 set_title "'"'msg=Welcome Visitors&fg;=255,255,255&bg;=red'"'"
        echo Example get: hccurl 192.168.7.101 8080 get_config.txt
    else
        HOST="$1"
        PORT="$2"
        CMD="$3"
        if [ $# -eq 3 ] ; then
            curl --get "http://$HOST:$PORT/$CMD"
        else
            VALU="$4"
            curl --get "http://$HOST:$PORT/$CMD" --data-urlencode "$VALU"
        fi
    fi

## 14. Install HamClock using Docker

Contributed by Julius, DO7JZ.

I've got no Pi but servers and Linux machines. So I thought, I'd create an easy way to run HamClock using Docker, exposing it as a website. The code and instructions can be found at <https://github.com/zeidlos/hamclock-docker>.

I have not tested it on Windows, but potentially it'll run in WSL2. Works fine on my MacBook as well as my Linux-Server. I'm pretty sure it's an easy way to install/run on Raspberry Pi, or NAS Systems that support Docker/Docker-Compose as well.

73

Contributed by Chris, NZ6F
A complete docker implementation with supporting information is at <https://github.com/ChrisRomp/hamclock-docker>.

## 15. Connect HamClock with N3FJP logger

Contributed by Chris, NZ6F

[N3FJP2HamClock](https://github.com/ChrisRomp/n3fjp2hamclock) is a Windows application to synchronize HamClock with the call sign entry field in N3FJP logger applications. This application connects to the N3FJP API server, and when you tab out of the call sign field, it will update your HamClock(s) with the DX of the entered call.

## 16. How to install and run HamClock on Android

Contributed by Todd, N9MWB

Install UserLAnd on your android device from the play store.

Start UserLAnd and create a new Debian App. Select:

    Desktop environment: Minimal
    Connection Type: Graphical
    Orientation: either

You can also choose Ubuntu or probably another app, but the instruction will be slightly different. These instructions have worked fine for me with Debian.

I recommend using a vnc viewer such as RealVNC from my laptop/desktop once you have your UserLAnd session started. On the device, when the UserLAnd session comes up, click outside the terminal window and select the keyboard icon. Click on the terminal window and run vncpasswd and set the vnc password to something you'll remember. Then from a regular computer where you have a vnc viewer installed (I used RealVNC's viewer), vnc into the UserLAnd session. You might have to install net-tools and get your UserLAnd session's IP address, although it should be the same as the devices IP address. Then connect to ip-address:5951. At this point, you should be ready to install hamclock into your UserLAnd session.

For Debian

    sudo apt update -y
    sudo apt upgrade -y
    sudo apt-get -y install curl make g++ libx11-dev libgpiod-dev zip
    cd
    rm -fr ESPHamClock
    curl -O http://www.clearskyinstitute.com/ham/HamClock/ESPHamClock.zip
    unzip ESPHamClock.zip
    cd ESPHamClock
    make -j 4 hamclock-800x480     (or 1600x960 or 2400x1440 or 3200x1920 depending upon the screen resolution of your device.
    sudo make install
    hamclock

You can now either use hamclock via vnc or connect with a browser to ip-address:8081/live.html

## 17. Show weather station data as an RSS feed

Contributed by Mark, G7LTT/NI2O

[Add WeeWX RSS feed to Hamclock.pdf](../hamclock-contrib/AddWeeWxRSSfeedtoHamclock.pdf)

## 18. Update local satellite TLE file

Contributed by Burns Fisher, WB1FJ

This script will update the TLE file HamClock can use for local satellite elements. The updates are from AMSAT so the names must match the ones they use. Run the script every few days, or make a crontab entry to run automatically.

[updateTLE.py](../hamclock-contrib/updateTLE.py)

## 19. Install HamClock in a Proxmox LXC

Contributed by Stuart, GM5DNA

I've recently developed a one line script for installing HamClock in a Proxmox LXC. Set up takes about 5 minutes. Details below if they're of any help / interest!

<https://github.com/gm5dna/proxmox-hamclock-lxc>

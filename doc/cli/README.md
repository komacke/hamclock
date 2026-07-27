# HamClock Client Command-line Interface

HamClock Client usage:

    hamclock [options]

The following command line options are defined:

    -0   : restore all original default Setup values
    -a x : set debug name=level, bogus name gives list
    -b h : set backend host:port to h; default is clearskyinstitute.com:80
    -d d : set working directory to d; default is ~/.hamclock/
    -e p : set RESTful web server port to p or -1 to disable; default is 8080
    -f o : force display full screen initially to "on" or "off"
    -g   : init DE using geolocation with current public IP; requires -k
    -h   : print this help summary then exit
    -i i : init DE using geolocation with IP i; requires -k
    -k   : start in normal mode, ie, don't offer Setup or wait for Skips
    -l l : set Mercator or Robinson center longitude to l degrees, +E; requires -k
    -m   : enable demo mode
    -n t : set live web idle timeout to t minutes; default forever
    -o   : write diagnostic log to stdout instead of in ~/.hamclock/
    -p f : require passwords in file f formatted as lines of "category password"
            changeUTC configurations exit newde newdx reboot restart setup shutdown unlock upgrade
    -q   : ignore saved startup screen location and size
    -r p : set read-only live web server port to p or -1 to disable; default 8082
    -s d : start time as if UTC now is d formatted as YYYY-MM-DDTHH:MM:SS
    -t p : throttle max cpu to p percent; default is 80
    -v   : show version info then exit
    -w p : set read-write live web server port to p or -1 to disable; default 8081
    -x n : set n max live web connections; max 100; default 10
    -y   : activate keyboard cursor control arrows/hjkl/Return -- beware stuck keys!

All derivative or compatible HamClock Clients are encouraged to use the same command-line options where applicable.

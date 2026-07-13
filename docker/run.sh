#!/bin/sh

# default size which only applies if all sizes are built
if [ -z "$HC_SIZE" ]; then
    HC_SIZE=2400x1440
fi

if [ -z "$BACKEND_HOST" -a -e /opt/hamclock/backend_host ]; then
    BACKEND_HOST="$(grep -v '^#' /opt/hamclock/backend_host)"
fi
if [ -n "$BACKEND_HOST" ]; then
    BACKEND_ARG="-b $BACKEND_HOST"
fi

# these values only matter if there is not an /opt/.hamclock/eeprom file.
perl hceeprom.pl NV_CALLSIGN $CALLSIGN && \
perl hceeprom.pl NV_DE_GRID $LOCATOR && \
perl hceeprom.pl NV_DE_LAT $LAT && \
perl hceeprom.pl NV_DE_LNG $LONG && \
perl hceeprom.pl NV_BCMODE $VOACAP_MODE && \
perl hceeprom.pl NV_BCPOWER $VOACAP_POWER && \
perl hceeprom.pl NV_CALL_BG_COLOR $CALLSIGN_BACKGROUND_COLOR && \
perl hceeprom.pl NV_CALL_BG_RAINBOW $CALLSIGN_BACKGROUND_RAINBOW && \
perl hceeprom.pl NV_CALL_FG_COLOR $CALLSIGN_COLOR && \
perl hceeprom.pl NV_FLRIGHOST $FLRIG_HOST && \
perl hceeprom.pl NV_FLRIGPORT $FLRIG_PORT && \
perl hceeprom.pl NV_FLRIGUSE $USE_FLRIG && \
perl hceeprom.pl NV_METRIC_ON $USE_METRIC && \

# this extra work causes the container to stop quickly. We need to 
# kill our own jobs or bash will zombie and then docker takes 10 seconds
# before it sends kill -9. The wait will respond to a TERM whereas 
# tail does not so we need to background tail.
cleanup() {
    echo "Caught SIGTERM, shutting down services..."
    kill $(jobs -p)
    exit 0
}

# Trap the TERM signal
trap cleanup SIGTERM

if [ -x /opt/hamclock/bin/hamclock-web-$HC_SIZE ]; then
    HC_EXEC="/opt/hamclock/bin/hamclock-web-$HC_SIZE"
elif [ -x /opt/hamclock/bin/hamclock ]; then
    HC_EXEC="/opt/hamclock/bin/hamclock"
else
    echo "ERROR: no hamclock executable for size '$HC_SIZE'"
    exit 1
fi

$HC_EXEC -t 10 $BACKEND_ARG &
wait $!

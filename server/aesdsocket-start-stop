#!/bin/sh

DAEMON="aesdsocket"
PIDFILE="/var/run/$DAEMON.pid"

AESD_ARGS="-d"

# shellcheck source=/dev/null
[ -r "/etc/default/$DAEMON" ] && . "/etc/default/$DAEMON"

if [ -f "/bin/$DAEMON" ] ; then
LOCDAEMON="/bin/${DAEMON}"
fi

if [ -f "/usr/bin/$DAEMON" ] ; then
LOCDAEMON="/usr/bin/${DAEMON}"
fi

# BusyBox' klogd does not create a pidfile, so pass "-n" in the command line
# and use "-m" to instruct start-stop-daemon to create one.
start() {
	printf 'Starting %s: ' "$DAEMON"
	# shellcheck disable=SC2086 # we need the word splitting
	#start-stop-daemon -b -m -S -q -p "$PIDFILE" -x "/sbin/$DAEMON" \
	#	-- -n $KLOGD_ARGS
	
	start-stop-daemon --start --quiet --pidfile "$PIDFILE" -x "$LOCDAEMON"  -- $AESD_ARGS
	status=$?
	if [ "$status" -eq 0 ]; then
		echo "OK"
	else
		echo "FAIL"
	fi
	return "$status"
}

stop() {
	printf 'Stopping %s: ' "$DAEMON"
	start-stop-daemon --stop --quiet --oknodo --pidfile "$PIDFILE"
	status=$?
	if [ "$status" -eq 0 ]; then
		rm -f "$PIDFILE"
		echo "OK"
	else
		echo "FAIL"
	fi
	return "$status"
}

restart() {
	stop
	sleep 1
	start
}

case "$1" in
	start|stop|restart)
		"$1";;
	reload)
		# Restart, since there is no true "reload" feature.
		restart;;
	*)
		echo "Usage: $0 {start|stop|restart|reload}"
		exit 1
esac

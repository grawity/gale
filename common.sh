run() {
	echo "$*"
	"$@" || exit 1
}

qgrep() {
	grep "$@" > /dev/null
}

testkey() {
	gkinfo -x "$1" 2>/dev/null | qgrep "^Trusted public key: <$1>"
}

testkey_stdin() {
	gkinfo -x 2>/dev/null | qgrep "^Trusted public key: <$1>"
}

if [ -n "$GALE_SYS_DIR" ]; then
	SYS_DIR="$GALE_SYS_DIR"
elif [ -n "$sysconfdir" ]; then
	SYS_DIR="$sysconfdir/gale"
else
	echo "Error: neither of \$GALE_SYS_DIR or \$sysconfdir defined."
	echo "\"make install\" calls this script; don't run it directly."
	exit 1
fi

CONF="$SYS_DIR/conf"
umask 022
PATH="$bindir:$sbindir:$PATH:/usr/ucb"
export PATH

if [ -f "$CONF" ]; then
	exec 3<"$CONF"

	while read var value <&3 ; do
		[ -z "$var" -o "x$var" = "x#" ] && continue
		eval "CONF_$var=\"$value\""
	done

	exec 3<&-
fi

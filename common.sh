run() {
	echo "$*"
	"$@" || exit 1
}

if [ -z "$SYS_DIR" ]; then
	echo "Error: SYS_DIR not defined."
	echo "\"make install\" calls this script; don't run it directly."
	exit 1
fi

CONF="$SYS_DIR/conf"


if [ -f "$CONF" ]; then
	exec 3<"$CONF"

	while read var value <&3 ; do
		[ -z "$var" -o "x$var" = "x#" ] && continue
		eval "CONF_$var=\"$value\""
		echo "CONF_$var=\"$value\""
	done

	exec 3<&-
fi

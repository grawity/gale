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

[ -f "$CONF" ] && while read var value ; do
	[ -z "$var" -o "x$var" = "x#" ] && continue
	eval "CONF_$var=\"$value\""
done < "$CONF"

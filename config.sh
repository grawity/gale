. "$srcdir/common.sh"

run mkdir -p "$SYS_DIR"
if [ ! -d "$SYS_DIR" ]; then
	echo "Error: Invalid or unauthorized SYS_DIR: \"$SYS_DIR\"."
	exit 1
fi

run mkdir -p "$SYS_DIR/auth/trusted"
run mkdir -p "$SYS_DIR/auth/private"
run mkdir -p "$SYS_DIR/auth/local"
run mkdir -p "$SYS_DIR/auth/cache"
run chmod 1777 "$SYS_DIR/auth/local"
run chmod 1777 "$SYS_DIR/auth/cache"

[ -n "$CONF_GALE_USER" ] && GALE_USER="$CONF_GALE_USER"
[ -n "$CONF_GALE_DOMAIN" ] && GALE_DOMAIN="$CONF_GALE_DOMAIN"
[ -n "$CONF_GALE_SERVER" ] && GALE_SERVER="$CONF_GALE_SERVER"

if [ -z "$GALE_USER" ]; then
cat << EOM

Hi.  You need to denote a user to own the Gale domain secret key.  You must
trust this user with Gale authentication for your domain; the "gksign" program
will run as this user.  I recommend using a special "gale" user; if you don't
have administrative priveleges here, you'll probably have to use your own
account.  I do not recommend the use of "root".

No harm done if you stop this script now to set up such a user.

EOM

	/bin/echo "Enter the Gale username: \c"
	read GALE_USER
	if [ -z "$GALE_USER" ]; then
		echo "Error: Invalid username or no home dir: \"$GALE_USER\"."
		exit 1
	fi
else
	echo "Using \"$GALE_USER\" as the Gale owner."
fi

run chown $GALE_USER "$sbindir/gksign"
run chmod 4755 "$sbindir/gksign"
if [ "x$bindir" != "x$sbindir" ]; then
	run ln -s "$sbindir/gksign" "$bindir/gksign.tmp.$$"
	run mv "$bindir/gksign.tmp.$$" "$bindir/gksign"
fi

[ -f "$SYS_DIR/auth/trusted/ROOT" ] || run cp "$srcdir/auth/ROOT" "$SYS_DIR/auth/trusted"

if [ -z "$GALE_DOMAIN" ] ; then
cat << EOM

You need to choose a name for your authentication domain.  The extent of the 
domain need not correspond to any given physical or logical setup.  The domain 
name is often a DNS domain (by convention), but need not be.  If you run Gale 
in the context of a larger organization, they may have a domain set up; ask 
your administrators about it.

EOM

	/bin/echo "Enter the Gale domain: \c"
	read GALE_DOMAIN
	case "$GALE_DOMAIN" in
		*" "*) echo "Error: Invalid domain specification." ; exit 1 ;;
		*""*) echo "Error: Invalid domain specification." ; exit 1 ;;
		*""*) echo "Error: Invalid domain specification." ; exit 1 ;;
		"") echo "Error: No domain specified." ; exit 1 ;;
	esac
else
	echo "Using \"$GALE_DOMAIN\" as the Gale domain."
fi

if [ -z "$GALE_SERVER" ] ; then
cat << EOM

You need to designate a machine as a Gale server.  This machine (not 
necessarily under your control, or part of this installation) will need to
run the Gale server "galed"; it will relay messages between Gale clients.
It may well be this machine.  Please supply a comma-separated list of one
or more fully-qualified host names, with no spaces.

EOM

	/bin/echo "Enter the Gale server hostname: \c"
	read GALE_SERVER
	case "$GALE_SERVER" in
		*" "*) echo "Error: Invalid server specification." ; exit 1 ;;
		*""*) echo "Error: Invalid server specification." ; exit 1 ;;
		*""*) echo "Error: Invalid server specification." ; exit 1 ;;
		"") echo "Error: No server specified." ; exit 1 ;;
	esac
else
	echo "Using \"$GALE_SERVER\" as the Gale server(s)."
fi

if [ ! -f "$CONF" ]; then
cat > "$CONF" <<EOM
# $CONF -- created by Gale installer; edit to suit.

# Other server(s) the server should make connections with.
# (Optional, but you'll be an island without these.)
# GALE_LINKS ofb.net
EOM

cat <<EOM

*** Creating "$CONF".
Examine and edit this file to your taste and local needs.
If you want to recreate it from scratch, remove it and re-run this.
EOM
fi

[ -n "$CONF_GALE_USER" ] || cat >> "$CONF" <<EOM

# The user who owns the domain secret key.  (Used in installation and upgrade)
GALE_USER $GALE_USER
EOM

[ -n "$CONF_GALE_DOMAIN" ] || cat >> "$CONF" << EOM

# The authentication domain to use.  (Mandatory)
GALE_DOMAIN $GALE_DOMAIN
EOM

[ -n "$CONF_GALE_SERVER" ] || cat >> "$CONF" << EOM

# The hostname(s) of server(s) clients should talk to.  (Mandatory)
# Feel free to list several, separated by commas; clients will choose 
# the server quickest to respond.
GALE_SERVER $GALE_SERVER
EOM

testkey "$GALE_DOMAIN" || cat << EOM

*** You lack a signed key for your domain, "$GALE_DOMAIN".
Become user "$GALE_USER" and make the "domain" target here to create
a new domain; contact your domain administrator if you wish to become
part of an existing domain.
EOM

echo ""

. $srcdir/common.sh

if [ -z "$HOME" ]; then
	cd
	HOME="`pwd`"
	echo "Warning: \$HOME not set, assuming \"$HOME\"."
fi

GALE="$HOME/.gale"
mkdir -p "$GALE"
if [ ! -w "$GALE" ]; then
	echo "Error: Cannot create \"$GALE\"."
	exit 1
fi

if [ -z "$CONF_GALE_USER" ]; then
	echo "Error: GALE_USER not configured."
	echo "Make sure you've completed the \"make install\" process."
	exit 1
fi

USER="`whoami`"
if [ "x$CONF_GALE_USER" != "x$USER" ]; then
cat <<EOM
Error: Current user "$USER" does not match "$CONF_GALE_USER".
The domain configuration process must run as the Gale user.
Become "$CONF_GALE_USER" and try again.
EOM
	exit 1
fi

if [ -z "$CONF_GALE_DOMAIN" ]; then
	echo "Error: GALE_DOMAIN not configured."
	echo "Make sure you've completed the \"make install\" process."
	exit 1
fi

if testkey "$CONF_GALE_DOMAIN" ; then

cat <<EOM

*** You already have a signed key available for your domain.
To re-do the domain setup process, remove these and try again:

EOM

	for dir in \
		"$GALE/auth/local" \
		"$GALE/auth/private" \
		"$GALE/auth/trusted" \
		"$SYS_DIR/auth/cache" \
		"$SYS_DIR/auth/local" \
		"$SYS_DIR/auth/private" \
		"$SYS_DIR/auth/trusted"
	do
		[ -f "$dir/$CONF_GALE_DOMAIN" ] && 
		echo "    \"$dir/$CONF_GALE_DOMAIN\""
	done

cat <<EOM

But if things are working, you're all set.

EOM

	exit 0
fi

unsigned="$GALE/$CONF_GALE_DOMAIN.unsigned"
signer="`echo "$CONF_GALE_DOMAIN" | sed 's%^[^.@:/]*[.@:/]%%'`"
[ -z "$signer" ] && signer="ROOT"

if [ -f "$unsigned" ]; then

cat << EOM

Okay, so we've already been here.  Last time, I created the file
"$unsigned" for you to 
send off to the owner of the "$signer" domain to pass through
"gksign" and return to you.

If you've done so, great!  I just need the filename of the signed key
they gave you back.  Otherwise, interrupt this script, remove the 
file mentioned above, and run this again.

EOM

	/bin/echo "Enter the signed key filename: \c"
	read skey

	if [ ! -f "$skey" ]; then
		echo "Error: I can't find \"$skey\"."
		exit 1
	fi

	if gkinfo -x < "$skey" 2>/dev/null | qgrep "public key: <$CONF_GALE_DOMAIN>" ; then
		echo "Good, it looks like your key..."
	else
		echo "Error: \"$skey\" not for \"$CONF_GALE_DOMAIN\"."
		echo "Here's what it looks like to me:"
		gkinfo < "$skey"
		exit 1
	fi

	if testkey_stdin "$CONF_GALE_DOMAIN" < "$skey" ; then
		echo "And it looks properly signed.  Hooray for you!"
	else
		echo "Error: \"$skey\" is not fully signed."
		echo "Here's what it looks like to me:"
		gkinfo < "$skey"
		exit 1
	fi

	if cp "$skey" "$SYS_DIR/auth/local/$CONF_GALE_DOMAIN" ; then
		echo "Signed key successfully installed."
	else
		echo "Error: copying \"$skey\" to \"$SYS_DIR/auth/local/$CONF_GALE_DOMAIN\"."
		exit 1
	fi

cat << EOM

The domain should be properly configured now.  Assuming users can access a
version of "gksign" setuid to "$CONF_GALE_USER" (this user), they should be
able to start running clients and generating IDs for themselves.

The installation process is complete!

EOM

	exit 0
fi

cat << EOM

Greetings.  We need to make a key for "$CONF_GALE_DOMAIN".
Please enter a description to go along with the key; for example, 
caltech.edu has the description "California Institute of Technology".

EOM

/bin/echo "Enter the description: \c"
read descr

echo "We will generate the key now.  Have patience."
gkgen -u "$unsigned" "$CONF_GALE_DOMAIN" "$descr" || exit 1

cat << EOM

*** What you must do: Take the file "$unsigned",
which contains the public part of the newly-generated key pair.  Send it to
the owner of the "$signer" domain through appropriate means.
Take care to preseve the file's binary data; you may need to uuencode it.

Assuming they trust you with your subdomain, they should pass the key through
"gksign" as a filter, returning the signed output to you.  When you have this
signed key file available, re-run this process, and we will move on to the
next step.

EOM

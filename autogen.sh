#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
ORIGDIR=`pwd`
cd $srcdir

# hack for a cluster I use often ...
if [ -d /usr/ug/share/aclocal ]; then
	ACLOCAL_FLAGS="$ACLOCAL_FLAGS --acdir=/usr/ug/share/aclocal"
fi

for I in . liboop; do
    (   cd $I
	libtoolize --copy --force
	aclocal $ACLOCAL_FLAGS
	if [ $I = . ]; then autoheader; fi
	automake --add-missing
	if [ $I = . ]; then autoheader; fi
	autoconf
    )
done

# Developers will probably want to be able to debug their programs...
[ -z "$CFLAGS" ] && CFLAGS="-g -Wall -pipe"

cd $ORIGDIR &&
$srcdir/configure --enable-maintainer-mode "$@" &&
echo "Now type 'make' to compile gale"

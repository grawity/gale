#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
ORIGDIR=`pwd`
cd $srcdir

for I in . liboop; do
    (   cd $I
	libtoolize --copy --force
	aclocal
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

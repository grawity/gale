%define	name	gale
%define	version	0.99
%define	release	2
%define	serial	1

Summary:	Gale
Name:		%{name}
Version:	%{version}
Release:	%{release}
Serial:		%{serial}
Copyright:	GPL
Group:		Applications/Networking
URL:		http://www.gale.org/
Vendor:		Dan Egnor <egnor@gale.org>
Source0:	http://gale.org/dist/%{name}-%{version}.tar.gz
BuildRoot:	/var/tmp/%{name}-%{version}

Distribution:	Gale Distribution
Packager:	Erik Ogan <erik@slackers.net>

Requires:	gale-base

%package base
Summary:	Gale Shared Components
Group:		Development/Libraries

%package server
Summary:	Gale Server
Group:		Development/Libraries
Requires:	gale-base

%package devel
Summary:	Gale Development Headers
Group:		Development/Libraries
Requires: gale-base

%description
Gale is instant messaging software distributed under the terms of the GPL.
It offers secure, reliable, scalable and usable instant messaging services.
This is the client package for command-line access to the Gale network.

%description base
Gale is instant messaging software distributed under the terms of the GPL.
This is the base package required by both server and clients.

%description server
Gale is a freeware instant messaging and presence software for Unix.
This is the server package needed to operate a Gale domain.

%description devel
Gale is a freeware instant messaging and presence software for Unix.
This is the development package needed to build programs with the Gale API.

%prep
#' 
%setup

CPPFLAGS="-I/usr/include/gc -I/usr/include/rsaref" ./configure --prefix=/usr 

%build
cd $RPM_BUILD_DIR/%{name}-%{version}
%ifarch i586 i686
perl -pi.prerpm -e 's/^(CFLAGS\s*=.*)/\1 -mcpu=%{_target_cpu}/' `find . -name Makefile -o -name Makefile.gc`
%endif
make

%install

if [ -d $RPM_BUILD_ROOT ] && [ ! -L $RPM_BUILD_ROOT ]; then rm -rf $RPM_BUILD_ROOT; fi
mkdir -p $RPM_BUILD_ROOT/usr
cd $RPM_BUILD_DIR/%{name}-%{version}
make prefix=$RPM_BUILD_ROOT/usr install 
### HACK!
rm $RPM_BUILD_ROOT/usr/bin/gksign
ln -s /usr/sbin/gksign $RPM_BUILD_ROOT/usr/bin/gksign
### end HACK!
touch $RPM_BUILD_ROOT/usr/etc/gale/conf

%clean
if [ -d $RPM_BUILD_ROOT ] && [ ! -L $RPM_BUILD_ROOT ]; then rm -rf $RPM_BUILD_ROOT; fi

%files
%defattr(-,root,root)
/usr/bin/gkgen
/usr/bin/gkinfo
/usr/bin/gksign
/usr/bin/gsend
/usr/bin/gsub
/usr/bin/gwatch
/usr/sbin/gksign

%files base
%config(noreplace) /usr/etc/gale/conf
%doc /usr/etc/gale/COPYING
/usr/lib/libgale*
/usr/bin/gale-config
/usr/bin/gale-install
%dir /usr/etc/gale
/usr/etc/gale/auth

%files devel
/usr/include

%files server
/usr/bin/galed
/usr/bin/gdomain

%post base
rm -f /usr/etc/gale/conf.rpmnew
echo "Running /usr/bin/gale-install -- feel free to suspend or stop it"
/usr/bin/gale-install && /bin/true

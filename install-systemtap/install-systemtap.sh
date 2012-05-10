#!/bin/bash
#
# Copyright (C) 2011 Canonical
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
DDEBS="deb http://ddebs.ubuntu.com $(lsb_release -cs) main restricted universe multiverse"
DDEBS_LIST=/etc/apt/sources.list.d/ddebs.list
RELEASE=`uname -r`

if [[ $EUID -ne 0 ]]; then
        echo "Need to run this as root" 1>&2
        exit 1
fi

#
#  Install SystemTap
#
apt-get install systemtap
if [ $? -ne 0 ]; then
	echo "Installing SystemTap failed"
	exit 1
fi

#
#  Append $DDEBS line to ddebs.list 
#
if [ -z `grep "$DDEBS" $DDEBS_LIST` ]; then
	echo "$DDEBS" >> $DDEBS_LIST
fi

#
#  Import the debug symbol archive signing key
#
apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 428D7C01
if [ $? -ne 0 ]; then
	echo "Importing debug symbold archive signing key failed"
	exit 1
fi

apt-get update
if [ $? -ne 0 ]; then
	echo "apt-get update failed"
	exit 1
fi

echo "Installing kernel debug symbols.. will take a while to download.."
apt-get install linux-image-${RELEASE}-dbgsym
if [ $? -ne 0 ]; then
	echo "Installatiobn of linux-image-${RELEASE}-dbgsym failed"
	exit 1
fi

exit 0

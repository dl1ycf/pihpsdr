#!/bin/sh

# Create directory structure
echo Create directory structure
mkdir pihpsdr_debian_aarch64
mkdir pihpsdr_debian_aarch64/DEBIAN

# Create package info
echo Create package info
echo Package: pihpsdr >> pihpsdr_debian_aarch64/DEBIAN/control
echo Version: 2.3.0-1 >> pihpsdr_debian_aarch64/DEBIAN/control
echo Maintainer: dl1ycf >> pihpsdr_debian_aarch64/DEBIAN/control
echo Architecture: all >> pihpsdr_debian_aarch64/DEBIAN/control
echo Description: piHPSDR transreceiver software >> pihpsdr_debian_aarch64/DEBIAN/control
#echo Depends: $2 >> pihpsdr_debian_aarch64/DEBIAN/control

# Copying files
ORIG_DIR=$PWD
cd $1
make install DESTDIR=$ORIG_DIR/pihpsdr_debian_aarch64
cd $ORIG_DIR

# Create package
echo Create package
dpkg-deb --build pihpsdr_debian_aarch64

# Cleanup
echo Cleanup
rm -rf pihpsdr_debian_aarch64

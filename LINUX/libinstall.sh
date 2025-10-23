#!/bin/sh

################################################################
#
# A script to setup everything that needs be done on a
# 'virgin' RaspPi.
#
################################################################

################################################################
#
# a) determine the location of THIS script
#    (this is where the files should be located)
#    and assume this is in the pihpsdr directory
#
################################################################

SCRIPTFILE=`realpath $0`
THISDIR=`dirname $SCRIPTFILE`
TARGET=`dirname $THISDIR`
PIHPSDR=$TARGET/release/pihpsdr

echo
echo "=============================================================="
echo "Script file absolute position  is " $SCRIPTFILE
echo "Pihpsdr target       directory is " $TARGET
echo "Icons and Udev rules  copied from " $PIHPSDR
echo "=============================================================="
echo

################################################################
#
# b) install lots of packages
# (many of them should already be there)
#
################################################################

echo "=============================================================="
echo
echo "... installing LOTS OF compiles/libraries/helpers"
echo
echo "=============================================================="

# ------------------------------------
# Install standard tools and compilers
# ------------------------------------

sudo apt-get --yes install build-essential
sudo apt-get --yes install module-assistant
sudo apt-get --yes install vim
sudo apt-get --yes install make
sudo apt-get --yes install gcc
sudo apt-get --yes install g++
sudo apt-get --yes install gfortran
sudo apt-get --yes install git
sudo apt-get --yes install pkg-config
sudo apt-get --yes install cmake
sudo apt-get --yes install autoconf
sudo apt-get --yes install autopoint
sudo apt-get --yes install gettext
sudo apt-get --yes install automake
sudo apt-get --yes install libtool
sudo apt-get --yes install cppcheck
sudo apt-get --yes install dos2unix
sudo apt-get --yes install libzstd-dev

# ---------------------------------------
# Install libraries necessary for piHPSDR
# ---------------------------------------

sudo apt-get --yes install libfftw3-dev
sudo apt-get --yes install libgtk-3-dev
sudo apt-get --yes install libasound2-dev
sudo apt-get --yes install libssl-dev
sudo apt-get --yes install libcurl4-openssl-dev
sudo apt-get --yes install libusb-1.0-0-dev
sudo apt-get --yes install libi2c-dev
sudo apt-get --yes install libgpiod-dev
sudo apt-get --yes install libpulse-dev
sudo apt-get --yes install pulseaudio
sudo apt-get --yes install libpcap-dev

# ----------------------------------------------
# Install standard libraries necessary for SOAPY
# ----------------------------------------------

sudo apt-get install --yes libaio-dev
sudo apt-get install --yes libavahi-client-dev
sudo apt-get install --yes libad9361-dev
sudo apt-get install --yes libiio-dev
sudo apt-get install --yes bison
sudo apt-get install --yes flex
sudo apt-get install --yes libxml2-dev
sudo apt-get install --yes librtlsdr-dev

################################################################
#
# c) download and install SoapySDR core
#
################################################################

echo "=============================================================="
echo
echo "... installing SoapySDR core"
echo
echo "=============================================================="

cd $THISDIR
yes | rm -r SoapySDR
git clone https://github.com/pothosware/SoapySDR.git

cd $THISDIR/SoapySDR
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
sudo ldconfig

################################################################
#
# d) create desktop icons, start scripts, etc.  for pihpsdr
#
################################################################

echo "=============================================================="
echo
echo "... creating Desktop Icons"
echo
echo "=============================================================="

rm -f $HOME/Desktop/pihpsdr.desktop
rm -f $HOME/.local/share/applications/pihpsdr.desktop

cat <<EOT > $TARGET/pihpsdr.sh
cd $TARGET
$TARGET/pihpsdr >log 2>&1
EOT
chmod +x $TARGET/pihpsdr.sh

cat <<EOT > $TARGET/pihpsdr.desktop
#!/usr/bin/env xdg-open
[Desktop Entry]
Version=1.0
Type=Application
Terminal=false
Name[eb_GB]=piHPSDR
Exec=$TARGET/pihpsdr.sh
Icon=$TARGET/piHPSDR_logo.png
Name=piHPSDR
EOT

cp $TARGET/pihpsdr.desktop $HOME/Desktop
mkdir -p $HOME/.local/share/applications
cp $TARGET/pihpsdr.desktop $HOME/.local/share/applications

cp $PIHPSDR/piHPSDR_logo.png $TARGET

################################################################
#
# ALL DONE.
#
################################################################


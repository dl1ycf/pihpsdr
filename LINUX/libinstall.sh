#!/bin/sh

################################################################
#
# A script to clone wdsp from github and to compile and 
# install it. This is a prerequisite for compiling pihpsdr
#
################################################################

################################################################
#
# a) determine the location of THIS script
#    (this is where the files should be located)
#    and assume this is in the pihpsdr directory
#
################################################################

SCRIPT_FILE=`realpath $0`
THIS_DIR=`dirname $SCRIPT_FILE`
TARGET=`dirname $THIS_DIR`
WORKDIR='/usr/src'

################################################################
#
# b) Clean up from old builds and old installs
#
################################################################

if [[ -f $HOME/Desktop/pihpsdr.desktop ]]; then
	rm -f $HOME/Desktop/pihpsdr.desktop
fi

if [[ -f $HOME/.local/share/applications/pihpsdr.desktop ]]; then
	rm -f $HOME/.local/share/applications/pihpsdr.desktop
fi

################################################################
#
# c) install lots of packages
# (many of them should already be there)
#
################################################################

# ------------------------------------
# Install standard tools and compilers
# ------------------------------------

apt -y install build-essential
apt -y install module-assistant
apt -y install vim
apt -y install make
apt -y install gcc
apt -y install g++
apt -y install gfortran
apt -y install git
apt -y install gpiod
apt -y install pkg-config
apt -y install cmake
apt -y install autoconf
apt -y install autopoint
apt -y install gettext
apt -y install automake
apt -y install libtool
apt -y install cppcheck
apt -y install dos2unix
apt -y install libzstd-dev

# ---------------------------------------
# Install libraries necessary for piHPSDR
# ---------------------------------------

apt -y install libfftw3-dev
apt -y install libgtk-3-dev
apt -y install libasound2-dev
apt -y install libcurl4-openssl-dev
apt -y install libusb-1.0-0-dev
apt -y install libi2c-dev
apt -y install libgpiod-dev
apt -y install libpulse-dev
apt -y install pulseaudio

# ----------------------------------------------
# Install standard libraries necessary for SOAPY
# ----------------------------------------------

apt -y install libaio-dev
apt -y install libavahi-client-dev
apt -y install libad9361-dev
apt -y install libiio-dev
apt -y install bison
apt -y install flex
apt -y install libxml2-dev
apt -y install librtlsdr-dev

# ----------------------------------------------
# Install SOAPYSDR
# ----------------------------------------------
apt -y install libsoapysdr-dev 
apt -y install libsoapysdr0.8
apt -y install soapysdr-module-all

# ----------------------------------------------------
# Install libraries necessary for SOAPYSDR ADALM Pluto
# ----------------------------------------------------
apt -y install libiio-dev

################################################################
#
# d) download and install WDSP
#
################################################################
cd $WORKDIR
yes | rm -rf wdsp
git clone https://github.com/dl1ycf/wdsp

cd $WORKDIR/wdsp
make -j 4
make install
ldconfig

################################################################
#
# e) download and install Soapy for Adalm Pluto
#
################################################################

cd $WORKDIR
yes | rm -rf SoapyPlutoSDR
git clone https://github.com/pothosware/SoapyPlutoSDR

cd $WORKDIR/SoapyPlutoSDR
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j 4
make install
ldconfig

echo " Libs Install Done "
#!/bin/sh

#####################################################
#
# prepeare your Macintosh for compiling piHPSDR
#
######################################################

################################################################
#
# a) MacOS does not have "realpath" so we need to fiddle around
#
################################################################

THISDIR="$(cd "$(dirname "$0")" && pwd -P)"

################################################################
#
# b) Initialize HomeBrew and required packages
#    (this does no harm if HomeBrew is already installed)
#
################################################################
  
#
# This installs the "command line tools", these are necessary to install the
# homebrew universe
#
xcode-select --install

#
# This installes the core of the homebrew universe
#
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"

#
# At this point, there is a "brew" command either in /usr/local/bin (Intel Mac) or in
# /opt/homebrew/bin (Silicon Mac). Look what applies
#
BREW=junk

if [ -x /usr/local/bin/brew ]; then
  BREW=/usr/local/bin/brew
fi

if [ -x /opt/homebrew/bin/brew ]; then
  BREW=/opt/homebrew/bin/brew
fi

if [ $BREW == "junk" ]; then
  echo HomeBrew installation obviously failed, exiting
  exit
fi

################################################################
#
# This adjusts the PATH. This is not bullet-proof, so if some-
# thing goes wrong here, the user will later not find the
# 'brew' command.
#
################################################################

if [ $SHELL == "/bin/sh" ]; then
$BREW shellenv sh >> $HOME/.profile
fi
if [ $SHELL == "/bin/csh" ]; then
$BREW shellenv csh >> $HOME/.cshrc
fi
if [ $SHELL == "/bin/zsh" ]; then
$BREW shellenv zsh >> $HOME/.zprofile
fi

################################################################
#
# create links in /usr/local if necessary (only if
# HomeBrew is installed in /opt/homebrew)
#
# Should be done HERE if some of the following packages
# have to be compiled from the sources
#
# Note existing DIRECTORIES in /usr/local will not be deleted,
# the "rm" commands only remove symbolic links should they
# already exist.
################################################################

if [ ! -d /usr/local/lib ]; then
  echo "/usr/local/lib does not exist, creating symbolic link ..."
  sudo rm -f /usr/local/lib
  sudo ln -s /opt/homebrew/lib /usr/local/lib
fi
if [ ! -d /usr/local/bin ]; then
  echo "/usr/local/bin does not exist, creating symbolic link ..."
  sudo rm -f /usr/local/bin
  sudo ln -s /opt/homebrew/bin /usr/local/bin
fi
if [ ! -d /usr/local/include ]; then
  echo "/usr/local/include does not exist, creating symbolic link ..."
  sudo rm -f /usr/local/include
  sudo ln -s /opt/homebrew/include /usr/local/include
fi

################################################################
#
# All homebrew packages needed for pihpsdr
#
################################################################
$BREW install gtk+3
$BREW install librsvg
$BREW install pkg-config
$BREW install portaudio
$BREW install fftw
$BREW install libusb

################################################################
#
# This is for the SoapySDR universe
# There are even more radios supported for which you need
# additional modules, for a list, goto the web page
# https://formulae.brew.sh
# and insert the search string "pothosware". In the long
# list produced, search for the same string using the
# "search" facility of your internet browser
#
$BREW install cmake
$BREW install python-setuptools
#
# If an older version of SoapySDR exist, a forced
# re-install may be necessary (note parts of this
# is always compiled from the sources).
#
$BREW reinstall soapysdr
$BREW reinstall pothosware/pothos/soapyplutosdr
$BREW reinstall pothosware/pothos/limesuite
$BREW reinstall pothosware/pothos/soapyrtlsdr
$BREW reinstall pothosware/pothos/soapyairspy
$BREW reinstall pothosware/pothos/soapyairspyhf
$BREW reinstall pothosware/pothos/soapyhackrf
$BREW reinstall pothosware/pothos/soapyredpitaya
$BREW reinstall pothosware/pothos/soapyrtlsdr

################################################################
#
# This is for PrivacyProtection
#
################################################################
$BREW analytics off


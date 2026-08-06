#!/bin/sh


#####################################################
# It has been reported that on some systems,
# /bin/sh is not present but /bin/zsh is. While I
# regard this as a misconfiguration, what you can do
# in this case is to change the first line of this
# file as to read #!/bin/zsh
#
# Note we cannot use "env" since this must also work
# for users (like me) that use csh.
#
#####################################################
#
# This shell script prepeares your Macintosh for
# compiling piHPSDR. To this end,
#
# - the HOMEBREW universe is initialized
# - lots of HOMEBREW libraries are installes
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
# This installes the core of the homebrew universe, if it is not already present
#
if [ -x /usr/local/bin/brew ] || [ -x /opt/homebrew/bin/brew ]; then
  echo "=============================================================="
  echo
  echo "... HomeBrew core already installed"
  echo
  echo "=============================================================="
else
  echo "=============================================================="
  echo
  echo "... installing HomeBrew core"
  echo
  echo "=============================================================="
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
fi

#
# At this point, there is a "brew" command either in /usr/local/bin (Intel Mac) or in
# /opt/homebrew/bin (Silicon Mac). Look what applies, and set the variable PREFIX
# accordingly (either /opt/homebrew or /usr/local).
#
BREW=junk
PREFIX=junk

if [ -x /usr/local/bin/brew ]; then
  BREW=/usr/local/bin/brew
  PREFIX=/usr/local
fi

if [ -x /opt/homebrew/bin/brew ]; then
  BREW=/opt/homebrew/bin/brew
  PREFIX=/opt/homebrew
fi

if [ $BREW == "junk" ]; then
  echo HomeBrew installation obviously failed, exiting
  exit
fi

#
# Prepeare the environment variables CPATH and LIBRARY_PATH. These are usually not needed
# for Intel Macs, but if "homebrew" is installed in /opt/homebrew, these guarantee
# that include files are found by the preprocessor, and libraries are found by the linker.
#
if [ z$CPATH == z ]; then
  export CPATH=$PREFIX/include
else
  export CPATH=$CPATH:$PREFIX/include
fi
if [ z$LIBRARY_PATH == z ]; then
  export LIBRARY_PATH=$PREFIX/lib
else
  export LIBRARY_PATH=$LIBRARY_PATH:$PREFIX/lib
fi

################################################################
#
# This adjusts the PATH in the shell startup file, together with
# CPATH and LIBRARY_PATH. This is not bullet-proof, so if some-
# thing goes wrong here, the user will later not find the
# 'brew' command. 
#
################################################################

if [ $SHELL == "/bin/sh" ]; then
$BREW shellenv sh >> $HOME/.profile
echo "export CPATH=$CPATH" >> $HOME/.profile
echo "export LIBRARY_PATH=$LIBRARY_PATH" >> $HOME/.profile
fi

if [ $SHELL == "/bin/csh" ]; then
$BREW shellenv csh >> $HOME/.cshrc
echo "setenv CPATH $CPATH" >> $HOME/.cshrc
echo "setenv LIBRARY_PATH $LIBRARY_PATH" >> $HOME/.cshrc
fi
if [ $SHELL == "/bin/zsh" ]; then
$BREW shellenv zsh >> $HOME/.zprofile
echo "export CPATH=$CPATH" >> $HOME/.zprofile
echo "export LIBRARY_PATH=$LIBRARY_PATH" >> $HOME/.zprofile
fi

export HOMEBREW_NO_ASK=yes
$BREW update
################################################################
#
# All homebrew packages needed for pihpsdr and for compilation
# of the SoapySDR core. Some packages such as cppcheck and
# makedepend are not required, but useful for maintainers.
#
################################################################
echo "=============================================================="
echo
echo "... installing needed HomeBrew packages"
echo
echo "=============================================================="
$BREW install gtk+3
$BREW install librsvg
$BREW install pkg-config
$BREW install portaudio
$BREW install fftw
$BREW install libusb
$BREW install makedepend
$BREW install cppcheck
$BREW install openssl@3
$BREW install opus
$BREW install miniupnpc
$BREW install libwebsockets
$BREW install zlib
$BREW install git-extras
$BREW install cmake
$BREW install python-setuptools
$BREW install librtlsdr
$BREW install hackrf
$BREW install libserialport

################################################################
#
# This is for PrivacyProtection
#
################################################################
$BREW analytics off


################################################################
#
# Since "pothosware" is now untrusted, compile SOAPY stuff
# from the sources.
#
################################################################

echo "=============================================================="
echo
echo "... compiling and installing SoapySDR core"
echo
echo "=============================================================="

cd $THISDIR
yes | rm -r SoapySDR
git clone https://github.com/pothosware/SoapySDR.git

cd $THISDIR/SoapySDR
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$PREFIX ..
make
make install

cd $THISDIR
yes | rm -r SoapySDR
#
# Replace @rpath entries by the real file names
#
for i in `find $PREFIX/lib -type f -depth 1 -name "libSoapySDR*" -print`; do
install_name_tool -id $i $i
done


echo "=============================================================="
echo
echo "... compiling and installing SoapySDR RTL-stick libraries"
echo
echo "=============================================================="

cd $THISDIR
yes | rm -rf SoapyRTLSDR
git clone https://github.com/pothosware/SoapyRTLSDR

cd $THISDIR/SoapyRTLSDR
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$PREFIX ..
make
make install

cd $THISDIR
yes | rm -rf SoapyRTLSDR

echo "=============================================================="
echo
echo "... compiling and installing HackRF SoapySDR support"
echo
echo "=============================================================="

cd $THISDIR
yes | rm -rf SoapyHackRF
git clone https://github.com/pothosware/SoapyHackRF.git

cd $THISDIR/SoapyHackRF
cd SoapyHackRF
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$PREFIX ..
make
make install

cd $THISDIR
yes | rm -rf SoapyHackRF

echo "=============================================================="
echo
echo "... compiling and installing SoapySDR AdalmPluto libraries"
echo "    and prerequisites (libiio-v0, libad9361-ii0-v0)"
echo
echo "=============================================================="

cd $THISDIR
yes | rm -rf libiio
git clone https://github.com/analogdevicesinc/libiio.git

cd $THISDIR/libiio
git checkout libiio-v0
mkdir build
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$PREFIX \
      -DOSX_FRAMEWORK=OFF -DOSX_PACKAGE=OFF -DWITH_DOC=OFF -DWITH_TESTS=OFF \
      -DWITH_EXAMPLES=OFF -DPYTHON_BINDINGS=OFF -DWITH_NETWORK_BACKEND=ON -DWITH_USB_BACKEND=ON \
      -DWITH_XML_BACKEND=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build
cmake --install build

cd $THISDIR
yes | rm -rf libiio
#
# Replace @rpath entries by the real file names
#
for i in `find $PREFIX/lib -type f -depth 1 -name "libiio*" -print`; do
install_name_tool -id $i $i
done

#
# A mis-configuration within libad9361 wants to include <iio/iio.h> rather than <iio.h> if __APPLE__ is
# defined. As a Q&D fix. create a symolic link in $PREFIX/include named iio that points to $PREFIX/include
#
rm -rf $PREFIX/include/iio
ln -s $PREFIX/include $PREFIX/include/iio

cd $THISDIR
yes | rm -rf libad9361-iio
git clone https://github.com/analogdevicesinc/libad9361-iio.git

cd $THISDIR/libad9361-iio
git checkout libad9361-iio-v0
mkdir build
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$PREFIX \
                    -DOSX_PACKAGE=OFF -DOSX_FRAMEWORK=OFF \
                     -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build
cmake --install build

cd $THISDIR
yes | rm -rf libad9361-iio
#
# Replace @rpath entries by the real file names
#
for i in `find $PREFIX/lib -type f -depth 1 -name "libad9361*" -print`; do
install_name_tool -id $i $i
done

cd $THISDIR
yes | rm -rf SoapyPlutoSDR
git clone https://github.com/pothosware/SoapyPlutoSDR

cd $THISDIR/SoapyPlutoSDR
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$PREFIX ..
make
make install

cd $THISDIR
yes | rm -rf SoapyPlutoSDR

echo "=============================================================="
echo
echo " MacOS libinstall done."
echo
echo "=============================================================="

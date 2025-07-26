#!/bin/zsh

echo "🔍 Checking for XQuartz..."
if [[ $(uname -m) == "arm64" ]]; then
  XQUARTZ_BIN="/opt/X11/bin/Xquartz"
else
  XQUARTZ_BIN="/usr/X11/bin/Xquartz"
fi

if [ -x "$XQUARTZ_BIN" ]; then
  echo "✅ XQuartz is already installed."
else
  echo "❌ XQuartz not found. Please download and install it from https://www.xquartz.org/ (version 2.8.5 recommended)."
  echo "🌐 Opening download page in your browser..."
  open "https://www.xquartz.org/"
  echo "⏳ Once XQuartz is installed, please re-run this script."
  exit 1
fi

echo "\n🔍 Checking for Xcode Command Line Tools..."
if ! xcode-select -p &>/dev/null; then
  echo "🛠️ Xcode Command Line Tools not found. Installing..."
  xcode-select --install
  echo "⏳ Please complete the Xcode CLI installation manually if prompted, then re-run this script."
  exit 1
else
  echo "✅ Xcode Command Line Tools are already installed."
fi

echo "\n🔍 Checking for Homebrew..."
if ! command -v brew &>/dev/null; then
  echo "🍺 Homebrew not found. Installing..."
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

  # Add Homebrew to PATH based on system architecture
  if [[ $(uname -m) == "arm64" ]]; then
    echo "📦 Detected Apple Silicon. Setting Homebrew path for /opt/homebrew..."
    eval "$(/opt/homebrew/bin/brew shellenv)"
  elif [[ $(uname -m) == "x86_64" ]]; then
    echo "📦 Detected Intel. Setting Homebrew path for /usr/local..."
    eval "$(/usr/local/bin/brew shellenv)"
  fi
else
  echo "✅ Homebrew is already installed."
fi

echo "\n🔄 Updating Homebrew..."
brew update
brew upgrade

echo "\n📦 Installing required packages with Homebrew..."
packages=(gtk+3 librsvg pkg-config portaudio fftw libusb makedepend cppcheck openssl@3 cmake python-setuptools soapysdr)

for pkg in $packages; do
  if brew list --formula | grep -q "^$pkg\$"; then
    echo "🔁 Reinstalling $pkg..."
    brew reinstall $pkg
  else
    echo "⬇️ Installing $pkg..."
    brew install $pkg
  fi
done

soapypackages=(pothosware/pothos/soapyplutosdr pothosware/pothos/limesuite pothosware/pothos/soapyrtlsdr pothosware/pothos/soapyairspy pothosware/pothos/soapyhackrf pothosware/pothos/soapyredpitaya)

echo "\nSkipping install of SoapySDR packages as they no longer compile on the latest version of CMake and macOS Operating Systems.  Check https://github.com/pothosware/homebrew-pothos/issues to see if the issues have been resolved."

echo "\nOnce the issues are resolved, open this file with TextEdit and remove the # on the lines after the \"Remove the # to enable for SoapySDR plugins\" comment."

####################################################
# Remove the # to enable for SoapySDR plugins
####################################################

# brew tap pothosware/pothos

# for soapypkg in $soapypackages; do
#   if brew list --formula | grep -q "^$soapypkg\$"; then
#     echo "🔁 Reinstalling $soapypkg..."
#     brew reinstall $soapypkg
#   else
#     echo "⬇️ Installing $soapypkg..."
#     brew install $soapypkg
#   fi
# done

echo "\n🎉 All done!"
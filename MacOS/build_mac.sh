#!/bin/zsh


# Exit immediately if a command exits with a non-zero status
set -e
# Trap errors and report the line number and command that failed
trap 'echo "❌ Error on line $LINENO: $BASH_COMMAND"; exit 1' ERR

# Reminder to run mac_install.sh before proceeding
echo "\n⚠️ Please ensure you've run mac_install.sh and it has completed all steps successfully before continuing."

 # Set default values
VERSION="REL"
UPGRADE="n"

# Use defaults if no arguments are provided
if [[ $# -eq 0 ]]; then
  echo "\nℹ️ No arguments provided. Using defaults: version=REL, upgrade=n"
else
  for arg in "$@"; do
    case $arg in
      version=*)
        VERSION="${arg#*=}"
        shift
        ;;
      upgrade=*)
        UPGRADE="${arg#*=}"
        shift
        ;;
      *)
        echo "❌ Unknown argument: $arg"
        echo "Usage: ./build_mac.sh version=REL|DEV upgrade=y|n"
        exit 1
        ;;
    esac
  done
fi

# Validate arguments
if [[ "$VERSION" != "REL" && "$VERSION" != "DEV" ]]; then
  echo "\n❌ Invalid version value. Use version=REL or version=DEV"
  exit 1
fi

if [[ "$UPGRADE" != "y" && "$UPGRADE" != "n" ]]; then
  echo "\n❌ Invalid upgrade value. Use upgrade=y or upgrade=n"
  exit 1
fi

echo "\n🚀 Building pihpsdr with version: $VERSION and upgrade: $UPGRADE"

# Perform upgrade if requested
if [[ "$UPGRADE" == "y" ]]; then
  echo "\n🔄 Performing git pull and submodule update..."
  if ! git pull; then
    echo "\n❌ Failed to pull latest changes."
    exit 1
  fi
  if ! git submodule update --recursive; then
    echo "\n❌ Failed to update submodules."
    exit 1
  fi
  echo "🔧 Re-running mac_install.sh after upgrade..."
  if ! ./mac_install.sh; then
    echo "\n❌ mac_install.sh failed after upgrade. Aborting build."
    exit 1
  fi
fi

# Checkout appropriate git branch
if [[ "$VERSION" == "REL" ]]; then
  echo "\n📦 Checking out release branch Rel-2.4..."
  if ! git checkout Rel-2.4; then
    echo "\n❌ Failed to checkout Rel-2.4 branch."
    exit 1
  fi
elif [[ "$VERSION" == "DEV" ]]; then
  echo "\n📦 Checking out development branch master..."
  if ! git checkout master; then
    echo "\n❌ Failed to checkout master branch."
    exit 1
  fi
fi

# Assume this script is running in the MacOS subdirectory of the Git repository, so
# cd up a level to where the Makefile is in the root directory
cd ..

echo "\n🧹 Running make clean..."
if ! make clean; then
  echo "\n❌ make clean failed."
  exit 1
fi

echo "\n🏗️ Building application with make app..."
if ! make app; then
  echo "\n❌ make app failed."
  exit 1
fi

if [[ -d "/Applications/pihpsdr.app" ]]; then
  echo "\n⚠️ pihpsdr.app already exists in /Applications. Do you want to replace it? (y/n): "
  read CONFIRM_DELETE
  if [[ "$CONFIRM_DELETE" != "y" ]]; then
    echo "🚫 Aborting installation to avoid overwriting existing application."
    exit 0
  fi
  echo "\n🗑️ Removing existing pihpsdr.app from /Applications..."
  if ! rm -rf /Applications/pihpsdr.app; then
    echo "\n❌ Failed to remove existing pihpsdr.app from /Applications."
    exit 1
  fi
fi
echo "\n📦 Moving pihpsdr.app to /Applications..."
if ! mv pihpsdr.app /Applications; then
  echo "\n❌ Failed to move pihpsdr.app to /Applications."
  exit 1
fi

# Prompt for desktop alias
echo "\n📎 Would you like to create a desktop alias for pihpsdr.app? (y/n): "
read CREATE_ALIAS
if [[ "$CREATE_ALIAS" == "y" ]]; then
  APP_PATH="/Applications/pihpsdr.app"
  DESKTOP_PATH="$HOME/Desktop"

  echo "📁 Creating desktop alias..."
  if ! osascript -e "tell application \"Finder\" to make alias file to POSIX file \"$APP_PATH\" at POSIX file \"$DESKTOP_PATH\""; then
    echo "❌ Failed to create desktop alias."
    echo "💡 You can manually create an alias by dragging pihpsdr.app from /Applications to your Desktop."
    exit 1
  fi
fi
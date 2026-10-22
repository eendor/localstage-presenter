#!/bin/sh
# Start LocalStage (release download) and open the presenter page.
# PDFs work out of the box. PowerPoint files need LibreOffice; this offers to install it once.
cd "$(dirname "$0")" || exit 1
mark="${XDG_CONFIG_HOME:-$HOME/.config}/localstage/asked-libreoffice"
office=""
command -v soffice >/dev/null && office=1
[ -x /Applications/LibreOffice.app/Contents/MacOS/soffice ] && office=1
if [ -z "$office" ] && [ ! -f "$mark" ] && [ -t 0 ]; then
  mkdir -p "$(dirname "$mark")" && touch "$mark"
  printf 'Install LibreOffice so you can present PowerPoint files? PDFs already work. [y/N] '
  read -r answer
  case $answer in
    y|Y)
      if command -v brew >/dev/null; then brew install --cask libreoffice
      elif command -v apt-get >/dev/null; then sudo apt-get install -y libreoffice-impress
      elif command -v dnf >/dev/null; then sudo dnf install -y libreoffice-impress
      elif command -v pacman >/dev/null; then sudo pacman -S --needed libreoffice-fresh
      elif command -v zypper >/dev/null; then sudo zypper install -y libreoffice-impress
      else echo "Please install LibreOffice from https://www.libreoffice.org/"; fi ;;
  esac
fi
OPEN_BROWSER=1 exec ./localstage

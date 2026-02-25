#!/bin/bash
# RTHextion Linux installer
# Downloads and installs RTHextion from GitHub releases

set -e

# Color output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}RTHextion Linux Installer${NC}"
echo

# Determine install location
INSTALL_DIR="${HOME}/.local/opt/rthextion"
BIN_LINK="${HOME}/.local/bin/rthextion"

# Create directories
mkdir -p "${INSTALL_DIR}" "${HOME}/.local/bin"

echo "Install location: ${INSTALL_DIR}"
echo

# Get latest release
echo -e "${BLUE}Fetching latest release...${NC}"
RELEASE_URL=$(curl -s https://api.github.com/repos/road-t/RTHextion/releases/latest | \
  grep "browser_download_url.*Linux-x86_64.tar.gz" | \
  cut -d'"' -f4)

if [ -z "$RELEASE_URL" ]; then
  echo -e "${RED}Error: Could not find latest Linux release${NC}"
  exit 1
fi

TARBALL=$(basename "$RELEASE_URL")
echo -e "${GREEN}Downloading: $TARBALL${NC}"
curl -L -o "/tmp/${TARBALL}" "$RELEASE_URL"

# Extract
echo -e "${BLUE}Extracting...${NC}"
tar -xzf "/tmp/${TARBALL}" -C "${INSTALL_DIR}"
rm "/tmp/${TARBALL}"

# Find the rthextion binary
BINARY="${INSTALL_DIR}/usr/bin/rthextion"
if [ ! -f "$BINARY" ]; then
  echo -e "${RED}Error: Could not find rthextion binary after extraction${NC}"
  exit 1
fi

# Make binary executable
chmod +x "$BINARY"

# Create symlink for convenience
ln -sf "$BINARY" "$BIN_LINK"
chmod +x "$BIN_LINK"

echo
echo -e "${GREEN}Installation complete!${NC}"
echo
echo "You can now run RTHextion with:"
echo "  rthextion"
echo
echo "Or directly:"
echo "  ${BINARY}"
echo
echo "To uninstall, run:"
echo "  rm -rf ${INSTALL_DIR} ${BIN_LINK}"

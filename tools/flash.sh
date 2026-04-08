#!/bin/bash
# ESP32OS Build, Flash & Monitor Helper
# Usage: ./tools/flash.sh [PORT] [BAUD]
# Example: ./tools/flash.sh /dev/ttyUSB0 115200

set -e

PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-115200}"
TARGET="${TARGET:-esp32}"

# Colours
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}  ESP32OS Build & Flash Tool${NC}"
echo -e "${CYAN}  Target: ${TARGET}  Port: ${PORT}  Baud: ${BAUD}${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Check IDF environment
if [ -z "$IDF_PATH" ]; then
    echo -e "${RED}Error: IDF_PATH not set. Run: . \$IDF_PATH/export.sh${NC}"
    exit 1
fi

# Check port exists
if [ ! -e "$PORT" ]; then
    echo -e "${RED}Error: Port $PORT not found.${NC}"
    echo "Available ports:"
    ls /dev/ttyUSB* /dev/ttyACM* /dev/cu.* 2>/dev/null || echo "  (none found)"
    exit 1
fi

# Set target
echo -e "\n${CYAN}[1/3] Setting target: ${TARGET}${NC}"
idf.py set-target "$TARGET"

# Build
echo -e "\n${CYAN}[2/3] Building...${NC}"
idf.py build

# Flash
echo -e "\n${CYAN}[3/3] Flashing to ${PORT}...${NC}"
idf.py -p "$PORT" -b 921600 flash

# Monitor
echo -e "\n${GREEN}Flash complete! Starting monitor (Ctrl+] to exit)...${NC}\n"
idf.py -p "$PORT" -b "$BAUD" monitor

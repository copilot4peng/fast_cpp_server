#!/bin/bash
# Thin wrapper for install - delegates to mode-specific script
set -euo pipefail

MODE="system"   # system | user
DEBUG=false

# Parse arguments
for arg in "$@"; do
  case "$arg" in
    --debug) DEBUG=true ;;
    --user) MODE="user" ;;
    --system) MODE="system" ;;
    --help|-h)
      echo "Usage: $0 [--system|--user] [--debug]"
      echo "  --system  Install for system (requires sudo, uses systemd)"
      echo "  --user    Install for current user (no sudo, no systemd)"
      echo "  --debug   Dry-run mode"
      exit 0
      ;;
  esac
done

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Delegate to mode-specific script
if [ "${MODE}" == "system" ]; then
  export DEBUG
  exec "${SCRIPT_DIR}/install-system.sh" "$@"
else
  export DEBUG
  exec "${SCRIPT_DIR}/install-user.sh" "$@"
fi
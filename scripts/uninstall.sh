#!/bin/bash
# Thin wrapper for uninstall - delegates to mode-specific script
set -euo pipefail

MODE="system"
DEBUG=false

# Parse arguments
for arg in "$@"; do
  case "$arg" in
    --debug) DEBUG=true ;;
    --user) MODE="user" ;;
    --system) MODE="system" ;;
    --help|-h)
      echo "Usage: $0 [--system|--user] [--debug]"
      echo "  --system  Uninstall from system (requires sudo)"
      echo "  --user    Uninstall from user directories"
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
  exec "${SCRIPT_DIR}/uninstall-system.sh" "$@"
else
  export DEBUG
  exec "${SCRIPT_DIR}/uninstall-user.sh" "$@"
fi
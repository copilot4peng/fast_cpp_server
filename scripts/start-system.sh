#!/bin/bash
# System mode start script - starts the core program once and waits
set -euo pipefail

PROGRAM_NAME="fast_cpp_server"
DEBUG=${DEBUG:-false}

# System paths
SYS_BIN_DIR="/usr/local/bin/fast_cpp_server_dir"
SYS_LIB_DIR="/usr/local/lib/fast_cpp_server"
SYS_CONFIG_DIR="/etc/fast_cpp_server"
APP_BIN="${SYS_BIN_DIR}/fast_cpp_server"
RUNTIME_LOG_FILE="/var/fast_cpp_server/logs/fast_cpp_server.log"

# Script logging (for script execution logs)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/logs"
TS="$(date '+%Y%m%d_%H%M%S')"
SCRIPT_LOG_FILE="${LOG_DIR}/start_system_${TS}.log"

# Colors
C_RESET="\033[0m"
C_GREEN="\033[32m"
C_YELLOW="\033[33m"
C_RED="\033[31m"
C_MAGENTA="\033[35m"
C_CYAN="\033[36m"

tag() {
  case "${1:-}" in
    dev)  echo -e "${C_MAGENTA}[dev]${C_RESET}" ;;
    pro)  echo -e "${C_GREEN}[pro]${C_RESET}" ;;
    info) echo -e "${C_CYAN}[info]${C_RESET}" ;;
    warn) echo -e "${C_YELLOW}[warn]${C_RESET}" ;;
    err)  echo -e "${C_RED}[err]${C_RESET}" ;;
    *)    echo -e "[log]" ;;
  esac
}

log_line() {
  local level="$1"; shift
  local msg="$*"
  mkdir -p "${LOG_DIR}" >/dev/null 2>&1 || true
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] [${level}] ${msg}" >> "${SCRIPT_LOG_FILE}" 2>/dev/null || true
  echo -e "$(tag "${level}") ${msg}"
}

# Parse args
for arg in "$@"; do
  case "$arg" in
    --debug) DEBUG=true ;;
  esac
done

log_line info "Start (system mode) begin: DEBUG=${DEBUG}"
log_line info "Script log: ${SCRIPT_LOG_FILE}"

# Print paths
log_line info "========== Runtime Paths =========="
log_line info "APP_BIN:          ${APP_BIN}"
log_line info "SYS_LIB_DIR:      ${SYS_LIB_DIR}"
log_line info "SYS_CONFIG_DIR:   ${SYS_CONFIG_DIR}"
log_line info "RUNTIME_LOG_FILE: ${RUNTIME_LOG_FILE}"
log_line info "==================================="

# Setup environment
export LD_LIBRARY_PATH="${SYS_LIB_DIR}:${LD_LIBRARY_PATH:-}"

# Ensure log directory exists
mkdir -p "$(dirname "${RUNTIME_LOG_FILE}")" || true

# Cleanup on exit/signal - forward signals to child process
cleanup() {
  log_line warn "Received stop signal, cleaning up..."
  if [ -n "${APP_PID:-}" ]; then
    log_line info "Sending SIGTERM to PID=${APP_PID}"
    kill -SIGTERM "${APP_PID}" 2>/dev/null || true
    wait "${APP_PID}" 2>/dev/null || true
  fi
  log_line info "Exiting."
  exit 0
}
trap cleanup SIGINT SIGTERM

log_line info "Starting ${PROGRAM_NAME}..."

if $DEBUG; then
  log_line dev "Would run: \"${APP_BIN}\""
  log_line dev "Would redirect to: ${RUNTIME_LOG_FILE} and journald"
  log_line dev "Would wait on PID"
  exit 0
fi

# Check if systemd-cat is available for journald integration
if command -v systemd-cat >/dev/null 2>&1; then
  log_line info "Using systemd-cat for journald integration"
  # Start the app - tee to both log file (append mode) and systemd-cat for journald
  "${APP_BIN}" 2>&1 | tee -a "${RUNTIME_LOG_FILE}" | systemd-cat -t "${PROGRAM_NAME}" &
  APP_PID=$!
else
  log_line warn "systemd-cat not available, logging to file only"
  # Start the app - append to log file
  "${APP_BIN}" >> "${RUNTIME_LOG_FILE}" 2>&1 &
  APP_PID=$!
fi

log_line info "Started ${PROGRAM_NAME} with PID=${APP_PID}"

# Wait for the process to exit
wait "${APP_PID}" || true
EXIT_CODE=$?

log_line info "${PROGRAM_NAME} exited with code=${EXIT_CODE}"
exit ${EXIT_CODE}

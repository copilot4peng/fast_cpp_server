#!/bin/bash
set -euo pipefail

PROGRAM_NAME="fast_cpp_server"

DEBUG=${DEBUG:-false}

# user paths
USER_PREFIX="${HOME}/.local/${PROGRAM_NAME}"
USER_CONFIG_PATH="${HOME}/.config/${PROGRAM_NAME}"
USER_DATA_ROOT="${HOME}/.local/share/${PROGRAM_NAME}"
USER_CACHE_ROOT="${HOME}/.cache/${PROGRAM_NAME}"

# logging
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/logs"
TS="$(date '+%Y%m%d_%H%M%S')"
LOG_FILE="${LOG_DIR}/uninstall_user_${TS}.log"

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
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] [${level}] ${msg}" >> "${LOG_FILE}" 2>/dev/null || true
  echo -e "$(tag "${level}") ${msg}"
}

run_cmd() {
  local cmd="$*"
  if $DEBUG; then
    log_line dev "${cmd}"
    return 0
  else
    log_line pro "${cmd}"
    set +e
    bash -c "${cmd}" >> "${LOG_FILE}" 2>&1
    local rc=$?
    set -e
    if [ $rc -ne 0 ]; then
      log_line err "Command failed (rc=${rc}): ${cmd}"
      log_line err "See log: ${LOG_FILE}"
      exit $rc
    fi
    return 0
  fi
}

for arg in "$@"; do
  case "$arg" in
    --debug) DEBUG=true ;;
  esac
done

log_line info "User Uninstall begin: DEBUG=${DEBUG}"
log_line info "Log file: ${LOG_FILE}"

# Print paths
log_line info "========== Uninstallation Paths =========="
log_line info "USER_PREFIX:      ${USER_PREFIX}"
log_line info "USER_CONFIG_PATH: ${USER_CONFIG_PATH}"
log_line info "USER_DATA_ROOT:   ${USER_DATA_ROOT}"
log_line info "USER_CACHE_ROOT:  ${USER_CACHE_ROOT}"
log_line info "=========================================="

log_line info "Remove user files"
run_cmd "rm -rf ${USER_PREFIX} || true"
run_cmd "rm -rf ${USER_CONFIG_PATH} || true"
run_cmd "rm -rf ${USER_DATA_ROOT} || true"
run_cmd "rm -rf ${USER_CACHE_ROOT} || true"

log_line info "Uninstall done (user)."
log_line info "Uninstall finished successfully."

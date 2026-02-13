#!/bin/bash
set -euo pipefail

PROGRAM_NAME="fast_cpp_server"
MQTT_PROGRAM_NAME="mosquitto"

DEBUG=${DEBUG:-false}
SUPER="sudo"

# -------- system paths (need root) --------
INSTALL_PATH="/usr/local"
BIN_FOLDER_PATH="/usr/local/bin/${PROGRAM_NAME}_dir"
LIB_PATH="${INSTALL_PATH}/lib/${PROGRAM_NAME}"
CONFIG_PATH="/etc/${PROGRAM_NAME}"
LOG_PATH="/var/${PROGRAM_NAME}/logs"
TEMP_DIR="/tmp/${PROGRAM_NAME}"
SHARE_DIR="/usr/share/${PROGRAM_NAME}"
SERVICE_PATH="/etc/systemd/system"

# -------- logging --------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/logs"
TS="$(date '+%Y%m%d_%H%M%S')"
LOG_FILE="${LOG_DIR}/install_system_${TS}.log"

# colors
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

need_sudo_or_die() {
  if $DEBUG; then return 0; fi
  if ! sudo -n true 2>/dev/null; then
    log_line err "sudo is not available (possibly no_new_privileges)."
    log_line err "Use: ./install.sh --user"
    exit 1
  fi
}

# Parse args
for arg in "$@"; do
  case "$arg" in
    --debug) DEBUG=true ;;
  esac
done

log_line info "System Install begin: PROGRAM_NAME=${PROGRAM_NAME}, DEBUG=${DEBUG}"
log_line info "Release dir: ${SCRIPT_DIR}"
log_line info "Log file: ${LOG_FILE}"

# Print all paths that will be used
log_line info "========== Installation Paths =========="
log_line info "BIN_FOLDER_PATH:  ${BIN_FOLDER_PATH}"
log_line info "LIB_PATH:         ${LIB_PATH}"
log_line info "CONFIG_PATH:      ${CONFIG_PATH}"
log_line info "LOG_PATH:         ${LOG_PATH}"
log_line info "TEMP_DIR:         ${TEMP_DIR}"
log_line info "SHARE_DIR:        ${SHARE_DIR}"
log_line info "SERVICE_PATH:     ${SERVICE_PATH}"
log_line info "========================================"



log_line info "Searching for mosquitto binary..."
MQTT_SRC=""
if [ -f "./bin/${MQTT_PROGRAM_NAME}" ]; then
  MQTT_SRC="./bin/${MQTT_PROGRAM_NAME}"
  log_line info "Found mosquitto in ./bin/"
elif [ -f "./sbin/${MQTT_PROGRAM_NAME}" ]; then
  MQTT_SRC="./sbin/${MQTT_PROGRAM_NAME}"
  log_line info "Found mosquitto in ./sbin/"
else
  if command -v ${MQTT_PROGRAM_NAME} >/dev/null 2>&1; then
    MQTT_SRC="$(command -v ${MQTT_PROGRAM_NAME})"
  fi
fi 

need_sudo_or_die

log_line info "Stopping existing service if any..."
run_cmd "${SUPER} systemctl stop ${PROGRAM_NAME}.service 2>/dev/null || true"
run_cmd "${SUPER} systemctl disable ${PROGRAM_NAME}.service 2>/dev/null || true"

log_line info "Prepare log dir: ${LOG_PATH}"
run_cmd "${SUPER} mkdir -p ${LOG_PATH}"
run_cmd "${SUPER} chmod 777 ${LOG_PATH} || true"

log_line info "Prepare config dir: ${CONFIG_PATH}"
run_cmd "${SUPER} mkdir -p ${CONFIG_PATH}"

log_line info "Install configs: ./config/* -> ${CONFIG_PATH}/"
run_cmd "${SUPER} cp -r ./config/* ${CONFIG_PATH}/"
run_cmd "${SUPER} chmod 644 ${CONFIG_PATH}/* || true"

# Ensure /etc/fast_cpp_server/config.ini exists (program reads config.ini)
if [ -f "./config/config.system.ini" ]; then
  log_line info "Ensure system config.ini: ./config/config.system.ini -> ${CONFIG_PATH}/config.ini"
  run_cmd "${SUPER} cp ./config/config.system.ini ${CONFIG_PATH}/config.ini"
  run_cmd "${SUPER} chmod 644 ${CONFIG_PATH}/config.ini || true"
else
  log_line warn "Missing ./config/config.system.ini (skip generating ${CONFIG_PATH}/config.ini)"
fi

log_line info "Prepare lib dir: ${LIB_PATH}"
run_cmd "${SUPER} mkdir -p ${LIB_PATH}"
log_line info "Install libs: ./lib/* -> ${LIB_PATH}/"
run_cmd "${SUPER} cp -r ./lib/* ${LIB_PATH}/"
run_cmd "${SUPER} chmod 644 ${LIB_PATH}/* || true"

log_line info "Prepare bin dir: ${BIN_FOLDER_PATH}"
run_cmd "${SUPER} mkdir -p ${BIN_FOLDER_PATH}"
log_line info "Install bins and scripts"
run_cmd "${SUPER} cp ./bin/${PROGRAM_NAME}      ${BIN_FOLDER_PATH}/${PROGRAM_NAME}"
run_cmd "${SUPER} cp ${MQTT_SRC} ${BIN_FOLDER_PATH}/${MQTT_PROGRAM_NAME}"
run_cmd "${SUPER} chmod 755 ${BIN_FOLDER_PATH}/${PROGRAM_NAME}"
run_cmd "${SUPER} chmod 755 ${BIN_FOLDER_PATH}/${MQTT_PROGRAM_NAME}"

# Install management scripts (start and uninstall) - not install scripts as they need source files
for script in start.sh start-system.sh start-user.sh uninstall.sh uninstall-system.sh uninstall-user.sh; do
  if [ -f "./${script}" ]; then
    run_cmd "${SUPER} cp ./${script} ${BIN_FOLDER_PATH}/${script}"
    run_cmd "${SUPER} chmod 755 ${BIN_FOLDER_PATH}/${script}"
    log_line info "Installed: ${BIN_FOLDER_PATH}/${script}"
  fi
done

log_line info "Prepare temp dir: ${TEMP_DIR}"
run_cmd "${SUPER} mkdir -p ${TEMP_DIR}"

log_line info "Prepare share dir: ${SHARE_DIR}"
run_cmd "${SUPER} mkdir -p ${SHARE_DIR}"
log_line info "Install swagger-res -> ${SHARE_DIR}/"
run_cmd "${SUPER} cp -r ./swagger-res ${SHARE_DIR}/"

log_line info "Install systemd service: ./service/${PROGRAM_NAME}.service -> ${SERVICE_PATH}/"
run_cmd "${SUPER} cp ./service/${PROGRAM_NAME}.service ${SERVICE_PATH}/${PROGRAM_NAME}.service"
run_cmd "${SUPER} chmod 644 ${SERVICE_PATH}/${PROGRAM_NAME}.service"

log_line info "Reload systemd daemon"
run_cmd "${SUPER} systemctl daemon-reload"

log_line info "Enable and start service"
run_cmd "${SUPER} systemctl enable ${PROGRAM_NAME}.service"
run_cmd "${SUPER} systemctl start ${PROGRAM_NAME}.service"

log_line info "Install done (system)."
log_line info "bin: ${BIN_FOLDER_PATH}/${PROGRAM_NAME}"
log_line info "mqtt: ${BIN_FOLDER_PATH}/${MQTT_PROGRAM_NAME}"
log_line info "cfg: ${CONFIG_PATH}/config.ini"
log_line info "lib: ${LIB_PATH}"
log_line info "log: ${LOG_PATH}"
log_line info "service: ${SERVICE_PATH}/${PROGRAM_NAME}.service"
log_line info "Check: ${SUPER} systemctl status ${PROGRAM_NAME}.service"

if ! $DEBUG; then
  set +e
  ${SUPER} systemctl status "${PROGRAM_NAME}.service" >> "${LOG_FILE}" 2>&1
  set -e
fi

log_line info "Install finished successfully."

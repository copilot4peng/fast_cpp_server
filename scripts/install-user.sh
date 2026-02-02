#!/bin/bash
set -euo pipefail

PROGRAM_NAME="fast_cpp_server"
MQTT_PROGRAM_NAME="mosquitto"

DEBUG=${DEBUG:-false}

# -------- user paths (no root) --------
USER_PREFIX="${HOME}/.local/${PROGRAM_NAME}"
USER_BIN_FOLDER_PATH="${USER_PREFIX}/bin"
USER_LIB_PATH="${USER_PREFIX}/lib"
USER_CONFIG_PATH="${HOME}/.config/${PROGRAM_NAME}"
USER_LOG_PATH="${HOME}/.local/share/${PROGRAM_NAME}/logs"
USER_DATA_PATH="${HOME}/.local/share/${PROGRAM_NAME}/data"
USER_TEMP_DIR="${HOME}/.cache/${PROGRAM_NAME}"
USER_SHARE_DIR="${USER_PREFIX}/share"

# -------- logging --------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/logs"
TS="$(date '+%Y%m%d_%H%M%S')"
LOG_FILE="${LOG_DIR}/install_user_${TS}.log"

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

# Parse args
for arg in "$@"; do
  case "$arg" in
    --debug) DEBUG=true ;;
  esac
done

log_line info "User Install begin: PROGRAM_NAME=${PROGRAM_NAME}, DEBUG=${DEBUG}"
log_line info "Release dir: ${SCRIPT_DIR}"
log_line info "Log file: ${LOG_FILE}"

# Print all paths that will be used
log_line info "========== Installation Paths =========="
log_line info "USER_PREFIX:         ${USER_PREFIX}"
log_line info "USER_BIN_FOLDER_PATH: ${USER_BIN_FOLDER_PATH}"
log_line info "USER_LIB_PATH:       ${USER_LIB_PATH}"
log_line info "USER_CONFIG_PATH:    ${USER_CONFIG_PATH}"
log_line info "USER_LOG_PATH:       ${USER_LOG_PATH}"
log_line info "USER_DATA_PATH:      ${USER_DATA_PATH}"
log_line info "USER_TEMP_DIR:       ${USER_TEMP_DIR}"
log_line info "USER_SHARE_DIR:      ${USER_SHARE_DIR}"
log_line info "========================================"

log_line info "User install: no sudo, no systemd."

run_cmd "mkdir -p ${USER_BIN_FOLDER_PATH} ${USER_LIB_PATH} ${USER_SHARE_DIR}"
run_cmd "mkdir -p ${USER_CONFIG_PATH} ${USER_LOG_PATH} ${USER_DATA_PATH} ${USER_TEMP_DIR}"

log_line info "Install bins -> ${USER_BIN_FOLDER_PATH}/"
run_cmd "cp ./bin/${PROGRAM_NAME}      ${USER_BIN_FOLDER_PATH}/${PROGRAM_NAME}"
run_cmd "cp ./bin/${MQTT_PROGRAM_NAME} ${USER_BIN_FOLDER_PATH}/${MQTT_PROGRAM_NAME}"
run_cmd "chmod 755 ${USER_BIN_FOLDER_PATH}/${PROGRAM_NAME}"
run_cmd "chmod 755 ${USER_BIN_FOLDER_PATH}/${MQTT_PROGRAM_NAME}"

# Install management scripts (start and uninstall) - not install scripts as they need source files
for script in start.sh start-system.sh start-user.sh uninstall.sh uninstall-system.sh uninstall-user.sh; do
  if [ -f "./${script}" ]; then
    run_cmd "cp ./${script} ${USER_BIN_FOLDER_PATH}/${script}"
    run_cmd "chmod 755 ${USER_BIN_FOLDER_PATH}/${script}"
    log_line info "Installed: ${USER_BIN_FOLDER_PATH}/${script}"
  fi
done

log_line info "Install libs -> ${USER_LIB_PATH}/"

mkdir -p "${USER_LIB_PATH}"
for f in ./lib/*; do
  # 如果没有匹配到项（通配符未展开），跳过
  [ ! -e "$f" ] && continue

  base=$(basename "$f")

  # 跳过 pkgconfig 目录
  if [ "$base" = "pkgconfig" ]; then
    log_line info "跳过 pkgconfig 目录: $f"
    continue
  fi

  # 使用 run_cmd 执行复制（以兼容 DEBUG 模式）
  run_cmd "cp -r \"$f\" \"${USER_LIB_PATH}/\""
done

run_cmd "chmod 644 ${USER_LIB_PATH}/* || true"

log_line info "Install swagger-res -> ${USER_SHARE_DIR}/"
run_cmd "cp -r ./swagger-res ${USER_SHARE_DIR}/"

# configs: non-overwrite
log_line info "Install configs (non-overwrite) -> ${USER_CONFIG_PATH}/"
if $DEBUG; then
  run_cmd "for f in ./config/*; do echo \"would copy \$f -> ${USER_CONFIG_PATH}/\"; done"
else
  # # 模板相对路径（源码包内）
  # template_src="./config/config.user.template.ini"
  # # 目标配置文件（源码包内覆盖），如果你想把它写到安装目标目录，可以改为 ${DESTDIR}${PREFIX}/etc/... 
  # target_cfg="./config/config.ini"
  # # 使用 sed 替换文字 ${HOME} -> 实际 home（转义分隔符）
  # # 1) 确保模板存在
  # if [ ! -f "${template_src}" ]; then
  #   log_line warn "[安装配置] 模板文件不存在：${template_src}，跳过配置替换"
  #   return 0
  # fi
  # log_line info "[安装配置] 生成覆盖模板配置文件：${target_cfg}"
  # sed "s|\\\${HOME}|${HOME}|g" "${template_src}" > "${target_cfg}"
  # chmod 644 "${target_cfg}" || true
  # log_line pro "[安装配置] 生成完成：${target_cfg}"

  for f in ./config/*; do
    base="$(basename "$f")"
    if [ ! -f "${USER_CONFIG_PATH}/${base}" ]; then
      log_line pro "cp ${f} ${USER_CONFIG_PATH}/${base}"
      cp "$f" "${USER_CONFIG_PATH}/${base}" >> "${LOG_FILE}" 2>&1
      chmod 644 "${USER_CONFIG_PATH}/${base}" >> "${LOG_FILE}" 2>&1 || true
    else
      log_line info "Keep existing config: ${USER_CONFIG_PATH}/${base}"
    fi
  done
fi

# Generate runtime config.ini from template (program reads config.ini)
TEMPLATE="./config/config.user.template.ini"
OUT_CFG="${USER_CONFIG_PATH}/config.ini"
log_line info "Generate user runtime config.ini -> ${OUT_CFG}"
if [ -f "${TEMPLATE}" ]; then
  if $DEBUG; then
    run_cmd "sed \"s|\\\${HOME}|${HOME}|g\" ${TEMPLATE} > ${OUT_CFG}"
  else
    sed "s|\${HOME}|${HOME}|g" "${TEMPLATE}" > "${OUT_CFG}"
    chmod 644 "${OUT_CFG}" || true
    log_line pro "Generated: ${OUT_CFG}"
  fi
else
  log_line warn "Missing template: ${TEMPLATE} (skip generating ${OUT_CFG})"
fi

log_line info "Install done (user)."
log_line info "Run: ${USER_BIN_FOLDER_PATH}/start.sh --user"
log_line info "Config: ${USER_CONFIG_PATH}/config.ini"
log_line info "Logs: ${USER_LOG_PATH}"
log_line info "Data: ${USER_DATA_PATH}"

log_line info "Install finished successfully."

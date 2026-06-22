#!/bin/bash
# ZX App Marquee 安装脚本
# 适用于 Debian 11 系统

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 安装目录
INSTALL_DIR="/userdata/zxbox/system/Marquee"
CONFIG_DIR="/userdata/zxbox/system/Marquee"
SERVICE_DIR="/lib/systemd/system"
SERVICE_NAME="zx_app_Marquee"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}ZX App Marquee 安装脚本${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 检查是否为 root 用户
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}错误: 请使用 sudo 运行此脚本${NC}"
    exit 1
fi

# 检查系统版本
if ! grep -q "Debian" /etc/os-release; then
    echo -e "${YELLOW}警告: 此脚本针对 Debian 系统设计，其他系统可能需要调整${NC}"
fi

# 检查可执行文件是否存在
if [ ! -f "zx_app_marquee" ]; then
    echo -e "${RED}错误: 未找到 zx_app_marquee 可执行文件${NC}"
    exit 1
fi

# 停止已有服务（避免 "Text file busy" 错误）
echo -e "${YELLOW}[0/5] 停止已有服务...${NC}"
systemctl stop ${SERVICE_NAME} 2>/dev/null || true
# 等待进程完全退出
sleep 1

# 创建安装目录
echo -e "${YELLOW}[1/5] 创建安装目录...${NC}"
mkdir -p ${INSTALL_DIR}
mkdir -p ${CONFIG_DIR}

# 复制文件到安装目录
echo -e "${YELLOW}[2/5] 安装程序文件...${NC}"
cp zx_app_marquee ${INSTALL_DIR}/
cp get_resolution.sh ${INSTALL_DIR}/
if [ -d "fonts" ]; then
    cp -r fonts ${INSTALL_DIR}/
    echo -e "${GREEN}已复制字体文件${NC}"
fi
chmod +x ${INSTALL_DIR}/get_resolution.sh
chmod +x ${INSTALL_DIR}/zx_app_marquee

# 复制配置文件
echo -e "${YELLOW}[3/5] 安装配置文件...${NC}"
if [ ! -f "${CONFIG_DIR}/config.json" ]; then
    cp config.json ${CONFIG_DIR}/
    echo -e "${GREEN}已安装默认配置文件${NC}"
else
    echo -e "${YELLOW}配置文件已存在，跳过${NC}"
fi

# mqtt_message.json 始终覆盖安装，避免残留损坏文件
cp mqtt_message.json ${CONFIG_DIR}/
echo -e "${GREEN}已安装 MQTT 消息配置文件${NC}"

# 安装 systemd 服务
echo -e "${YELLOW}[4/5] 安装 systemd 服务...${NC}"
cp zx_app_Marquee.service ${SERVICE_DIR}/
systemctl daemon-reload
systemctl enable ${SERVICE_NAME}

# 启动服务
echo -e "${YELLOW}[5/5] 启动服务...${NC}"
systemctl start ${SERVICE_NAME}

# 检查服务状态
sleep 2
if systemctl is-active --quiet ${SERVICE_NAME}; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}安装成功！${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "服务状态: ${GREEN}运行中${NC}"
    echo ""
    echo "常用命令:"
    echo "  查看服务状态: systemctl status ${SERVICE_NAME}"
    echo "  查看日志: journalctl -u ${SERVICE_NAME} -f"
    echo "  重启服务: systemctl restart ${SERVICE_NAME}"
    echo "  停止服务: systemctl stop ${SERVICE_NAME}"
    echo ""
    echo "配置文件位置:"
    echo "  启动配置: ${CONFIG_DIR}/config.json"
    echo "  MQTT消息: ${CONFIG_DIR}/mqtt_message.json"
    echo ""
    echo "服务文件位置:"
    echo "  ${SERVICE_DIR}/zx_app_Marquee.service"
    echo ""
    echo -e "${YELLOW}提示: 请编辑 ${CONFIG_DIR}/config.json 配置 MQTT 服务器地址${NC}"
else
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}安装完成，但服务启动失败！${NC}"
    echo -e "${RED}========================================${NC}"
    echo ""
    echo "请检查日志:"
    echo "  journalctl -u ${SERVICE_NAME} -n 50"
    exit 1
fi

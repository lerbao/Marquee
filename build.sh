#!/bin/bash
# ZX App Marquee 编译脚本
# 在RK3588设备上执行此脚本进行编译
# 编译产物直接输出到output目录

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}ZX App Marquee 编译脚本${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 检查源文件是否存在
if [ ! -f "src/test.cpp" ]; then
    echo -e "${RED}错误: 未找到 src/test.cpp 源文件${NC}"
    exit 1
fi

# 确保output目录存在
if [ ! -d "output" ]; then
    echo -e "${RED}错误: output目录不存在${NC}"
    exit 1
fi

# 编译（直接输出到output目录）
echo -e "${YELLOW}[1/1] 正在编译...${NC}"
g++ -std=c++11 -DUSE_MQTT -o output/zx_app_marquee src/test.cpp \
    $(pkg-config --cflags --libs gbm egl glesv2 libdrm freetype2 libmosquitto) \
    -ldl

if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败！${NC}"
    exit 1
fi

chmod +x output/zx_app_marquee
echo -e "${GREEN}编译成功！输出到 output/zx_app_marquee${NC}"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}编译完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "可执行文件: output/zx_app_marquee"
echo ""
echo "部署步骤:"
echo "  1. 打包output目录并上传到设备"
echo "  2. 在设备上执行: sudo bash install.sh"

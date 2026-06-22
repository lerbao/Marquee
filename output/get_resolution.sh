#!/bin/bash
# 通过DRM/KMS获取HDMI分辨率，并写入config.json
# 用法: ./get_resolution.sh [config_json_path] [drm_card] [connector_name]
# 示例: ./get_resolution.sh config.json 0 HDMI-A-1

CONFIG_FILE="${1:-config.json}"
CARD="${2:-0}"
CONN_NAME="${3:-HDMI-A-1}"
BASE="/sys/class/drm/card${CARD}"

CONFIG_DIR=$(dirname "$CONFIG_FILE")

for conn_dir in ${BASE}/card${CARD}-*/; do
    conn=$(basename "$conn_dir")
    case "$conn" in
        *${CONN_NAME}*|*HDMI*|*hdmi*)
            ;;
        *)
            continue
            ;;
    esac

    status=$(cat "${conn_dir}/status" 2>/dev/null)
    if [ "$status" != "connected" ]; then
        continue
    fi

    modes_file="${conn_dir}/modes"
    if [ -f "$modes_file" ]; then
        mode=$(head -1 "$modes_file")
        if [ -n "$mode" ]; then
            WIDTH=${mode%%x*}
            HEIGHT=${mode#*x}
            echo "WIDTH=${WIDTH}"
            echo "HEIGHT=${HEIGHT}"
            echo "MODE=${mode}"

            # 将分辨率写入 config.json（使用python3处理JSON）
            if [ -f "$CONFIG_FILE" ]; then
                python3 -c "
import json, sys
try:
    with open('$CONFIG_FILE', 'r') as f:
        cfg = json.load(f)
    cfg['win_w'] = $WIDTH
    cfg['win_h'] = $HEIGHT
    with open('$CONFIG_FILE', 'w') as f:
        json.dump(cfg, f, indent=4)
    print('已更新 config.json: win_w=$WIDTH, win_h=$HEIGHT')
except Exception as e:
    print('更新config.json失败:', e, file=sys.stderr)
"
            fi
            exit 0
        fi
    fi
done

echo "错误：未找到已连接的HDMI显示器" >&2
exit 1

# 跑马灯 MQTT 通信接口文档

## 一、MQTT 连接配置

跑马灯程序启动时从 `/userdata/zxbox/system/Marquee/config.json` 读取 MQTT 连接参数。

```json
{
    "mqtt": {
        "host": "172.20.32.158",
        "port": 1883,
        "client_id": "marquee_client",
        "topic": "marquee/control"
    }
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `mqtt.host` | string | 是 | MQTT Broker 地址 |
| `mqtt.port` | integer | 是 | MQTT Broker 端口 |
| `mqtt.client_id` | string | 是 | MQTT 客户端 ID |
| `mqtt.topic` | string | 是 | 控制指令订阅主题，下发配置时向此 Topic 发送 JSON |

> **注意**：程序启动后会自动重试连接（最多5次，每次间隔2秒），网络就绪后自动恢复。

---

## 二、控制指令（MQTT → 跑马灯）

**通信方式**：上层向 `mqtt.topic`（默认 `marquee/control`）Publish 一条 JSON 格式消息。

**规则**：
- 只需发送需要变更的字段，未发送的字段保持不变
- 如果 `enable` 字段未传但传了其他参数，跑马灯自动激活（等同于 `enable: true`）
- 每次成功接收后，消息持久化到 `/userdata/zxbox/system/Marquee/mqtt_message.json`，程序重启后自动恢复上次配置

---

## 三、全部控制字段一览

### 3.1 跑马灯开关

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enable` | boolean | `false` | 跑马灯总开关，`true` 显示 / `false` 隐藏 |

### 3.2 文字内容

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `text` | string | 命令行参数值 | 跑马灯显示的文字内容，支持中文 |

### 3.3 字体样式

| 字段 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| `font_size` | integer | 命令行参数值 | 10 ~ min(50, height) | 字体大小（像素），不能超过 height |
| `font_bold` | boolean | `false` | `true` / `false` | 是否加粗 |
| `font_italic` | integer | `0` | `0` / `1` | 斜体，0=关闭，1=开启 |

> **注意**：`font_size` 动态上限为 `min(50, height)`，即当 height 设为 40 时，font_size 最大只能到 40。

### 3.4 滚动控制

| 字段 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| `scroll_speed` | integer | 命令行参数值 | 0 ~ 10 | 滚动速度，0=暂停，值越大越快 |
| `scroll_mode` | integer | `0` | 0 / 1 / 2 / 3 | 滚动模式（见下表） |
| `stay_time` | integer | `1000` | 无限制 | 滚动结束后停留时长（毫秒） |

**scroll_mode 取值说明**：

| 值 | 模式 | 行为描述 |
|----|------|----------|
| `0` | 循环滚动 | 文字从右到左连续滚动，一轮结束后立即从右侧重新开始 |
| `1` | 单次滚动 | 文字从右到左滚动一次，结束后停在左侧，不再重复 |
| `2` | 静止居中 | 文字不滚动，居中显示 |
| `3` | 循环滚动(带暂停) | 文字从右到左滚动一轮，结束后停留 `stay_time` 毫秒，再从右侧重新开始 |

### 3.5 显示区域

| 字段 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| `position` | integer | 命令行参数值 | 0 / 1 | 区域在屏幕的位置：0=顶部，1=底部 |
| `height` | integer | `70` | 20 ~ 90 | 跑马灯区域高度（像素），用于自动计算 Y 坐标 |
| `pos_y` | integer | 自动计算 | 无限制 | 手动指定垂直偏移（覆盖自动计算） |

> **自动计算规则**：  
> `position=0` 时 `pos_y = 0`（顶部）  
> `position=1` 时 `pos_y = screen_h - height`（底部，screen_h 默认为 1080）

### 3.6 屏幕对齐

| 字段 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| `screen_align` | integer | `0` | 0 / 1 / 2 | 静止模式下文字在屏幕的水平对齐：0=左对齐，1=居中，2=右对齐 |
| `text_align` | integer | 命令行参数值 | 0 / 1 / 2 | 文字对齐方式：0=左对齐，1=居中，2=右对齐 |

### 3.7 颜色与透明度

| 字段 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| `text_color` | array[3] | 命令行参数值 | 每个分量 0 ~ 255 | 文字颜色 RGB，如 `[255, 255, 250]` |
| `bg_color` | array[3] | 命令行参数值 | 每个分量 0 ~ 255 | 背景颜色 RGB，如 `[50, 50, 50]` |
| `alpha` | float | `1.0` | 0.0 ~ 1.0 | 整体透明度，0.0=全透明，1.0=不透明 |
| `border_width` | integer | `0` | 无限制 | 边框宽度（像素），0=无边框 |

### 3.8 世界时间

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `show_world_time` | boolean | `false` | 是否显示世界时间 |
| `world_time_cities` | array[string] | 无 | 要显示的城市名列表，如 `["北京", "洛杉矶"]` |

**支持的城市名**（按发送顺序显示）：

| 城市 | 时区(UTC) | 城市 | 时区(UTC) |
|------|-----------|------|-----------|
| 北京 | +8 | 上海 | +8 |
| 香港 | +8 | 新加坡 | +8 |
| 东京 | +9 | 悉尼 | +10 |
| 迪拜 | +4 | 莫斯科 | +3 |
| 巴黎 | +1 | 柏林 | +1 |
| 伦敦 | 0 | 纽约 | -5 |
| 洛杉矶 | -8 | | |

> **注意**：发送 `world_time_cities` 后，只会启用列表中指定的城市，其余城市自动禁用。列表中未匹配的城市名会被忽略。

### 3.9 天气预报（新）

| 字段 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| `show_weather_forecast` | boolean | `false` | `true` / `false` | 是否启用天气预报显示 |
| `forecast_city` | string | `""` | 无限制 | 城市名，如 `"北京"` |
| `forecast_weather` | string | `""` | 无限制 | 天气状况，如 `"晴"`、`"多云"` |
| `forecast_temp_low` | integer | `0` | 无限制 | 最低温度（℃） |
| `forecast_temp_high` | integer | `0` | 无限制 | 最高温度（℃） |
| `forecast_position` | integer | `0` | 0 / 1 / 2 | 显示位置（见下表） |

**forecast_position 取值说明**：

| 值 | 模式 | 行为描述 |
|----|------|----------|
| `0` | 固定最左侧 | 天气预报文字始终显示在屏幕最左侧，跑马灯文字从天气区域右侧开始滚动，不覆盖天气区域 |
| `1` | 固定最右侧 | 天气预报文字始终显示在屏幕最右侧，跑马灯文字滚动到天气区域左侧时逐渐消失，不覆盖天气区域 |
| `2` | 随文字滚动 | 天气预报文字拼接在跑马灯文字后面，一起滚动显示 |

**显示格式**：`城市: 天气 最低℃~最高℃`，例如：`北京: 晴 6°C~20°C`

### 3.10 旧版天气（已废弃，建议使用天气预报）

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `show_weather` | boolean | `false` | 是否显示旧版天气信息 |
| `weather_position` | integer | `0` | 旧版天气显示位置 |

---

## 四、完整示例

### 示例1：激活跑马灯 + 基础配置

```json
{
    "enable": true,
    "text": "欢迎使用跑马灯系统",
    "font_size": 35,
    "scroll_speed": 5,
    "scroll_mode": 0,
    "height": 50,
    "position": 0,
    "text_color": [255, 255, 250],
    "bg_color": [50, 50, 50],
    "alpha": 1.0
}
```

### 示例2：带世界时间 + 天气预报（固定左侧）

```json
{
    "enable": true,
    "text": "公司欢迎标语",
    "font_size": 30,
    "scroll_speed": 3,
    "scroll_mode": 0,
    "height": 60,
    "position": 0,
    "show_world_time": true,
    "world_time_cities": ["北京", "洛杉矶", "伦敦"],
    "show_weather_forecast": true,
    "forecast_city": "北京",
    "forecast_weather": "晴",
    "forecast_temp_low": 6,
    "forecast_temp_high": 20,
    "forecast_position": 0
}
```

### 示例3：天气预报随文字滚动

```json
{
    "enable": true,
    "text": "4422动态修改跑马灯文字",
    "font_size": 35,
    "scroll_speed": 5,
    "scroll_mode": 0,
    "height": 50,
    "position": 0,
    "show_world_time": true,
    "world_time_cities": ["北京", "洛杉矶"],
    "show_weather_forecast": true,
    "forecast_city": "北京",
    "forecast_weather": "晴",
    "forecast_temp_low": 6,
    "forecast_temp_high": 20,
    "forecast_position": 2
}
```

### 示例4：部分更新（只改速度和颜色）

```json
{
    "scroll_speed": 8,
    "text_color": [0, 255, 0]
}
```

> 不传 `enable` 但传了其他参数时，跑马灯自动激活。只传增量字段即可，其余字段保持不变。

### 示例5：关闭跑马灯

```json
{
    "enable": false
}
```

---

## 五、字段速查表

| 字段 | 类型 | 默认值 | 范围/可选值 | 必填 |
|------|------|--------|-------------|------|
| `enable` | boolean | `false` | `true` / `false` | 否 |
| `text` | string | - | 任意字符串 | 否 |
| `font_size` | integer | - | 10 ~ min(50, height) | 否 |
| `font_bold` | boolean | `false` | `true` / `false` | 否 |
| `font_italic` | integer | `0` | 0 / 1 | 否 |
| `scroll_speed` | integer | - | 0 ~ 10 | 否 |
| `scroll_mode` | integer | `0` | 0 / 1 / 2 / 3 | 否 |
| `stay_time` | integer | `1000` | 任意正整数 | 否 |
| `position` | integer | - | 0 / 1 | 否 |
| `height` | integer | `70` | 20 ~ 90 | 否 |
| `pos_y` | integer | 自动 | 任意整数 | 否 |
| `screen_align` | integer | `0` | 0 / 1 / 2 | 否 |
| `text_align` | integer | - | 0 / 1 / 2 | 否 |
| `text_color` | array[3] | - | 每分量 0~255 | 否 |
| `bg_color` | array[3] | - | 每分量 0~255 | 否 |
| `alpha` | float | `1.0` | 0.0 ~ 1.0 | 否 |
| `border_width` | integer | `0` | 任意正整数 | 否 |
| `show_world_time` | boolean | `false` | `true` / `false` | 否 |
| `world_time_cities` | array[string] | - | 见支持城市表 | 否 |
| `show_weather_forecast` | boolean | `false` | `true` / `false` | 否 |
| `forecast_city` | string | `""` | 任意字符串 | 否 |
| `forecast_weather` | string | `""` | 任意字符串 | 否 |
| `forecast_temp_low` | integer | `0` | 任意整数 | 否 |
| `forecast_temp_high` | integer | `0` | 任意整数 | 否 |
| `forecast_position` | integer | `0` | 0 / 1 / 2 | 否 |
| `show_weather` | boolean | `false` | `true` / `false` | 否（已废弃） |
| `weather_position` | integer | `0` | 0 / 1 / 2 | 否（已废弃） |

---

## 六、文件路径一览

| 文件 | 路径 | 说明 |
|------|------|------|
| 启动配置 | `/userdata/zxbox/system/Marquee/config.json` | MQTT 连接参数、窗口参数 |
| 持久化消息 | `/userdata/zxbox/system/Marquee/mqtt_message.json` | 最后一次成功接收的 MQTT 消息，重启自动恢复 |
| 字体文件 | `/userdata/zxbox/system/Marquee/fonts/NotoSansCJK-Regular.ttc` | 中文字体 |
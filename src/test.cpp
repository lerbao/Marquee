#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <map>
#include <cstring>
#include <sys/time.h>
#include <signal.h>
#include <mutex>

#include "test.h"
#include "json.hpp"
using json = nlohmann::json;

#ifdef USE_MQTT
#include <mosquitto.h>
#endif

static double get_timestamp_ms() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() / 1000.0;
    return ms;
}

static void sleep_until_next_frame(std::chrono::steady_clock::time_point& last_frame_time, int fps = 60) {
    auto frame_duration = std::chrono::microseconds(1000000 / fps);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_frame_time;

    if (elapsed < frame_duration) {
        auto sleep_time = frame_duration - elapsed;
        usleep(std::chrono::duration_cast<std::chrono::microseconds>(sleep_time).count());
    }

    last_frame_time = std::chrono::steady_clock::now();
}

typedef struct {
    std::string name;
    int timezone;
    bool enabled;
} CityTime;

typedef struct {
    std::string city;
    std::string region;
    std::string weather;
    int temp;
    int humidity;
} WeatherData;

// 天气预报数据结构
typedef struct {
    std::string city;           // 城市名
    std::string weather;        // 天气状况
    int temp_low;              // 最低温度
    int temp_high;             // 最高温度
    bool enabled;              // 是否启用天气预报显示
    int position;              // 显示位置: 0=最左侧固定, 1=最右侧固定, 2=随文字滚动
} WeatherForecastData;

typedef struct {
    std::string text;
    int font_size;
    float text_color[4];
    float bg_color[4];
    int position;
    int scroll_speed;
    bool font_bold;
    int text_align;
    bool show_weather;
    bool show_world_time;
    int weather_position;
	// ============ 新增扩展字段 ============
    int scroll_mode;        // 滚动模式：0=循环滚动 1=单次滚动 2=静止居中
    int stay_time;          // 文字停留时长(ms)，滚动结束后停留
    float alpha;            // 整体透明度 0.0~1.0
    int font_italic;        // 斜体 0关闭 1开启
    float border_color[4];  // 边框颜色 RGBA
    int border_width;       // 边框宽度 0=无边框
    int screen_align;       // 整体屏幕对齐 0左 1中 2右
    bool enable;            // 跑马灯开关，默认false，MQTT下发true才显示
    int height;             // MQTT下发的跑马灯区域高度，用于自动计算pos_y
    // =====================================
	int pos_y;              // 图层垂直偏移，自动计算：顶部=0，底部=screen_h - height
} MarqueeConfig;

static MarqueeConfig marquee_config;
static std::mutex config_mutex;
static bool running = true;
static bool config_updated = false;

#ifdef USE_MQTT
static struct mosquitto *mosq = NULL;
static std::string mqtt_topic = "marquee/control";
#endif

// 配置文件路径
static const char* CONFIG_FILE = "/userdata/zxbox/system/Marquee/config.json";
static const char* MQTT_MSG_FILE = "/userdata/zxbox/system/Marquee/mqtt_message.json";
static const char* FONT_PATH = "/userdata/zxbox/system/Marquee/fonts/NotoSansCJK-Regular.ttc";

// 启动时是否激活跑马灯显示（默认false，等待MQTT激活）
static bool marquee_active = false;

// 屏幕实际高度（从get_resolution.sh获取）
static int screen_h = 1080;

// 启动配置结构
typedef struct {
    int plane_id;
    int win_w;
    int win_h;
    int pos_x;
    int pos_y;
    float alpha;
    bool show_weather;
    bool show_world_time;
    int weather_position;
    std::string mqtt_host;
    int mqtt_port;
    std::string mqtt_client_id;
    std::string mqtt_topic;
} StartupConfig;

static StartupConfig startup_config;

// 用户请求的城市顺序，用于控制显示顺序
std::vector<std::string> requested_city_order;

std::vector<CityTime> world_cities = {
    {"北京", 8, true},
    {"上海", 8, true},
    {"香港", 8, false},
    {"新加坡", 8, false},
    {"东京", 9, false},
    {"悉尼", 10, false},
    {"迪拜", 4, false},
    {"莫斯科", 3, false},
    {"巴黎", 1, false},
    {"柏林", 1, false},
    {"伦敦", 0, false},
    {"纽约", -5, false},
    {"洛杉矶", -8, false}
};

WeatherData weather_data = {
    "北京",
    "朝阳区",
    "晴",
    28,
    45
};

// 天气预报全局数据
static WeatherForecastData weather_forecast = {
    "",      // city
    "",      // weather
    0,       // temp_low
    0,       // temp_high
    false,   // enabled
    0        // position (0=左侧, 1=右侧, 2=随滚动)
};

// 加载启动配置文件
static bool load_startup_config() {
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        fprintf(stderr, "Warning: Cannot open config file %s, using defaults\n", CONFIG_FILE);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::string json_str(fsize, '\0');
    fread(&json_str[0], 1, fsize, fp);
    fclose(fp);

    try {
        json j = json::parse(json_str);

        if (j.contains("plane_id")) startup_config.plane_id = j["plane_id"].get<int>();
        if (j.contains("win_w")) startup_config.win_w = j["win_w"].get<int>();
        if (j.contains("win_h")) startup_config.win_h = j["win_h"].get<int>();
        // 宽高范围校验 [0,7680]/[0,4320]
        if (startup_config.win_w < 0 || startup_config.win_w > 7680) startup_config.win_w = 7680;
        if (startup_config.win_h < 0 || startup_config.win_h > 4320) startup_config.win_h = 4320;
        if (j.contains("pos_x")) startup_config.pos_x = j["pos_x"].get<int>();
        if (j.contains("pos_y")) startup_config.pos_y = j["pos_y"].get<int>();
        if (j.contains("alpha")) startup_config.alpha = j["alpha"].get<float>();
        if (j.contains("show_weather")) startup_config.show_weather = j["show_weather"].get<bool>();
        if (j.contains("show_world_time")) startup_config.show_world_time = j["show_world_time"].get<bool>();
        if (j.contains("weather_position")) startup_config.weather_position = j["weather_position"].get<int>();

        if (j.contains("mqtt")) {
            json mqtt = j["mqtt"];
            if (mqtt.contains("host")) startup_config.mqtt_host = mqtt["host"].get<std::string>();
            if (mqtt.contains("port")) startup_config.mqtt_port = mqtt["port"].get<int>();
            if (mqtt.contains("client_id")) startup_config.mqtt_client_id = mqtt["client_id"].get<std::string>();
            if (mqtt.contains("topic")) {
                startup_config.mqtt_topic = mqtt["topic"].get<std::string>();
                mqtt_topic = startup_config.mqtt_topic;
            }
        }

        printf("Loaded config: plane=%d, pos=(%d,%d), alpha=%.2f\n",
               startup_config.plane_id, startup_config.pos_x, startup_config.pos_y, startup_config.alpha);
        printf("MQTT: %s:%d, topic=%s\n",
               startup_config.mqtt_host.c_str(), startup_config.mqtt_port, mqtt_topic.c_str());
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Error parsing config: %s\n", e.what());
        return false;
    }
}

// 保存MQTT消息到持久化文件
static void save_mqtt_message(const char* payload) {
    FILE* fp = fopen(MQTT_MSG_FILE, "w");
    if (fp) {
        fprintf(fp, "%s\n", payload);
        fclose(fp);
    }
}

// 从持久化文件加载MQTT消息
static bool load_mqtt_message(char* buffer, size_t size) {
    FILE* fp = fopen(MQTT_MSG_FILE, "r");
    if (!fp) return false;

    // 读取整个文件内容
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize > 0 && fsize < (long)size) {
        fread(buffer, 1, fsize, fp);
        buffer[fsize] = '\0';
        fclose(fp);
        return true;
    }
    fclose(fp);
    return false;
}

// 根据 height 和 position 自动计算 pos_y
static void auto_calc_pos_y() {
    if (marquee_config.position == 0) {
        marquee_config.pos_y = 0;  // 顶部
    } else {
        marquee_config.pos_y = screen_h - marquee_config.height;  // 底部
    }
    if (marquee_config.pos_y < 0) marquee_config.pos_y = 0;
    printf("DEBUG: auto pos_y=%d (screen_h=%d, height=%d, position=%d)\n",
           marquee_config.pos_y, screen_h, marquee_config.height, marquee_config.position);
}

static void handle_mqtt_message(const char* payload) {
    setvbuf(stdout, NULL, _IONBF, 0);
    double t0 = get_timestamp_ms();
    std::string json_str(payload);
    printf("DEBUG: [%.3f ms] Received MQTT payload: %s\n", t0, payload);
    printf("DEBUG: [%.3f ms] Payload length: %zu\n", get_timestamp_ms(), json_str.size());

    double t1 = get_timestamp_ms();
    std::lock_guard<std::mutex> lock(config_mutex);
    printf("DEBUG: [%.3f ms] Lock acquired (took %.3f ms)\n", get_timestamp_ms(), get_timestamp_ms() - t1);

    try {
        json j = json::parse(json_str);

        // 解析成功才保存到持久化文件
        save_mqtt_message(payload);

        // 先解析 enable 字段，控制跑马灯开关
        if (j.contains("enable") && j["enable"].is_boolean()) {
            marquee_config.enable = j["enable"].get<bool>();
            marquee_active = marquee_config.enable;
            printf("DEBUG: Marquee %s\n", marquee_active ? "activated" : "deactivated");
        } else if (j.size() > 0) {
            // 未传 enable 但有其他参数，按激活处理
            marquee_active = true;
            printf("DEBUG: Marquee activated (no enable field, other params present)\n");
        }

        // 文本内容
        if (j.contains("text") && j["text"].is_string()) {
            marquee_config.text = j["text"].get<std::string>();
            printf("DEBUG: Updated text to: %s\n", marquee_config.text.c_str());
        }

        // 字体大小（限制范围：10 ~ min(50, height)）
        if (j.contains("font_size") && j["font_size"].is_number_integer()) {
            marquee_config.font_size = j["font_size"].get<int>();
        }

        // 滚动速度（限制范围：0~10）
        if (j.contains("scroll_speed") && j["scroll_speed"].is_number_integer()) {
            int raw_speed = j["scroll_speed"].get<int>();
            marquee_config.scroll_speed = raw_speed;
            if (marquee_config.scroll_speed < 0) {
                printf("DEBUG: scroll_speed %d < 0, clamped to 0\n", raw_speed);
                marquee_config.scroll_speed = 0;
            }
            if (marquee_config.scroll_speed > 10) {
                printf("DEBUG: scroll_speed %d > 10, clamped to 10\n", raw_speed);
                marquee_config.scroll_speed = 10;
            }
        }

        // 显示区域屏幕位置：0=顶部 1=底部
        if (j.contains("position") && j["position"].is_number_integer()) {
            marquee_config.position = j["position"].get<int>();
        }

        // 跑马灯区域高度（MQTT下发，限制20~90，自动计算pos_y）
        if (j.contains("height") && j["height"].is_number_integer()) {
            marquee_config.height = j["height"].get<int>();
            if (marquee_config.height < 20) marquee_config.height = 20;
            if (marquee_config.height > 90) marquee_config.height = 90;
        }

        // position 或 height 变更时重新计算
        if (j.contains("position") || j.contains("height")) {
            auto_calc_pos_y();
        }

        // 字体内上限：最大不能超过height，最小不能小于10
        {
            int max_font = marquee_config.height < 50 ? marquee_config.height : 50;
            if (marquee_config.font_size > max_font) marquee_config.font_size = max_font;
            if (marquee_config.font_size < 10) marquee_config.font_size = 10;
        }

        // 对齐
        if (j.contains("text_align") && j["text_align"].is_number_integer()) {
            marquee_config.text_align = j["text_align"].get<int>();
        }

        // 加粗
        if (j.contains("font_bold") && j["font_bold"].is_boolean()) {
            marquee_config.font_bold = j["font_bold"].get<bool>();
        }

        // 显示天气
        if (j.contains("show_weather") && j["show_weather"].is_boolean()) {
            marquee_config.show_weather = j["show_weather"].get<bool>();
        }

        // 显示世界时间
        if (j.contains("show_world_time") && j["show_world_time"].is_boolean()) {
            marquee_config.show_world_time = j["show_world_time"].get<bool>();
        }

        // 世界时间城市列表，发送后启用指定城市，未发送的城市自动禁用
        if (j.contains("world_time_cities") && j["world_time_cities"].is_array()) {
            bool cities_changed = false;
            std::vector<std::string> new_order;
            // 先全部禁用
            for (auto& city : world_cities) {
                if (city.enabled) cities_changed = true;
                city.enabled = false;
            }
            // 按名称匹配启用，并保存请求顺序
            for (const auto& name_json : j["world_time_cities"]) {
                try {
                    std::string name = name_json.get<std::string>();
                    new_order.push_back(name);
                    for (auto& city : world_cities) {
                        if (city.name == name) {
                            city.enabled = true;
                            cities_changed = true;
                            break;
                        }
                    }
                } catch (...) {
                    // 忽略解析失败的城市名
                }
            }
            // 更新请求顺序
            requested_city_order = new_order;
            if (cities_changed) {
                config_updated = true;
                printf("DEBUG: Updated world_time_cities to: ");
                for (const auto& name : requested_city_order) {
                    printf("%s ", name.c_str());
                }
                printf("\n");
            }
        }

        // 天气位置
        if (j.contains("weather_position") && j["weather_position"].is_number_integer()) {
            marquee_config.weather_position = j["weather_position"].get<int>();
        }

		// ========= 新增扩展字段 =========
        // 滚动模式
        if (j.contains("scroll_mode") && j["scroll_mode"].is_number_integer()) {
            marquee_config.scroll_mode = j["scroll_mode"].get<int>();
        }
        // 停留时间
        if (j.contains("stay_time") && j["stay_time"].is_number_integer()) {
            marquee_config.stay_time = j["stay_time"].get<int>();
        }
        // 整体透明度
        if (j.contains("alpha") && j["alpha"].is_number_float()) {
            marquee_config.alpha = j["alpha"].get<float>();
        }
        // 斜体
        if (j.contains("font_italic") && j["font_italic"].is_number_integer()) {
            marquee_config.font_italic = j["font_italic"].get<int>();
        }
        // 文字颜色 RGB 数组（范围0~255，只取前3个值）
        if (j.contains("text_color") && j["text_color"].is_array() && j["text_color"].size() >= 3) {
            float r = j["text_color"][0].get<float>() / 255.0f;
            float g = j["text_color"][1].get<float>() / 255.0f;
            float b = j["text_color"][2].get<float>() / 255.0f;
            marquee_config.text_color[0] = r;
            marquee_config.text_color[1] = g;
            marquee_config.text_color[2] = b;
            // text_color[3] 不修改，由单独的 alpha 字段控制
            printf("DEBUG: Updated text_color RGB to: [%.2f, %.2f, %.2f]\n", r, g, b);
        }
        // 背景颜色 RGB 数组（范围0~255，只取前3个值）
        if (j.contains("bg_color") && j["bg_color"].is_array() && j["bg_color"].size() >= 3) {
            float r = j["bg_color"][0].get<float>() / 255.0f;
            float g = j["bg_color"][1].get<float>() / 255.0f;
            float b = j["bg_color"][2].get<float>() / 255.0f;
            marquee_config.bg_color[0] = r;
            marquee_config.bg_color[1] = g;
            marquee_config.bg_color[2] = b;
            // bg_color[3] 不修改，由单独的 alpha 字段控制
            printf("DEBUG: Updated bg_color RGB to: [%.2f, %.2f, %.2f]\n", r, g, b);
        }
        // 边框宽度
        if (j.contains("border_width") && j["border_width"].is_number_integer()) {
            marquee_config.border_width = j["border_width"].get<int>();
        }
        // 屏幕整体对齐
        if (j.contains("screen_align") && j["screen_align"].is_number_integer()) {
            marquee_config.screen_align = j["screen_align"].get<int>();
        }
		// ========= 新增：解析 pos_y（对应 -Y） =========
		if (j.contains("pos_y") && j["pos_y"].is_number_integer()) {
			marquee_config.pos_y = j["pos_y"].get<int>();
			printf("DEBUG: Updated pos_y to: %d\n", marquee_config.pos_y);
		}
		//####

        // ========= 天气预报字段解析 =========
        // 是否启用天气预报显示
        if (j.contains("show_weather_forecast") && j["show_weather_forecast"].is_boolean()) {
            weather_forecast.enabled = j["show_weather_forecast"].get<bool>();
            printf("DEBUG: Updated show_weather_forecast to: %s\n", weather_forecast.enabled ? "true" : "false");
        }
        // 天气预报城市
        if (j.contains("forecast_city") && j["forecast_city"].is_string()) {
            weather_forecast.city = j["forecast_city"].get<std::string>();
            printf("DEBUG: Updated forecast_city to: %s\n", weather_forecast.city.c_str());
        }
        // 天气状况
        if (j.contains("forecast_weather") && j["forecast_weather"].is_string()) {
            weather_forecast.weather = j["forecast_weather"].get<std::string>();
            printf("DEBUG: Updated forecast_weather to: %s\n", weather_forecast.weather.c_str());
        }
        // 最低温度
        if (j.contains("forecast_temp_low") && j["forecast_temp_low"].is_number_integer()) {
            weather_forecast.temp_low = j["forecast_temp_low"].get<int>();
            printf("DEBUG: Updated forecast_temp_low to: %d\n", weather_forecast.temp_low);
        }
        // 最高温度
        if (j.contains("forecast_temp_high") && j["forecast_temp_high"].is_number_integer()) {
            weather_forecast.temp_high = j["forecast_temp_high"].get<int>();
            printf("DEBUG: Updated forecast_temp_high to: %d\n", weather_forecast.temp_high);
        }
        // 天气预报显示位置: 0=最左侧固定, 1=最右侧固定, 2=随文字滚动
        if (j.contains("forecast_position") && j["forecast_position"].is_number_integer()) {
            weather_forecast.position = j["forecast_position"].get<int>();
            if (weather_forecast.position < 0) weather_forecast.position = 0;
            if (weather_forecast.position > 2) weather_forecast.position = 2;
            printf("DEBUG: Updated forecast_position to: %d (%s)\n",
                   weather_forecast.position,
                   weather_forecast.position == 0 ? "左侧固定" :
                   (weather_forecast.position == 1 ? "右侧固定" : "随文字滚动"));
        }
        // =====================================

        config_updated = true;
        printf("DEBUG: [%.3f ms] Config update complete\n", get_timestamp_ms());
    }
    catch (json::parse_error& e) {
        fprintf(stderr, "JSON parse error: %s\n", e.what());
        // 解析失败时重置持久化文件为空JSON，避免下次启动重复解析失败
        save_mqtt_message("{}");
    }
    catch (...) {
        fprintf(stderr, "Unknown JSON parse exception\n");
    }
}
/*
static void handle_mqtt_message(const char* payload) {
    double t0 = get_timestamp_ms();
    std::string json(payload);
    printf("DEBUG: [%.3f ms] Received MQTT payload: %s\n", t0, payload);
    printf("DEBUG: [%.3f ms] Payload length: %zu\n", get_timestamp_ms(), json.size());

    double t1 = get_timestamp_ms();
    std::lock_guard<std::mutex> lock(config_mutex);
    printf("DEBUG: [%.3f ms] Lock acquired (took %.3f ms)\n", get_timestamp_ms(), get_timestamp_ms() - t1);

    size_t pos;

    if ((pos = json.find("\"text\"")) != std::string::npos) {
        printf("DEBUG: Found \"text\" at position: %zu\n", pos);

        size_t colon_pos = json.find(":", pos);
        if (colon_pos != std::string::npos) {
            printf("DEBUG: Found colon at position: %zu\n", colon_pos);

            size_t value_start = colon_pos + 1;
            while (value_start < json.size() && (json[value_start] == ' ' || json[value_start] == '\t')) {
                value_start++;
            }
            printf("DEBUG: Value starts at position: %zu\n", value_start);

            if (value_start < json.size() && json[value_start] == '"') {
                printf("DEBUG: Found opening quote\n");

                size_t str_start = value_start + 1;
                size_t str_end = str_start;
                while (str_end < json.size()) {
                    if (json[str_end] == '\\' && str_end + 1 < json.size()) {
                        str_end += 2;
                    } else if (json[str_end] == '"') {
                        break;
                    } else {
                        str_end++;
                    }
                }

                printf("DEBUG: String ends at position: %zu\n", str_end);

                if (str_end < json.size()) {
                    marquee_config.text = json.substr(str_start, str_end - str_start);
                    printf("DEBUG: Updated text to: %s\n", marquee_config.text.c_str());
                } else {
                    printf("DEBUG: String end not found\n");
                }
            } else {
                printf("DEBUG: No opening quote found\n");
            }
        } else {
            printf("DEBUG: Colon not found\n");
        }
    } else {
        printf("DEBUG: \"text\" field not found\n");
    }

    if ((pos = json.find("\"font_size\":")) != std::string::npos) {
        marquee_config.font_size = atoi(json.c_str() + pos + 11);
    }
    if ((pos = json.find("\"scroll_speed\":")) != std::string::npos) {
        marquee_config.scroll_speed = atoi(json.c_str() + pos + 14);
    }
    if ((pos = json.find("\"position\":")) != std::string::npos) {
        marquee_config.position = atoi(json.c_str() + pos + 11);
    }
    if ((pos = json.find("\"text_align\":")) != std::string::npos) {
        marquee_config.text_align = atoi(json.c_str() + pos + 13);
    }
    if ((pos = json.find("\"font_bold\":")) != std::string::npos) {
        marquee_config.font_bold = (json.find("true", pos + 12) != std::string::npos);
    }
    if ((pos = json.find("\"show_weather\":")) != std::string::npos) {
        marquee_config.show_weather = (json.find("true", pos + 15) != std::string::npos);
    }
    if ((pos = json.find("\"show_world_time\":")) != std::string::npos) {
        marquee_config.show_world_time = (json.find("true", pos + 18) != std::string::npos);
    }
    if ((pos = json.find("\"weather_position\":")) != std::string::npos) {
        marquee_config.weather_position = atoi(json.c_str() + pos + 18);
    }

    config_updated = true;
    printf("DEBUG: [%.3f ms] Config update complete\n", get_timestamp_ms());
}
*/
#ifdef USE_MQTT
static void on_mqtt_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    if (msg->payloadlen > 0) {
        handle_mqtt_message((const char*)msg->payload);
    }
}

static void on_mqtt_connect(struct mosquitto *mosq, void *obj, int result) {
    if (result == 0) {
        printf("MQTT connected, subscribing to %s\n", mqtt_topic.c_str());
        mosquitto_subscribe(mosq, NULL, mqtt_topic.c_str(), 0);
    } else {
        printf("MQTT connect failed: %d\n", result);
    }
}
static int init_mqtt(const char* host, int port, const char* client_id) {
    mosquitto_lib_init();
    mosq = mosquitto_new(client_id, true, NULL);
    if (!mosq) {
        fprintf(stderr, "Failed to create MQTT client\n");
        return -1;
    }

    mosquitto_connect_callback_set(mosq, on_mqtt_connect);
    mosquitto_message_callback_set(mosq, on_mqtt_message);

    // 重试连接，网络可能尚未就绪（最多重试5次，每次间隔2秒）
    int rc = MOSQ_ERR_SUCCESS;
    for (int retry = 0; retry < 5; retry++) {
        rc = mosquitto_connect(mosq, host, port, 60);
        if (rc == MOSQ_ERR_SUCCESS) break;
        fprintf(stderr, "MQTT connect attempt %d/5 failed: %s, retrying...\n",
                retry + 1, mosquitto_strerror(rc));
        sleep(2);
    }
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to connect to MQTT broker after 5 attempts: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosq = NULL;
        return -1;
    }

    mosquitto_loop_start(mosq);
    printf("MQTT connected to %s:%d\n", host, port);
    return 0;
}

static void cleanup_mqtt() {
    if (mosq) {
        mosquitto_loop_stop(mosq, true);
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
    }
}
#endif

static void sig_handler(int sig) {
    running = false;
}

// 批量获取多个城市时间，按照用户请求的顺序返回
std::vector<std::pair<std::string, std::string>> get_all_city_times() {
    std::vector<std::pair<std::string, std::string>> result;
    time_t now = time(NULL);
    struct tm* tm = gmtime(&now);
    int base_hour = tm->tm_hour;
    int base_min = tm->tm_min;

    // 按照用户请求的顺序遍历
    for (const auto& city_name : requested_city_order) {
        // 查找对应的城市
        for (const auto& city : world_cities) {
            if (city.name == city_name && city.enabled) {
                int hours = base_hour + city.timezone;
                if (hours >= 24) hours -= 24;
                if (hours < 0) hours += 24;
                char buffer[9];
                snprintf(buffer, sizeof(buffer), "%02d:%02d", hours, base_min);
                result.push_back({city.name, std::string(buffer)});
                break;
            }
        }
    }
    return result;
}

std::string get_city_time(const CityTime& city) {
    time_t now = time(NULL);
    struct tm* tm = gmtime(&now);

    int hours = tm->tm_hour + city.timezone;
    int minutes = tm->tm_min;

    if (hours >= 24) hours -= 24;
    if (hours < 0) hours += 24;

    char buffer[9];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", hours, minutes);
    return std::string(buffer);
}

void render_world_time(int x, int y, int font_size, float r, float g, float b, float a, float global_alpha,
                       int screen_w, int screen_h, FT_Face face, GLuint prog, GLuint vbo, bool font_bold) {
    int y_offset = y;
    for (const auto& city : world_cities) {
        if (!city.enabled) continue;

        std::string time_str = get_city_time(city);
        std::string display_str = city.name + "  " + time_str;

        render_text_gles_ft(display_str.c_str(), x, y_offset, font_size, r, g, b, a, global_alpha,
                           screen_w, screen_h, face, prog, vbo, font_bold);
        y_offset += font_size + 10;
    }
}

void render_weather(int x, int y, int font_size, float r, float g, float b, float a, float global_alpha,
                    int screen_w, int screen_h, FT_Face face, GLuint prog, GLuint vbo, bool font_bold) {
    std::string city_str = weather_data.city + " " + weather_data.region;
    std::string weather_str = weather_data.weather + " " + std::to_string(weather_data.temp) + "C";
    std::string humidity_str = "湿度: " + std::to_string(weather_data.humidity) + "%";

    render_text_gles_ft(city_str.c_str(), x, y, font_size, r, g, b, a, global_alpha,
                       screen_w, screen_h, face, prog, vbo, font_bold);
    render_text_gles_ft(weather_str.c_str(), x, y + font_size + 10, font_size, r, g, b, a, global_alpha,
                       screen_w, screen_h, face, prog, vbo, font_bold);
    render_text_gles_ft(humidity_str.c_str(), x, y + (font_size + 10) * 2, font_size, r, g, b, a, global_alpha,
                       screen_w, screen_h, face, prog, vbo, font_bold);
}

static time_t getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto milliseconds = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    return milliseconds.time_since_epoch().count();
}

static std::string convertTimeStamp2TimeStrMs(const std::string& fmtSecond, time_t timeStamp)
{
    int num = timeStamp % 1000;
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << num;

    time_t seconds = timeStamp / 1000;

    struct tm *timeinfo = nullptr;
    char buffer[80];
    timeinfo = localtime(&seconds);
    strftime(buffer, 80, fmtSecond.c_str(), timeinfo);
    return std::string(buffer) + "." + oss.str();
}

///////////////////
FT_Library ft;
FT_Face face;
GLuint prog;
GLuint vbo;
///////////////////

static PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = NULL;

const char* text_vs =
"attribute vec2 a_pos;\n"
"attribute vec2 a_uv;\n"
"uniform vec2 u_screen;\n"
"varying vec2 v_uv;\n"
"void main() {\n"
"    vec2 ndc = (a_pos / u_screen) * 2.0 - 1.0;\n"
"    ndc.y = -ndc.y;\n"
"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
"    v_uv = a_uv;\n"
"}\n";

const char* text_fs =
"precision mediump float;\n"
"varying vec2 v_uv;\n"
"uniform sampler2D u_tex;\n"
"uniform vec4 u_color;\n"
"void main() {\n"
"    float a = texture2D(u_tex, v_uv).r;\n"
"    gl_FragColor = vec4(u_color.rgb, u_color.a * a);\n"
"}\n";

const char* vs_src =
    "attribute vec2 pos;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
const char* fs_src =
    "precision mediump float;\n"
    "uniform vec4 u_bg_color;\n"
    "void main() { gl_FragColor = u_bg_color; }\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
        fprintf(stderr, "shader compile error: %s\n", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static int drm_find_connector(int drm_fd, drmModeConnector **out_conn, drmModeRes **out_res) {
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) return -1;
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; ++i) {
        conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (!conn) continue;
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            *out_conn = conn;
            *out_res = res;
            return 0;
        }
        drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);
    return -1;
}

static int drm_find_crtc_for_connector(drmModeRes *res, drmModeConnector *conn, int drm_fd, uint32_t *crtc_id) {
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
        if (enc) {
            if (enc->crtc_id) {
                *crtc_id = enc->crtc_id;
                drmModeFreeEncoder(enc);
                return 0;
            }
            drmModeFreeEncoder(enc);
        }
    }
    for (int i = 0; i < conn->count_encoders; ++i) {
        drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoders[i]);
        if (!enc) continue;
        for (int j = 0; j < res->count_crtcs; ++j) {
            uint32_t possible = (1 << j);
            if (enc->possible_crtcs & possible) {
                *crtc_id = res->crtcs[j];
                drmModeFreeEncoder(enc);
                return 0;
            }
        }
        drmModeFreeEncoder(enc);
    }
    return -1;
}

static int add_fb_for_bo(int drm_fd, struct gbm_bo *bo, uint32_t width, uint32_t height, uint32_t *fb_id, uint32_t drm_format) {
    uint32_t handles[4] = {0}, strides[4] = {0}, offsets[4] = {0};
    int ret;

    union gbm_bo_handle handle = gbm_bo_get_handle(bo);
    handles[0] = handle.u32;
    strides[0] = gbm_bo_get_stride(bo);
    offsets[0] = 0;
    ret = drmModeAddFB2(drm_fd, width, height, drm_format,
                        handles, strides, offsets, fb_id, 0);

    if (ret) {
        uint32_t pitch = strides[0];
        ret = drmModeAddFB(drm_fd, width, height, 24, 32, pitch, handles[0], fb_id);
        if (ret) {
            fprintf(stderr, "drmModeAddFB2/AddFB failed: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

static void dump_plane_props(int drm_fd, uint32_t plane_id) {
    drmModeObjectProperties *props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) { printf("(no props for plane %u)\n", plane_id); return; }
    printf("Plane %u has %u props:\n", plane_id, props->count_props);
    for (uint32_t i = 0; i < props->count_props; ++i) {
        uint32_t pid = props->props[i];
        drmModePropertyRes *p = drmModeGetProperty(drm_fd, pid);
        if (!p) continue;
        printf("  prop %u: name='%s' flags=0x%x\n", pid, p->name ? p->name : "(null)", p->flags);
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
}

static int try_set_plane_alpha(int drm_fd, uint32_t plane_id, float alpha) {
    drmModeObjectProperties *props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) return -1;
    int found = 0;
    int ret = -1;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        uint32_t pid = props->props[i];
        drmModePropertyRes *p = drmModeGetProperty(drm_fd, pid);
        if (!p) continue;
        if (p->name && strcasestr(p->name, "alpha") != NULL) {
            found = 1;
            uint64_t candidates[3];
            if (alpha >= 1.0f) {
                candidates[0] = 65535ULL; candidates[1] = 255ULL; candidates[2] = 0xFFFFFFFFULL;
            } else if (alpha <= 0.0f) {
                candidates[0] = 0ULL; candidates[1] = 0ULL; candidates[2] = 0ULL;
            } else {
                candidates[0] = (uint64_t)(alpha * 65535.0f);
                candidates[1] = (uint64_t)(alpha * 255.0f);
                candidates[2] = (uint64_t)(alpha * 0xFFFFFFFFULL);
            }
            for (int ci = 0; ci < 3; ++ci) {
                uint64_t val = candidates[ci];
                if (drmModeObjectSetProperty(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, pid, val) == 0) {
                    ret = 0; break;
                }
            }
        }
        drmModeFreeProperty(p);
        if (ret == 0) break;
    }
    if (!found) {
        drmModeFreeObjectProperties(props);
        return -1;
    }
    drmModeFreeObjectProperties(props);
    return ret;
}

void cleanup_egl_ctx(EGLDisplay egl_dpy, EGLSurface egl_surf, EGLContext egl_ctx) {
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
}

void cleanup_gbm(struct gbm_surface *gbm_surf, struct gbm_device *gbm, drmModeConnector *conn, drmModeRes *res, int drm_fd)
{
    gbm_surface_destroy(gbm_surf);
    gbm_device_destroy(gbm);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    close(drm_fd);
}

void egl_terminate(EGLDisplay egl_dpy) {
    eglTerminate(egl_dpy);
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        fprintf(stderr, "Shader compile error: %s\n", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint linkProgram(const char* vs_src, const char* fs_src) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); glDeleteShader(fs); return 0; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    glLinkProgram(prog);
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        fprintf(stderr, "Program link error: %s\n", buf);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void font_init() {
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "FT init failed\n");
        return;
    }

    // 探测TTC face数量（-1只读face数，不加载字体数据）
    FT_Long numFaces = 0;
    FT_Face tmpFace = nullptr;
    if (FT_New_Face(ft, FONT_PATH, -1, &tmpFace) == 0) {
        numFaces = tmpFace->num_faces;
        FT_Done_Face(tmpFace);
    }
    printf("Font has %ld face(s)\n", numFaces);

    // 优先尝试face 2（NotoSansCJK SC简体中文），回退到face 0
    int idx = (numFaces > 2) ? 2 : 0;
    if (FT_New_Face(ft, FONT_PATH, idx, &face)) {
        fprintf(stderr, "Load font failed: %s\n", FONT_PATH);
        ft = nullptr;
        return;
    }
    printf("Using face %d\n", idx);

    FT_Set_Pixel_Sizes(face, 0, 48);
    printf("Font loaded successfully: %s\n", FONT_PATH);
}

void font_fini() {
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

static uint32_t utf8_next(const unsigned char **s) {
    const unsigned char *p = *s;
    uint32_t cp = 0;
    if (*p < 0x80) { cp = *p++; }
    else if ((*p & 0xE0) == 0xC0) { cp = (*p & 0x1F) << 6; cp |= (p[1] & 0x3F); p += 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = (*p & 0x0F) << 12; cp |= (p[1] & 0x3F) << 6; cp |= (p[2] & 0x3F); p += 3; }
    else if ((*p & 0xF8) == 0xF0) { cp = (*p & 0x07) << 18; cp |= (p[1] & 0x3F) << 12; cp |= (p[2] & 0x3F) << 6; cp |= (p[3] & 0x3F); p += 4; }
    else { p++; }
    *s = p;
    return cp;
}

static int compute_text_width(const char *text, FT_Face face) {
    if (!text || !face) return 100; // face未加载时返回默认宽度，避免除零等异常
    const unsigned char *s = (const unsigned char*)text;
    FT_UInt prev_glyph = 0;
    int width = 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        if (cp == 0) continue;
        FT_UInt glyph_index = FT_Get_Char_Index(face, (FT_ULong)cp);
        if (glyph_index == 0) {
            continue;
        }
        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT)) continue;

        if (FT_HAS_KERNING(face) && prev_glyph && glyph_index) {
            FT_Vector kern;
            if (FT_Get_Kerning(face, prev_glyph, glyph_index, FT_KERNING_DEFAULT, &kern) == 0) {
                width += (kern.x >> 6);
            }
        }

        width += (face->glyph->advance.x >> 6);
        prev_glyph = glyph_index;
    }
    return width;
}

void render_text_gles_ft(const char* text_utf8,
                         float x, float y, float font_px,
                         float r, float g, float b, float a,
                         float global_alpha,
                         int screen_w, int screen_h,
                         FT_Face face,
                         GLuint prog, GLuint vbo,
                         bool font_bold)
{
    if (!text_utf8 || !face || !prog) return;

    float pen_x = x;
    float pen_y = y;

    glUseProgram(prog);
    GLint loc_screen = glGetUniformLocation(prog, "u_screen");
    GLint loc_color = glGetUniformLocation(prog, "u_color");
    GLint loc_tex = glGetUniformLocation(prog, "u_tex");
    if (loc_screen >= 0) glUniform2f(loc_screen, (float)screen_w, (float)screen_h);
    if (loc_color >= 0) glUniform4f(loc_color, r, g, b, a * global_alpha);
    glActiveTexture(GL_TEXTURE0);
    if (loc_tex >= 0) glUniform1i(loc_tex, 0);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    const unsigned char* s = (const unsigned char*)text_utf8;
    while (*s) {
        uint32_t codepoint = utf8_next(&s);
        if (codepoint == 0) continue;
        if (FT_Load_Char(face, (FT_ULong)codepoint, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot g = face->glyph;
        int gw = g->bitmap.width;
        int gh = g->bitmap.rows;
        if (gw == 0 || gh == 0) {
            pen_x += (float)(g->advance.x >> 6);
            continue;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, gw, gh, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, g->bitmap.buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        float x0 = pen_x + (float)g->bitmap_left;
        float y0 = pen_y - (float)g->bitmap_top;
        float x1 = x0 + gw;
        float y1 = y0 + gh;

        if (font_bold) {
            float offsets[] = {-1.0f, 1.0f};
            for (float dx : offsets) {
                float verts[6 * 4] = {
                    x0 + dx, y0, 0.0f, 0.0f,
                    x1 + dx, y0, 1.0f, 0.0f,
                    x0 + dx, y1, 0.0f, 1.0f,
                    x0 + dx, y1, 0.0f, 1.0f,
                    x1 + dx, y0, 1.0f, 0.0f,
                    x1 + dx, y1, 1.0f, 1.0f
                };
                glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
                glBindTexture(GL_TEXTURE_2D, tex);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }

        float verts[6 * 4] = {
            x0, y0, 0.0f, 0.0f,
            x1, y0, 1.0f, 0.0f,
            x0, y1, 0.0f, 1.0f,
            x0, y1, 0.0f, 1.0f,
            x1, y0, 1.0f, 0.0f,
            x1, y1, 1.0f, 1.0f
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &tex);

        pen_x += (float)(g->advance.x >> 6);
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

// 绘制矩形边框
static void render_border(int x, int y, int w, int h, int border_w,
                          float r, float g, float b, float a,
                          int screen_w, int screen_h, GLuint prog, GLuint vbo)
{
    if (border_w <= 0) return;
    if (!prog) return;

    glUseProgram(prog);
    GLint loc_screen = glGetUniformLocation(prog, "u_screen");
    GLint loc_color = glGetUniformLocation(prog, "u_color");
    if (loc_screen >= 0) glUniform2f(loc_screen, (float)screen_w, (float)screen_h);
    if (loc_color >= 0) glUniform4f(loc_color, r, g, b, a);

    // 四条边：上、下、左、右
    float lines[][4] = {
        // 上边
        {(float)x, (float)y, (float)x + w, (float)y},
        // 下边
        {(float)x, (float)y + h, (float)x + w, (float)y + h},
        // 左边
        {(float)x, (float)y, (float)x, (float)y + h},
        // 右边
        {(float)x + w, (float)y, (float)x + w, (float)y + h}
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glLineWidth((float)border_w);
    for (auto& line : lines) {
        float verts[] = {
            line[0], line[1], 0.f,0.f,
            line[2], line[3], 0.f,0.f
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glDrawArrays(GL_LINES, 0, 2);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

int main(int argc, char** argv) {
    // 初始化启动配置默认值（宽高默认7680x4320）
    startup_config.plane_id = 0;
    startup_config.win_w = 7680;
    startup_config.win_h = 4320;
    startup_config.pos_x = 0;
    startup_config.pos_y = 0;
    startup_config.alpha = 1.0f;
    startup_config.show_weather = false;
    startup_config.show_world_time = false;
    startup_config.weather_position = 0;
    startup_config.mqtt_host = "localhost";
    startup_config.mqtt_port = 1883;
    startup_config.mqtt_client_id = "marquee_client";
    startup_config.mqtt_topic = "marquee/control";

    // 从配置文件加载启动参数
    load_startup_config();

    uint32_t target_plane = startup_config.plane_id;
    uint32_t win_w = startup_config.win_w > 0 ? startup_config.win_w : 400;
    uint32_t win_h = startup_config.win_h > 0 ? startup_config.win_h : 400;

    // 记录屏幕高度（用于后续 pos_y 自动计算）
    if (startup_config.win_h > 0) screen_h = startup_config.win_h;
    int pos_x = startup_config.pos_x, pos_y = startup_config.pos_y;
    float alpha = startup_config.alpha;
    int show_timestamp = 0;
    int show_timestamp_align = 0;

    float font_size = 48.0f;
    float text_r = 1.0f, text_g = 0.9f, text_b = 0.2f, text_a = 1.0f;
    float bg_r = 0.2f, bg_g = 0.2f, bg_b = 0.3f, bg_a = 1.0f;

    int position = 0;
    int scroll_speed = 3;
    int font_bold = 0;
    int text_align = 0;

    int display_mode = 0;
    std::string add_city;
    int show_world_time = 0;
    int show_weather = 0;
    int weather_position = 0;
    std::string custom_text = "这是一段测试文字";

#ifdef USE_MQTT
    std::string mqtt_host = startup_config.mqtt_host;
    int mqtt_port = startup_config.mqtt_port;
    std::string mqtt_client_id = startup_config.mqtt_client_id;
#endif

    marquee_config.text = custom_text;
    marquee_config.font_size = font_size;
    marquee_config.text_color[0] = text_r;
    marquee_config.text_color[1] = text_g;
    marquee_config.text_color[2] = text_b;
    marquee_config.bg_color[0] = bg_r;
    marquee_config.bg_color[1] = bg_g;
    marquee_config.bg_color[2] = bg_b;
    marquee_config.position = position;
    marquee_config.scroll_speed = scroll_speed;
    marquee_config.font_bold = font_bold;
    marquee_config.text_align = text_align;
    marquee_config.show_weather = show_weather;
    marquee_config.show_world_time = show_world_time;
    marquee_config.weather_position = weather_position;

	 // ============ 新增字段默认初始化 ============
    marquee_config.scroll_mode = 0;
    marquee_config.stay_time = 1000;
    marquee_config.alpha = 1.0f;
    marquee_config.font_italic = 0;
    marquee_config.border_color[0] = 0.0f;
    marquee_config.border_color[1] = 0.0f;
    marquee_config.border_color[2] = 0.0f;
    marquee_config.border_color[3] = 1.0f;
    marquee_config.border_width = 0;
    marquee_config.screen_align = 0;
    marquee_config.enable = false;  // 默认关闭，等待MQTT激活
    marquee_config.height = 70;     // 默认跑马灯区域高度70px
	marquee_config.pos_y = pos_y; // 用命令行解析后的 pos_y 初始化
    // ==========================================

    for (int i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "--plane") || !strcmp(argv[i], "-p")) && i + 1 < argc) {
            target_plane = (uint32_t)strtoul(argv[i+1], NULL, 0);
            i++;
        } else if ((!strcmp(argv[i], "--win-w") || !strcmp(argv[i], "-W")) && i + 1 < argc) {
            win_w = (uint32_t)strtoul(argv[i+1], NULL, 0); i++;
        } else if ((!strcmp(argv[i], "--win-h") || !strcmp(argv[i], "-H")) && i + 1 < argc) {
            win_h = (uint32_t)strtoul(argv[i+1], NULL, 0); i++;
        } else if ((!strcmp(argv[i], "--x") || !strcmp(argv[i], "-X")) && i + 1 < argc) {
            pos_x = atoi(argv[i+1]); i++;
        } else if ((!strcmp(argv[i], "--y") || !strcmp(argv[i], "-Y")) && i + 1 < argc) {
            pos_y = atoi(argv[i+1]); i++;
        } else if ((!strcmp(argv[i], "--alpha") || !strcmp(argv[i], "-a")) && i + 1 < argc) {
            alpha = strtof(argv[i+1], NULL); if (alpha < 0) alpha = 0; if (alpha > 1) alpha = 1; i++;
        } else if ((!strcmp(argv[i], "--show-timestamp") || !strcmp(argv[i], "-T"))) {
            show_timestamp = 1;
        } else if ((!strcmp(argv[i], "--show-timestamp-align") || !strcmp(argv[i], "-A"))) {
            show_timestamp_align = 1;
        } else if ((!strcmp(argv[i], "--font-size") || !strcmp(argv[i], "-fs")) && i + 1 < argc) {
            font_size = strtof(argv[i+1], NULL); if (font_size < 8) font_size = 8; if (font_size > 256) font_size = 256; i++;
        } else if ((!strcmp(argv[i], "--text-color") || !strcmp(argv[i], "-tc")) && i + 4 < argc) {
            text_r = strtof(argv[i+1], NULL); if (text_r < 0) text_r = 0; if (text_r > 1) text_r = 1;
            text_g = strtof(argv[i+2], NULL); if (text_g < 0) text_g = 0; if (text_g > 1) text_g = 1;
            text_b = strtof(argv[i+3], NULL); if (text_b < 0) text_b = 0; if (text_b > 1) text_b = 1;
            text_a = strtof(argv[i+4], NULL); if (text_a < 0) text_a = 0; if (text_a > 1) text_a = 1;
            i += 4;
        } else if ((!strcmp(argv[i], "--bg-color") || !strcmp(argv[i], "-bg")) && i + 4 < argc) {
            bg_r = strtof(argv[i+1], NULL); if (bg_r < 0) bg_r = 0; if (bg_r > 1) bg_r = 1;
            bg_g = strtof(argv[i+2], NULL); if (bg_g < 0) bg_g = 0; if (bg_g > 1) bg_g = 1;
            bg_b = strtof(argv[i+3], NULL); if (bg_b < 0) bg_b = 0; if (bg_b > 1) bg_b = 1;
            bg_a = strtof(argv[i+4], NULL); if (bg_a < 0) bg_a = 0; if (bg_a > 1) bg_a = 1;
            i += 4;
        } else if ((!strcmp(argv[i], "--position") || !strcmp(argv[i], "-pos")) && i + 1 < argc) {
            position = atoi(argv[i+1]); if (position < 0) position = 0; if (position > 1) position = 1; i++;
        } else if ((!strcmp(argv[i], "--scroll-speed") || !strcmp(argv[i], "-ss")) && i + 1 < argc) {
            scroll_speed = atoi(argv[i+1]); if (scroll_speed < 0) scroll_speed = 0; if (scroll_speed > 10) scroll_speed = 10; i++;
        } else if ((!strcmp(argv[i], "--font-bold") || !strcmp(argv[i], "-fb"))) {
            font_bold = 1;
        } else if ((!strcmp(argv[i], "--text-align") || !strcmp(argv[i], "-ta")) && i + 1 < argc) {
            text_align = atoi(argv[i+1]); if (text_align < 0) text_align = 0; if (text_align > 1) text_align = 1; i++;
        } else if ((!strcmp(argv[i], "--display-mode") || !strcmp(argv[i], "-dm")) && i + 1 < argc) {
            display_mode = atoi(argv[i+1]); if (display_mode < 0) display_mode = 0; if (display_mode > 2) display_mode = 2; i++;
        } else if ((!strcmp(argv[i], "--add-city") || !strcmp(argv[i], "-ac")) && i + 1 < argc) {
            add_city = argv[i+1]; i++;
        } else if ((!strcmp(argv[i], "--show-world-time") || !strcmp(argv[i], "-wt"))) {
            show_world_time = 1;
        } else if ((!strcmp(argv[i], "--show-weather") || !strcmp(argv[i], "-we"))) {
            show_weather = 1;
        } else if ((!strcmp(argv[i], "--text") || !strcmp(argv[i], "-t")) && i + 1 < argc) {
            custom_text = argv[i+1]; i++;
            marquee_config.text = custom_text;
        } else if ((!strcmp(argv[i], "--weather-position") || !strcmp(argv[i], "-wp")) && i + 1 < argc) {
            weather_position = atoi(argv[i+1]);
            if (weather_position < 0) weather_position = 0;
            if (weather_position > 2) weather_position = 2;
            i++;
            marquee_config.weather_position = weather_position;
        }
#ifdef USE_MQTT
        else if ((!strcmp(argv[i], "--mqtt-host") || !strcmp(argv[i], "-mh")) && i + 1 < argc) {
            mqtt_host = argv[i+1]; i++;
        } else if ((!strcmp(argv[i], "--mqtt-port") || !strcmp(argv[i], "-mp")) && i + 1 < argc) {
            mqtt_port = atoi(argv[i+1]); i++;
        } else if ((!strcmp(argv[i], "--mqtt-client-id") || !strcmp(argv[i], "-mc")) && i + 1 < argc) {
            mqtt_client_id = argv[i+1]; i++;
        }
#endif
    }

    if (!add_city.empty()) {
        for (auto& city : world_cities) {
            if (city.name == add_city) {
                city.enabled = true;
                printf("Enabled city: %s\n", add_city.c_str());
                break;
            }
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

#ifdef USE_MQTT
    if (init_mqtt(mqtt_host.c_str(), mqtt_port, mqtt_client_id.c_str()) != 0) {
        fprintf(stderr, "Warning: Failed to initialize MQTT, continuing without MQTT support\n");
    }
#endif

    const char* drm_dev = "/dev/dri/card0";
    int drm_fd = open(drm_dev, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { perror("open drm"); return 1; }

    drmModeConnector *conn = NULL;
    drmModeRes *res = NULL;
    if (drm_find_connector(drm_fd, &conn, &res) != 0) {
        fprintf(stderr, "No connected connector found\n");
        close(drm_fd);
        return 1;
    }
    drmModeModeInfo mode = conn->modes[0];
    uint32_t width = mode.hdisplay, height = mode.vdisplay;
    // 校验DRM分辨率，异常时使用默认值7680x4320
    if (width <= 0 || width > 7680 || height <= 0 || height > 4320) {
        fprintf(stderr, "DRM resolution %dx%d invalid, using default 7680x4320\n", width, height);
        width = 7680;
        height = 4320;
    }
    printf("Found connector %d mode %dx%d\n", conn->connector_id, width, height);

    uint32_t crtc_id = 0;
    if (drm_find_crtc_for_connector(res, conn, drm_fd, &crtc_id) != 0) {
        fprintf(stderr, "No crtc found\n");
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        close(drm_fd);
        return 1;
    }
    printf("Using crtc %u\n", crtc_id);

    int crtc_index = -1;
    for (int i = 0; i < res->count_crtcs; ++i) {
        if (res->crtcs[i] == crtc_id) { crtc_index = i; break; }
    }
    if (crtc_index < 0) {
        fprintf(stderr, "Failed to find crtc index for crtc %u\n", crtc_id);
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        close(drm_fd);
        return 1;
    }

    drmModePlane *plane = NULL;
    if (target_plane) {
        plane = drmModeGetPlane(drm_fd, target_plane);
        if (!plane) {
            fprintf(stderr, "Requested plane %u not found\n", target_plane);
            target_plane = 0;
        } else {
            if (!(plane->possible_crtcs & (1 << crtc_index))) {
                fprintf(stderr, "Plane %u is not usable with crtc index %d\n", target_plane, crtc_index);
                drmModeFreePlane(plane);
                plane = NULL;
                target_plane = 0;
            } else {
                printf("Using plane %u for presentation\n", target_plane);
            }
        }
    }

    struct gbm_device *gbm = gbm_create_device(drm_fd);
    if (!gbm) { fprintf(stderr, "gbm_create_device failed\n"); close(drm_fd); return 1; }

    uint32_t surf_w = (target_plane ? win_w : width);
    uint32_t surf_h = (target_plane ? win_h : height);
    bool use_argb = true;
    uint32_t gbm_format = use_argb ? GBM_FORMAT_ARGB8888 : GBM_FORMAT_XRGB8888;
    struct gbm_surface *gbm_surf = gbm_surface_create(gbm, surf_w, surf_h,
                                                      gbm_format,
                                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surf) { fprintf(stderr, "gbm_surface_create failed\n"); gbm_device_destroy(gbm); close(drm_fd); return 1; }

    eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    if (eglGetPlatformDisplayEXT) {
        egl_dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    } else {
        egl_dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    }
    if (egl_dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetDisplay failed\n");
        cleanup_gbm(gbm_surf, gbm, conn, res, drm_fd);
        return -1;
    }
    if (!eglInitialize(egl_dpy, NULL, NULL)) {
        fprintf(stderr, "eglInitialize failed\n");
        cleanup_gbm(gbm_surf, gbm, conn, res, drm_fd);
        return -1;
    }

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, use_argb ? 8 : 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };
    EGLConfig egl_conf;
    EGLint num_conf;
    if (!eglChooseConfig(egl_dpy, config_attribs, &egl_conf, 1, &num_conf) || num_conf < 1) {
        fprintf(stderr, "eglChooseConfig failed\n");
        egl_terminate(egl_dpy);
    }

    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext egl_ctx = eglCreateContext(egl_dpy, egl_conf, EGL_NO_CONTEXT, ctx_attribs);
    if (egl_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed\n");
        egl_terminate(egl_dpy);
        return -1;
    }

    EGLSurface egl_surf = eglCreateWindowSurface(egl_dpy, egl_conf, (EGLNativeWindowType)gbm_surf, NULL);
    if (egl_surf == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface failed\n");
        eglDestroyContext(egl_dpy, egl_ctx);
        egl_terminate(egl_dpy);
        return -1;
    }

    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) {
        fprintf(stderr, "eglMakeCurrent failed\n"); eglDestroySurface(egl_dpy, egl_surf);
        eglDestroyContext(egl_dpy, egl_ctx);
        egl_terminate(egl_dpy);
        return -1;
    }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, text_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, text_fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    glLinkProgram(prog);

    GLfloat verts[] = { 0.0f,  0.5f, -0.5f, -0.5f, 0.5f, -0.5f };
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glViewport(0, 0, surf_w, surf_h);

    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    if (use_argb) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    GLint uni_alpha = glGetUniformLocation(prog, "u_alpha");
    //glUniform1f(uni_alpha, alpha);
	glUniform1f(uni_alpha, marquee_config.alpha);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (!eglSwapBuffers(egl_dpy, egl_surf)) {
        fprintf(stderr, "eglSwapBuffers failed on initial swap\n");
    }

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    if (!bo) {
        fprintf(stderr, "gbm_surface_lock_front_buffer failed\n");
        cleanup_egl_ctx(egl_dpy, egl_surf, egl_ctx);
        return -1;
    }
    uint32_t fb;
    uint32_t drm_format = gbm_format;
    if (add_fb_for_bo(drm_fd, bo, surf_w, surf_h, &fb, drm_format) != 0) {
        fprintf(stderr, "add_fb failed\n"); gbm_surface_release_buffer(gbm_surf, bo);
        cleanup_egl_ctx(egl_dpy, egl_surf, egl_ctx);
        return -1;
    }

    if (!target_plane) {
        if (drmModeSetCrtc(drm_fd, crtc_id, fb, 0, 0, &conn->connector_id, 1, &mode)) {
            fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno));
        }
    }

    font_init();

    // 启动时从持久化文件加载上次的MQTT消息配置
    {
        char msg_buf[8192] = {0};
        if (load_mqtt_message(msg_buf, sizeof(msg_buf))) {
            printf("Loaded persisted MQTT message, applying saved config...\n");
            handle_mqtt_message(msg_buf);
        } else {
            printf("No persisted MQTT message found, waiting for activation...\n");
        }
    }

    uint32_t prev_fb = fb;
    struct gbm_bo *prev_bo = bo;
    auto last_frame_time = std::chrono::steady_clock::now();
    int frame = 0;
    static int text_x = win_w;
    static int text_width = 0;
    static std::string cached_marquee_text;  // 缓存完整显示文本（含时间）
    static std::string cached_base_text;     // 缓存基础文本（不含时间，用于判断是否重置位置）
    static long long last_time_update_ms = 0;
    static int last_scroll_mode = -1;

	// ========= 新增：滚动/停留状态变量 =========
	static bool scroll_once_finished = false;
	static long long stay_start_ms = 0;
	// =========================================

    while(running) {
        double frame_start = get_timestamp_ms();

        frame++;
        if(frame == win_w) {
            frame = 0;
        }

        std::lock_guard<std::mutex> lock(config_mutex);

        // 未激活时只渲染透明背景，不显示跑马灯内容
        if (!marquee_active) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            eglSwapBuffers(egl_dpy, egl_surf);

            struct gbm_bo *idle_bo = gbm_surface_lock_front_buffer(gbm_surf);
            if (idle_bo) {
                uint32_t idle_fb;
                if (add_fb_for_bo(drm_fd, idle_bo, surf_w, surf_h, &idle_fb, drm_format) == 0) {
                    if (target_plane) {
                        uint32_t src_w = surf_w << 16;
                        uint32_t src_h = surf_h << 16;
                        drmModeSetPlane(drm_fd, target_plane, crtc_id, idle_fb, 0,
                                        pos_x, marquee_config.pos_y, surf_w, surf_h,
                                        0, 0, src_w, src_h);
                    }
                    drmModeRmFB(drm_fd, idle_fb);
                }
                gbm_surface_release_buffer(gbm_surf, idle_bo);
            }
            usleep(100000); // 未激活时100ms刷新一次，降低CPU占用
            continue;
        }

        glClearColor(marquee_config.bg_color[0], marquee_config.bg_color[1],
                     marquee_config.bg_color[2], marquee_config.alpha);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
        GLint loc_bg_color = glGetUniformLocation(prog, "u_bg_color");
        if (loc_bg_color >= 0) glUniform4f(loc_bg_color,
            marquee_config.bg_color[0], marquee_config.bg_color[1],
            marquee_config.bg_color[2], marquee_config.alpha);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // 字体已加载成功才进行文字渲染，避免 SEGV
        if (face) {
            FT_Set_Pixel_Sizes(face, 0, (FT_UInt)marquee_config.font_size);

            FT_Matrix matrix;
            matrix.xx = 0x10000;
            matrix.xy = marquee_config.font_italic ? 0x2000 : 0;
            matrix.yx = 0;
            matrix.yy = 0x10000;
            FT_Set_Transform(face, &matrix, NULL);
        }

        // 文字在跑马灯区域内垂直居中
        // 注意：drmModeSetPlane 已通过 pos_y 将 surface 定位到屏幕顶部/底部，
        // surface 内部始终从 y=0 开始渲染，不要加 region_offset
        int render_h = marquee_config.height > 0 ? marquee_config.height : (int)win_h;
        int text_y = (render_h + marquee_config.font_size) / 2;

        // 字体未加载时只渲染背景色，跳过文字渲染避免SEGV
        if (face) {
            if (display_mode == 1) {
                render_world_time(50, marquee_config.font_size, marquee_config.font_size,
                             marquee_config.text_color[0], marquee_config.text_color[1],
                             marquee_config.text_color[2], 1.0f,
                             marquee_config.alpha,
                             win_w, win_h, face, prog, vbo,
                             marquee_config.font_bold);
            } else if (display_mode == 2) {
                int weather_x = marquee_config.text_align == 0 ? 50 : (win_w - 300);
                render_weather(weather_x, marquee_config.font_size, marquee_config.font_size,
                          marquee_config.text_color[0], marquee_config.text_color[1],
                          marquee_config.text_color[2], 1.0f,
                          marquee_config.alpha,
                          win_w, win_h, face, prog, vbo,
                          marquee_config.font_bold);
            } else if (show_timestamp) {
            time_t now = getCurrentTime();
            int time_x = 50;
            if(show_timestamp_align) {
                time_x = win_w - 600;
            }
            std::string time_str = convertTimeStamp2TimeStrMs("%Y-%m-%d %H:%M:%S", now);
            render_text_gles_ft(time_str.c_str(), time_x, text_y, marquee_config.font_size,
                                marquee_config.text_color[0], marquee_config.text_color[1],
                                marquee_config.text_color[2], 1.0f,
                                marquee_config.alpha,
                                win_w, win_h, face, prog, vbo,
                                marquee_config.font_bold);
            } else {
                long long now_ms = (long long)get_timestamp_ms();

            // ===== 每秒或配置变化时重建完整显示文本 =====
            if (config_updated || marquee_config.scroll_mode != last_scroll_mode
                || now_ms - last_time_update_ms >= 1000)
            {
                bool config_changed = config_updated || marquee_config.scroll_mode != last_scroll_mode;
                if (config_changed) {
                    config_updated = false;
                    last_scroll_mode = marquee_config.scroll_mode;
                }
                last_time_update_ms = now_ms;

                // 构建基础文本（不含时间和世界时间城市名），按照用户请求的顺序
                std::string base_text;
                if (marquee_config.show_weather && marquee_config.weather_position == 0) {
                    base_text += weather_data.city + ": " + weather_data.weather + " " +
                                 std::to_string(weather_data.temp) + "°C | ";
                }
                base_text += marquee_config.text;
                // 天气预报随文字滚动时，追加到基础文本
                if (weather_forecast.enabled && weather_forecast.position == 2) {
                    base_text += " | " + weather_forecast.city + ": " + weather_forecast.weather + " " +
                                 std::to_string(weather_forecast.temp_low) + "°C~" +
                                 std::to_string(weather_forecast.temp_high) + "°C";
                }
                // 注意：世界时间城市名+时间不在base_text中，只在完整文本中添加
                if (marquee_config.show_weather && marquee_config.weather_position == 1) {
                    base_text += " | " + weather_data.city + ": " + weather_data.weather + " " +
                                 std::to_string(weather_data.temp) + "°C";
                }

                // 基础文本变化时才重置位置（时间变化不重置，config变化但base_text不变也不重置）
                bool base_text_changed = (base_text != cached_base_text);
                if (base_text_changed) {
                    cached_base_text = base_text;
                    // 循环滚动模式(0和3)不重置位置，非循环模式(1,2)才重置
                    if (marquee_config.scroll_mode == 0) {
                        // 模式0(右到左循环)：保持当前位置
                        // 等完整文本构建后再计算text_width进行边界检查
                    } else if (marquee_config.scroll_mode == 3) {
                        // 模式3(左到右循环)：保持当前位置
                    } else {
                        // 模式1(单次滚动)和模式2(静止)：重置到起始位置
                        text_x = win_w;
                        scroll_once_finished = false;
                        stay_start_ms = 0;
                    }
                }

                // 构建完整显示文本（城市+时间配对）- 只有需要更新时才重建
                bool marquee_text_rebuilt = false;
                if (base_text_changed || now_ms - last_time_update_ms >= 1000) {
                    // 使用基础文本作为起点（已包含天气预报随滚动内容）
                    cached_marquee_text = base_text;
                    // 添加时间信息
                    if (marquee_config.show_world_time) {
                        // 批量获取所有城市时间，只调用一次系统时间
                        auto city_times = get_all_city_times();
                        for (const auto& ct : city_times) {
                            cached_marquee_text += " | " + ct.first + " " + ct.second;
                        }
                    }
                    if (marquee_config.show_weather && marquee_config.weather_position == 1) {
                        cached_marquee_text += " | " + weather_data.city + ": " + weather_data.weather + " " +
                                              std::to_string(weather_data.temp) + "°C";
                    }
                    marquee_text_rebuilt = true;
                }

                // 使用完整显示文本（含时间）计算实际宽度
                if (marquee_text_rebuilt) {
                    text_width = compute_text_width(cached_marquee_text.c_str(), face);
                    // 循环模式在重建文本后进行边界检查
                    if (marquee_config.scroll_mode == 0) {
                        // 模式0：如果当前位置已超出新文本范围，从右侧重新开始
                        if (text_x < -text_width) {
                            text_x = win_w;
                        }
                    } else if (marquee_config.scroll_mode == 3) {
                        // 模式3：如果当前位置已超出新文本范围，从左侧重新开始
                        if (text_x > (int)win_w) {
                            text_x = -text_width;
                        }
                    }
                }
            }

            int base_x = 0;
            int base_y = text_y;

            // 计算天气预报固定区域宽度和边距
            int forecast_fixed_width = 0;
            int margin = 20;
            if (weather_forecast.enabled && weather_forecast.position != 2) {
                std::string forecast_text = weather_forecast.city + ": " + weather_forecast.weather + " " +
                                           std::to_string(weather_forecast.temp_low) + "°C~" +
                                           std::to_string(weather_forecast.temp_high) + "°C";
                forecast_fixed_width = compute_text_width(forecast_text.c_str(), face) + margin * 2;
            }

            // ========= 1. 屏幕整体对齐 screen_align:0左 1中 2右 =========
            if (marquee_config.scroll_speed <= 0 || marquee_config.scroll_mode == 2)
            {
                // 速度=0或静止模式：按screen_align对齐
                // 考虑天气预报固定区域
                int available_width = (int)win_w - forecast_fixed_width;
                if (marquee_config.screen_align == 1) {
                    base_x = ((int)win_w - forecast_fixed_width - text_width) / 2;
                    if (weather_forecast.enabled && weather_forecast.position == 0) {
                        base_x += forecast_fixed_width; // 左侧有固定区域，向右偏移
                    }
                } else if (marquee_config.screen_align == 2) {
                    base_x = (int)win_w - text_width - margin;
                    if (weather_forecast.enabled && weather_forecast.position == 1) {
                        base_x -= forecast_fixed_width; // 右侧有固定区域，向左偏移
                    }
                } else {
                    base_x = margin;
                    if (weather_forecast.enabled && weather_forecast.position == 0) {
                        base_x += forecast_fixed_width; // 左侧有固定区域，向右偏移
                    }
                }
                // 确保不超出屏幕
                if (base_x < margin) base_x = margin;
                if (base_x + text_width > (int)win_w - margin) {
                    base_x = (int)win_w - text_width - margin;
                }
                text_x = base_x;
            }

            // ========= 2. 滚动模式逻辑 scroll_mode 0右到左循环/1右到左单次停留/2静止/3左到右循环 =========
            if (marquee_config.scroll_mode == 2)
            {
                // 模式2：完全静止
                text_x = base_x;
            }
            else if (marquee_config.scroll_mode == 1)
            {
                // 模式1：单次滚动 + 结束停留（右到左）
                // 起始位置考虑右侧固定区域
                int start_x = (int)win_w;
                if (weather_forecast.enabled && weather_forecast.position == 1) {
                    start_x -= forecast_fixed_width; // 右侧有固定区域，从固定区域左侧开始
                }

                if (!scroll_once_finished)
                {
                    text_x -= marquee_config.scroll_speed;
                    // 结束位置考虑左侧固定区域
                    int end_x = -text_width;
                    if (weather_forecast.enabled && weather_forecast.position == 0) {
                        end_x = forecast_fixed_width - text_width; // 左侧有固定区域，停在固定区域右侧
                    }
                    if (text_x < end_x)
                    {
                        scroll_once_finished = true;
                        stay_start_ms = now_ms;
                    }
                }
                else
                {
                    // 停留倒计时
                    if (now_ms - stay_start_ms >= marquee_config.stay_time)
                    {
                        text_x = start_x;
                        scroll_once_finished = false;
                        stay_start_ms = 0;
                    }
                }
            }
            else if (marquee_config.scroll_mode == 3)
            {
                // 模式3：循环滚动（左到右）
                // 起始位置考虑左侧固定区域
                int left_bound = -text_width;
                if (weather_forecast.enabled && weather_forecast.position == 0) {
                    left_bound = forecast_fixed_width - text_width; // 左侧有固定区域
                }
                // 结束位置考虑右侧固定区域
                int right_bound = (int)win_w;
                if (weather_forecast.enabled && weather_forecast.position == 1) {
                    right_bound -= forecast_fixed_width; // 右侧有固定区域
                }

                if (marquee_config.scroll_speed > 0)
                {
                    text_x += marquee_config.scroll_speed;
                    if (text_x > right_bound) {
                        text_x = left_bound;
                    }
                }
                else
                {
                    text_x = base_x;
                }
            }
            else
            {
                // 模式0：默认循环滚动（右到左）
                // 起始位置考虑右侧固定区域
                int right_bound = (int)win_w;
                if (weather_forecast.enabled && weather_forecast.position == 1) {
                    right_bound -= forecast_fixed_width; // 右侧有固定区域
                }
                // 结束位置考虑左侧固定区域
                int left_bound = -text_width;
                if (weather_forecast.enabled && weather_forecast.position == 0) {
                    left_bound = forecast_fixed_width - text_width; // 左侧有固定区域
                }

                if (marquee_config.scroll_speed > 0)
                {
                    text_x -= marquee_config.scroll_speed;
                    if (text_x < left_bound) {
                        text_x = right_bound;
                    }
                }
                else
                {
                    text_x = base_x;
                }
            }

            // ========= 3. 计算天气预报固定区域（用于裁剪） =========
            int forecast_clip_left = 0;
            int forecast_clip_right = (int)win_w;
            bool has_fixed_forecast = false;
            std::string forecast_text;
            int forecast_width = 0;
            int forecast_x = 0;
            // margin 已在前面声明，使用外部 margin 变量

            if (weather_forecast.enabled && weather_forecast.position != 2) {
                forecast_text = weather_forecast.city + ": " + weather_forecast.weather + " " +
                               std::to_string(weather_forecast.temp_low) + "°C~" +
                               std::to_string(weather_forecast.temp_high) + "°C";
                forecast_width = compute_text_width(forecast_text.c_str(), face);
                has_fixed_forecast = true;

                if (weather_forecast.position == 0) {
                    // 最左侧固定
                    forecast_x = margin;
                    forecast_clip_left = forecast_x + forecast_width + margin / 2;
                } else if (weather_forecast.position == 1) {
                    // 最右侧固定
                    forecast_x = (int)win_w - forecast_width - margin;
                    forecast_clip_right = forecast_x - margin / 2;
                }

                // 确保不超出屏幕边界
                if (forecast_x < margin) forecast_x = margin;
                if (forecast_x + forecast_width > (int)win_w - margin) {
                    forecast_x = (int)win_w - forecast_width - margin;
                }
            }

            // ========= 4. 绘制主行文字（带裁剪，避开天气预报固定区域） =========
            // printf("DEBUG: Rendering text='%s', text_x=%d, text_width=%d\n",
            //      cached_marquee_text.c_str(), text_x, text_width);

            if (has_fixed_forecast) {
                // 启用裁剪，跑马灯文字不覆盖天气预报区域
                glEnable(GL_SCISSOR_TEST);
                // OpenGL裁剪区域：左下角坐标，宽度和高度
                // 注意：OpenGL坐标系原点在左下角，y轴向上
                int scissor_x = forecast_clip_left;
                int scissor_y = 0;
                int scissor_w = forecast_clip_right - forecast_clip_left;
                int scissor_h = (int)win_h;
                if (scissor_w < 0) scissor_w = 0;
                glScissor(scissor_x, scissor_y, scissor_w, scissor_h);
            }

            render_text_gles_ft(cached_marquee_text.c_str(), (float)text_x, (float)base_y,
                                marquee_config.font_size,
                                marquee_config.text_color[0],
                                marquee_config.text_color[1],
                                marquee_config.text_color[2],
                                1.0f,
                                marquee_config.alpha,
                                win_w, win_h, face, prog, vbo,
                                marquee_config.font_bold);

            if (has_fixed_forecast) {
                // 关闭裁剪，绘制天气预报
                glDisable(GL_SCISSOR_TEST);
            }

            // ========= 5. 绘制天气预报（固定位置：最左或最右） =========
            if (has_fixed_forecast) {
                render_text_gles_ft(forecast_text.c_str(), (float)forecast_x, (float)base_y,
                                    marquee_config.font_size,
                                    marquee_config.text_color[0],
                                    marquee_config.text_color[1],
                                    marquee_config.text_color[2],
                                    1.0f,
                                    marquee_config.alpha,
                                    win_w, win_h, face, prog, vbo,
                                    marquee_config.font_bold);
            }

            // ========= 6. 绘制边框 border_width / border_color =========
            if (marquee_config.border_width > 0)
            {
                int border_x = 20;
                int border_y = 0;
                int border_w = win_w - 40;
                int border_h = render_h;

                render_border(border_x, border_y, border_w, border_h,
                              marquee_config.border_width,
                              marquee_config.border_color[0],
                              marquee_config.border_color[1],
                              marquee_config.border_color[2],
                              marquee_config.border_color[3],
                              win_w, win_h, prog, vbo);
            }
            } // end else (marquee_text rendering)
        } // end if (face)
        eglSwapBuffers(egl_dpy, egl_surf);

        if (config_updated) {
            config_updated = false;
            printf("DEBUG: [%.3f ms] Config updated, skipping delay for immediate render\n", get_timestamp_ms());
        }

        struct gbm_bo *cur_bo = gbm_surface_lock_front_buffer(gbm_surf);
        if (!cur_bo) { fprintf(stderr, "lock_front_buffer failed\n"); break; }

        uint32_t cur_fb;
        if (add_fb_for_bo(drm_fd, cur_bo, surf_w, surf_h, &cur_fb, drm_format) != 0) {
            fprintf(stderr, "add_fb_for_bo failed for cur\n");
            gbm_surface_release_buffer(gbm_surf, cur_bo);
            break;
        }

        if (target_plane) {
            // plane目标矩形高度=跑马灯配置高度(render_h)，而非整个surface高度
            // position=0时 plane在屏幕顶部占70px；position=1时在底部占70px
            uint32_t plane_h = marquee_config.height > 0 ? (uint32_t)marquee_config.height : surf_h;
            uint32_t src_w = surf_w << 16;
            uint32_t src_h = plane_h << 16;
			// 加锁内读取最新配置
			int cur_y = marquee_config.pos_y;
			if (drmModeSetPlane(drm_fd, target_plane, crtc_id, cur_fb, 0,
								pos_x, cur_y, surf_w, plane_h,
								0, 0, src_w, src_h)) {
                fprintf(stderr, "drmModeSetPlane failed for plane %u: %s\n", target_plane, strerror(errno));
                drmModeRmFB(drm_fd, cur_fb);
                gbm_surface_release_buffer(gbm_surf, cur_bo);
                break;
            }
            {
                int r = try_set_plane_alpha(drm_fd, target_plane, alpha);
                if (r == 0) {
                } else {
                    printf("Plane alpha property not set/unsupported on %u\n", target_plane);
                }
            }
        }

        if (!target_plane) {
            if (drmModePageFlip(drm_fd, crtc_id, cur_fb, DRM_MODE_PAGE_FLIP_EVENT, NULL)) {
                fprintf(stderr, "drmModePageFlip failed: %s\n", strerror(errno));
                drmModeRmFB(drm_fd, cur_fb);
                gbm_surface_release_buffer(gbm_surf, cur_bo);
                break;
            }
            struct pollfd pfd = { .fd = drm_fd, .events = POLLIN };
            int ret = poll(&pfd, 1, 2000);
            if (ret > 0) {
                drmEventContext evctx = { .version = DRM_EVENT_CONTEXT_VERSION, .vblank_handler = NULL, .page_flip_handler = NULL };
                drmHandleEvent(drm_fd, &evctx);
            }
        }

        if (prev_fb) drmModeRmFB(drm_fd, prev_fb);
        if (prev_bo) gbm_surface_release_buffer(gbm_surf, prev_bo);

        prev_fb = cur_fb;
        prev_bo = cur_bo;

        if (config_updated) {
            continue;
        }

        if (target_plane) {
            // 60 FPS = 16.6ms per frame，降低CPU占用
            usleep(16000);
        }

        // printf("DEBUG: [%.3f ms] Frame completed (took %.3f ms)\n", get_timestamp_ms(), get_timestamp_ms() - frame_start);
    }

    font_fini();

    if (prev_fb) drmModeRmFB(drm_fd, prev_fb);
    if (prev_bo) gbm_surface_release_buffer(gbm_surf, prev_bo);

    if (plane) drmModeFreePlane(plane);

    return 0;
}
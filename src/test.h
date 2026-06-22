#pragma once
// sudo apt install libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
// g++ -o test test.cpp $(pkg-config --cflags --libs gbm egl glesv2 libdrm) -ldl
// g++ -o test test.cpp $(pkg-config --cflags --libs gbm egl glesv2 libdrm) -ldl -lfreetype
// g++ -o test test.cpp -I/usr/include/freetype2 $(pkg-config --cflags --libs gbm egl glesv2 libdrm) -lfreetype -ldl
// g++ -o test test.cpp $(pkg-config --cflags --libs gbm egl glesv2 libdrm freetype2) -ldl

// 编译命令:
// g++ -o test test.cpp $(pkg-config --cflags --libs gbm egl glesv2 libdrm freetype2) -ldl
// g++ -DUSE_MQTT -o test test.cpp $(pkg-config --cflags --libs gbm egl glesv2 libdrm freetype2 mosquitto) -ldl

// 运行示例:
// ./test -p 79 -W 400 -H 400 -X 100 -Y 100
// ./test --plane 79 --win-w 1920 --win-h 1080 --x 0 --y 0 --alpha 1.1
// ./test --plane 79 --win-w 1920 --win-h 100 --x 0 --y 0 --alpha 0.8 --show-timestamp --show-timestamp-align
// ./test -p 173 -W 1920 -H 100 -fs 48 -tc 1.0 0.9 0.2 1.0 -bg 0.2 0.2 0.3 1.0
// ./test -p 173 -W 1920 -H 100 -pos 0 -ss 8 -fb -ta 0
// ./test -p 173 -W 800 -H 400 -dm 1 -fs 32  # 世界时间模式
// ./test -p 173 -W 800 -H 400 -dm 2 -fs 32  # 天气预报模式
// ./test -p 173 -W 800 -H 400 -dm 1 -ac 纽约  # 添加纽约时间
// ./test -p 173 -W 1920 -H 100 -we -wt -t "这是一段测试文字"  # 跑马灯包含天气和世界时间
// ./test -p 173 -W 1920 -H 100 -we -wt -wp 0 -mh localhost -mp 1883  # 启用MQTT

// void test_gles_surface();

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/poll.h>
#include <stdbool.h>

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
/* prefer GBM format constants from <gbm.h> to avoid depending on drm_fourcc.h */

#ifndef DRM_MODE_OBJECT_PLANE
#define DRM_MODE_OBJECT_PLANE 0x00000004
#endif

#ifndef DRM_MODE_PAGE_FLIP_EVENT
#define DRM_MODE_PAGE_FLIP_EVENT 0x01
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H

void cleanup_egl_ctx(EGLDisplay egl_dpy, EGLSurface egl_surf, EGLContext egl_ctx);
void cleanup_gbm(struct gbm_surface *gbm_surf, struct gbm_device *gbm, drmModeConnector *conn, drmModeRes *res, int drm_fd);
void egl_terminate(EGLDisplay egl_dpy);

void font_init();
void font_fini();

void render_text_gles_ft(const char* text_utf8,
                         float x, float y, float font_px,
                         float r, float g, float b, float a,
                         float global_alpha,
                         int screen_w, int screen_h,
                         FT_Face face,
                         GLuint prog, GLuint vbo,
                         bool font_bold = false);
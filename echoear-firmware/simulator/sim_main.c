/*
 * Dashboard simulator.
 *   dashboard_sim                     SDL window, live; the robot cycles through its moods
 *   dashboard_sim --shot out.ppm      render one frame headless and write it as PPM
 *   --stress                          worst-case values (long status, 100%, big cost, low battery)
 *   --unknown                         every value unknown, as at boot before the bridge reports
 *   --confirm                         firmware UI (ui.c) with the permission modal open
 *   --mood NAME                       robot mood: idle busy sleep happy attention error
 *   --after-ms N                      with --shot: let animations run N ms before capturing
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "agent_dashboard.h"
#include "robot_face.h"
#include "ui.h"

#define HOR_RES 360
#define VER_RES 360

#define MOOD_CYCLE_MS 4000

static uint16_t s_framebuffer[HOR_RES * VER_RES];

static const char *const MOOD_NAMES[] = {
    [ROBOT_MOOD_IDLE] = "idle",       [ROBOT_MOOD_BUSY] = "busy",
    [ROBOT_MOOD_SLEEP] = "sleep",     [ROBOT_MOOD_HAPPY] = "happy",
    [ROBOT_MOOD_ATTENTION] = "attention", [ROBOT_MOOD_ERROR] = "error",
};
#define MOOD_COUNT ((int)(sizeof(MOOD_NAMES) / sizeof(MOOD_NAMES[0])))

static int mood_from_name(const char *name)
{
    for (int i = 0; i < MOOD_COUNT; i++) {
        if (strcmp(name, MOOD_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static void mood_show(robot_mood_t mood)
{
    if (mood <= ROBOT_MOOD_SLEEP) {
        robot_face_set_base(mood);
    } else {
        robot_face_react(mood, ROBOT_LOOK_LEFT, 0);
    }
}

static void mood_cycle_cb(lv_timer_t *timer)
{
    static int next = 1;
    printf("mood: %s\n", MOOD_NAMES[next]);
    robot_face_set_base(ROBOT_MOOD_IDLE);
    mood_show((robot_mood_t)next);
    next = (next + 1) % MOOD_COUNT;
}

static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void headless_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LV_UNUSED(area);
    LV_UNUSED(px_map); /* direct mode renders straight into s_framebuffer */
    lv_display_flush_ready(disp);
}

static void sim_decision(uint32_t id, bool approve)
{
    printf("decision #%u: %s\n", (unsigned)id, approve ? "approve" : "deny");
}

static bool sim_link_probe(void)
{
    return true;
}

static int write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", HOR_RES, VER_RES);
    for (int i = 0; i < HOR_RES * VER_RES; i++) {
        uint16_t c = s_framebuffer[i];
        uint8_t rgb[3] = {
            (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
            (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
            (uint8_t)((c & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, sizeof(rgb), f);
    }
    fclose(f);
    return 0;
}

static int render_shot(const char *path, uint32_t after_ms)
{
    for (uint32_t start = tick_ms(); tick_ms() - start < after_ms;) {
        lv_timer_handler();
        usleep(1000);
    }
    lv_refr_now(NULL);
    return write_ppm(path);
}

static int run_window(void)
{
    while (1) {
        uint32_t wait_ms = lv_timer_handler();
        usleep((wait_ms > 0 && wait_ms < 50 ? wait_ms : 5) * 1000);
    }
}

int main(int argc, char **argv)
{
    const char *shot_path = NULL;
    bool stress = false;
    bool unknown = false;
    bool confirm = false;
    uint32_t after_ms = 0;
    int mood = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (strcmp(argv[i], "--stress") == 0) {
            stress = true;
        } else if (strcmp(argv[i], "--unknown") == 0) {
            unknown = true;
        } else if (strcmp(argv[i], "--confirm") == 0) {
            confirm = true;
        } else if (strcmp(argv[i], "--mood") == 0 && i + 1 < argc && (mood = mood_from_name(argv[++i])) >= 0) {
            continue;
        } else if (strcmp(argv[i], "--after-ms") == 0 && i + 1 < argc) {
            after_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "usage: %s [--shot out.ppm [--after-ms N]] [--stress | --unknown | --confirm] [--mood NAME]\n", argv[0]);
            return 2;
        }
    }

    lv_init();
    if (shot_path != NULL) {
        lv_tick_set_cb(tick_ms);
        lv_display_t *disp = lv_display_create(HOR_RES, VER_RES);
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(disp, s_framebuffer, NULL, sizeof(s_framebuffer), LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(disp, headless_flush_cb);
    } else {
        lv_sdl_window_create(HOR_RES, VER_RES);
        lv_sdl_mouse_create();
    }

    if (confirm) {
        ui_init(sim_decision, sim_link_probe);
        ui_show_confirm(7, "claude", "Bash",
                        "cd echoear-firmware && idf.py -p /dev/cu.usbmodem1301 flash monitor 2>&1 | tee flash.log && "
                        "git push origin main --force-with-lease",
                        UI_RISK_HIGH);
        return shot_path != NULL ? render_shot(shot_path, after_ms) : run_window();
    }

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    agent_dashboard_create(lv_screen_active());

    agent_dashboard_data_t data;
    agent_dashboard_mock_data(&data);
    if (stress) {
        data.claude_usage_percent = 100;
        data.codex_usage_percent = 0;
        data.claude_weekly_percent = 100;
        data.codex_weekly_percent = 7;
        data.daily_cost = 1234.56f;
        data.wifi_connected = false;
        data.host_linked = true;
        data.battery_percent = 8;
        snprintf(data.time_text, sizeof(data.time_text), "23:59");
        snprintf(data.date_text, sizeof(data.date_text), "Wed, Sep 30");
        snprintf(data.status_text, sizeof(data.status_text), "Đã sửa xong lỗi build, tất cả test đều pass");
        data.status_tone = AGENT_DASHBOARD_TONE_CLAUDE;
    } else if (unknown) {
        data = (agent_dashboard_data_t){
            .claude_usage_percent = -1,
            .codex_usage_percent = -1,
            .claude_weekly_percent = -1,
            .codex_weekly_percent = -1,
            .daily_cost = -1,
            .battery_percent = -1,
        };
    }
    agent_dashboard_update(&data);

    if (mood >= 0) {
        mood_show((robot_mood_t)mood);
    } else if (shot_path == NULL) {
        lv_timer_create(mood_cycle_cb, MOOD_CYCLE_MS, NULL);
    }

    return shot_path != NULL ? render_shot(shot_path, after_ms) : run_window();
}

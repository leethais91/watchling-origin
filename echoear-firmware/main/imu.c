#include "imu.h"

#include <stdbool.h>
#include <stdlib.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"

#include "assets/bmi270_config.h"

static const char *TAG = "imu";

/* BMI270 registers (datasheet section 5) */
#define REG_CHIP_ID         0x00
#define REG_ACC_X_LSB       0x0C
#define REG_INTERNAL_STATUS 0x21
#define REG_ACC_CONF        0x40
#define REG_ACC_RANGE       0x41
#define REG_INIT_CTRL       0x59
#define REG_INIT_ADDR_0     0x5B
#define REG_INIT_DATA       0x5E
#define REG_PWR_CONF        0x7C
#define REG_PWR_CTRL        0x7D
#define REG_CMD             0x7E

#define CHIP_ID_BMI270      0x24
#define CMD_SOFT_RESET      0xB6
#define INIT_OK             0x01
#define PWR_CTRL_ACC_EN     0x04
/* Performance filter, normal averaging, 25 Hz: plenty for posture at a 10 Hz poll. */
#define ACC_CONF_25HZ       0xA6
#define ACC_RANGE_2G        0x00
#define LSB_PER_G           16384

#define I2C_SPEED_HZ        400000
#define I2C_TIMEOUT_MS      50
/* The config blob goes up in chunks, each preceded by its word offset. */
#define UPLOAD_CHUNK        128
#define POLL_US             (100 * 1000)

/*
 * Mounting, measured on a V1.2 board: standing on its base it reads Z = -1 g, leaning to its
 * left drives X negative, lying on its face reads Y = +0.9 g (on its back, -0.95 g).
 */
#define AXIS_LATERAL        0
#define AXIS_NORMAL         1
#define FACE_DOWN_SIGN      1
#define TILT_SIGN           1

/* Thresholds in mg on the smoothed gravity vector; counts are 10 Hz samples. */
#define FACE_DOWN_MG        800
#define FACE_DOWN_EXIT_MG   500
#define FACE_DOWN_SAMPLES   10
#define FACE_UP_SAMPLES     5
#define TILT_MG             420 /* about 25 degrees */
#define LEVEL_MG            250
#define TILT_SAMPLES        3
/* Tilt means nothing while the robot lies on its back or face. */
#define TILT_MAX_NORMAL_MG  600
/* Change between two raw samples, summed over the axes, that counts as being moved. */
#define MOVE_JERK_MG        350
#define MOVE_COOLDOWN_US    (3 * 1000 * 1000)

static i2c_master_dev_handle_t s_dev;
static imu_event_cb_t s_cb;

/* Poll state: esp_timer task only. */
static bool s_primed;
static int s_gravity[3];
static int s_last[3];
static bool s_face_down;
static int s_face_count;
static int s_tilt; /* -1 left, 0 level, 1 right */
static int s_tilt_count;
static int64_t s_moved_at;

static esp_err_t reg_write(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, I2C_TIMEOUT_MS);
}

/* Datasheet 4.5: the feature-engine config must be in place before the sensors run. */
static esp_err_t config_upload(void)
{
    ESP_RETURN_ON_ERROR(reg_write(REG_CMD, CMD_SOFT_RESET), TAG, "soft reset");
    /* The chip NAKs while it reboots; the datasheet's 2 ms is not enough on this board. */
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_CONF, 0x00), TAG, "disable power save");
    esp_rom_delay_us(450);
    ESP_RETURN_ON_ERROR(reg_write(REG_INIT_CTRL, 0x00), TAG, "init start");
    uint8_t chunk[1 + UPLOAD_CHUNK];
    for (size_t offset = 0; offset < BMI270_CONFIG_SIZE; offset += UPLOAD_CHUNK) {
        uint16_t word = (uint16_t)(offset / 2);
        uint8_t addr[3] = {REG_INIT_ADDR_0, (uint8_t)(word & 0x0F), (uint8_t)(word >> 4)};
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, addr, sizeof(addr), I2C_TIMEOUT_MS), TAG, "init addr");
        chunk[0] = REG_INIT_DATA;
        for (size_t i = 0; i < UPLOAD_CHUNK; i++) {
            chunk[1 + i] = bmi270_config_file[offset + i];
        }
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, chunk, sizeof(chunk), I2C_TIMEOUT_MS), TAG, "init data");
    }
    ESP_RETURN_ON_ERROR(reg_write(REG_INIT_CTRL, 0x01), TAG, "init end");
    uint8_t status = 0;
    for (int tries = 0; tries < 15 && (status & 0x0F) != INIT_OK; tries++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(reg_read(REG_INTERNAL_STATUS, &status, 1), TAG, "status");
    }
    ESP_RETURN_ON_FALSE((status & 0x0F) == INIT_OK, ESP_ERR_INVALID_RESPONSE, TAG, "config rejected (0x%02x)",
                        status);
    return ESP_OK;
}

static esp_err_t accel_read_mg(int out[3])
{
    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(reg_read(REG_ACC_X_LSB, raw, sizeof(raw)), TAG, "accel read");
    for (int i = 0; i < 3; i++) {
        int16_t value = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8);
        out[i] = value * 1000 / LSB_PER_G;
    }
    return ESP_OK;
}

static void face_update(void)
{
    int normal = s_gravity[AXIS_NORMAL] * FACE_DOWN_SIGN;
    bool leaving = s_face_down ? normal < FACE_DOWN_EXIT_MG : normal > FACE_DOWN_MG;
    s_face_count = leaving ? s_face_count + 1 : 0;
    if (s_face_count >= (s_face_down ? FACE_UP_SAMPLES : FACE_DOWN_SAMPLES)) {
        s_face_down = !s_face_down;
        s_face_count = 0;
        s_cb(s_face_down ? IMU_EVENT_FACE_DOWN : IMU_EVENT_FACE_UP);
    }
}

static void tilt_update(void)
{
    if (s_face_down || abs(s_gravity[AXIS_NORMAL]) > TILT_MAX_NORMAL_MG) {
        return;
    }
    int lateral = s_gravity[AXIS_LATERAL] * TILT_SIGN;
    int want = lateral > TILT_MG ? 1 : lateral < -TILT_MG ? -1 : abs(lateral) < LEVEL_MG ? 0 : s_tilt;
    s_tilt_count = want != s_tilt ? s_tilt_count + 1 : 0;
    if (s_tilt_count >= TILT_SAMPLES) {
        s_tilt = want;
        s_tilt_count = 0;
        s_cb(want > 0 ? IMU_EVENT_TILT_RIGHT : want < 0 ? IMU_EVENT_TILT_LEFT : IMU_EVENT_LEVEL);
    }
}

/* esp_timer task: one 6-byte read every 100 ms. */
static void poll_cb(void *arg)
{
    int a[3];
    if (accel_read_mg(a) != ESP_OK) {
        return;
    }
    if (a[0] == 0 && a[1] == 0 && a[2] == 0) {
        return; /* no sample converted yet */
    }
    if (!s_primed) {
        s_primed = true;
        for (int i = 0; i < 3; i++) {
            s_gravity[i] = s_last[i] = a[i];
        }
        ESP_LOGI(TAG, "Boot posture x=%d y=%d z=%d mg", a[0], a[1], a[2]);
        return;
    }
    int jerk = 0;
    for (int i = 0; i < 3; i++) {
        jerk += abs(a[i] - s_last[i]);
        s_last[i] = a[i];
        s_gravity[i] += (a[i] - s_gravity[i]) / 4;
    }
    int64_t now = esp_timer_get_time();
    if (jerk > MOVE_JERK_MG && (s_moved_at == 0 || now - s_moved_at >= MOVE_COOLDOWN_US)) {
        s_moved_at = now;
        s_cb(IMU_EVENT_MOVED);
    }
    face_update();
    tilt_update();
}

static esp_err_t device_add(i2c_master_bus_handle_t bus, uint16_t address)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &config, &s_dev);
}

esp_err_t imu_start(imu_event_cb_t cb)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialised");
    /* SDO picks the address; the board's strapping is not documented, so try both. */
    uint16_t address = i2c_master_probe(bus, 0x68, I2C_TIMEOUT_MS) == ESP_OK   ? 0x68
                       : i2c_master_probe(bus, 0x69, I2C_TIMEOUT_MS) == ESP_OK ? 0x69
                                                                               : 0;
    if (address == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(device_add(bus, address), TAG, "add device");
    uint8_t id = 0;
    esp_err_t err = reg_read(REG_CHIP_ID, &id, 1);
    if (err != ESP_OK || id != CHIP_ID_BMI270) {
        ESP_LOGW(TAG, "Device at 0x%02x is not a BMI270 (id 0x%02x)", address, id);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(config_upload(), TAG, "config upload");
    ESP_RETURN_ON_ERROR(reg_write(REG_ACC_CONF, ACC_CONF_25HZ), TAG, "accel config");
    ESP_RETURN_ON_ERROR(reg_write(REG_ACC_RANGE, ACC_RANGE_2G), TAG, "accel range");
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_CTRL, PWR_CTRL_ACC_EN), TAG, "accel enable");
    ESP_LOGI(TAG, "BMI270 at 0x%02x ready", address);

    s_cb = cb;
    const esp_timer_create_args_t args = {.callback = poll_cb, .name = "imu"};
    esp_timer_handle_t timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &timer), TAG, "timer");
    return esp_timer_start_periodic(timer, POLL_US);
}

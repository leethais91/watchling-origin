#include "battery.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "battery";

#define BQ27220_ADDR        0x55
#define BQ27220_CMD_CURRENT 0x0C /* mA, signed; positive while charging */
#define BQ27220_CMD_SOC     0x2C /* state of charge, % */
#define I2C_TIMEOUT_MS      50
#define I2C_SPEED_HZ        100000

static i2c_master_dev_handle_t s_gauge;

static esp_err_t read_word(uint8_t cmd, uint16_t *out)
{
    uint8_t raw[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_gauge, &cmd, 1, raw, sizeof(raw), I2C_TIMEOUT_MS), TAG,
                        "read 0x%02x failed", cmd);
    *out = (uint16_t)(raw[0] | raw[1] << 8);
    return ESP_OK;
}

esp_err_t battery_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialised");
    if (i2c_master_probe(bus, BQ27220_ADDR, I2C_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ27220_ADDR,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &config, &s_gauge);
}

esp_err_t battery_read(battery_state_t *out)
{
    ESP_RETURN_ON_FALSE(s_gauge, ESP_ERR_INVALID_STATE, TAG, "gauge not initialised");
    uint16_t soc;
    uint16_t current;
    ESP_RETURN_ON_ERROR(read_word(BQ27220_CMD_SOC, &soc), TAG, "no state of charge");
    ESP_RETURN_ON_ERROR(read_word(BQ27220_CMD_CURRENT, &current), TAG, "no current");
    out->percent = soc > 100 ? 100 : soc;
    out->charging = (int16_t)current > 0;
    return ESP_OK;
}

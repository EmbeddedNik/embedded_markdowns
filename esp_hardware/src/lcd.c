/*
 * @file    lcd.c
 * @brief   HD44780 LCD1602 driver via PCF8574 I2C expander (4-bit mode).
 *          Uses the ESP-IDF 5.x new I2C master driver (driver/i2c_master.h).
 *          PCF8574 bit mapping: P7=D7 P6=D6 P5=D5 P4=D4 P3=BL P2=E P1=RW P0=RS
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "lcd.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd";

/* ── PCF8574 control bit masks ───────────────────────────────────── */
#define PCF_RS  0x01   /* Register Select: 0=command, 1=data */
#define PCF_RW  0x02   /* Read/Write: always 0 (write)       */
#define PCF_EN  0x04   /* Enable pulse                        */
#define PCF_BL  0x08   /* Backlight                           */

/* HD44780 command constants */
#define LCD_CMD_CLEAR         0x01
#define LCD_CMD_ENTRY_MODE    0x06   /* increment, no shift  */
#define LCD_CMD_DISPLAY_ON    0x0C   /* on, cursor off       */
#define LCD_CMD_FUNCTION_SET  0x28   /* 4-bit, 2 lines, 5x8  */

/* Row start addresses for HD44780 */
static const uint8_t ROW_ADDR[LCD_ROWS] = {0x00, 0x40};

/* ── Driver handles ──────────────────────────────────────────────── */
static i2c_master_bus_handle_t s_bus_handle   = NULL;
static i2c_master_dev_handle_t s_dev_handle   = NULL;
static uint8_t                 s_backlight    = PCF_BL;

/* ── Low-level write one byte to PCF8574 ─────────────────────────── */
static esp_err_t pcf8574_write(uint8_t data)
{
    return i2c_master_transmit(s_dev_handle, &data, 1, 10 /* ms timeout */);
}

/* ── Send one nibble (upper 4 bits) with E pulse ─────────────────── */
static esp_err_t lcd_send_nibble(uint8_t nibble, uint8_t rs_flag)
{
    uint8_t base = ((nibble & 0x0F) << 4) | s_backlight | rs_flag;
    esp_err_t ret = pcf8574_write(base | PCF_EN);   /* E = 1 */
    if (ret != ESP_OK) return ret;
    ets_delay_us(1);
    ret = pcf8574_write(base);                    /* E = 0 – latch */
    if (ret != ESP_OK) return ret;
    ets_delay_us(50);
    return ESP_OK;
}

/* ── Send full byte as two nibbles ───────────────────────────────── */
static esp_err_t lcd_write_byte(uint8_t byte, uint8_t rs_flag)
{
    esp_err_t ret = lcd_send_nibble(byte >> 4, rs_flag);
    if (ret != ESP_OK) return ret;
    return lcd_send_nibble(byte & 0x0F, rs_flag);
}

static esp_err_t lcd_cmd(uint8_t cmd)  { return lcd_write_byte(cmd, 0);      }
static esp_err_t lcd_data(uint8_t d)   { return lcd_write_byte(d,   PCF_RS); }

/* ── Public: initialise I2C bus and LCD ──────────────────────────── */
esp_err_t lcd_init(void)
{
    /* I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = LCD_I2C_PORT,
        .sda_io_num        = LCD_I2C_SDA_PIN,
        .scl_io_num        = LCD_I2C_SCL_PIN,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* PCF8574 device on the bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = LCD_I2C_ADDR,
        .scl_speed_hz    = LCD_I2C_CLK_HZ,
    };
    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* HD44780 4-bit initialisation sequence */
    vTaskDelay(pdMS_TO_TICKS(50));          /* >40 ms power-on delay */

    ret = lcd_send_nibble(0x03, 0);
    if (ret != ESP_OK) goto fail;
    vTaskDelay(pdMS_TO_TICKS(5));
    ret = lcd_send_nibble(0x03, 0);
    if (ret != ESP_OK) goto fail;
    ets_delay_us(150);
    ret = lcd_send_nibble(0x03, 0);
    if (ret != ESP_OK) goto fail;
    ets_delay_us(150);
    ret = lcd_send_nibble(0x02, 0);               /* switch to 4-bit mode  */
    if (ret != ESP_OK) goto fail;

    if ((ret = lcd_cmd(LCD_CMD_FUNCTION_SET)) != ESP_OK) goto fail;
    if ((ret = lcd_cmd(LCD_CMD_DISPLAY_ON)) != ESP_OK) goto fail;
    if ((ret = lcd_cmd(LCD_CMD_CLEAR)) != ESP_OK) goto fail;
    vTaskDelay(pdMS_TO_TICKS(2));
    if ((ret = lcd_cmd(LCD_CMD_ENTRY_MODE)) != ESP_OK) goto fail;

    ESP_LOGI(TAG, "LCD1602 initialised (I2C addr=0x%02X SDA=%d SCL=%d)",
             LCD_I2C_ADDR, LCD_I2C_SDA_PIN, LCD_I2C_SCL_PIN);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "LCD init write failed: %s", esp_err_to_name(ret));
    return ret;
}

/* ── Public: clear display ───────────────────────────────────────── */
void lcd_clear(void)
{
    lcd_cmd(LCD_CMD_CLEAR);
    vTaskDelay(pdMS_TO_TICKS(2));
}

/* ── Public: set cursor position ─────────────────────────────────── */
void lcd_set_cursor(uint8_t col, uint8_t row)
{
    if (row >= LCD_ROWS) row = LCD_ROWS - 1;
    if (col >= LCD_COLS) col = LCD_COLS - 1;
    lcd_cmd(0x80 | (ROW_ADDR[row] + col));
}

/* ── Public: write string ────────────────────────────────────────── */
void lcd_write_string(const char *str)
{
    while (*str) {
        lcd_data((uint8_t)*str++);
    }
}

/* ── Public: backlight control ───────────────────────────────────── */
void lcd_backlight(uint8_t on)
{
    s_backlight = on ? PCF_BL : 0;
    pcf8574_write(s_backlight);
}

/*
 * @file    lcd.h
 * @brief   HD44780 LCD1602 driver via PCF8574 I2C expander (4-bit mode)
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include "esp_err.h"

/* ── I2C configuration ──────────────────────────────────────────── */
#define LCD_I2C_PORT        I2C_NUM_0
#define LCD_I2C_SDA_PIN     21
#define LCD_I2C_SCL_PIN     22
#define LCD_I2C_ADDR        0x27   /* PCF8574 default address on Smart Farm Kit */
#define LCD_I2C_CLK_HZ      100000

/* ── Display geometry ────────────────────────────────────────────── */
#define LCD_COLS            16
#define LCD_ROWS            2

/* ── Public API ──────────────────────────────────────────────────── */

/* Initialise I2C bus and LCD (call once from actuator_task_init) */
esp_err_t lcd_init(void);

/* Re-run HD44780 init sequence only — I2C bus must already be up.
 * Call after a write error to recover from relay-noise corruption. */
esp_err_t lcd_reinit(void);

/* Clear all characters and return cursor home */
void lcd_clear(void);

/* Place cursor at zero-based (col, row); returns ESP_OK or I2C error */
esp_err_t lcd_set_cursor(uint8_t col, uint8_t row);

/* Write a null-terminated string at current cursor position;
 * returns first I2C error encountered, or ESP_OK */
esp_err_t lcd_write_string(const char *str);

/* Control backlight (1 = on, 0 = off) */
void lcd_backlight(uint8_t on);

#endif /* LCD_H */

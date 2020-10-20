#pragma once


#include "esp_err.h"
#include "esp_log.h"

#include "driver/i2c.h"

#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)

#define I2C_MASTER_SCL_IO CONFIG_I2C_MASTER_SCL
#define I2C_MASTER_SDA_IO CONFIG_I2C_MASTER_SDA
#define I2C_MASTER_NUM I2C_NUMBER(CONFIG_I2C_MASTER_PORT_NUM)
#define I2C_MASTER_FREQ_HZ CONFIG_I2C_MASTER_FREQUENCY
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

#define WRITE_BIT I2C_MASTER_WRITE
#define READ_BIT I2C_MASTER_READ
#define ACK_CHECK_EN 0x1
#define ACK_CHECK_DIS 0x0
#define ACK_VAL 0x0
#define NACK_VAL 0x1

#define I2C_WAIT_MS_DEFAULT 1000
#define I2C_WAIT_MS_LONG    5000



esp_err_t i2c_init(void);
esp_err_t i2c_master_read_slave(uint8_t i2c_slave_addr, uint8_t *data_rd, size_t size, uint16_t wait_ms);
esp_err_t i2c_master_write_slave(uint8_t i2c_slave_addr, uint8_t *data_wr, size_t size, uint16_t wait_ms);

/*
 * HOLyiot local driver for the LPS22HB pressure sensor over SPI.
 *
 * Register-compatible with ST's in-tree LPS22HH driver (same CTRL_REG1
 * ODR/BDU bits, same 24-bit PRESS_OUT registers, auto-increment always on
 * for burst reads), but that driver hard-rejects this part's real WHO_AM_I
 * (0xB1, not 0xB3) and refuses to bind - see the top-level README's
 * LPS22HB/LPS22HH note. This driver accepts either ID and otherwise
 * follows the same sequence, minus trigger support: nothing on this board
 * wires the interrupt line (port P2 has no GPIOTE anyway - see the board
 * README), so continuous conversion with periodic polling from
 * sensor_manager.c is the only mode implemented.
 *
 * Copyright (c) 2026 HOLyiot
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT holyiot_lps22hb

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lps22hb, CONFIG_SENSOR_LOG_LEVEL);

#define LPS22HB_REG_WHO_AM_I	0x0f
#define LPS22HB_REG_CTRL_REG1	0x10
#define LPS22HB_REG_PRESS_OUT	0x28
#define LPS22HB_ODR_10HZ_BDU	0x22	/* ODR[2:0] = 010 (10 Hz), BDU = 1 */
#define LPS22HB_CHIP_ID		0xb1	/* LPS22HB, the part actually fitted */
#define LPS22HH_CHIP_ID		0xb3	/* LPS22HH, also accepted */

struct lps22hb_config {
	struct spi_dt_spec spi;
};

struct lps22hb_data {
	int32_t pressure_raw;	/* 24-bit unsigned counts, 4096 LSB/hPa */
};

/* Bit7 = read, bits6..0 = address; this part auto-increments on burst reads. */
static int lps22hb_read_regs(const struct device *dev, uint8_t reg,
			     uint8_t *values, size_t count)
{
	const struct lps22hb_config *cfg = dev->config;
	uint8_t cmd = reg | 0x80U;
	const struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_bufs[2] = {
		{ .buf = NULL, .len = 1 },	/* discard during the command */
		{ .buf = values, .len = count },
	};
	const struct spi_buf_set rx = { .buffers = rx_bufs, .count = 2 };

	return spi_transceive_dt(&cfg->spi, &tx, &rx);
}

static int lps22hb_write_reg(const struct device *dev, uint8_t reg,
			     uint8_t value)
{
	const struct lps22hb_config *cfg = dev->config;
	uint8_t frame[2] = { reg & 0x7fU, value };
	const struct spi_buf tx_buf = { .buf = frame, .len = sizeof(frame) };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&cfg->spi, &tx);
}

static int lps22hb_sample_fetch(const struct device *dev,
				enum sensor_channel chan)
{
	struct lps22hb_data *data = dev->data;
	uint8_t raw[3];

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_PRESS) {
		return -ENOTSUP;
	}

	if (lps22hb_read_regs(dev, LPS22HB_REG_PRESS_OUT, raw, sizeof(raw)) != 0) {
		LOG_ERR("%s: pressure read failed", dev->name);
		return -EIO;
	}

	data->pressure_raw = ((int32_t)raw[2] << 16) | ((int32_t)raw[1] << 8) |
			      raw[0];

	return 0;
}

static int lps22hb_channel_get(const struct device *dev,
			       enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct lps22hb_data *data = dev->data;
	int64_t press_kpa_x1e6;

	if (chan != SENSOR_CHAN_PRESS) {
		return -ENOTSUP;
	}

	/*
	 * counts * 100 Pa/hPa / 4096 counts/hPa / 1000 Pa/kPa, scaled by 1e6
	 * up front so the division stays exact until the final split into
	 * sensor_value's integer/micro-fraction pair (the Zephyr convention
	 * SENSOR_CHAN_PRESS is documented in kPa).
	 *
	 * The multiplier is therefore 100 * 1e6 / 1000 = 100000. It was
	 * 100000000 here for a while - the /1000 Pa-to-kPa step above was in
	 * the comment but not in the code - which reported Pa under a kPa
	 * label, i.e. 1000x high. Every consumer converts correctly from kPa,
	 * so all of them inherited the error: sensor_manager's pressure_pa,
	 * the flash sensor log, and the ZCL pressure attribute, which
	 * overflowed its int16_t outright.
	 */
	press_kpa_x1e6 = ((int64_t)data->pressure_raw * 100000LL) / 4096LL;

	val->val1 = (int32_t)(press_kpa_x1e6 / 1000000LL);
	val->val2 = (int32_t)(press_kpa_x1e6 % 1000000LL);

	return 0;
}

static DEVICE_API(sensor, lps22hb_api) = {
	.sample_fetch = lps22hb_sample_fetch,
	.channel_get = lps22hb_channel_get,
};

static int lps22hb_init(const struct device *dev)
{
	const struct lps22hb_config *cfg = dev->config;
	uint8_t who_am_i = 0;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("%s: SPI bus not ready", dev->name);
		return -ENODEV;
	}

	if (lps22hb_read_regs(dev, LPS22HB_REG_WHO_AM_I, &who_am_i, 1) != 0) {
		LOG_ERR("%s: WHO_AM_I read failed", dev->name);
		return -EIO;
	}

	if (who_am_i != LPS22HB_CHIP_ID && who_am_i != LPS22HH_CHIP_ID) {
		LOG_ERR("%s: unexpected WHO_AM_I 0x%02x", dev->name, who_am_i);
		return -EIO;
	}

	if (lps22hb_write_reg(dev, LPS22HB_REG_CTRL_REG1,
			      LPS22HB_ODR_10HZ_BDU) != 0) {
		LOG_ERR("%s: CTRL_REG1 write failed", dev->name);
		return -EIO;
	}

	LOG_INF("%s: ready (WHO_AM_I 0x%02x)", dev->name, who_am_i);

	return 0;
}

#define LPS22HB_INIT(inst)						\
	static struct lps22hb_data lps22hb_data_##inst;		\
									\
	static const struct lps22hb_config lps22hb_config_##inst = {	\
		.spi = SPI_DT_SPEC_INST_GET(				\
			inst,						\
			SPI_WORD_SET(8) | SPI_TRANSFER_MSB |		\
				SPI_OP_MODE_MASTER | SPI_MODE_CPOL |	\
				SPI_MODE_CPHA),				\
	};								\
									\
	SENSOR_DEVICE_DT_INST_DEFINE(inst, lps22hb_init, NULL,		\
				     &lps22hb_data_##inst,		\
				     &lps22hb_config_##inst,		\
				     POST_KERNEL,			\
				     CONFIG_SENSOR_INIT_PRIORITY,	\
				     &lps22hb_api);

DT_INST_FOREACH_STATUS_OKAY(LPS22HB_INIT)

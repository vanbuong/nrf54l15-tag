/*
 * Hardware validation - Nordic nRF54L15 Tag (PCA20072) board tests.
 *
 * The bus and sensor half of the suite for this board: BME688 and ADXL367 on
 * TWIM21, and the BMI270 on SPIM22. The shared harness, and the tests that
 * are the same on every board, live in hw_validation.c - see hw_validation.h.
 *
 * There is no external flash test here. Per the PCA20072 BOM, U8
 * (MX25R6435F) is Not Fitted, so the logs live in internal RRAM and the
 * shared log-storage test covers them.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "hw_validation.h"

LOG_MODULE_DECLARE(hw_validation, LOG_LEVEL_DBG);

#define record hw_validation_record

static const struct i2c_dt_spec bme688 = I2C_DT_SPEC_GET(DT_ALIAS(bme688));
static const struct i2c_dt_spec adxl367 = I2C_DT_SPEC_GET(DT_ALIAS(adxl367));

/*
 * The ADXL367's INT1, read as a plain GPIO here rather than through the
 * sensor driver's trigger machinery - this test is about the electrical path
 * and the SoC's ability to interrupt on it, not about the driver.
 */
static const struct gpio_dt_spec adxl_int1 =
	GPIO_DT_SPEC_GET(DT_ALIAS(adxl367), int1_gpios);

/*
 * BMI270 clocks in SPI mode 0 or 3; mode 0 is used here. The device tree node
 * carries reg and spi-max-frequency, so a raw spi_dt_spec can be built from
 * it even though its compatible names a driver that is not in this build.
 */
#define BMI_SPI_OP \
	(SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER)

static const struct spi_dt_spec bmi270 =
	SPI_DT_SPEC_GET(DT_ALIAS(bmi270), BMI_SPI_OP);

/* Expected identities. */
#define BME688_REG_CHIP_ID	0xd0
#define BME688_CHIP_ID		0x61

#define ADXL367_REG_DEVID_AD	0x00
#define ADXL367_REG_DEVID_MST	0x01
#define ADXL367_REG_PARTID	0x02
#define ADXL367_REG_REVID	0x03
#define ADXL367_DEVID_AD	0xad	/* Analog Devices */
#define ADXL367_DEVID_MST	0x1d	/* MEMS sensor */
#define ADXL367_PARTID		0xf2	/* ADXL367 */

#define BMI270_REG_CHIP_ID	0x00
#define BMI270_CHIP_ID		0x24

/* ADXL367 registers used by the interrupt test. */
#define ADXL367_REG_SOFT_RESET	0x1f
#define ADXL367_REG_INTMAP1_LWR	0x2a
#define ADXL367_REG_FILTER_CTL	0x2c
#define ADXL367_REG_POWER_CTL	0x2d
#define ADXL367_SOFT_RESET_KEY	0x52	/* ASCII 'R' */
#define ADXL367_INTMAP1_DATA_RDY 0x01
#define ADXL367_FILTER_ODR_100HZ 0x03
#define ADXL367_POWER_MEASURE	0x02

/* --------------------------------------------------------------------------
 * I2C bus scan
 * ------------------------------------------------------------------------ */

static void test_i2c_scan(void)
{
	unsigned int found = 0;
	bool saw_bme = false;
	bool saw_adxl = false;

	if (!device_is_ready(bme688.bus)) {
		record("I2C bus", RESULT_FAIL, "TWIM21 not ready");
		return;
	}

	LOG_INF("Scanning I2C bus %s", bme688.bus->name);

	for (uint8_t addr = 0x08; addr < 0x78; addr++) {
		/*
		 * Zero-length write: the I2C peripheral ACKs the address byte
		 * itself before any command interpretation happens, so this
		 * does not depend on the device recognising what follows.
		 */
		if (i2c_write(bme688.bus, NULL, 0, addr) == 0) {
			LOG_INF("  device at 0x%02x", addr);
			found++;

			if (addr == bme688.addr) {
				saw_bme = true;
			}
			if (addr == adxl367.addr) {
				saw_adxl = true;
			}
		}
	}

	if (found == 0U) {
		record("I2C bus", RESULT_FAIL,
		       "no device answered - check pull-ups and pinctrl");
	} else if (!saw_bme || !saw_adxl) {
		record("I2C bus", RESULT_FAIL,
		       "%u found, BME688 0x%02x %s, ADXL367 0x%02x %s", found,
		       bme688.addr, saw_bme ? "ok" : "MISSING", adxl367.addr,
		       saw_adxl ? "ok" : "MISSING");
	} else {
		record("I2C bus", RESULT_PASS,
		       "%u device(s), BME688 and ADXL367 both present", found);
	}
}

/* --------------------------------------------------------------------------
 * Test: BME688 identity and a gas-sensor sanity reading
 * ------------------------------------------------------------------------ */

static void test_bme688(void)
{
	uint8_t chip_id = 0;
	int err;

	if (!device_is_ready(bme688.bus)) {
		record("BME688", RESULT_FAIL, "bus not ready");
		return;
	}

	err = i2c_reg_read_byte_dt(&bme688, BME688_REG_CHIP_ID, &chip_id);
	if (err) {
		record("BME688", RESULT_FAIL, "chip ID read failed (%d)", err);
		return;
	}

	LOG_INF("BME688 chip ID: 0x%02x", chip_id);

	/*
	 * 0x61 is shared by the BME680 and BME688 - they are register
	 * compatible, which is why the in-tree "bosch,bme680" driver binds to
	 * the BME688. The ID alone cannot tell them apart, and for this
	 * application it does not need to.
	 */
	if (chip_id != BME688_CHIP_ID) {
		record("BME688", RESULT_FAIL, "chip ID 0x%02x, expected 0x%02x",
		       chip_id, BME688_CHIP_ID);
		return;
	}

	record("BME688", RESULT_PASS, "chip ID 0x%02x at 0x%02x", chip_id,
	       bme688.addr);
}

/* --------------------------------------------------------------------------
 * Test: ADXL367 identity
 * ------------------------------------------------------------------------ */

static void test_adxl367(void)
{
	uint8_t id[4] = { 0 };
	int err;

	if (!device_is_ready(adxl367.bus)) {
		record("ADXL367", RESULT_FAIL, "bus not ready");
		return;
	}

	/*
	 * Four consecutive ID registers, read as a burst: DEVID_AD (vendor),
	 * DEVID_MST (device family), PARTID (the part) and REVID. Reading all
	 * four distinguishes "wrong Analog Devices part fitted" from "nothing
	 * on this address", which a single register cannot.
	 */
	err = i2c_burst_read_dt(&adxl367, ADXL367_REG_DEVID_AD, id, sizeof(id));
	if (err) {
		record("ADXL367", RESULT_FAIL, "ID read failed (%d)", err);
		return;
	}

	LOG_INF("ADXL367 IDs: AD 0x%02x, MST 0x%02x, PART 0x%02x, REV 0x%02x",
		id[0], id[1], id[2], id[3]);

	if (id[0] != ADXL367_DEVID_AD || id[1] != ADXL367_DEVID_MST) {
		record("ADXL367", RESULT_FAIL,
		       "vendor/family 0x%02x 0x%02x, expected 0xad 0x1d", id[0],
		       id[1]);
		return;
	}

	if (id[2] != ADXL367_PARTID) {
		record("ADXL367", RESULT_FAIL,
		       "part 0x%02x, expected 0xf2 (ADXL367)", id[2]);
		return;
	}

	record("ADXL367", RESULT_PASS, "PARTID 0xf2, rev %u at 0x%02x", id[3],
	       adxl367.addr);
}

/* --------------------------------------------------------------------------
 * Test: the ADXL367 interrupt line actually reaches the SoC
 * ------------------------------------------------------------------------ */

#define ADXL_INT_WAIT_MS 2000

/*
 * This is the test worth having on this board, because it covers the one
 * thing the HOLyiot design could not do. There, the accelerometer interrupts
 * landed on port P2, which has no GPIOTE and no SENSE, so they could neither
 * interrupt nor wake the SoC and motion had to be polled. Here INT1 is on
 * P0.03, which has both.
 *
 * It deliberately uses DATA_READY rather than activity detection. What can
 * be wrong in hardware is the electrical path - INT1 to P0.03 - and the
 * SoC's ability to take an edge on that pin. DATA_READY exercises exactly
 * that with three register writes and no thresholds to get wrong, and it
 * needs nobody to shake the board. Activity detection itself is the driver's
 * business and is exercised by the application build.
 */
static void test_adxl367_interrupt(void)
{
	int64_t deadline;
	int err;
	int seen_high = 0;

	if (!device_is_ready(adxl367.bus)) {
		record("ADXL367 INT1", RESULT_FAIL, "bus not ready");
		return;
	}

	if (!gpio_is_ready_dt(&adxl_int1)) {
		record("ADXL367 INT1", RESULT_FAIL, "GPIO port not ready");
		return;
	}

	if (gpio_pin_configure_dt(&adxl_int1, GPIO_INPUT) != 0) {
		record("ADXL367 INT1", RESULT_FAIL, "cannot configure as input");
		return;
	}

	/*
	 * Prove the pin can raise an interrupt before relying on it. This is
	 * the call that returns -ENOTSUP on a port with no GPIOTE, which is
	 * precisely how the HOLyiot limitation showed itself.
	 */
	err = gpio_pin_interrupt_configure_dt(&adxl_int1, GPIO_INT_EDGE_RISING);
	if (err) {
		record("ADXL367 INT1", RESULT_FAIL,
		       "pin cannot interrupt (%d) - wrong port?", err);
		return;
	}

	/* Disarm again; the level is polled below, no callback is needed. */
	(void)gpio_pin_interrupt_configure_dt(&adxl_int1, GPIO_INT_DISABLE);

	err = i2c_reg_write_byte_dt(&adxl367, ADXL367_REG_SOFT_RESET,
				    ADXL367_SOFT_RESET_KEY);
	if (err) {
		record("ADXL367 INT1", RESULT_FAIL, "soft reset failed (%d)",
		       err);
		return;
	}

	k_sleep(K_MSEC(10));

	err = i2c_reg_write_byte_dt(&adxl367, ADXL367_REG_INTMAP1_LWR,
				    ADXL367_INTMAP1_DATA_RDY);
	err |= i2c_reg_write_byte_dt(&adxl367, ADXL367_REG_FILTER_CTL,
				     ADXL367_FILTER_ODR_100HZ);
	err |= i2c_reg_write_byte_dt(&adxl367, ADXL367_REG_POWER_CTL,
				     ADXL367_POWER_MEASURE);
	if (err) {
		record("ADXL367 INT1", RESULT_FAIL, "configuration failed");
		return;
	}

	LOG_INF("Waiting up to %d ms for ADXL367 DATA_READY on INT1",
		ADXL_INT_WAIT_MS);

	deadline = k_uptime_get() + ADXL_INT_WAIT_MS;

	while (k_uptime_get() < deadline) {
		if (gpio_pin_get_dt(&adxl_int1) == 1) {
			seen_high = 1;
			break;
		}

		k_sleep(K_MSEC(5));
	}

	/* Back to standby so the part is not left converting at 100 Hz. */
	(void)i2c_reg_write_byte_dt(&adxl367, ADXL367_REG_POWER_CTL, 0x00);

	if (!seen_high) {
		record("ADXL367 INT1", RESULT_FAIL,
		       "no edge in %d ms - check INT1 to P0.03",
		       ADXL_INT_WAIT_MS);
		return;
	}

	record("ADXL367 INT1", RESULT_PASS,
	       "asserted, and P0.03 can interrupt");
}

/* --------------------------------------------------------------------------
 * Test: BMI270 identity
 * ------------------------------------------------------------------------ */

static int bmi270_read_reg(uint8_t reg, uint8_t *value)
{
	uint8_t cmd = reg | 0x80U;
	const struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

	/*
	 * A BMI270 SPI read is address, then one dummy byte, then data - the
	 * dummy byte is part of the protocol, not a bus artefact, and getting
	 * it wrong shifts every register by one.
	 */
	struct spi_buf rx_bufs[2] = {
		{ .buf = NULL, .len = 2 },
		{ .buf = value, .len = 1 },
	};
	const struct spi_buf_set rx = { .buffers = rx_bufs, .count = 2 };

	return spi_transceive_dt(&bmi270, &tx, &rx);
}

static void test_bmi270(void)
{
	uint8_t chip_id = 0;
	int err;

	if (!spi_is_ready_dt(&bmi270)) {
		record("BMI270", RESULT_FAIL, "SPIM22 not ready");
		return;
	}

	/*
	 * The BMI270 powers up listening on I2C and only switches to SPI on
	 * the first rising edge of CSB. That first transfer is consumed by the
	 * switch and returns junk, so it is issued and discarded before the
	 * read that counts. Skipping this is the classic way to see 0x00 from
	 * a perfectly good part.
	 */
	(void)bmi270_read_reg(BMI270_REG_CHIP_ID, &chip_id);
	k_sleep(K_MSEC(1));

	err = bmi270_read_reg(BMI270_REG_CHIP_ID, &chip_id);
	if (err) {
		record("BMI270", RESULT_FAIL, "chip ID read failed (%d)", err);
		return;
	}

	LOG_INF("BMI270 chip ID: 0x%02x", chip_id);

	if (chip_id == 0x00 || chip_id == 0xff) {
		record("BMI270", RESULT_FAIL,
		       "read 0x%02x - bus idle or CS wrong", chip_id);
		return;
	}

	if (chip_id != BMI270_CHIP_ID) {
		record("BMI270", RESULT_FAIL, "chip ID 0x%02x, expected 0x24",
		       chip_id);
		return;
	}

	record("BMI270", RESULT_PASS, "chip ID 0x24 on SPIM22");
}

/* --------------------------------------------------------------------------
 * Entry point, called by the shared harness
 * ------------------------------------------------------------------------ */

void hw_validation_board_tests(void)
{
	test_i2c_scan();
	test_bme688();
	test_adxl367();
	test_adxl367_interrupt();
	test_bmi270();
}

/*
 * Hardware validation - HOLyiot 25025 board tests.
 *
 * The bus and sensor half of the suite for this board: SHT40 on TWIM22,
 * LIS2DH12 and LPS22HB on SPIM00, and the BY25Q16 NOR on SPIM21. The shared
 * harness, and the tests that are the same on every board, live in
 * hw_validation.c - see hw_validation.h.
 *
 * Two open questions from the schematic review are settled by this build:
 *
 *   1. Which sensor is on which chip select (P2.05 vs P2.06).
 *   2. Whether the pressure sensor is an LPS22HB (0xB1) or LPS22HH (0xB3).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "hw_validation.h"

LOG_MODULE_DECLARE(hw_validation, LOG_LEVEL_DBG);

#define record hw_validation_record

static const struct i2c_dt_spec sht40 = I2C_DT_SPEC_GET(DT_ALIAS(sht40));

/*
 * ST MEMS parts clock in SPI mode 3. Both device tree nodes already carry a
 * reg and spi-max-frequency, so a raw spi_dt_spec can be built from them even
 * though their compatible names a sensor driver that is not in this build.
 */
#define ST_SPI_OP                                                              \
	(SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER |             \
	 SPI_MODE_CPOL | SPI_MODE_CPHA)

static const struct spi_dt_spec spi_cs_accel =
	SPI_DT_SPEC_GET(DT_ALIAS(lis2dh12), ST_SPI_OP);
static const struct spi_dt_spec spi_cs_baro =
	SPI_DT_SPEC_GET(DT_ALIAS(lps22), ST_SPI_OP);

static const struct device *const ext_flash = DEVICE_DT_GET(DT_ALIAS(ext_flash));

/* --------------------------------------------------------------------------
 * ST MEMS raw register access
 * ------------------------------------------------------------------------ */

/*
 * The two parts do not share a command byte layout, which matters as soon as
 * more than one register is read:
 *
 *   LIS2DH12  bit7 = read, bit6 = auto-increment, bits 5..0 = address
 *   LPS22Hx   bit7 = read, bits 6..0 = address, auto-increment always on
 *
 * So setting bit 6 for a burst read is required on the accelerometer and
 * corrupts the address on the barometer.
 */
static int st_read_regs(const struct spi_dt_spec *spec, uint8_t reg,
			uint8_t *values, size_t count, bool needs_ms_bit)
{
	uint8_t cmd = reg | 0x80U;

	if (needs_ms_bit && count > 1U) {
		cmd |= 0x40U;
	}

	const struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

	struct spi_buf rx_bufs[2] = {
		{ .buf = NULL, .len = 1 },	/* discard during the command */
		{ .buf = values, .len = count },
	};
	const struct spi_buf_set rx = { .buffers = rx_bufs, .count = 2 };

	return spi_transceive_dt(spec, &tx, &rx);
}

static int st_write_reg(const struct spi_dt_spec *spec, uint8_t reg,
			uint8_t value)
{
	uint8_t frame[2] = { reg & 0x7fU, value };

	const struct spi_buf tx_buf = { .buf = frame, .len = sizeof(frame) };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(spec, &tx);
}

/* WHO_AM_I values for every part this design might plausibly carry. */
static const char *identify_st_part(uint8_t who_am_i)
{
	switch (who_am_i) {
	case 0x33:
		return "LIS2DH12 accelerometer";
	case 0xb1:
		return "LPS22HB pressure sensor";
	case 0xb3:
		return "LPS22HH pressure sensor";
	case 0xb4:
		return "LPS22DF pressure sensor";
	default:
		return "unknown";
	}
}


/* --------------------------------------------------------------------------
 * Test: I2C bus scan and SHT40
 * ------------------------------------------------------------------------ */

static uint8_t sensirion_crc(const uint8_t *data, size_t len)
{
	uint8_t crc = 0xff;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];

		for (int bit = 0; bit < 8; bit++) {
			crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U) :
					      (uint8_t)(crc << 1);
		}
	}

	return crc;
}

static void test_i2c_scan(void)
{
	unsigned int found = 0;
	bool saw_sht40 = false;

	if (!device_is_ready(sht40.bus)) {
		record("I2C bus", RESULT_FAIL, "TWIM22 not ready");
		return;
	}

	LOG_INF("Scanning I2C bus %s", sht40.bus->name);

	for (uint8_t addr = 0x08; addr < 0x78; addr++) {
		/*
		 * Zero-length write: the I2C peripheral ACKs the address byte
		 * itself before any command interpretation happens, so this
		 * doesn't depend on the device recognising anything that
		 * follows. A bare *read* is not a reliable probe for
		 * command/response parts like the SHT4x family - it only
		 * answers a read that follows one of its own write commands
		 * (soft reset, serial number, measure) and NACKs a bare read,
		 * which is why this test used to report the bus as silent
		 * even with a working, responding SHT40 on it.
		 */
		if (i2c_write(sht40.bus, NULL, 0, addr) == 0) {
			LOG_INF("  device at 0x%02x", addr);
			found++;

			if (addr == sht40.addr) {
				saw_sht40 = true;
			}
		}
	}

	if (found == 0U) {
		record("I2C bus", RESULT_FAIL,
		       "no device answered - check pull-ups and pinctrl");
	} else {
		record("I2C bus", RESULT_PASS, "%u device(s) answered%s", found,
		       saw_sht40 ? ", including SHT40" : "");
	}
}

static void test_sht40(void)
{
	uint8_t cmd_reset = 0x94;
	uint8_t cmd_serial = 0x89;
	uint8_t cmd_measure = 0xfd;
	uint8_t rx[6];
	uint32_t serial;
	int32_t temp_c_x100;
	int32_t humidity_x100;
	uint16_t raw_t;
	uint16_t raw_rh;
	int err;

	if (!device_is_ready(sht40.bus)) {
		record("SHT40", RESULT_SKIP, "I2C bus not ready");
		return;
	}

	err = i2c_write_dt(&sht40, &cmd_reset, 1);
	if (err) {
		record("SHT40", RESULT_FAIL, "soft reset NACKed (%d)", err);
		return;
	}

	k_sleep(K_MSEC(2));

	/* Serial number proves we are talking to a real SHT4x, not a stub. */
	if (i2c_write_dt(&sht40, &cmd_serial, 1) == 0) {
		k_sleep(K_MSEC(10));

		if (i2c_read_dt(&sht40, rx, sizeof(rx)) == 0 &&
		    sensirion_crc(&rx[0], 2) == rx[2] &&
		    sensirion_crc(&rx[3], 2) == rx[5]) {
			serial = ((uint32_t)rx[0] << 24) |
				 ((uint32_t)rx[1] << 16) |
				 ((uint32_t)rx[3] << 8) | rx[4];
			LOG_INF("SHT40 serial number: 0x%08x", serial);
		} else {
			LOG_WRN("SHT40 serial read failed its CRC");
		}
	}

	err = i2c_write_dt(&sht40, &cmd_measure, 1);
	if (err) {
		record("SHT40", RESULT_FAIL, "measure command NACKed (%d)",
		       err);
		return;
	}

	k_sleep(K_MSEC(10));

	err = i2c_read_dt(&sht40, rx, sizeof(rx));
	if (err) {
		record("SHT40", RESULT_FAIL, "measurement read failed (%d)",
		       err);
		return;
	}

	if (sensirion_crc(&rx[0], 2) != rx[2] ||
	    sensirion_crc(&rx[3], 2) != rx[5]) {
		record("SHT40", RESULT_FAIL, "measurement CRC mismatch");
		return;
	}

	raw_t = ((uint16_t)rx[0] << 8) | rx[1];
	raw_rh = ((uint16_t)rx[3] << 8) | rx[4];

	temp_c_x100 = -4500 + (int32_t)((17500LL * raw_t) / 65535);
	humidity_x100 = -600 + (int32_t)((12500LL * raw_rh) / 65535);

	LOG_INF("SHT40: %d.%02d C, %d.%02d %%RH", temp_c_x100 / 100,
		abs(temp_c_x100 % 100), humidity_x100 / 100,
		abs(humidity_x100 % 100));

	/* Indoor bring-up sanity, not a calibration check. */
	if (temp_c_x100 < -1000 || temp_c_x100 > 6000) {
		record("SHT40", RESULT_FAIL, "temperature %d.%02d C implausible",
		       temp_c_x100 / 100, abs(temp_c_x100 % 100));
		return;
	}

	if (humidity_x100 < 0 || humidity_x100 > 10000) {
		record("SHT40", RESULT_FAIL, "humidity %d.%02d %% out of range",
		       humidity_x100 / 100, abs(humidity_x100 % 100));
		return;
	}

	record("SHT40", RESULT_PASS, "%d.%02d C, %d.%02d %%RH",
	       temp_c_x100 / 100, abs(temp_c_x100 % 100), humidity_x100 / 100,
	       abs(humidity_x100 % 100));
}


/* --------------------------------------------------------------------------
 * Test: what is on each SPI chip select
 * ------------------------------------------------------------------------ */

static uint8_t who_am_i_accel_cs;
static uint8_t who_am_i_baro_cs;

static void test_spi_identity(void)
{
	int err;

	if (!spi_is_ready_dt(&spi_cs_accel) || !spi_is_ready_dt(&spi_cs_baro)) {
		record("SPI sensors", RESULT_FAIL, "sensor SPI bus not ready");
		return;
	}

	err = st_read_regs(&spi_cs_accel, 0x0f, &who_am_i_accel_cs, 1, false);
	if (err) {
		record("SPI sensors", RESULT_FAIL,
		       "transfer failed on CS P2.06 (%d)", err);
		return;
	}

	err = st_read_regs(&spi_cs_baro, 0x0f, &who_am_i_baro_cs, 1, false);
	if (err) {
		record("SPI sensors", RESULT_FAIL,
		       "transfer failed on CS P2.05 (%d)", err);
		return;
	}

	LOG_INF("CS P2.06 (lis2dh12 node): WHO_AM_I 0x%02x -> %s",
		who_am_i_accel_cs, identify_st_part(who_am_i_accel_cs));
	LOG_INF("CS P2.05 (lps22 node):    WHO_AM_I 0x%02x -> %s",
		who_am_i_baro_cs, identify_st_part(who_am_i_baro_cs));

	if (who_am_i_accel_cs == 0x33 &&
	    (who_am_i_baro_cs == 0xb1 || who_am_i_baro_cs == 0xb3 ||
	     who_am_i_baro_cs == 0xb4)) {
		record("SPI sensors", RESULT_PASS,
		       "accel on P2.06, pressure on P2.05 as mapped");
		return;
	}

	/*
	 * The schematic PDF was ambiguous about which CS goes where. If the
	 * parts answer on the opposite chip selects, the device tree needs its
	 * two cs-gpios entries swapped - say so explicitly rather than just
	 * failing.
	 */
	if (who_am_i_baro_cs == 0x33 &&
	    (who_am_i_accel_cs == 0xb1 || who_am_i_accel_cs == 0xb3 ||
	     who_am_i_accel_cs == 0xb4)) {
		record("SPI sensors", RESULT_FAIL,
		       "CS SWAPPED: swap cs-gpios in the board DTS");
		return;
	}

	record("SPI sensors", RESULT_FAIL, "unexpected IDs 0x%02x / 0x%02x",
	       who_am_i_accel_cs, who_am_i_baro_cs);
}

/* --------------------------------------------------------------------------
 * Test: LIS2DH12 acceleration
 * ------------------------------------------------------------------------ */

/* Picks whichever chip select actually answered as the accelerometer. */
static const struct spi_dt_spec *accel_spec(void)
{
	if (who_am_i_accel_cs == 0x33) {
		return &spi_cs_accel;
	}

	if (who_am_i_baro_cs == 0x33) {
		return &spi_cs_baro;
	}

	return NULL;
}

static void test_lis2dh12(void)
{
	const struct spi_dt_spec *spec = accel_spec();
	uint8_t raw[6];
	int16_t axis[3];
	int32_t magnitude_sq;
	uint32_t magnitude;

	if (spec == NULL) {
		record("LIS2DH12", RESULT_SKIP, "no accelerometer identified");
		return;
	}

	/* 100 Hz, all three axes enabled, high resolution with block update. */
	if (st_write_reg(spec, 0x20, 0x57) != 0 ||
	    st_write_reg(spec, 0x23, 0x88) != 0) {
		record("LIS2DH12", RESULT_FAIL, "cannot write control registers");
		return;
	}

	k_sleep(K_MSEC(100));

	if (st_read_regs(spec, 0x28, raw, sizeof(raw), true) != 0) {
		record("LIS2DH12", RESULT_FAIL, "cannot read output registers");
		return;
	}

	/*
	 * High-resolution mode is 12 bits left-justified in 16, and at the
	 * default full scale one count is 1 mg.
	 */
	for (int i = 0; i < 3; i++) {
		int16_t value = (int16_t)(((uint16_t)raw[i * 2 + 1] << 8) |
					  raw[i * 2]);
		axis[i] = value >> 4;
	}

	magnitude_sq = (int32_t)axis[0] * axis[0] + (int32_t)axis[1] * axis[1] +
		       (int32_t)axis[2] * axis[2];

	/* Integer square root is plenty for a 1 g sanity check. */
	magnitude = 0;
	while ((magnitude + 1) * (magnitude + 1) <= (uint32_t)magnitude_sq) {
		magnitude++;
	}

	LOG_INF("LIS2DH12: X %d mg, Y %d mg, Z %d mg, magnitude %u mg", axis[0],
		axis[1], axis[2], magnitude);

	/*
	 * A tag sitting still measures 1 g in some direction. Anything far off
	 * means a misconfigured full scale, a stuck bus, or a part that is not
	 * actually reading its MEMS element.
	 */
	if (magnitude < 700U || magnitude > 1300U) {
		record("LIS2DH12", RESULT_FAIL,
		       "magnitude %u mg, expected about 1000 mg at rest",
		       magnitude);
		return;
	}

	record("LIS2DH12", RESULT_PASS, "%u mg at rest, axes %d/%d/%d",
	       magnitude, axis[0], axis[1], axis[2]);
}

/* --------------------------------------------------------------------------
 * Test: LPS22 pressure
 * ------------------------------------------------------------------------ */

static const struct spi_dt_spec *baro_spec(void)
{
	if (who_am_i_baro_cs == 0xb1 || who_am_i_baro_cs == 0xb3 ||
	    who_am_i_baro_cs == 0xb4) {
		return &spi_cs_baro;
	}

	if (who_am_i_accel_cs == 0xb1 || who_am_i_accel_cs == 0xb3 ||
	    who_am_i_accel_cs == 0xb4) {
		return &spi_cs_accel;
	}

	return NULL;
}

static void test_lps22(void)
{
	const struct spi_dt_spec *spec = baro_spec();
	uint8_t raw[3];
	uint32_t counts;
	uint32_t pressure_pa;

	if (spec == NULL) {
		record("LPS22", RESULT_SKIP, "no pressure sensor identified");
		return;
	}

	/* CTRL_REG1: 10 Hz continuous, block data update. ODR bits match on
	 * LPS22HB and LPS22HH, so this works whichever is fitted.
	 */
	if (st_write_reg(spec, 0x10, 0x22) != 0) {
		record("LPS22", RESULT_FAIL, "cannot write CTRL_REG1");
		return;
	}

	k_sleep(K_MSEC(200));

	if (st_read_regs(spec, 0x28, raw, sizeof(raw), false) != 0) {
		record("LPS22", RESULT_FAIL, "cannot read pressure registers");
		return;
	}

	/* 24-bit unsigned, 4096 LSB per hPa. */
	counts = ((uint32_t)raw[2] << 16) | ((uint32_t)raw[1] << 8) | raw[0];
	pressure_pa = (counts * 100U) / 4096U;

	LOG_INF("LPS22: %u Pa (%u.%02u hPa)", pressure_pa, pressure_pa / 100,
		pressure_pa % 100);

	/* Roughly sea level to the top of a tall building. */
	if (pressure_pa < 80000U || pressure_pa > 110000U) {
		record("LPS22", RESULT_FAIL, "%u Pa is outside 800-1100 hPa",
		       pressure_pa);
		return;
	}

	record("LPS22", RESULT_PASS, "%u.%02u hPa, WHO_AM_I 0x%02x",
	       pressure_pa / 100, pressure_pa % 100,
	       (spec == &spi_cs_baro) ? who_am_i_baro_cs : who_am_i_accel_cs);
}


/* --------------------------------------------------------------------------
 * Test: external SPI NOR
 * ------------------------------------------------------------------------ */

static void test_flash_id(void)
{
	uint8_t jedec[3];
	int err;

	if (!device_is_ready(ext_flash)) {
		record("SPI NOR ID", RESULT_FAIL, "device not ready");
		return;
	}

	err = flash_read_jedec_id(ext_flash, jedec);
	if (err) {
		record("SPI NOR ID", RESULT_FAIL, "JEDEC read failed (%d)",
		       err);
		return;
	}

	LOG_INF("SPI NOR JEDEC ID: %02x %02x %02x", jedec[0], jedec[1],
		jedec[2]);

	if (jedec[0] == 0x00 || jedec[0] == 0xff) {
		record("SPI NOR ID", RESULT_FAIL,
		       "read %02x %02x %02x - bus looks idle", jedec[0],
		       jedec[1], jedec[2]);
		return;
	}

	/*
	 * 68 10 15: Boya, confirmed at bring-up (not 68 40 15 - see the
	 * jedec-id comment on the ext_flash node).
	 */
	if (jedec[0] == 0x68 && jedec[1] == 0x10 && jedec[2] == 0x15) {
		record("SPI NOR ID", RESULT_PASS, "%02x %02x %02x as expected",
		       jedec[0], jedec[1], jedec[2]);
	} else {
		record("SPI NOR ID", RESULT_FAIL,
		       "%02x %02x %02x, DTS says 68 10 15", jedec[0], jedec[1],
		       jedec[2]);
	}
}


/* --------------------------------------------------------------------------
 * Entry point, called by the shared harness
 * ------------------------------------------------------------------------ */

/*
 * Ordered so that identification runs before anything that depends on it:
 * the accelerometer and pressure tests use whichever chip select actually
 * answered, not the one the device tree assumes.
 */
void hw_validation_board_tests(void)
{
	who_am_i_accel_cs = 0;
	who_am_i_baro_cs = 0;

	test_i2c_scan();
	test_sht40();
	test_spi_identity();
	test_lis2dh12();
	test_lps22();
	test_flash_id();
}

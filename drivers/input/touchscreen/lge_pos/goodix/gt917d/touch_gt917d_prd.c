/* touch_gt917d_prd.c
 *
 * Copyright (C) 2019 LGE.
 *
 * Author: BSP-TOUCH@lge.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 *  Include to Local Header File
 */
#include "touch_gt917d.h"
#include "touch_gt917d_prd.h"

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>

#include <touch_core.h>
#include <touch_hwif.h>

#define MAX_ROW 29
#define MAX_COL 14

static char line[10000];
static char buffer[PAGE_SIZE];

static s16 jitter_limit_temp[MAX_ROW * MAX_COL];
static u16 accord_limit_temp[MAX_ROW * MAX_COL];
static u16 max_limit_vale_id0[MAX_ROW * MAX_COL];
static u16 min_limit_vale_id0[MAX_ROW * MAX_COL];
static u16 accord_limit_vale_id0[MAX_ROW * MAX_COL];
static u16 jitter_limit_vale_id0[MAX_ROW * MAX_COL];

static int test_error_code;

struct gt917d_short_fw {
	const u8 *fw_data;
	u32 fw_total_len;
	const struct firmware *fw;
};
static struct gt917d_short_fw g_short_fw;

static u8 test_config_jitter[GT917D_CONFIG_MAX_LENGTH + GT917D_ADDR_LENGTH] = {
	(u8)(GT917D_REG_CONFIG_DATA >> 8), (u8)GT917D_REG_CONFIG_DATA, 0};

static u8 gt917d_drv_num = MAX_COL;
static u8 gt917d_sen_num = MAX_ROW;

static struct gt917d_open_info *touchpad_sum;
static struct gt917d_iot_result_info *Ito_result_info;

static u8 cfg_drv_order[MAX_DRIVER_NUM];
static u8 cfg_sen_order[MAX_SENSOR_NUM];

static void print_sd_log(char *buf)
{
	int i = 0;
	int index = 0;
	char logbuf[LOG_BUF_SIZE] = {0, };

	TOUCH_TRACE();

	while (index < strlen(buf) && buf[index] != '\0' && i < LOG_BUF_SIZE - 1) {
		logbuf[i++] = buf[index];

		/* Final character is not '\n' */
		if ((index == strlen(buf) - 1 || i == LOG_BUF_SIZE - 2)
				&& logbuf[i - 1] != '\n')
			logbuf[i++] = '\n';

		if (logbuf[i - 1] == '\n') {
			logbuf[i - 1] = '\0';
			if (i - 1 != 0)
				TOUCH_I("%s\n", logbuf);

			i = 0;
		}
		index++;
	}
}

static void write_file(struct device *dev, char *data, int write_time)
{
	int fd = 0;
	char *fname = NULL;
	char time_string[64] = {0};
	struct timespec my_time = {0};
	struct tm my_date = {0};
	mm_segment_t old_fs = get_fs();
	int boot_mode = 0;

	TOUCH_TRACE();

	set_fs(KERNEL_DS);

	boot_mode = touch_check_boot_mode(dev);

	switch (boot_mode) {
	case TOUCH_NORMAL_BOOT:
		fname = "/data/vendor/touch/touch_self_test.txt";
		break;
	case TOUCH_MINIOS_AAT:
		fname = "/data/touch/touch_self_test.txt";
		break;
	case TOUCH_MINIOS_MFTS_FOLDER:
	case TOUCH_MINIOS_MFTS_FLAT:
	case TOUCH_MINIOS_MFTS_CURVED:
		fname = "/data/touch/touch_self_mfts.txt";
		break;
	default:
		TOUCH_I("%s : not support mode\n", __func__);
		break;
	}

	if (fname) {
		fd = sys_open(fname, O_WRONLY | O_CREAT | O_APPEND, 0666);
		sys_chmod(fname, 0666);
	} else {
		TOUCH_E("fname is NULL, can not open FILE\n");
		set_fs(old_fs);
		return;
	}

	if (fd >= 0) {
		if (write_time == TIME_INFO_WRITE) {
			my_time = __current_kernel_time();
			time_to_tm(my_time.tv_sec,
					sys_tz.tz_minuteswest * 60 * (-1),
					&my_date);
			snprintf(time_string, 64,
					"\n[%02d-%02d %02d:%02d:%02d.%03lu]\n",
					my_date.tm_mon + 1,
					my_date.tm_mday, my_date.tm_hour,
					my_date.tm_min, my_date.tm_sec,
					(unsigned long) my_time.tv_nsec / 1000000);
			sys_write(fd, time_string, strlen(time_string));
		}
		sys_write(fd, data, strlen(data));
		sys_close(fd);
	} else {
		TOUCH_I("File open failed\n");
	}
	set_fs(old_fs);
}

static int spec_file_read(struct device *dev)
{
	int ret = 0;
	struct touch_core_data *ts = to_touch_core(dev);
	const struct firmware *fwlimit = NULL;
	const char *path[2] = {ts->dual_panel_spec[0], ts->dual_panel_spec_mfts[0]};
	int boot_mode = TOUCH_NORMAL_BOOT;
	int path_idx = 0;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);
	if ((boot_mode == TOUCH_MINIOS_MFTS_FOLDER)
			|| (boot_mode == TOUCH_MINIOS_MFTS_FLAT)
			|| (boot_mode == TOUCH_MINIOS_MFTS_CURVED))
		path_idx = 1;
	else
		path_idx = 0;

	if (ts->dual_panel_spec[0] == NULL || ts->dual_panel_spec_mfts[0] == NULL) {
		TOUCH_E("dual_panel_spec file name is null\n");
		ret = -ENOENT;
		goto error;
	}

	TOUCH_I("touch_panel_spec file path[%d] = %s\n", path_idx, path[path_idx]);

	ret = request_firmware(&fwlimit, path[path_idx], dev);
	if (ret) {
		TOUCH_E("request ihex is failed in normal mode\n");
		goto error;
	}

	if (fwlimit->data == NULL) {
		TOUCH_E("fwlimit->data is NULL\n");
		ret = -EINVAL;
		goto error;
	}

	if (fwlimit->size == 0) {
		TOUCH_E("fwlimit->size is 0\n");
		ret = -EINVAL;
		goto error;
	}
	strlcpy(line, fwlimit->data, fwlimit->size);

	TOUCH_I("spec_file_read success\n");

error:
	if (fwlimit)
		release_firmware(fwlimit);

	return ret;
}

static int spec_get_limit(struct device *dev, char *breakpoint, u16 limit_data[MAX_ROW * MAX_COL])
{
	int p = 0;
	int q = 0;
	int r = 0;
	int cipher = 1;
	int ret = 0;
	int retval = 0;
	char *found = NULL;
	int num = 0;

	TOUCH_TRACE();

	if (breakpoint == NULL) {
		ret = -1;
		goto error;
	}

	retval = spec_file_read(dev);
	if (retval) {
		ret = retval;
		goto error;
	}

	if (line == NULL) {
		ret = -1;
		goto error;
	}

	found = strnstr(line, breakpoint, sizeof(line));

	if (found != NULL) {
		q = found - line;
	} else {
		TOUCH_E("failed to find breakpoint. The panel_spec_file is wrong\n");
		ret = -1;
		goto error;
	}

	memset(limit_data, 0, MAX_ROW * MAX_COL * 2);

	while (1) {
		if (line[q] == ',') {
			cipher = 1;
			for (p = 1; (line[q - p] >= '0') &&
					(line[q - p] <= '9'); p++) {
				limit_data[num] += ((line[q - p] - '0') * cipher);
				cipher *= 10;
			}
			r++;
			num++;
		}
		q++;
		if (r == (int)MAX_ROW * (int)MAX_COL) {
			TOUCH_I("[%s] panel_spec_file scanning is success\n", breakpoint);
			break;
		}
	}

	if (ret == 0) {
		ret = -1;
		goto error;

	} else {
		TOUCH_I("panel_spec_file scanning is success\n");
		return ret;
	}

error:
	return ret;
}

static s32 gt917d_short_parse_cfg(struct i2c_client *client)
{
	u8 i = 0;
	u8 drv_num = 0, sen_num = 0;
	u8 config[256] = {(u8)(GT917D_REG_CONFIG_DATA >> 8), (u8)GT917D_REG_CONFIG_DATA, 0};

	TOUCH_TRACE();

	if (gt917d_i2c_read(client, config, GT917D_CONFIG_MAX_LENGTH + GT917D_ADDR_LENGTH) < 0) {
		TOUCH_E("Failed to read config!\n");
		return FAIL;
	}

	drv_num = (config[GT917D_ADDR_LENGTH + GT917D_REG_SEN_DRV_CNT - GT917D_REG_CFG_BEG] & 0x1F)
		+ (config[GT917D_ADDR_LENGTH + GT917D_REG_SEN_DRV_CNT + 1 - GT917D_REG_CFG_BEG] & 0x1F);
	sen_num = (config[GT917D_ADDR_LENGTH + GT917D_REG_SEN_DRV_CNT + 2 - GT917D_REG_CFG_BEG] & 0x0F)
		+ ((config[GT917D_ADDR_LENGTH + GT917D_REG_SEN_DRV_CNT + 2 - GT917D_REG_CFG_BEG] >> 4) & 0x0F);
	TOUCH_D(JITTER, "drv_num=%d, sen_num=%d\n", drv_num, sen_num);
	if (drv_num < MIN_DRIVER_NUM || drv_num > MAX_DRIVER_NUM) {
		TOUCH_E("driver number error!\n");
		return FAIL;
	}
	if (sen_num < MIN_SENSOR_NUM || sen_num > MAX_SENSOR_NUM) {
		TOUCH_E("sensor number error!\n");
		return FAIL;
	}
	// get sensor and driver order
	memset(cfg_sen_order, 0xFF, MAX_SENSOR_NUM);
	for (i = 0; i < sen_num; ++i)
		cfg_sen_order[i] = config[GT917D_ADDR_LENGTH + GT917D_REG_SEN_ORD - GT917D_REG_CFG_BEG + i];

	memset(cfg_drv_order, 0xFF, MAX_DRIVER_NUM);
	for (i = 0; i < drv_num; ++i)
		cfg_drv_order[i] = config[GT917D_ADDR_LENGTH + GT917D_REG_DRV_ORD - GT917D_REG_CFG_BEG + i];

	return SUCCESS;
}

static u8 gt917d_get_short_tp_chnl(u8 phy_chnl, u8 is_driver)
{
	u8 i = 0;

	TOUCH_TRACE();

	if (is_driver) {
		for (i = 0; i < MAX_DRIVER_NUM; ++i) {
			if (cfg_drv_order[i] == phy_chnl)
				return i;
			else if (cfg_drv_order[i] == 0xFF)
				return 0xFF;
		}
	} else {
		for (i = 0; i < MAX_SENSOR_NUM; ++i) {
			if (cfg_sen_order[i] == phy_chnl)
				return i;
			else if (cfg_sen_order[i] == 0xFF)
				return 0xFF;
		}
	}

	return 0xFF;
}

static s32 gt917d_i2c_end_cmd(struct i2c_client *client)
{
	u8 end_cmd[3] = {GT917D_READ_COOR_ADDR >> 8, GT917D_READ_COOR_ADDR & 0xFF, 0};
	s32 ret = 0;

	TOUCH_TRACE();

	ret = gt917d_i2c_write(client, end_cmd, 3);
	if (ret < 0)
		TOUCH_I("I2C write end_cmd error!\n");

	return ret;
}

static void gt917d_open_test_init(struct device *dev, struct i2c_client *client, int type)
{
	u8 test_config[GT917D_CONFIG_MAX_LENGTH + GT917D_ADDR_LENGTH] = {
		(u8)(GT917D_REG_CONFIG_DATA >> 8), (u8)GT917D_REG_CONFIG_DATA, 0};

	TOUCH_TRACE();

	memset(&test_config[GT917D_ADDR_LENGTH], 0, GT917D_CONFIG_MAX_LENGTH);
	memcpy(&test_config[GT917D_ADDR_LENGTH], test_cfg_info_group, ARRAY_SIZE(test_cfg_info_group));

	gt917d_i2c_write(client, test_config, ARRAY_SIZE(test_cfg_info_group) + GT917D_ADDR_LENGTH);
	touch_msleep(150);//will check this time more

	switch (type) {
	case _RAWDATA_TEST:
		spec_get_limit(dev, "max_limit_vale_id0", max_limit_vale_id0);
		spec_get_limit(dev, "min_limit_vale_id0", min_limit_vale_id0);
		break;
	case _ACCORD_TEST:
		spec_get_limit(dev, "accord_limit_vale_id0", accord_limit_vale_id0);
		break;
	case _JITTER_TEST:
		memset(&test_config_jitter[GT917D_ADDR_LENGTH], 0, GT917D_CONFIG_MAX_LENGTH);
		memcpy(&test_config_jitter[GT917D_ADDR_LENGTH], test_jitter_cfg_info_group,
				ARRAY_SIZE(test_jitter_cfg_info_group));
		spec_get_limit(dev, "jitter_limit_vale_id0", jitter_limit_vale_id0);
		break;
	default:
		TOUCH_E("GT917D:Unrecognized sensor_id,keep last!\n");
		break;
	}
}

static s32 gt917d_write_register(struct i2c_client *client, u16 addr, u8 val)
{
	s32 ret = 0;
	u8 buf[3] = {0, };

	TOUCH_TRACE();

	buf[0] = (addr >> 8) & 0xFF;
	buf[1] = addr & 0xFF;
	buf[2] = val;

	ret = gt917d_i2c_write(client, buf, 3);

	if (ret < 0)
		return ret;
	else
		return 1;
}

static s32 gt917d_read_register(struct i2c_client *client, u16 reg, u8 *buf)
{
	s32 ret = 0;

	TOUCH_TRACE();

	buf[0] = (u8)(reg >> 8);
	buf[1] = (u8)reg;
	ret = gt917d_i2c_read(client, buf, 3);

	return ret;
}

static s32 gt917d_burn_dsp_short(struct i2c_client *client)
{
	s32 ret = 0;
	u8 *opr_buf = NULL;
	u16 i = 0;
	u16 addr = GT917D_REG_DSP_SHORT;
	u16 opr_len = 0;
	u16 left = 0;
	u16 retry = 0;
	u8 read_buf[3] = {0x00};

	TOUCH_TRACE();

	TOUCH_D(JITTER, "Start writing dsp_short code\n");
	opr_buf = kzalloc(sizeof(u8) * (DSP_SHORT_BURN_CHK + 2), GFP_KERNEL);
	if (!opr_buf) {
		TOUCH_E("failed to allocate memory for check buffer!\n");
		return FAIL;
	}

	left = g_short_fw.fw_total_len;
	while (left > 0) {
		opr_buf[0] = (u8)(addr >> 8);
		opr_buf[1] = (u8)(addr);

		if (left > DSP_SHORT_BURN_CHK)
			opr_len = DSP_SHORT_BURN_CHK;
		else
			opr_len = left;

		memcpy(&opr_buf[2], &g_short_fw.fw_data[addr - GT917D_REG_DSP_SHORT], opr_len);

		ret = gt917d_i2c_write(client, opr_buf, 2 + opr_len);
		if (ret < 0) {
			TOUCH_E("write dsp_short code failed!\n");
			kfree(opr_buf);
			return FAIL;
		}
		addr += opr_len;
		left -= opr_len;
	}

	// check code: 0xC000~0xCFFF
	TOUCH_D(JITTER, "Start checking dsp_short code\n");
	addr = GT917D_REG_DSP_SHORT;
	left = g_short_fw.fw_total_len;
	while (left > 0) {
		memset(opr_buf, 0, opr_len + 2);
		opr_buf[0] = (u8)(addr >> 8);
		opr_buf[1] = (u8)(addr);

		if (left > DSP_SHORT_BURN_CHK)
			opr_len = DSP_SHORT_BURN_CHK;
		else
			opr_len = left;

		ret = gt917d_i2c_read(client, opr_buf, opr_len + 2);
		if (ret < 0) {
			kfree(opr_buf);
			return FAIL;
		}
		for (i = 0; i < opr_len; ++i) {
			if (opr_buf[i + 2] != g_short_fw.fw_data[addr - GT917D_REG_DSP_SHORT + i]) {
				TOUCH_E("check dsp_short code failed!\n");

				gt917d_write_register(client, addr + i, g_short_fw.fw_data[addr - GT917D_REG_DSP_SHORT + i]);

				TOUCH_D(JITTER, "(%d)Location: %d, 0x%02X, 0x%02X\n",
						retry + 1, addr - GT917D_REG_DSP_SHORT + i,
						opr_buf[i + 2], g_short_fw.fw_data[addr - GT917D_REG_DSP_SHORT + i]);

				touch_msleep(1);
				gt917d_read_register(client, addr + i, read_buf);
				opr_buf[i + 2] = read_buf[2];
				i--;
				retry++;
				if (retry >= 200) {
					TOUCH_D(JITTER, "Burn dsp retry timeout!\n");
					kfree(opr_buf);
					return FAIL;
				}
			}
		}

		addr += opr_len;
		left -= opr_len;
	}
	kfree(opr_buf);
	return SUCCESS;
}

static s32 gt917d_short_resist_check(struct gt917d_short_info *short_node)
{
	s32 short_resist = 0;
	struct gt917d_short_info *node = short_node;
	u8 master = node->master;
	u8 slave = node->slave;
	u8 chnnl_tx[4] = {GT917D_DRV_HEAD | 13, GT917D_DRV_HEAD | 28,
		GT917D_DRV_HEAD | 29, GT917D_DRV_HEAD | 42};
	s32 numberator = 0;
	u32 amplifier = 1000;	// amplify 1000 times to emulate float computing

	TOUCH_TRACE();

	// Tx-ABIST & Tx_ABIST
	if ((((master > chnnl_tx[0]) && (master <= chnnl_tx[1])) &&
				((slave > chnnl_tx[0]) && (slave <= chnnl_tx[1]))) ||
			(((master >= chnnl_tx[2]) && (master <= chnnl_tx[3])) &&
			 ((slave >= chnnl_tx[2]) && (slave <= chnnl_tx[3])))) {
		numberator = node->self_data * 40 * amplifier;
		short_resist = numberator / node->short_code - 40 * amplifier;
	} else if ((node->slave & (GT917D_DRV_HEAD | 0x01)) == 0x01) {	// Receiver is Rx-odd(1,3,5)
		numberator = node->self_data * 60 * amplifier;
		short_resist = numberator / node->short_code - 40 * amplifier;
	} else {
		numberator = node->self_data * 60 * amplifier;
		short_resist = numberator / node->short_code - 60 * amplifier;
	}
	TOUCH_D(JITTER, "self_data = %d\n", node->self_data);
	TOUCH_D(JITTER, "master = 0x%02X, slave = 0x%02X\n", node->master, node->slave);
	TOUCH_D(JITTER, "short_code = %d, short_resist = %d\n", node->short_code, short_resist);

	if (short_resist < 0)
		short_resist = 0;

	if (short_resist < (gt917d_resistor_threshold * amplifier)) {
		node->impedance = short_resist / amplifier;
		return SUCCESS;
	} else {
		return FAIL;
	}
}

static s32 gt917d_compute_rslt(struct i2c_client *client)
{
	u16 *self_data = NULL;
	u8 *result_buf = NULL;
	struct gt917d_short_info *short_sum = NULL;
	u16 data_len = 3 + (MAX_DRIVER_NUM + MAX_SENSOR_NUM) * 2 + 2;	// a short data frame length
	u8 i = 0;
	u8 j = 0;
	u8 tx_short_num = 0;
	u8 rx_short_num = 0;
	u16 result_addr = 0;
	s32 ret = 0;
	struct gt917d_short_info short_node = {0, };
	u8 master = 0;
	u8 slave = 0;
	u16 short_code = 0;
	u16 node_idx = 0;	// short_sum index: 0~_SHORT_INFO_MAX

	TOUCH_TRACE();

	self_data = kzalloc(sizeof(u16) * ((MAX_DRIVER_NUM + MAX_SENSOR_NUM)), GFP_KERNEL);
	result_buf = kzalloc(sizeof(u8) * (data_len + 2), GFP_KERNEL);
	short_sum = kzalloc(sizeof(struct gt917d_short_info) * _SHORT_INFO_MAX, GFP_KERNEL);

	if (!self_data || !result_buf || !short_sum) {
		TOUCH_E("allocate memory for short result failed!\n");
		kfree(self_data);
		kfree(result_buf);
		kfree(short_sum);
		return FAIL;
	}

	// Get Selfdata
	result_buf[0] = 0xA4;
	result_buf[1] = 0xA1;
	gt917d_i2c_read(client, result_buf, 2 + 144);

	for (i = 0, j = 0; i < 144; i += 2)
		self_data[j++] = (u16)(result_buf[2 + i] << 8) + (u16)(result_buf[2 + i + 1]);

	TOUCH_D(JITTER, "Self Data:\n");

	// Get TxShortNum & RxShortNum
	result_buf[0] = 0x88;
	result_buf[1] = 0x02;
	gt917d_i2c_read(client, result_buf, 2 + 2);
	tx_short_num = result_buf[2];
	rx_short_num = result_buf[3];

	TOUCH_D(JITTER, "Tx Short Num: %d, Rx Short Num: %d\n", tx_short_num, rx_short_num);

	result_addr = 0x8860;
	data_len = 3 + (MAX_DRIVER_NUM + MAX_SENSOR_NUM) * 2 + 2;
	for (i = 0; i < tx_short_num; ++i) {
		result_buf[0] = (u8) (result_addr >> 8);
		result_buf[1] = (u8) (result_addr);
		ret = gt917d_i2c_read(client, result_buf, data_len + 2);
		if (ret < 0)
			TOUCH_E("read result data failed!\n");

		TOUCH_D(JITTER, "Result Buffer:\n");

		short_node.master_is_driver = 1;
		short_node.master = result_buf[2];

		// Tx - Tx
		for (j = i + 1; j < MAX_DRIVER_NUM; ++j) {
			short_code = (result_buf[2 + 3 + j * 2] << 8) + result_buf[2 + 3 + j * 2 + 1];
			if (short_code > gt917d_short_threshold) {
				short_node.slave_is_driver = 1;
				short_node.slave = ChannelPackage_TX[j] | GT917D_DRV_HEAD;
				short_node.self_data = self_data[j];
				short_node.short_code = short_code;

				ret = gt917d_short_resist_check(&short_node);
				if (ret == SUCCESS) {
					if (node_idx < _SHORT_INFO_MAX)
						short_sum[node_idx++] = short_node;
				}
			}
		}
		// Tx - Rx
		for (j = 0; j < MAX_SENSOR_NUM; ++j) {
			short_code = (result_buf[2 + 3 + 84 + j * 2] << 8) + result_buf[2 + 3 + 84 + j * 2 + 1];

			if (short_code > gt917d_short_threshold) {
				short_node.slave_is_driver = 0;
				short_node.slave = j | GT917D_SEN_HEAD;
				short_node.self_data = self_data[MAX_DRIVER_NUM + j];
				short_node.short_code = short_code;

				ret = gt917d_short_resist_check(&short_node);
				if (ret == SUCCESS) {
					if (node_idx < _SHORT_INFO_MAX)
						short_sum[node_idx++] = short_node;
				}
			}
		}

		result_addr += data_len;
	}

	result_addr = 0xA0D2;
	data_len = 3 + MAX_SENSOR_NUM * 2 + 2;
	for (i = 0; i < rx_short_num; ++i) {
		result_buf[0] = (u8)(result_addr >> 8);
		result_buf[1] = (u8)(result_addr);
		ret = gt917d_i2c_read(client, result_buf, data_len + 2);
		if (ret < 0)
			TOUCH_E("read result data failed!\n");

		short_node.master_is_driver = 0;
		short_node.master = result_buf[2];

		// Rx - Rx
		for (j = 0; j < MAX_SENSOR_NUM; ++j) {
			if ((j == i) || ((j < i) && (j & 0x01) == 0))
				continue;

			short_code = (result_buf[2 + 3 + j * 2] << 8) + result_buf[2 + 3 + j * 2 + 1];

			if (short_code > gt917d_short_threshold) {
				short_node.slave_is_driver = 0;
				short_node.slave = j | GT917D_SEN_HEAD;
				short_node.self_data = self_data[MAX_DRIVER_NUM + j];
				short_node.short_code = short_code;

				ret = gt917d_short_resist_check(&short_node);
				if (ret == SUCCESS) {
					if (node_idx < _SHORT_INFO_MAX)
						short_sum[node_idx++] = short_node;
				}
			}
		}

		result_addr += data_len;
	}

	if (node_idx == 0) {
		ret = SUCCESS;
	} else {
		for (i = 0, j = 0; i < node_idx; ++i) {
			TOUCH_D(JITTER, "Orignal Shorted Channels: %s%d, %s%d\n",
					(short_sum[i].master_is_driver) ? "Drv" : "Sen",
					short_sum[i].master & (~GT917D_DRV_HEAD),
					(short_sum[i].slave_is_driver) ? "Drv" : "Sen",
					short_sum[i].slave & (~GT917D_DRV_HEAD));

			if ((short_sum[i].master_is_driver))
				master = gt917d_get_short_tp_chnl(short_sum[i].master - GT917D_DRV_HEAD, 1);
			else
				master = gt917d_get_short_tp_chnl(short_sum[i].master, 0);

			if ((short_sum[i].slave_is_driver))
				slave = gt917d_get_short_tp_chnl(short_sum[i].slave - GT917D_DRV_HEAD, 1);
			else
				slave = gt917d_get_short_tp_chnl(short_sum[i].slave, 0);

			if (master == 0xFF && slave == 0xFF) {
				TOUCH_D(JITTER, "unbonded channel (%d, %d) shorted!\n",
						short_sum[i].master, short_sum[i].slave);
				continue;
			} else {
				short_sum[j].slave = slave;
				short_sum[j].master = master;
				short_sum[j].slave_is_driver = short_sum[i].slave_is_driver;
				short_sum[j].master_is_driver = short_sum[i].master_is_driver;
				short_sum[j].impedance = short_sum[i].impedance;
				short_sum[j].self_data = short_sum[i].self_data;
				short_sum[j].short_code = short_sum[i].short_code;
				++j;
			}
		}
		node_idx = j;
		if (node_idx == 0) {
			ret = SUCCESS;
		} else {
			for (i = 0; i < node_idx; ++i) {
				TOUCH_I("  %s%02d & %s%02d Shorted! (R = %dKOhm)\n",
						(short_sum[i].master_is_driver) ? "Drv" : "Sen",
						short_sum[i].master,
						(short_sum[i].slave_is_driver) ? "Drv" : "Sen",
						short_sum[i].slave,
						short_sum[i].impedance);
			}
			ret = FAIL;
		}
	}
	kfree(self_data);
	kfree(short_sum);
	kfree(result_buf);

	return ret;
}

static s32 gt917d_test_gnd_vdd_short(struct i2c_client *client)
{
	u8 *data = NULL;
	s32 ret = 0;
	s32 i = 0;
	u16 len = (MAX_DRIVER_NUM + MAX_SENSOR_NUM) * 2;
	u16 short_code = 0;
	s32 r = -1;
	u32 short_res = 0;
	u8 short_chnl = 0;
	u16 amplifier = 1000;

	TOUCH_TRACE();

	data = kzalloc(sizeof(u8) * (len + 2), GFP_KERNEL);
	if (data == NULL) {
		TOUCH_E("failed to allocate memory for gnd vdd test data buffer\n");
		return FAIL;
	}

	data[0] = 0xA5;
	data[1] = 0x31;
	gt917d_i2c_read(client, data, 2 + len);

	ret = SUCCESS;
	for (i = 0; i < len; i += 2) {
		short_code = (data[2 + i] << 8) + (data[2 + i + 1]);
		if (short_code == 0)
			continue;

		if ((short_code & 0x8000) == 0) {	// short with GND
			r = 5266285 * 10 / (short_code & (~0x8000)) - 40 * amplifier;
		} else {				// short with VDD
			r = 40 * 9 * 1024 * (100 * GT917D_VDD - 900) / ((short_code & (~0x8000)) * 7) - 40 * 1000;
			TOUCH_D(JITTER, "vdd short_code: %d\n", short_code & (~0x8000));
		}
		TOUCH_D(JITTER, "resistor: %d, short_code: %d\n", r, short_code);

		short_res = (r >= 0) ? r : 0xFFFF;
		if (short_res == 0xFFFF) {
		} else {
			if (short_res < (gt917d_gnd_resistor_threshold * amplifier)) {
				if (i < MAX_DRIVER_NUM * 2) {	// driver
					short_chnl = gt917d_get_short_tp_chnl(ChannelPackage_TX[i / 2], 1);
					TOUCH_I("driver%02d & gnd/vdd shorted!\n", short_chnl);
					if (short_chnl == 0xFF) {
						TOUCH_I("unbonded channel\n");
					} else {
						TOUCH_I("Drv%02d & GND/VDD Shorted! (R = %dKOhm)\n",
								short_chnl, short_res / amplifier);
					}
				} else {
					short_chnl = gt917d_get_short_tp_chnl((i/2) - MAX_DRIVER_NUM, 0);
					TOUCH_I("sensor%02d & gnd/vdd shorted!\n", short_chnl);
					if (short_chnl == 0xFF) {
						TOUCH_I("unbonded channel\n");
					} else {
						TOUCH_I("Sen%02d & GND/VDD Shorted! (R = %dKOhm)\n",
								short_chnl, short_res / amplifier);
					}
				}
				ret = FAIL;
			}
		}
	}
	kfree(data);
	return ret;
}

static void gt917d_leave_short_test(struct i2c_client *client)
{
	TOUCH_TRACE();

	gt917d_hw_reset(&client->dev);
	touch_msleep(100);
	TOUCH_D(JITTER, "---gt917d short test out reset---\n");
	gt917d_send_cfg(&client->dev);
	touch_msleep(150);//will check this time more
	TOUCH_I("recover set cfg\n");
	TOUCH_I("---gt917d short test end---\n");
}

static s32 gt917d_get_short_file(struct i2c_client *client)
{
	s32 ret = 0;
	char short_fwpath[256] = {0};
	struct touch_core_data *ts = to_touch_core(&client->dev);

	TOUCH_TRACE();

	if (ts->def_fwcnt) {
		memcpy(short_fwpath, ts->def_fwpath[1], sizeof(short_fwpath)); // 0 : normal bin, 1 : shorttest bin
		TOUCH_I("get short_fwpath from def_fwpath : %s\n", short_fwpath);
	} else {
		TOUCH_E("no firmware file def in dts\n");
		return -EPERM;
	}
	short_fwpath[sizeof(short_fwpath) - 1] = '\0';

	if (short_fwpath == NULL) {
		TOUCH_E("error get short_fwpath\n");
		return -EPERM;
	}

	ret = request_firmware(&g_short_fw.fw, short_fwpath, &client->dev);
	if (ret) {
		TOUCH_E("Failed get shorttest firmware:%d\n", ret);
		goto error;
	}

	g_short_fw.fw_data = g_short_fw.fw->data;
	g_short_fw.fw_total_len = g_short_fw.fw->size;

	return 0;

error:
	release_firmware(g_short_fw.fw);
	return -EPERM;
}

static s32 gt917d_short_test(struct i2c_client *client)
{
	s32 ret = 0;
	s32 ret2 = 0;
	u8 i = 0;
	u8 opr_buf[60] = {0};
	u8 retry = 0;
	u8 drv_sen_chksum = 0;
	u8 retry_load = 0;

	TOUCH_TRACE();

	TOUCH_I("---gt917d short test---\n");
	TOUCH_I("Step 1: reset guitar, hang up ss51 dsp\n");
	/*get short bin file*/
	ret = gt917d_get_short_file(client);
	if (ret < 0) {
		TOUCH_E("failed to get short file\n");
		return ret;
	}

	ret = gt917d_short_parse_cfg(client);
	if (ret == FAIL) {
		TOUCH_E("You May check your IIC connection.\n");
		goto short_test_exit;
	}

load_dsp_again:
	gt917d_hw_reset(&client->dev);

	while (retry++ < 200) {
		// Hold ss51 & dsp
		ret = gt917d_write_register(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
		if (ret <= 0) {
			TOUCH_D(JITTER, "Hold ss51 & dsp I2C error,retry:%d\n", retry);
			gt917d_hw_reset(&client->dev);
			continue;
		}
		TOUCH_D(JITTER, "Hold ss51 & dsp confirm 0x4180 failed,value:%d\n", opr_buf[GT917D_ADDR_LENGTH]);
		touch_msleep(2);

		//confirm hold
		opr_buf[GT917D_ADDR_LENGTH] = 0x00;

		ret = gt917d_read_register(client, _rRW_MISCTL__SWRST_B0_, opr_buf);
		if (ret < 0) {
			TOUCH_D(JITTER, "Hold ss51 & dsp I2C error,retry:%d\n", retry);
			gt917d_hw_reset(&client->dev);
			continue;
		}
		if (opr_buf[GT917D_ADDR_LENGTH] == 0x0C) {
			TOUCH_D(JITTER, "Hold ss51 & dsp confirm SUCCESS\n");
			break;
		}
	}

	if (retry >= 200) {
		TOUCH_E("Enter update Hold ss51 failed.\n");
		goto short_test_exit;
	}
	/* DSP_CK and DSP_ALU_CK PowerOn */
	ret2 = gt917d_write_register(client, 0x4010, 0x00);
	if (ret2 <= 0) {
		TOUCH_E("Enter update PowerOn DSP failed.\n");
		goto short_test_exit;
	}

	// step2: burn dsp_short code
	TOUCH_I("step 2: burn dsp_short code\n");
	gt917d_write_register(client, _bRW_MISCTL__TMR0_EN, 0x00); // clear watchdog
	gt917d_write_register(client, _bRW_MISCTL__CACHE_EN, 0x00); // clear cache
	gt917d_write_register(client, _rRW_MISCTL__BOOTCTL_B0_, 0x02); // boot from sram

	gt917d_write_register(client, _bRW_MISCTL__SRAM_BANK, 0x00); // select bank 0
	gt917d_write_register(client, _bRW_MISCTL__MEM_CD_EN, 0x01); // allow AHB bus accessing code sram

	// ---: burn dsp_short code
	ret = gt917d_burn_dsp_short(client);

	if (ret != SUCCESS) {
		if (retry_load++ < 5) {
			TOUCH_E("Load dsp failed,times %d retry load!\n", retry_load);
			goto load_dsp_again;
		} else {
			TOUCH_I("Step 2: burn dsp_short code\n");
			TOUCH_E("burn dsp_short Timeout!\n");
			goto short_test_exit;
		}
	}

	TOUCH_I("Step 2: burn dsp_short code\n");
	// step3: run dsp_short, read results
	TOUCH_I("Step 3: run dsp_short code, confirm it's running\n");
	gt917d_write_register(client, _rRW_MISCTL__SHORT_BOOT_FLAG, 0x00);	// clear dsp_short running flag
	gt917d_write_register(client, _rRW_MISCTL__BOOT_OPT_B0_, 0x03);	//set scramble

	gt917d_write_register(client, _bWO_MISCTL__CPU_SWRST_PULSE, 0x01);	// reset software

	gt917d_write_register(client, _rRW_MISCTL__SWRST_B0_, 0x08);	// release dsp

	touch_msleep(50);
	//confirm dsp is running
	i = 0;
	while (1) {
		opr_buf[2] = 0x00;
		gt917d_read_register(client, _rRW_MISCTL__SHORT_BOOT_FLAG, opr_buf);
		if (opr_buf[2] == 0xAA)
			break;
		++i;
		if (i >= 20) {
			TOUCH_E("step 3: dsp is not running!\n");
			goto short_test_exit;
		}
		touch_msleep(10);
	}
	// step4: host configure ic, get test result

	TOUCH_I("Step 4: host config ic, get test result\n");
	// Short Threshold
	TOUCH_D(JITTER, " Short Threshold: 10\n");
	opr_buf[0] = 0x88;
	opr_buf[1] = 0x04;
	opr_buf[2] = 0;
	opr_buf[3] = 10;
	gt917d_i2c_write(client, opr_buf, 4);

	// ADC Read Delay
	TOUCH_D(JITTER, " ADC Read Delay: 150\n");
	opr_buf[0] = 0x88;
	opr_buf[1] = 0x06;
	opr_buf[2] = (u8)(150 >> 8);
	opr_buf[3] = (u8)(150);
	gt917d_i2c_write(client, opr_buf, 4);

	// DiffCode Short Threshold
	TOUCH_D(JITTER, " DiffCode Short Threshold: 20\n");
	opr_buf[0] = 0x88;
	opr_buf[1] = 0x51;
	opr_buf[2] = (u8)(20 >> 8);
	opr_buf[3] = (u8)(20);
	gt917d_i2c_write(client, opr_buf, 4);

	// Config Driver & Sensor Order
	TOUCH_D(JITTER, "<<-GT917D-DEBUG->> Driver Map:\n");
	TOUCH_D(JITTER, "IC Driver:\n");
	for (i = 0; i < MAX_DRIVER_NUM; ++i)
		TOUCH_D(JITTER, " %2d", cfg_drv_order[i]);
	TOUCH_D(JITTER, "\n");
	TOUCH_D(JITTER, "TP Driver:\n");
	for (i = 0; i < MAX_DRIVER_NUM; ++i)
		TOUCH_D(JITTER, " %2d", i);
	TOUCH_D(JITTER, "\n");

	TOUCH_D(JITTER, "<<-GT917D-DEBUG->> Sensor Map:\n");
	TOUCH_D(JITTER, "IC Sensor:\n");
	for (i = 0; i < MAX_SENSOR_NUM; ++i)
		TOUCH_D(JITTER, " %2d", cfg_sen_order[i]);
	TOUCH_D(JITTER, "\n");
	TOUCH_D(JITTER, "TP Sensor:\n");
	for (i = 0; i < MAX_SENSOR_NUM; ++i)
		TOUCH_D(JITTER, " %2d", i);
	TOUCH_D(JITTER, "\n");

	opr_buf[0] = 0x88;
	opr_buf[1] = 0x08;
	for (i = 0; i < MAX_DRIVER_NUM; ++i) {
		opr_buf[2 + i] = cfg_drv_order[i];
		drv_sen_chksum += cfg_drv_order[i];
	}
	gt917d_i2c_write(client, opr_buf, MAX_DRIVER_NUM + 2);

	opr_buf[0] = 0x88;
	opr_buf[1] = 0x32;
	for (i = 0; i < MAX_SENSOR_NUM; ++i) {
		opr_buf[2 + i] = cfg_sen_order[i];
		drv_sen_chksum += cfg_sen_order[i];
	}
	gt917d_i2c_write(client, opr_buf, MAX_SENSOR_NUM + 2);

	opr_buf[0] = 0x88;
	opr_buf[1] = 0x50;
	opr_buf[2] = 0 - drv_sen_chksum;
	gt917d_i2c_write(client, opr_buf, 2 + 1);

	// clear waiting flag, run dsp
	gt917d_write_register(client, _rRW_MISCTL__SHORT_BOOT_FLAG, 0x04);

	// inquirying test status until it's okay
	for (i = 0; ; ++i) {
		gt917d_read_register(client, 0x8800, opr_buf);
		if (opr_buf[2] == 0x88)
			break;

		touch_msleep(50);
		if (i > 100) {
			TOUCH_E("step 4: inquiry test status timeout!\n");
			goto short_test_exit;
		}
	}

	// step 5: compute the result
	/* short flag:
	bit0: Rx & Rx
	bit1: Tx & Tx
	bit2: Tx & Rx
	bit3: Tx/Rx & GND/VDD
	 */
	gt917d_read_register(client, 0x8801, opr_buf);
	TOUCH_D(JITTER, "short_flag = 0x%02X\n", opr_buf[2]);

	TOUCH_I("\n");
	TOUCH_I("Short Test Result:\n");

	TOUCH_I("ctptest_TP-short---gt917d_resistor_threshold = %d\n", gt917d_resistor_threshold);
	TOUCH_I("ctptest_TP-shrlt---gt917d_gnd_resistor_threshold = %d\n", gt917d_gnd_resistor_threshold);

	if ((opr_buf[2] & 0x0f) == 0) {
		TOUCH_I("PASS!\n");
		ret = SUCCESS;
	} else {
		ret2 = SUCCESS;
		if ((opr_buf[2] & 0x08) == 0x08)
			ret2 = gt917d_test_gnd_vdd_short(client);

		ret = gt917d_compute_rslt(client);
		if (ret == SUCCESS && ret2 == SUCCESS) {
			TOUCH_I("PASS!\n");
			ret = SUCCESS;
		} else {
			ret = FAIL;
		}
	}
	// boot from rom and download code from flash to ram
	gt917d_write_register(client, _rRW_MISCTL__BOOT_CTL_, 0x99);
	gt917d_write_register(client, _rRW_MISCTL__BOOTCTL_B0_, 0x08);

	gt917d_leave_short_test(client);

	if (g_short_fw.fw != NULL) {
		g_short_fw.fw_data = NULL;
		g_short_fw.fw_total_len = 0;
		release_firmware(g_short_fw.fw);
	}

	return ret;

short_test_exit:
	if (g_short_fw.fw != NULL) {
		g_short_fw.fw_data = NULL;
		g_short_fw.fw_total_len = 0;
		release_firmware(g_short_fw.fw);
	}
	// boot from rom and download code from flash to ram
	gt917d_write_register(client, _rRW_MISCTL__BOOT_CTL_, 0x99);
	gt917d_write_register(client, _rRW_MISCTL__BOOTCTL_B0_, 0x08);

	gt917d_leave_short_test(client);

	return FAIL;
}

static u32 endian_mode(void)
{
	union {
		s32 i;
		s8 c;
	} endian;

	TOUCH_TRACE();

	endian.i = 1;

	if (endian.c == 1)
		return MYBIG_ENDIAN;
	else
		return MYLITLE_ENDIAN;
}

static s32 gt917d_read_raw_cmd(struct i2c_client *client)
{
	u8 raw_cmd[3] = {(u8)(GT917D_REG_READ_RAW >> 8), (u8)GT917D_REG_READ_RAW, 0x01};
	s32 ret = -1;

	TOUCH_TRACE();

	TOUCH_D(JITTER, "Send read raw data command\n");
	ret = gt917d_i2c_write(client, raw_cmd, 3);
	if (ret < 0) {
		TOUCH_E("i2c write failed.\n");
		return FAIL;
	}
	touch_msleep(10);

	return SUCCESS;
}

static s32 gt917d_read_coor_cmd(struct i2c_client *client)
{
	u8 raw_cmd[3] = {(u8)(GT917D_REG_READ_RAW >> 8), (u8)GT917D_REG_READ_RAW, 0x0};
	s32 ret = -1;

	TOUCH_TRACE();

	ret = gt917d_i2c_write(client, raw_cmd, 3);
	if (ret < 0) {
		TOUCH_E("i2c write coor cmd failed!\n");
		return FAIL;
	}
	touch_msleep(10);

	return SUCCESS;
}

static s32 gt917d_read_rawdata(struct i2c_client *client, u16 *data)
{
	s32 ret = -1;
	u16 retry = 0;
	u8 read_state[3] = {(u8)(GT917D_REG_RAW_READY >> 8), (u8)GT917D_REG_RAW_READY, 0};
	u16 i = 0;
	u8 *read_rawbuf = NULL;
	u8 tail = 0;
	u8 head = 0;

	TOUCH_TRACE();

	read_rawbuf = kzalloc(sizeof(u8) * (gt917d_drv_num * gt917d_sen_num * 2 + GT917D_ADDR_LENGTH), GFP_KERNEL);

	if (read_rawbuf == NULL) {
		TOUCH_E("failed to allocate for read_rawbuf\n");
		return FAIL;
	}

	if (data == NULL) {
		TOUCH_E("Invalid raw buffer.\n");
		goto have_error;
	}

	touch_msleep(10);
	while (retry++ < GT917D_WAIT_RAW_MAX_TIMES) {
		ret = gt917d_i2c_read(client, read_state, 3);
		if (ret < 0) {
			TOUCH_E("i2c read failed.return: %d\n", ret);
			continue;
		}
		if ((read_state[GT917D_ADDR_LENGTH] & 0x80) == 0x80) {
			TOUCH_D(JITTER, "Raw data is ready.\n");
			break;
		}
		if ((retry % 20) == 0) {
			TOUCH_D(JITTER, "(%d)read_state[2] = 0x%02X\n", retry, read_state[GT917D_ADDR_LENGTH]);
			if (retry == 100)
				gt917d_read_raw_cmd(client);
		}
		touch_msleep(5);
	}
	if (retry >= GT917D_WAIT_RAW_MAX_TIMES) {
		TOUCH_E("Wait raw data ready timeout.\n");
		goto have_error;
	}

	read_rawbuf[0] = (u8)(GT917D_REG_RAW_DATA >> 8);
	read_rawbuf[1] = (u8)(GT917D_REG_RAW_DATA);

	ret = gt917d_i2c_read(client, read_rawbuf, GT917D_ADDR_LENGTH + ((gt917d_drv_num * gt917d_sen_num) * 2));
	if (ret < 0) {
		TOUCH_E("i2c read rawdata failed.\n");
		goto have_error;
	}
	gt917d_i2c_end_cmd(client);	// clear buffer state

	if (endian_mode() == MYBIG_ENDIAN) {
		head = 0;
		tail = 1;
		TOUCH_D(JITTER, "Big Endian.\n");
	} else {
		head = 1;
		tail = 0;
		TOUCH_D(JITTER, "Little Endian.\n");
	}
	TOUCH_D(JITTER, "raw addr:%d, %d\n", read_rawbuf[0], read_rawbuf[1]);
	for (i = 0; i < ((gt917d_drv_num * gt917d_sen_num) * 2); i += 2) {
		data[i / 2] = (u16)(read_rawbuf[i + head + GT917D_ADDR_LENGTH] << 8)
			+ (u16)read_rawbuf[GT917D_ADDR_LENGTH + i + tail];
	}

	kfree(read_rawbuf);
	return SUCCESS;
have_error:
	kfree(read_rawbuf);
	return FAIL;
}

static s32 gt917d_read_refdata(struct i2c_client *client, u16 *data)
{
	s32 ret = -1;
	u16 retry = 0;
	u8 read_state[3] = {(u8)(GT917D_REG_RAW_READY >> 8), (u8)GT917D_REG_RAW_READY, 0};
	u16 i = 0;
	u8 *read_rawbuf = NULL;
	u8 tail = 0;
	u8 head = 0;

	TOUCH_TRACE();

	read_rawbuf = kzalloc(sizeof(u8) * (gt917d_drv_num * gt917d_sen_num * 2 + GT917D_ADDR_LENGTH), GFP_KERNEL);

	if (read_rawbuf == NULL) {
		TOUCH_E("failed to allocate for read_rawbuf\n");
		return FAIL;
	}

	if (data == NULL) {
		TOUCH_E("Invalid raw buffer.\n");
		goto have_error;
	}

	touch_msleep(10);
	while (retry++ < GT917D_WAIT_RAW_MAX_TIMES) {
		ret = gt917d_i2c_read(client, read_state, 3);
		if (ret < 0) {
			TOUCH_E("i2c read failed.return: %d\n", ret);
			continue;
		}
		if ((read_state[GT917D_ADDR_LENGTH] & 0x80) == 0x80) {
			TOUCH_D(JITTER, "Raw data is ready.\n");
			break;
		}
		if ((retry % 20) == 0) {
			TOUCH_D(JITTER, "(%d)read_state[2] = 0x%02X\n", retry, read_state[GT917D_ADDR_LENGTH]);
			if (retry == 100)
				gt917d_read_raw_cmd(client);
		}
		touch_msleep(5);
	}
	if (retry >= GT917D_WAIT_RAW_MAX_TIMES) {
		TOUCH_E("Wait raw data ready timeout.\n");
		goto have_error;
	}

	read_rawbuf[0] = (u8)(GT917D_REG_REF_DATA >> 8);
	read_rawbuf[1] = (u8)(GT917D_REG_REF_DATA);

	ret = gt917d_i2c_read(client, read_rawbuf, GT917D_ADDR_LENGTH + ((gt917d_drv_num * gt917d_sen_num) * 2));
	if (ret < 0) {
		TOUCH_E("i2c read rawdata failed.\n");
		goto have_error;
	}
	gt917d_i2c_end_cmd(client);	// clear buffer state

	if (endian_mode() == MYBIG_ENDIAN) {
		head = 0;
		tail = 1;
		TOUCH_D(JITTER, "Big Endian.\n");
	} else {
		head = 1;
		tail = 0;
		TOUCH_D(JITTER, "Little Endian.\n");
	}
	TOUCH_D(JITTER, "raw addr:%d, %d\n", read_rawbuf[0], read_rawbuf[1]);
	for (i = 0; i < ((gt917d_drv_num * gt917d_sen_num) * 2); i += 2) {
		data[i / 2] = (u16)(read_rawbuf[i + head + GT917D_ADDR_LENGTH] << 8)
			+ (u16)read_rawbuf[GT917D_ADDR_LENGTH + i + tail];
	}

	kfree(read_rawbuf);
	return SUCCESS;
have_error:
	kfree(read_rawbuf);
	return FAIL;
}

static s32 gt917d_raw_test_init(void)
{
	u16 i = 0;

	TOUCH_TRACE();

	touchpad_sum = kzalloc(sizeof(struct gt917d_open_info) * (4 * _BEYOND_INFO_MAX + 1), GFP_KERNEL);

	if (touchpad_sum == NULL) {
		TOUCH_E("touchpad_sum failed\n");
		return FAIL;
	}
	memset(touchpad_sum, 0, sizeof(struct gt917d_open_info) * (4 * _BEYOND_INFO_MAX + 1));

	for (i = 0; i < (4 * _BEYOND_INFO_MAX); ++i)
		touchpad_sum[i].driver = 0xFF;

	return SUCCESS;
}

static s32 gt917d_raw_max_test_re(u16 *raw_buf)
{
	u16 i = 0;
	u16 j = 0;
	u16 gt917d_pixel_cnt = MAX_ROW * MAX_COL;
	u8 driver = 0;
	u8 sensor = 0;
	u8 sum_base = 0 * _BEYOND_INFO_MAX;
	u8 new_flag = 0;
	s32 ret = SUCCESS;

	TOUCH_TRACE();

	for (i = 0; i < gt917d_pixel_cnt; i++) {
		if (raw_buf[i] > max_limit_vale_id0[i]) {	//max_limit_value)
			driver = (i / gt917d_sen_num);
			sensor = (i % gt917d_sen_num);
			new_flag = 0;
			for (j = sum_base; j < (sum_base + _BEYOND_INFO_MAX); ++j) {
				if (touchpad_sum[j].driver == 0xFF) {
					new_flag = 1;
					break;
				}
				if ((driver == touchpad_sum[j].driver) && (sensor == touchpad_sum[j].sensor)) {
					touchpad_sum[j].times++;
					new_flag = 0;
					break;
				}
			}
			if (new_flag) {	// new one
				touchpad_sum[j].driver = driver;
				touchpad_sum[j].sensor = sensor;
				touchpad_sum[j].beyond_type |= _BEYOND_MAX_LIMIT;
				touchpad_sum[j].raw_val = raw_buf[i];
				touchpad_sum[j].times = 1;
				TOUCH_D(JITTER, "[%d, %d]rawdata: %d, raw max limit: %d\n",
						driver, sensor, raw_buf[i], max_limit_vale_id0[i]);
			}
			return FAIL;
		}
	}
	return ret;
}

static s32 gt917d_raw_min_test_re(u16 *raw_buf)
{
	u16 i = 0;
	u16 j = 0;
	u16 gt917d_pixel_cnt = MAX_ROW * MAX_COL;
	u8 driver = 0;
	u8 sensor = 0;
	u8 sum_base = 1 * _BEYOND_INFO_MAX;
	u8 new_flag = 0;
	s32 ret = SUCCESS;

	TOUCH_TRACE();

	for (i = 0; i < gt917d_pixel_cnt; i++) {
		if (raw_buf[i] < min_limit_vale_id0[i]) {	//min_limit_value)
			driver = (i / gt917d_sen_num);
			sensor = (i % gt917d_sen_num);
			new_flag = 0;
			for (j = sum_base; j < (sum_base + _BEYOND_INFO_MAX); ++j) {
				if (touchpad_sum[j].driver == 0xFF) {
					new_flag = 1;
					break;
				}
				if ((driver == touchpad_sum[j].driver) && (sensor == touchpad_sum[j].sensor)) {
					touchpad_sum[j].times++;
					new_flag = 0;
					break;
				}
			}
			if (new_flag) {	// new one
				touchpad_sum[j].driver = driver;
				touchpad_sum[j].sensor = sensor;
				touchpad_sum[j].beyond_type |= _BEYOND_MIN_LIMIT;
				touchpad_sum[j].raw_val = raw_buf[i];
				touchpad_sum[j].times = 1;
				TOUCH_D(JITTER, "[%d, %d]rawdata: %d, raw min limit: %d\n",
						driver, sensor, raw_buf[i], min_limit_vale_id0[i]);
			}
			ret = FAIL;
		}
	}
	return ret;
}

static unsigned char AreaAccordCheck(u16 *raw_buf)
{
	int i = 0;
	int j = 0;
	int index = 0;
	u16 temp = 0;
	u16 accord_temp = 0;
	s32 ret = SUCCESS;

	TOUCH_TRACE();

	for (i = 0; i < gt917d_sen_num; i++) {
		for (j = 0; j < gt917d_drv_num; j++) {
			index = i + j * gt917d_sen_num;

			accord_temp = 0;
			temp = 0;

			if (j == 0) {
				if (raw_buf[i + (j + 1) * gt917d_sen_num] > raw_buf[index])
					accord_temp = ((1000 * (raw_buf[i + (j + 1) * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					accord_temp = ((1000 * (raw_buf[index] - raw_buf[i + (j + 1) * gt917d_sen_num])) / raw_buf[index]);
			} else if (j == gt917d_drv_num - 1) {
				if (raw_buf[i + (j - 1) * gt917d_sen_num] > raw_buf[index])
					accord_temp = ((1000 * (raw_buf[i + (j - 1) * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					accord_temp = ((1000 * (raw_buf[index] - raw_buf[i + (j - 1) * gt917d_sen_num])) / raw_buf[index]);
			} else {
				if (raw_buf[i + (j + 1) * gt917d_sen_num] > raw_buf[index])
					accord_temp = ((1000 * (raw_buf[i + (j + 1) * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					accord_temp = ((1000 * (raw_buf[index] - raw_buf[i + (j + 1) * gt917d_sen_num])) / raw_buf[index]);
				if (raw_buf[i + (j - 1) * gt917d_sen_num] > raw_buf[index])
					temp = ((1000 * (raw_buf[i + (j - 1) * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					temp = ((1000 * (raw_buf[index] - raw_buf[i + (j - 1) * gt917d_sen_num])) / raw_buf[index]);

				if (temp > accord_temp)
					accord_temp = temp;
			}

			if (i == 0) {
				if (raw_buf[i + 1 + j * gt917d_sen_num] > raw_buf[index])
					temp = ((1000 * (raw_buf[i + 1 + j * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					temp = ((1000 * (raw_buf[index] - raw_buf[i + 1 + j * gt917d_sen_num])) / raw_buf[index]);

				if (temp > accord_temp)
					accord_temp = temp;
			} else if (i == gt917d_sen_num - 1) {
				if (raw_buf[i - 1 + j * gt917d_sen_num] > raw_buf[index])
					temp = ((1000 * (raw_buf[i - 1 + j * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					temp = ((1000 * (raw_buf[index] - raw_buf[i - 1 + j * gt917d_sen_num])) / raw_buf[index]);

				if (temp > accord_temp)
					accord_temp = temp;
			} else {
				if (raw_buf[i + 1 + j * gt917d_sen_num] > raw_buf[index])
					temp = ((1000 * (raw_buf[i + 1 + j * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					temp = ((1000 * (raw_buf[index] - raw_buf[i + 1 + j * gt917d_sen_num])) / raw_buf[index]);

				if (temp > accord_temp)
					accord_temp = temp;

				if (raw_buf[i - 1 +  j * gt917d_sen_num] > raw_buf[index])
					temp = ((1000 * (raw_buf[i - 1 + j * gt917d_sen_num] - raw_buf[index])) / raw_buf[index]);
				else
					temp = ((1000 * (raw_buf[index] - raw_buf[i - 1 + j * gt917d_sen_num])) / raw_buf[index]);

				if (temp > accord_temp)
					accord_temp = temp;
			}

			TOUCH_D(JITTER, "AreaAccordCheck----index==%d\n", index);
			if (accord_temp > accord_limit_vale_id0[index]) {
				TOUCH_E("AreaAccordCheck----gt917d_drv_num = %d, gt917d_sen_num = %d, index = %d, accord_temp = %u, accord_limit_vale_id0[index] = %u\n",
						gt917d_drv_num, gt917d_sen_num, index, accord_temp, accord_limit_vale_id0[index]);
				ret = FAIL;
			}
			accord_limit_temp[index] = accord_temp;
		}
	}
	return ret;
}

static u32 gt917d_raw_test_re(u16 *raw_buf, u32 check_types)
{
	s32 ret = 0;

	TOUCH_TRACE();

	if (raw_buf == NULL) {
		TOUCH_E("Invalid raw buffer pointer!\n");
		return FAIL;
	}

	if (check_types & _MAX_TEST) {
		TOUCH_I("accord max test\n");
		ret = gt917d_raw_max_test_re(raw_buf);
		TOUCH_I("accord---max ret = %d\n", ret);
		if (ret) {
			Ito_result_info[RAWDATA_MAXDATA_ID].testitem = RAWDATA_MAXDATA_ID;
			Ito_result_info[RAWDATA_MAXDATA_ID].result = 'P';
			TOUCH_I("gt917d_raw_test_re max test result = pass\n");
		} else {
			Ito_result_info[RAWDATA_MAXDATA_ID].testitem = RAWDATA_MAXDATA_ID;
			Ito_result_info[RAWDATA_MAXDATA_ID].result = 'F';
			test_error_code |= 0x01;
			TOUCH_I("gt917d_raw_test_re max test result = failed\n");
		}
	}

	if (check_types & _MIN_TEST) {
		TOUCH_I("accord min test\n");
		ret += gt917d_raw_min_test_re(raw_buf);//0x02:SUCCESS;0:FAIL
		TOUCH_I("accord---min ret = %d\n", ret);
		if (ret) {
			Ito_result_info[RAWDATA_MINDATA_ID].testitem = RAWDATA_MINDATA_ID;
			Ito_result_info[RAWDATA_MINDATA_ID].result = 'P';
			TOUCH_I("gt917d_raw_test_re min test result = pass\n");
		} else {
			Ito_result_info[RAWDATA_MINDATA_ID].testitem = RAWDATA_MINDATA_ID;
			Ito_result_info[RAWDATA_MINDATA_ID].result = 'F';
			test_error_code |= 0x02;
			TOUCH_I("gt917d_raw_test_re min test result = failed\n");
		}
	}

	if (ret != 2)
		ret = 0;

	if (check_types & _ACCORD_TEST) {		// accord
		TOUCH_I("accord check\n");
		ret += AreaAccordCheck(raw_buf);	// 0x04:SUCCESS;0:FAIL
		TOUCH_I("accord---accord ret = %d\n", ret);
		if (ret) {
			Ito_result_info[RAWDATA_ACCORD_ID].testitem = RAWDATA_ACCORD_ID;
			Ito_result_info[RAWDATA_ACCORD_ID].result = 'P';
			TOUCH_I("gt917d_raw_test_re check result = pass\n");
		} else {
			Ito_result_info[RAWDATA_ACCORD_ID].testitem = RAWDATA_ACCORD_ID;
			Ito_result_info[RAWDATA_ACCORD_ID].result = 'F';
			test_error_code |= 0x04;
			TOUCH_I("gt917d_raw_test_re check result = failed\n");
		}
	}
	TOUCH_I("gt917d_raw_test_re result ret = %d\n", ret);

	if (ret == 0x03) {
		TOUCH_I("PASS!\n");
		ret = SUCCESS;
	} else {
		ret = FAIL;
	}

	return ret;
}

static s32 gt917d_jitter_test(void)
{
	s32 result = SUCCESS;
	u16 over_count = 0;
	u16 i = 0;

	TOUCH_TRACE();

	for (i = 0; i < (gt917d_drv_num * gt917d_sen_num); i++) {
		if (jitter_limit_temp[i] > jitter_limit_vale_id0[i])
			over_count++;

		if (over_count > 0) {
			result = FAIL;
			break;
		}
	}

	if (result == SUCCESS) {
		Ito_result_info[JITTER_TEST_ID].testitem = JITTER_TEST_ID;
		Ito_result_info[JITTER_TEST_ID].result = 'P';
		TOUCH_D(JITTER, "gt917d_jitter_test test result = pass\n");
	} else {
		Ito_result_info[JITTER_TEST_ID].testitem = JITTER_TEST_ID;
		Ito_result_info[JITTER_TEST_ID].result = 'F';
		test_error_code |= 0x10;
		TOUCH_D(JITTER, "gt917d_jitter_test test result = failed\n");
	}

	return result;
}

static s32 gt917d_read_data(struct i2c_client *client, u16 *data, u16 address)
{
	s32 ret = -1;
	u16 retry = 0;
	u8 read_state[3] = {(u8)(GT917D_REG_RAW_READY >> 8), (u8)GT917D_REG_RAW_READY, 0};
	u16 i = 0;
	u16 j = 0;
	u8 *read_rawbuf = NULL;
	u8 tail = 0;
	u8 head = 0;

	TOUCH_TRACE();

	read_rawbuf = kzalloc(sizeof(u8) * (gt917d_drv_num * gt917d_sen_num * 2 + GT917D_ADDR_LENGTH), GFP_KERNEL);

	if (read_rawbuf == NULL) {
		TOUCH_E("failed to allocate for read_rawbuf\n");
		return FAIL;
	}

	if (data == NULL) {
		TOUCH_E("Invalid raw buffer.\n");
		goto have_error;
	}

	touch_msleep(10);
	while (retry++ < GT917D_WAIT_RAW_MAX_TIMES) {
		ret = gt917d_i2c_read(client, read_state, 3);
		if (ret < 0) {
			TOUCH_E("i2c read failed.return: %d\n", ret);
			continue;
		}
		if ((read_state[GT917D_ADDR_LENGTH] & 0x80) == 0x80) {
			TOUCH_D(JITTER, "Raw data is ready.\n");
			break;
		}
		if ((retry % 20) == 0) {
			TOUCH_D(JITTER, "(%d)read_state[2] = 0x%02X\n", retry, read_state[GT917D_ADDR_LENGTH]);
			if (retry == 100)
				gt917d_read_raw_cmd(client);
		}
		touch_msleep(5);
	}
	if (retry >= GT917D_WAIT_RAW_MAX_TIMES) {
		TOUCH_E("Wait raw data ready timeout.\n");
		goto have_error;
	}

	read_rawbuf[0] = (u8)(address >> 8);
	read_rawbuf[1] = (u8)(address);

	ret = gt917d_i2c_read(client, read_rawbuf, GT917D_ADDR_LENGTH + ((gt917d_drv_num * gt917d_sen_num) * 2));
	if (ret < 0) {
		TOUCH_E("i2c read rawdata failed.\n");
		goto have_error;
	}
	gt917d_i2c_end_cmd(client);	// clear buffer state

	if (endian_mode() == MYBIG_ENDIAN) {
		head = 0;
		tail = 1;
		TOUCH_D(JITTER, "Big Endian.\n");
	} else {
		head = 1;
		tail = 0;
		TOUCH_D(JITTER, "Little Endian.\n");
	}
	TOUCH_D(JITTER, "raw addr:%d, %d\n", read_rawbuf[0], read_rawbuf[1]);
	for (i = 0, j = 0; i < ((gt917d_drv_num * gt917d_sen_num) * 2); i += 2) {
		data[i / 2] = (u16)(read_rawbuf[i + head + GT917D_ADDR_LENGTH] << 8)
			+ (u16)read_rawbuf[GT917D_ADDR_LENGTH + i + tail];
	}

	kfree(read_rawbuf);
	return SUCCESS;
have_error:
	kfree(read_rawbuf);
	return FAIL;
}

static int rawdata_test(struct device *dev, int *test_result)
{
	s32 ret = FAIL;
	s32 bytes = 0;
	s32 i = 0;
	s32 j = 0;
	s32 index = 0;
	int max = 0;
	int min = 30000;
	u16 rawdata = 0;
	u16 *raw_buf = NULL;
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;

	TOUCH_TRACE();

	//for compat
	Ito_result_info = kzalloc(sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM, GFP_KERNEL);
	if (Ito_result_info == NULL) {
		TOUCH_I("%s : memory alloc failed\n", __func__);
		return -EFAULT;
	}
	memset(Ito_result_info, 0, sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM);

	raw_buf = kzalloc(sizeof(u16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (raw_buf == NULL) {
		TOUCH_E("failed to allocate mem for raw_buf_storage!\n");
		return -EFAULT;
	}

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);//delay 2s for esd close
	// modify for esd end-----------------------

	test_error_code = 0;//init error code
	// rawdata test start

	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Rawdata Cmd failed!\n");
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_I("Step 2: Sample Rawdata\n");
	//just sample one frame

	gt917d_read_rawdata(client, raw_buf);

	gt917d_raw_test_re(raw_buf, _MAX_TEST | _MIN_TEST);

	TOUCH_I("Step 3: Save result\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Rawdata Test Result ================\n");
	//save raw data result
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			index = i + j * gt917d_sen_num;
			rawdata = raw_buf[index];

			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, " %4d", rawdata);
			if ((rawdata < min_limit_vale_id0[index]) || (rawdata > max_limit_vale_id0[index]))
				bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "*");
			else
				bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, " ");

			if (rawdata > max)
				max = rawdata;
			if (rawdata < min)
				min = rawdata;
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"MAX = %d,  MIN = %d\n\n", max, min);

	if ((test_error_code & _BEYOND_MAX_LIMIT) || (test_error_code & _BEYOND_MIN_LIMIT))
		*test_result = TEST_FAIL;

open_test_exit:
	clear_bit(PANEL_ITO_TESTING, &d->flags);

	touch_interrupt_control(dev, INTERRUPT_ENABLE);

	kfree(Ito_result_info);

	gt917d_read_coor_cmd(client);

	gt917d_send_cfg(dev);
	touch_msleep(150);

	kfree(raw_buf);

	return bytes;
}

static int accord_test(struct device *dev, int *test_result)
{
	s32 ret = FAIL;
	s32 bytes = 0;
	s32 i = 0;
	s32 j = 0;
	s32 index = 0;
	u16 accord = 0;
	u16 *raw_buf = NULL;
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;

	TOUCH_TRACE();

	//for compat
	Ito_result_info = kzalloc(sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM, GFP_KERNEL);
	if (Ito_result_info == NULL) {
		TOUCH_I("%s : memory alloc failed\n", __func__);
		return -EFAULT;
	}
	memset(Ito_result_info, 0, sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM);

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);//delay 2s for esd close
	// modify for esd end-----------------------

	test_error_code = 0;//init error code
	// accord test start

	raw_buf = kzalloc(sizeof(u16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (raw_buf == NULL) {
		TOUCH_E("failed to allocate mem for raw_buf_storage!\n");
		ret = FAIL;
		goto open_test_exit;
	}

	TOUCH_I("Step 1: Send Rawdata Cmd\n");

	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Rawdata Cmd failed!\n");
		ret = FAIL;
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_I("Step 2: Sample Rawdata\n");
	ret = gt917d_read_rawdata(client, raw_buf);

	ret = gt917d_raw_test_re(raw_buf, _ACCORD_TEST);

	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Accord Test Result ================\n");
	//save accord data result
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"Channel_Accord :(%d)\n", FLOAT_AMPLIFIER);
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			index = i + j * gt917d_sen_num;
			accord = accord_limit_temp[index];

			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, " %d.%d%d%d",
					accord / 1000,
					(accord % 1000) / 100,
					(accord % 100) / 10,
					accord % 10);
			if (accord > accord_limit_vale_id0[index])
				bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "*");
			else
				bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, " ");
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");
	//save accord result
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "Area Accord:  ");
	if (test_error_code & _BEYOND_ACCORD_LIMIT) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "NG !\n\n");
		*test_result = TEST_FAIL;
	} else {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "pass\n\n");
	}
	// accord test end

open_test_exit:
	clear_bit(PANEL_ITO_TESTING, &d->flags);
	touch_interrupt_control(dev, INTERRUPT_ENABLE);

	kfree(Ito_result_info);

	gt917d_read_coor_cmd(client);

	gt917d_send_cfg(dev);
	touch_msleep(150);
	kfree(raw_buf);

	return bytes;
}

static int jitter_test(struct device *dev, int *test_result)
{
	s32 ret = 0;
	s32 bytes = 0;
	int current_data_index = 0;
	int i = 0;
	int j = 0;
	int index = 0;
	s16 max = 0;
	s16 jitter = 0;
	u16 *raw_buf_storage = NULL;
	s16 *jitter_compat = NULL;
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;

	TOUCH_TRACE();

	TOUCH_I("jitter test enter\n");
	memset(jitter_limit_temp, 0, sizeof(jitter_limit_temp));

	Ito_result_info = kzalloc(sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM, GFP_KERNEL);
	if (Ito_result_info == NULL) {
		TOUCH_E("failed to allocate mem for Ito_result_info!\n");
		ret = FAIL;
		goto open_test_exit;
	}
	memset(Ito_result_info, 0, sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM);

	jitter_compat = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (jitter_compat == NULL) {
		TOUCH_E("failed to allocate mem for jitter_compat!\n");
		ret = FAIL;
		goto open_test_exit;
	}

	raw_buf_storage = kzalloc(sizeof(u16) * (gt917d_drv_num * gt917d_sen_num * GT917D_OPEN_JITTER_SAMPLE_NUM), GFP_KERNEL);
	if (raw_buf_storage == NULL) {
		TOUCH_E("failed to allocate mem for raw_buf_storage!\n");
		ret = FAIL;
		goto open_test_exit;
	}

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);	// delay 2s for esd close
	// modify for esd end-----------------------

	test_error_code = 0;	// init error code

	// send jitter config
	TOUCH_I("Step 1: switch to jitter sample mode\n");
	gt917d_i2c_write(client, test_config_jitter, ARRAY_SIZE(test_cfg_info_group) + GT917D_ADDR_LENGTH);

	touch_msleep(150);
	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Rawdata Cmd failed!\n");
		ret = FAIL;
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_I("Step 2: Sample raw data\n");
	for (i = GT917D_OPEN_RAW_SAMPLE_NUM; i < GT917D_OPEN_ALL_SAMPLE_NUM; i++) {
		current_data_index = i;
		ret = gt917d_read_rawdata(client,
				&raw_buf_storage[gt917d_drv_num * gt917d_sen_num *
				(current_data_index - GT917D_OPEN_RAW_SAMPLE_NUM)]);
		if (ret == FAIL) {
			TOUCH_E("Read diff data error!\n");
			ret = FAIL;
			goto open_test_exit;
		}
	}

	for (i = GT917D_OPEN_RAW_SAMPLE_NUM; i < GT917D_OPEN_ALL_SAMPLE_NUM; i++) {
		current_data_index = i;
		TOUCH_D(JITTER, "NO.%d\n", current_data_index - GT917D_OPEN_RAW_SAMPLE_NUM);
	}

	// cal the jitter
	TOUCH_I("Step 3: Get the jitter\n");
	for (i = GT917D_OPEN_RAW_SAMPLE_NUM; i < GT917D_OPEN_ALL_SAMPLE_NUM - 1; i++) {
		current_data_index = i;
		for (j = 0; j < (gt917d_drv_num * gt917d_sen_num); j++) {
			jitter_compat[j] = raw_buf_storage[j + (i - GT917D_OPEN_RAW_SAMPLE_NUM + 1) * gt917d_drv_num * gt917d_sen_num]
				- raw_buf_storage[j + (i - GT917D_OPEN_RAW_SAMPLE_NUM) * gt917d_drv_num * gt917d_sen_num];
		}

		for (j = 0; j < (gt917d_drv_num * gt917d_sen_num); j++) {
			if (jitter_compat[j] < 0)
				jitter_compat[j] = 0 - jitter_compat[j];
			if (jitter_limit_temp[j] < jitter_compat[j])
				jitter_limit_temp[j] = jitter_compat[j];
		}

	}

	gt917d_jitter_test();

	//save jitter test
	TOUCH_I("Step 4: save jitter result\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Jitter Test Result ================\n");
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			index = i + j * gt917d_sen_num;
			jitter = jitter_limit_temp[index];

			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, " %3d", jitter);
			if (jitter > jitter_limit_vale_id0[index])
				bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "*");
			else
				bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, " ");

			if (jitter > max)
				max = jitter;
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}

	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "Max Jitter: %d\n", max);

	if (test_error_code & _BEYOND_JITTER_LIMIT) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "NG !\n\n");
		*test_result = TEST_FAIL;
		TOUCH_I("JITTER TEST FAIL!!\n");
	} else {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "PASS\n\n");
		TOUCH_I("JITTER TEST PASS!!\n");
	}

open_test_exit:
	clear_bit(PANEL_ITO_TESTING, &d->flags);
	touch_interrupt_control(dev, INTERRUPT_ENABLE);

	kfree(Ito_result_info);
	kfree(jitter_compat);
	kfree(raw_buf_storage);

	gt917d_read_coor_cmd(client);

	gt917d_send_cfg(dev);
	touch_msleep(150);
	return bytes;
}

static int short_test(struct device *dev, int *test_result)
{
	s32 ret = 0;
	s32 bytes = 0;
	struct gt917d_data *d = to_gt917d_data(dev);

	TOUCH_TRACE();

	//for compat
	Ito_result_info = kzalloc(sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM, GFP_KERNEL);
	if (Ito_result_info == NULL) {
		TOUCH_I("%s : memory alloc failed\n", __func__);
		return -EFAULT;
	}
	memset(Ito_result_info, 0, sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM);

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);//delay 2s for esd close
	// modify for esd end-----------------------

	test_error_code = 0;//init error code
	// short test
	ret = gt917d_short_test(d->client);
	if (ret) {
		Ito_result_info[SHORT_TEST_ID].testitem = SHORT_TEST_ID;
		Ito_result_info[SHORT_TEST_ID].result = 'P';
		TOUCH_I("gt917d_ito_test_show short test result = pass\n");
	} else {
		Ito_result_info[SHORT_TEST_ID].testitem = SHORT_TEST_ID;
		Ito_result_info[SHORT_TEST_ID].result = 'F';
		test_error_code |= _SENSOR_SHORT;
		TOUCH_I("gt917d_ito_test_show short test result = failed\n");
	}

	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"============= Short Test Result =============\n");
	if (test_error_code & _SENSOR_SHORT) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "NG !\n\n");
		*test_result = TEST_FAIL;
	} else {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "PASS\n\n");
	}

	clear_bit(PANEL_ITO_TESTING, &d->flags);

	touch_interrupt_control(dev, INTERRUPT_ENABLE);

	kfree(Ito_result_info);

	return bytes;
};

static ssize_t show_sd(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 bytes = 0;
	int rawstatus = 0;
	int short_status = 0;
	int jitter_status = 0;
	int accord_status = 0;
	int ret_total_size = 0;
	int ret = 0;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.fb) != FB_RESUME) {
		TOUCH_I("%s: state.fb is not FB_RESUME\n", __func__);
		return 0;
	}

	mutex_lock(&ts->lock);
	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	TOUCH_I("[Self-Diagnostic Test] Start\n");

	write_file(dev, "\n\n[Self-Diagnostic Test] Start", TIME_INFO_SKIP);
	write_file(dev, "\n", TIME_INFO_WRITE);

	memset(buffer, 0, PAGE_SIZE);
	bytes += short_test(dev, &short_status);
	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);

	gt917d_open_test_init(dev, client, _RAWDATA_TEST);
	ret = gt917d_raw_test_init();
	if (ret == FAIL) {
		TOUCH_E("Allocate memory for open test failed!\n");
		ret = FAIL;
		goto error;
	}
	memset(buffer, 0, PAGE_SIZE);
	bytes += rawdata_test(dev, &rawstatus);
	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);

	gt917d_open_test_init(dev, client, _ACCORD_TEST);
	memset(buffer, 0, PAGE_SIZE);
	bytes += accord_test(dev, &accord_status);
	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);

	gt917d_open_test_init(dev, client, _JITTER_TEST);
	memset(buffer, 0, PAGE_SIZE);
	bytes += jitter_test(dev, &jitter_status);
	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);

	memset(buffer, 0, PAGE_SIZE);
	bytes = snprintf(buffer, PAGE_SIZE, "\n========RESULT=======\n");

	/* Rawdata Check */
	if (rawstatus != TEST_PASS)	/* FAIL */
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "Raw Data : Fail\n");
	else
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "Raw Data : Pass\n");

	/* Channel Status Check */
	if (short_status != TEST_PASS || accord_status != TEST_PASS || jitter_status != TEST_PASS) {	/* FAIL */
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "Channel Status : Fail\n");
		if (short_status != TEST_PASS)
			TOUCH_I("Short_test fail\n");
		if (accord_status != TEST_PASS)
			TOUCH_I("accord_test fail\n");
		if (jitter_status != TEST_PASS)
			TOUCH_I("jitter_test fail\n");
	} else {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "Channel Status : Pass\n");
	}

	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);
	memcpy(buf + ret_total_size, buffer, bytes);
	ret_total_size += bytes;

error:
	kfree(touchpad_sum);
	touchpad_sum = NULL;

	TOUCH_I("[Self-Diagnostic Test] END\n");
	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	return ret_total_size;
}

static void gt917d_lpwg_sd_set(struct device *dev, bool value)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	int boot_mode = TOUCH_NORMAL_BOOT;

	TOUCH_TRACE();

	if (value) {
		TOUCH_I("%s: before lpwg_sd\n", __func__);

		TOUCH_I("%s: set lpwg -> active\n", __func__);
		touch_report_all_event(ts);
		gt917d_wakeup(dev);
		d->state = TC_STATE_ACTIVE;

		TOUCH_I("%s: set active -> lpwg\n", __func__);
		gt917d_lpwg_control(dev, LPWG_DOUBLE_TAP);
		d->state = TC_STATE_LPWG;
	} else {
		TOUCH_I("%s: after lpwg_sd\n", __func__);

		boot_mode = touch_check_boot_mode(dev);
		TOUCH_I("%s: boot_mode: %d\n", __func__, boot_mode);

		switch (boot_mode) {
		case TOUCH_MINIOS_AAT:
		case TOUCH_MINIOS_MFTS_FOLDER:
		case TOUCH_MINIOS_MFTS_FLAT:
		case TOUCH_MINIOS_MFTS_CURVED:
			TOUCH_I("%s: miniOS: set lpwg -> acvive\n", __func__);
			touch_report_all_event(ts);
			gt917d_wakeup(dev);
			d->state = TC_STATE_ACTIVE;
			break;
		case TOUCH_NORMAL_BOOT:
		case TOUCH_CHARGER_MODE:
		case TOUCH_LAF_MODE:
		case TOUCH_RECOVERY_MODE:
		default:
			TOUCH_I("%s: not miniOS: skip\n", __func__);
			break;
		}
	}
}

static ssize_t show_lpwg_sd(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 bytes = 0;
	int lpwg_rawstatus = 0;
	int accord_status = 0;
	int jitter_status = 0;
	int ret_total_size = 0;
	int ret = 0;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.fb) != FB_SUSPEND) {
		TOUCH_I("%s: state.fb is not FB_SUSPEND\n", __func__);
		return 0;
	}

	mutex_lock(&ts->lock);
	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	TOUCH_I("Start LPWG SD TEST!!\n");

	write_file(dev, "\n\n[LPWG Self-Diagnostic Test] Start", TIME_INFO_SKIP);
	write_file(dev, "\n", TIME_INFO_WRITE);

	gt917d_lpwg_sd_set(dev, true);

	gt917d_open_test_init(dev, client, _RAWDATA_TEST);
	ret = gt917d_raw_test_init();
	if (ret == FAIL) {
		TOUCH_E("Allocate memory for open test failed!\n");
		ret = FAIL;
		goto error;
	}
	memset(buffer, 0, PAGE_SIZE);
	bytes += rawdata_test(dev, &lpwg_rawstatus);
	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);

	gt917d_open_test_init(dev, client, _ACCORD_TEST);
	memset(buffer, 0, PAGE_SIZE);
	bytes += accord_test(dev, &accord_status);
	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);

	gt917d_open_test_init(dev, client, _JITTER_TEST);
	memset(buffer, 0, PAGE_SIZE);
	bytes += jitter_test(dev, &jitter_status);
	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);

	memset(buffer, 0, PAGE_SIZE);
	bytes = snprintf(buffer, PAGE_SIZE, "\n========RESULT=======\n");

	/* LPWG Rawdata Check */
	if (lpwg_rawstatus != TEST_PASS || accord_status != TEST_PASS || jitter_status != TEST_PASS) {	/* FAIL */
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "LPWG RawData : Fail\n");
		if (lpwg_rawstatus != TEST_PASS)
			TOUCH_I("lpwg_rawstatus FAIL!!\n");
		if (accord_status != TEST_PASS)
			TOUCH_I("accord_status FAIL!!\n");
		if (jitter_status != TEST_PASS)
			TOUCH_I("jitter_status FAIL!!\n");
	} else {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "LPWG RawData : Pass\n");
	}

	write_file(dev, buffer, TIME_INFO_SKIP);
	print_sd_log(buffer);
	memcpy(buf + ret_total_size, buffer, bytes);
	ret_total_size += bytes;

error:
	gt917d_lpwg_sd_set(dev, false);

	kfree(touchpad_sum);
	touchpad_sum = NULL;

	TOUCH_I("END LPWG SD TEST!!\n");
	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	return ret_total_size;
}

static ssize_t show_rawdata(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 bytes = 0;
	int test_result = 0;
	int ret = 0;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	mutex_lock(&ts->lock);

	gt917d_open_test_init(dev, client, _RAWDATA_TEST);

	ret = gt917d_raw_test_init();
	if (ret == FAIL) {
		TOUCH_E("Allocate memory for open test failed!\n");
		ret = FAIL;
		goto error;
	}

	bytes = rawdata_test(dev, &test_result);

	memcpy(buf, buffer, bytes);

error:
	mutex_unlock(&ts->lock);

	kfree(touchpad_sum);
	touchpad_sum = NULL;

	return bytes;
}

static ssize_t show_delta(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 ret = FAIL;
	s32 bytes = 0;
	s32 i = 0;
	s32 j = 0;
	s32 tmp = 0;
	s16 *diff_buf = NULL;
	s16 *raw_buf = NULL;
	s16 *ref_buf = NULL;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	mutex_lock(&ts->lock);

	diff_buf = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (diff_buf == NULL) {
		TOUCH_E("failed to allocate mem for diff_buf!\n");
		goto open_test_exit;
	}

	raw_buf = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (raw_buf == NULL) {
		TOUCH_E("failed to allocate mem for diff_buf!\n");
		goto open_test_exit;
	}

	ref_buf = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (ref_buf == NULL) {
		TOUCH_E("failed to allocate mem for diff_buf!\n");
		goto open_test_exit;
	}

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);//delay 2s for esd close
	// modify for esd end-----------------------

	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Diff Cmd failed!\n");
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_D(JITTER, "Step 2: Sample Diffdata\n");

	gt917d_read_rawdata(client, raw_buf);
	gt917d_read_refdata(client, ref_buf);

	for (i = 0; i < (gt917d_drv_num * gt917d_sen_num); i++)
		diff_buf[i] = ref_buf[i] - raw_buf[i];

	memset(buffer, 0, PAGE_SIZE);
	TOUCH_D(JITTER, "Step 3: Save result\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Delta Test Result ================\n");
	//save raw data result
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			tmp = diff_buf[i + j * gt917d_sen_num];
			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, " %3d ", tmp);
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");
	// rawdata test end

open_test_exit:
	gt917d_read_coor_cmd(client);

	clear_bit(PANEL_ITO_TESTING, &d->flags);

	kfree(diff_buf);
	kfree(raw_buf);
	kfree(ref_buf);

	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	memcpy(buf, buffer, bytes);
	mutex_unlock(&ts->lock);
	return bytes;
}

static ssize_t show_accord(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 bytes = 0;
	int test_result = 0;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	mutex_lock(&ts->lock);

	gt917d_open_test_init(dev, client, _ACCORD_TEST);

	bytes = accord_test(dev, &test_result);

	memcpy(buf, buffer, bytes);

	mutex_unlock(&ts->lock);

	return bytes;
}

static ssize_t show_jitter(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 bytes = 0;
	int test_result = 0;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	mutex_lock(&ts->lock);

	gt917d_open_test_init(dev, client, _JITTER_TEST);

	bytes = jitter_test(dev, &test_result);

	memcpy(buf, buffer, bytes);

	mutex_unlock(&ts->lock);

	return bytes;
}

static ssize_t show_short(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	s32 bytes = 0;
	int test_result = 0;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	mutex_lock(&ts->lock);

	bytes = short_test(dev, &test_result);

	memcpy(buf, buffer, bytes);

	mutex_unlock(&ts->lock);

	return bytes;
}

static ssize_t show_debug_data1(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 ret = FAIL;
	s32 bytes = 0;
	s32 i = 0;
	s32 j = 0;
	s32 tmp = 0;
	s16 *diff_buf = NULL;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	diff_buf = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (diff_buf == NULL) {
		TOUCH_E("failed to allocate mem for diff_buf!\n");
		return -EFAULT;
	}

	mutex_lock(&ts->lock);
	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);//delay 2s for esd close
	// modify for esd end-----------------------

	TOUCH_D(JITTER, "Step 1: Send Diff Cmd\n");
	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Diff Cmd failed!\n");
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_D(JITTER, "Step 2: Sample Diffdata\n");

	gt917d_read_data(client, diff_buf, 0xA160);

	memset(buffer, 0, PAGE_SIZE);
	TOUCH_D(JITTER, "Step 3: Save result\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Delta Test Result ================\n");
	//save raw data result
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			tmp = diff_buf[i + j * gt917d_sen_num];
			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "%3d ", tmp);
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");
	// rawdata test end

open_test_exit:
	gt917d_read_coor_cmd(client);

	clear_bit(PANEL_ITO_TESTING, &d->flags);

	kfree(diff_buf);

	memcpy(buf, buffer, bytes);

	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	return bytes;
}

static ssize_t show_debug_data2(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 ret = FAIL;
	s32 bytes = 0;
	s32 i = 0;
	s32 j = 0;
	s32 tmp = 0;
	s16 *diff_buf = NULL;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	diff_buf = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (diff_buf == NULL) {
		TOUCH_E("failed to allocate mem for diff_buf!\n");
		return -EFAULT;
	}

	mutex_lock(&ts->lock);
	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);//delay 2s for esd close
	// modify for esd end-----------------------

	TOUCH_D(JITTER, "Step 1: Send Diff Cmd\n");
	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Diff Cmd failed!\n");
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_D(JITTER, "Step 2: Sample Diffdata\n");

	gt917d_read_data(client, diff_buf, 0x86C0);

	memset(buffer, 0, PAGE_SIZE);
	TOUCH_D(JITTER, "Step 3: Save result\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Delta Test Result ================\n");
	//save raw data result
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			tmp = diff_buf[i + j * gt917d_sen_num];
			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "%3d ", tmp);
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");
	// rawdata test end

open_test_exit:
	gt917d_read_coor_cmd(client);

	clear_bit(PANEL_ITO_TESTING, &d->flags);

	kfree(diff_buf);

	memcpy(buf, buffer, bytes);

	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	return bytes;
}

static ssize_t show_debug_data3(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;
	s32 ret = FAIL;
	s32 bytes = 0;
	s32 i = 0;
	s32 j = 0;
	s32 tmp = 0;
	s16 *diff_buf = NULL;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	diff_buf = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (diff_buf == NULL) {
		TOUCH_E("failed to allocate mem for diff_buf!\n");
		return -EFAULT;
	}

	mutex_lock(&ts->lock);
	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);//delay 2s for esd close
	// modify for esd end-----------------------

	TOUCH_D(JITTER, "Step 1: Send Diff Cmd\n");
	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Diff Cmd failed!\n");
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_D(JITTER, "Step 2: Sample Diffdata\n");

	gt917d_read_data(client, diff_buf, 0xB3FC);

	memset(buffer, 0, PAGE_SIZE);
	TOUCH_D(JITTER, "Step 3: Save result\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Delta Test Result ================\n");
	//save raw data result
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			tmp = diff_buf[i + j * gt917d_sen_num];
			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "%3d ", tmp);
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");
	// rawdata test end

open_test_exit:
	gt917d_read_coor_cmd(client);

	clear_bit(PANEL_ITO_TESTING, &d->flags);

	kfree(diff_buf);

	memcpy(buf, buffer, bytes);

	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	return bytes;
}

static int jitter_test_debug(struct device *dev, int *test_result)
{
	s32 ret = 0;
	s32 bytes = 0;
	int current_data_index = 0;
	int i = 0;
	int j = 0;
	u16 *raw_buf_storage = NULL;
	s16 *jitter_compat = NULL;
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = d->client;

	TOUCH_TRACE();

	TOUCH_I("jitter test enter\n");
	memset(jitter_limit_temp, 0, sizeof(jitter_limit_temp));

	Ito_result_info = kzalloc(sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM, GFP_KERNEL);
	if (Ito_result_info == NULL) {
		TOUCH_E("failed to allocate mem for Ito_result_info!\n");
		ret = FAIL;
		goto open_test_exit;
	}
	memset(Ito_result_info, 0, sizeof(struct gt917d_iot_result_info) * ITO_TEST_ITEM_NUM);

	jitter_compat = kzalloc(sizeof(s16) * (gt917d_drv_num * gt917d_sen_num), GFP_KERNEL);
	if (jitter_compat == NULL) {
		TOUCH_E("failed to allocate mem for jitter_compat!\n");
		ret = FAIL;
		goto open_test_exit;
	}

	raw_buf_storage = kzalloc(sizeof(u16) * (gt917d_drv_num * gt917d_sen_num * GT917D_OPEN_JITTER_SAMPLE_NUM), GFP_KERNEL);
	if (raw_buf_storage == NULL) {
		TOUCH_E("failed to allocate mem for raw_buf_storage!\n");
		ret = FAIL;
		goto open_test_exit;
	}

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	set_bit(PANEL_ITO_TESTING, &d->flags);

	touch_msleep(20);	// delay 2s for esd close
	// modify for esd end-----------------------

	test_error_code = 0;	// init error code

	// send jitter config
	TOUCH_I("Step 1: switch to jitter sample mode\n");
	// gt917d_i2c_write(client, test_config_jitter, ARRAY_SIZE(test_cfg_info_group) + GT917D_ADDR_LENGTH);

	touch_msleep(150);
	ret = gt917d_read_raw_cmd(client);
	if (ret == FAIL) {
		TOUCH_E("Send Read Rawdata Cmd failed!\n");
		ret = FAIL;
		goto open_test_exit;
	}
	touch_msleep(20);

	TOUCH_I("Step 2: Sample raw data\n");
	for (i = GT917D_OPEN_RAW_SAMPLE_NUM; i < GT917D_OPEN_ALL_SAMPLE_NUM; i++) {
		current_data_index = i;
		ret = gt917d_read_rawdata(client,
				&raw_buf_storage[gt917d_drv_num * gt917d_sen_num *
				(current_data_index - GT917D_OPEN_RAW_SAMPLE_NUM)]);
		if (ret == FAIL) {
			TOUCH_E("Read diff data error!\n");
			ret = FAIL;
			goto open_test_exit;
		}
	}

	for (i = GT917D_OPEN_RAW_SAMPLE_NUM; i < GT917D_OPEN_ALL_SAMPLE_NUM; i++) {
		current_data_index = i;
		TOUCH_D(JITTER, "NO.%d\n", current_data_index - GT917D_OPEN_RAW_SAMPLE_NUM);
	}

	// cal the jitter
	TOUCH_I("Step 3: Get the jitter\n");
	for (i = GT917D_OPEN_RAW_SAMPLE_NUM; i < GT917D_OPEN_ALL_SAMPLE_NUM - 1; i++) {
		current_data_index = i;
		for (j = 0; j < (gt917d_drv_num * gt917d_sen_num); j++) {
			jitter_compat[j] = raw_buf_storage[j + (i - GT917D_OPEN_RAW_SAMPLE_NUM + 1) * gt917d_drv_num * gt917d_sen_num]
				- raw_buf_storage[j + (i - GT917D_OPEN_RAW_SAMPLE_NUM) * gt917d_drv_num * gt917d_sen_num];
		}

		for (j = 0; j < (gt917d_drv_num * gt917d_sen_num); j++) {
			if (jitter_compat[j] < 0)
				jitter_compat[j] = 0 - jitter_compat[j];
			if (jitter_limit_temp[j] < jitter_compat[j])
				jitter_limit_temp[j] = jitter_compat[j];
		}

	}

	gt917d_jitter_test();

	//save jitter test
	TOUCH_I("Step 4: save jitter result\n");
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"================ Jitter Test Result ================\n");
	for (i = 0; i < gt917d_sen_num; i++) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "[%2d] ", i);
		for (j = 0; j < gt917d_drv_num; j++) {
			bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "%3d ",
					jitter_limit_temp[i + j * gt917d_sen_num]);
		}
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "\n");
	}
	bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes,
			"=====================================================\n");

	if (test_error_code & _BEYOND_JITTER_LIMIT) {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "NG !\n\n");
		*test_result = TEST_FAIL;
		TOUCH_I("JITTER TEST FAIL!!\n");
	} else {
		bytes += snprintf(buffer + bytes, PAGE_SIZE - bytes, "PASS\n\n");
		TOUCH_I("JITTER TEST PASS!!\n");
	}

open_test_exit:
	clear_bit(PANEL_ITO_TESTING, &d->flags);
	touch_interrupt_control(dev, INTERRUPT_ENABLE);

	kfree(Ito_result_info);
	kfree(jitter_compat);
	kfree(raw_buf_storage);

	gt917d_read_coor_cmd(client);

	gt917d_send_cfg(dev);
	touch_msleep(150);
	return bytes;
}

static ssize_t show_jitter_debug(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	s32 bytes = 0;
	int test_result = 0;

	TOUCH_TRACE();

	mutex_lock(&ts->lock);

	bytes = jitter_test_debug(dev, &test_result);
	memcpy(buf, buffer, bytes);

	mutex_unlock(&ts->lock);

	return bytes;
}

static TOUCH_ATTR(sd, show_sd, NULL);
static TOUCH_ATTR(lpwg_sd, show_lpwg_sd, NULL);
static TOUCH_ATTR(rawdata, show_rawdata, NULL);
static TOUCH_ATTR(delta, show_delta, NULL);
static TOUCH_ATTR(accord, show_accord, NULL);
static TOUCH_ATTR(jitter, show_jitter, NULL);
static TOUCH_ATTR(short, show_short, NULL);
static TOUCH_ATTR(debug_data1, show_debug_data1, NULL);
static TOUCH_ATTR(debug_data2, show_debug_data2, NULL);
static TOUCH_ATTR(debug_data3, show_debug_data3, NULL);
static TOUCH_ATTR(debug_jitter, show_jitter_debug, NULL);

static struct attribute *prd_attribute_list[] = {
	&touch_attr_sd.attr,
	&touch_attr_lpwg_sd.attr,
	&touch_attr_rawdata.attr,
	&touch_attr_delta.attr,
	&touch_attr_accord.attr,
	&touch_attr_jitter.attr,
	&touch_attr_short.attr,
	&touch_attr_debug_data1.attr,
	&touch_attr_debug_data2.attr,
	&touch_attr_debug_data3.attr,
	&touch_attr_debug_jitter.attr,
	NULL,
};

static const struct attribute_group prd_attribute_group = {
	.attrs = prd_attribute_list,
};

int gt917d_prd_register_sysfs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;

	TOUCH_TRACE();

	ret = sysfs_create_group(&ts->kobj, &prd_attribute_group);

	if (ret < 0) {
		TOUCH_E("failed to create sysfs\n");
		return ret;
	}

	return ret;
}

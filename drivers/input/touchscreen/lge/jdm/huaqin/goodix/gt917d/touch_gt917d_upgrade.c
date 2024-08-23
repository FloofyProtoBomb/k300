/* touch_gt917d_upgrade.c
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

#include <linux/kthread.h>
#include "touch_gt917d.h"

#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/firmware.h>
#include <linux/ctype.h>

#define GT917D_REG_HW_INFO	0x4220
#define GT917D_REG_PID_VID	0x8140

#define FW_HEAD_LENGTH		14
#define FW_SECTION_LENGTH	0x2000		/*  8K */
#define FW_DSP_ISP_LENGTH	0x1000		/*  4K */
#define FW_DSP_LENGTH		0x1000		/*  4K */
#define FW_BOOT_LENGTH		0x800		/*  2K */
#define FW_BOOT_ISP_LENGTH	0x800		/*  2k */
#define FW_GLINK_LENGTH		0x3000		/*  12k */
#define FW_GWAKE_LENGTH		(4 * FW_SECTION_LENGTH)	/*  32k */

#define PACK_SIZE		256
#define MAX_FRAME_CHECK_TIME	5

#pragma pack(1)
struct st_fw_head {
	u8 hw_info[4];	/* hardware info */
	u8 pid[8];	/* product id */
	u16 vid;	/* version id */
};
#pragma pack()

struct st_update_msg {
	u8 fw_damaged;
	u8 fw_flag;
	const u8 *fw_data;
	struct file *cfg_file;
	struct st_fw_head ic_fw_msg;
	u32 fw_total_len;
	u32 fw_burned_len;
	const struct firmware *fw;
};

static struct st_update_msg update_msg;

u16 show_len;
u16 total_len;

static u8 gt917d_get_ic_msg(struct device *dev, u16 addr, u8 *msg, s32 len)
{
	s32 i = 0;

	TOUCH_TRACE();

	for (i = 0; i < 5; i++) {
		if (gt917d_reg_read(dev, addr, msg, len) == 0)
			break;
	}

	if (i >= 5) {
		TOUCH_E("Read data from 0x%02x failed!\n", addr);
		return FAIL;
	}

	return SUCCESS;
}

static u8 gt917d_set_ic_msg(struct device *dev, u16 addr, u8 val)
{
	s32 i = 0;
	u8 msg = val;

	TOUCH_TRACE();

	for (i = 0; i < 5; i++) {
		if (gt917d_reg_write(dev, addr, &msg, 1) == 0)
			break;
	}

	if (i >= 5) {
		TOUCH_E(" Set data to 0x%02x failed!\n", addr);
		return FAIL;
	}

	return SUCCESS;
}

static u8 gt917d_get_ic_fw_msg(struct device *dev)
{
	s32 ret = -1;
	u8 retry = 0;
	u8 buf[16] = {0, };
	u8 i = 0;

	TOUCH_TRACE();

	/* step1:get hardware info */
	ret = gt917d_i2c_read_dbl_check(dev, GT917D_REG_HW_INFO, &buf[GT917D_ADDR_LENGTH], 4);
	if (ret == FAIL) {
		TOUCH_E("[get_ic_fw_msg]get hw_info failed, exit\n");
		return FAIL;
	}

	/* buf[2~5]: 00 06 90 00
	 * hw_info: 00 90 06 00
	 */
	for (i = 0; i < 4; i++)
		update_msg.ic_fw_msg.hw_info[i] = buf[GT917D_ADDR_LENGTH + 3 - i];

	TOUCH_D(FW_UPGRADE, "IC Hardware info:%02x%02x%02x%02x\n",
			update_msg.ic_fw_msg.hw_info[0],
			update_msg.ic_fw_msg.hw_info[1],
			update_msg.ic_fw_msg.hw_info[2],
			update_msg.ic_fw_msg.hw_info[3]);

	/* step2:get firmware message */
	for (retry = 0; retry < 2; retry++) {
		ret = gt917d_get_ic_msg(dev, GT917D_REG_FW_MSG, &buf[GT917D_ADDR_LENGTH], 1);
		if (ret == FAIL) {
			TOUCH_E("Read firmware message fail.\n");
			return ret;
		}

		update_msg.fw_damaged = buf[GT917D_ADDR_LENGTH];
		if ((update_msg.fw_damaged != 0xBE) && (!retry)) {
			TOUCH_I("The check sum in ic is error.\n");
			TOUCH_I("The IC will be updated by force.\n");
			continue;
		}
		break;
	}
	TOUCH_D(FW_UPGRADE, "IC force update flag:0x%x\n", update_msg.fw_damaged);

	/* step3:get pid & vid */
	ret = gt917d_i2c_read_dbl_check(dev, GT917D_REG_PID_VID, &buf[GT917D_ADDR_LENGTH], 6);
	if (ret == FAIL) {
		TOUCH_E("[get_ic_fw_msg]get pid & vid failed, exit\n");
		return FAIL;
	}

	memset(update_msg.ic_fw_msg.pid, 0, sizeof(update_msg.ic_fw_msg.pid));
	memcpy(update_msg.ic_fw_msg.pid, &buf[GT917D_ADDR_LENGTH], 4);
	TOUCH_D(FW_UPGRADE, "IC Product id:%s\n", update_msg.ic_fw_msg.pid);

	/* GT9XX PID MAPPING */
	/*|-----FLASH-----RAM-----|
	 *|------918------918-----|
	 *|------968------968-----|
	 *|------913------913-----|
	 *|------913P-----913P----|
	 *|------927------927-----|
	 *|------927P-----927P----|
	 *|------9110-----9110----|
	 *|------9110P----9111----|*/
	if (update_msg.ic_fw_msg.pid[0] != 0) {
		if (!memcmp(update_msg.ic_fw_msg.pid, "9111", 4)) {
			TOUCH_D(FW_UPGRADE, "IC Mapping Product id:%s\n",
					update_msg.ic_fw_msg.pid);
			memcpy(update_msg.ic_fw_msg.pid, "9110P", 5);
		}
	}

	update_msg.ic_fw_msg.vid = buf[GT917D_ADDR_LENGTH + 4] +
		(buf[GT917D_ADDR_LENGTH + 5] << 8);
	TOUCH_D(FW_UPGRADE, "IC version id:%04x\n", update_msg.ic_fw_msg.vid);

	return SUCCESS;
}

s32 gt917d_enter_update_mode(struct device *dev)
{
	s32 ret = -1;
	s32 retry = 0;
	u8 rd_buf[3] = {0, };

	TOUCH_TRACE();

	gt917d_hw_reset(dev);
	touch_msleep(6);

	while (retry++ < 200) {
		/* step4:Hold ss51 & dsp */
		ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x0C);
		if (ret <= 0) {
			TOUCH_D(FW_UPGRADE, "Hold ss51 & dsp I2C error, retry:%d\n", retry);
			continue;
		}

		/* step5:Confirm hold */
		ret = gt917d_get_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, rd_buf, 1);
		if (ret <= 0) {
			TOUCH_D(FW_UPGRADE, "Hold ss51 & dsp I2C error, retry:%d\n", retry);
			continue;
		}
		if (rd_buf[0] == 0x0C) {
			TOUCH_D(FW_UPGRADE, "Hold ss51 & dsp confirm SUCCESS\n");
			break;
		}
		TOUCH_D(FW_UPGRADE, "Hold ss51 & dsp confirm 0x4180 failed, value:%d\n", rd_buf[0]);
	}
	if (retry >= 200) {
		TOUCH_E("Enter update Hold ss51 failed.\n");
		return FAIL;
	}

	/* step6:DSP_CK and DSP_ALU_CK PowerOn */
	ret = gt917d_set_ic_msg(dev, 0x4010, 0x00);

	return ret;
}

static u8 gt917d_enter_update_judge(struct device *dev, struct st_fw_head *fw_head)
{
	u16 u16_tmp = 0;
	s32 i = 0;
	u32 fw_len = 0;
	s32 pid_cmp_len = 0;

	TOUCH_TRACE();

	u16_tmp = fw_head->vid;
	fw_head->vid = (u16)(u16_tmp >> 8) + (u16)(u16_tmp << 8);

	TOUCH_I("FILE HARDWARE INFO:%*ph\n", 4, &fw_head->hw_info[0]);
	TOUCH_I("FILE PID:%s\n", fw_head->pid);
	TOUCH_I("FILE VID:%04x\n", fw_head->vid);

	TOUCH_I("IC HARDWARE INFO:%*ph\n", 4, &update_msg.ic_fw_msg.hw_info[0]);
	TOUCH_I("IC PID:%s\n", update_msg.ic_fw_msg.pid);
	TOUCH_I("IC VID:%04x\n", update_msg.ic_fw_msg.vid);

	if (!memcmp(fw_head->pid, "9158", 4) &&
			!memcmp(update_msg.ic_fw_msg.pid, "915S", 4)) {
		TOUCH_I("Update GT915S to GT9158 directly!\n");
		return SUCCESS;
	}

	if (!memcmp(fw_head->hw_info, update_msg.ic_fw_msg.hw_info,
				sizeof(update_msg.ic_fw_msg.hw_info))) {
		fw_len = 42 * 1024;
	} else {
		fw_len = fw_head->hw_info[3];
		fw_len += (((u32)fw_head->hw_info[2]) << 8);
		fw_len += (((u32)fw_head->hw_info[1]) << 16);
		fw_len += (((u32)fw_head->hw_info[0]) << 24);
	}
	if (update_msg.fw_total_len != fw_len) {
		TOUCH_E("Inconsistent firmware size, Update aborted!\n");
		TOUCH_E("Default size: %d(%dK), actual size: %d(%dK)\n",
				fw_len, fw_len / 1024, update_msg.fw_total_len,
				update_msg.fw_total_len / 1024);
		return FAIL;
	}
	TOUCH_I("Firmware length:%d(%dK)\n",
			update_msg.fw_total_len,
			update_msg.fw_total_len / 1024);

	if (update_msg.fw_damaged != 0xBE) {
		TOUCH_I("FW chksum error, need enter update.\n");
		return SUCCESS;
	}

	if (strlen(update_msg.ic_fw_msg.pid) < 3) {
		TOUCH_I("Illegal IC pid, need enter update\n");
		return SUCCESS;
	}

	/* check pid legality */
	for (i = 0; i < 3; i++) {
		if (!isdigit(update_msg.ic_fw_msg.pid[i])) {
			TOUCH_I("Illegal IC pid, need enter update\n");
			return SUCCESS;
		}
	}

	pid_cmp_len = strlen(fw_head->pid);
	if (pid_cmp_len < strlen(update_msg.ic_fw_msg.pid))
		pid_cmp_len = strlen(update_msg.ic_fw_msg.pid);

	if ((!memcmp(fw_head->pid, update_msg.ic_fw_msg.pid, pid_cmp_len)) ||
			(!memcmp(update_msg.ic_fw_msg.pid, "91XX", 4)) ||
			(!memcmp(fw_head->pid, "91XX", 4))) {
		if (!memcmp(fw_head->pid, "91XX", 4))
			TOUCH_I("Force none same pid update mode.\n");
		else
			TOUCH_I("Get the same pid.\n");

		if (fw_head->vid != update_msg.ic_fw_msg.vid) {
			TOUCH_I("Need enter update.\n");
			return SUCCESS;
		}
		TOUCH_I("File VID == Ic VID, update aborted!\n");
	} else {
		TOUCH_I("File PID != Ic PID, update aborted!\n");
	}

	return FAIL;
}

static u8 gt917d_get_update_file(struct device *dev, struct st_fw_head *fw_head, u8 *path)
{
	s32 ret = 0;
	s32 i = 0;
	s32 fw_checksum = 0;

	TOUCH_TRACE();

	ret = request_firmware(&update_msg.fw, path, dev);
	if (ret) {
		TOUCH_E("Failed get firmware:%d\n", ret);
		goto invalied_fw;
	}

	TOUCH_I("FW File: %s size = %zu\n", path, update_msg.fw->size);
	update_msg.fw_data = update_msg.fw->data;
	update_msg.fw_total_len = update_msg.fw->size;

	if (update_msg.fw_total_len <
			FW_HEAD_LENGTH
			+ FW_SECTION_LENGTH * 4
			+ FW_DSP_ISP_LENGTH
			+ FW_DSP_LENGTH
			+ FW_BOOT_LENGTH) {
		TOUCH_E("INVALID bin file(size: %d), update aborted.\n",
				update_msg.fw_total_len);
		goto invalied_fw;
	}

	update_msg.fw_total_len -= FW_HEAD_LENGTH;

	TOUCH_D(FW_UPGRADE, "Bin firmware actual size: %d(%dK)\n",
			update_msg.fw_total_len,
			update_msg.fw_total_len / 1024);
	memcpy(fw_head, update_msg.fw_data, FW_HEAD_LENGTH);

	/* check firmware legality */
	fw_checksum = 0;
	for (i = 0; i < update_msg.fw_total_len; i += 2) {
		u16 temp;

		temp = (update_msg.fw_data[FW_HEAD_LENGTH + i] << 8) +
			update_msg.fw_data[FW_HEAD_LENGTH + i + 1];
		fw_checksum += temp;
	}
	TOUCH_D(FW_UPGRADE, "firmware checksum:%x\n", fw_checksum & 0xFFFF);
	if (fw_checksum & 0xFFFF) {
		TOUCH_E("Illegal firmware file.\n");
		goto invalied_fw;
	}

	return SUCCESS;

invalied_fw:
	update_msg.fw_data = NULL;
	update_msg.fw_total_len = 0;

	release_firmware(update_msg.fw);
	return FAIL;
}

static u8 gt917d_burn_proc(struct device *dev, u8 *burn_buf, u16 start_addr, u16 total_length)
{
	s32 ret = 0;
	u16 burn_addr = start_addr;
	u16 frame_length = 0;
	u16 burn_length = 0;
	u8 wr_buf[PACK_SIZE] = {0,};
	u8 rd_buf[PACK_SIZE] = {0,};
	u8 retry = 0;

	TOUCH_TRACE();

	TOUCH_D(FW_UPGRADE, "Begin burn %dk data to addr 0x%x\n", total_length / 1024, start_addr);
	while (burn_length < total_length) {
		TOUCH_D(FW_UPGRADE, "B/T:%04d/%04d\n", burn_length, total_length);
		frame_length = ((total_length - burn_length)
				> PACK_SIZE) ? PACK_SIZE : (total_length - burn_length);

		memcpy(&wr_buf[0], &burn_buf[burn_length], frame_length);

		for (retry = 0; retry < MAX_FRAME_CHECK_TIME; retry++) {
			ret = gt917d_reg_write(dev, burn_addr, wr_buf, frame_length);
			if (ret < 0) {
				TOUCH_E("Write frame data i2c error.\n");
				continue;
			}
			ret = gt917d_reg_read(dev, burn_addr, rd_buf, frame_length);
			if (ret < 0) {
				TOUCH_E("Read back frame data i2c error.\n");
				continue;
			}
			if (memcmp(&wr_buf[0], &rd_buf[0], frame_length)) {
				TOUCH_E("Check frame data fail, not equal.\n");
				TOUCH_D(FW_UPGRADE, "write array:\n");
				TOUCH_D(FW_UPGRADE, "read array:\n");
				continue;
			} else {
				TOUCH_D(FW_UPGRADE, "Check frame data success.\n");
				break;
			}
		}
		if (retry >= MAX_FRAME_CHECK_TIME) {
			TOUCH_E("Burn frame data time out, exit.\n");
			return FAIL;
		}
		burn_length += frame_length;
		burn_addr += frame_length;
	}

	return SUCCESS;
}

static u8 gt917d_load_section_file(u8 *buf, u32 offset, u16 length, u8 set_or_end)
{
	TOUCH_TRACE();

	if (!update_msg.fw_data || update_msg.fw_total_len < offset + length) {
		TOUCH_E("cannot load section data. fw_len=%d read end=%d\n",
				update_msg.fw_total_len,
				FW_HEAD_LENGTH + offset + length);
		return FAIL;
	}

	if (set_or_end == SEEK_SET) {
		memcpy(buf, &update_msg.fw_data[FW_HEAD_LENGTH + offset],
				length);
	} else {
		/* seek end */
		memcpy(buf, &update_msg.fw_data[update_msg.fw_total_len +
				FW_HEAD_LENGTH - offset], length);
	}

	return SUCCESS;
}

static u8 gt917d_recall_check(struct device *dev, u8 *chk_src, u16 start_rd_addr, u16 chk_length)
{
	u8 rd_buf[PACK_SIZE + GT917D_ADDR_LENGTH];
	s32 ret = 0;
	u16 recall_addr = start_rd_addr;
	u16 recall_length = 0;
	u16 frame_length = 0;

	TOUCH_TRACE();

	while (recall_length < chk_length) {
		frame_length = ((chk_length - recall_length)
				> PACK_SIZE) ? PACK_SIZE :
			(chk_length - recall_length);
		ret = gt917d_get_ic_msg(dev, recall_addr, rd_buf, frame_length);
		if (ret <= 0) {
			TOUCH_E("recall i2c error, exit\n");
			return FAIL;
		}

		if (memcmp(&rd_buf[0], &chk_src[recall_length], frame_length)) {
			TOUCH_E("Recall frame data fail, not equal.\n");
			return FAIL;
		}

		recall_length += frame_length;
		recall_addr += frame_length;
	}
	TOUCH_D(FW_UPGRADE, "Recall check %dk firmware success.\n", (chk_length / 1024));

	return SUCCESS;
}

static u8 gt917d_burn_fw_section(struct device *dev, u8 *fw_section, u16 start_addr, u8 bank_cmd)
{
	s32 ret = 0;
	u8 rd_buf[5] = {0, };

	TOUCH_TRACE();

	/* step1:hold ss51 & dsp */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]hold ss51 & dsp fail.\n");
		return FAIL;
	}

	/* step2:set scramble */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]set scramble fail.\n");
		return FAIL;
	}

	/* step3:select bank */
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__SRAM_BANK, (bank_cmd >> 4) & 0x0F);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]select bank %d fail.\n", (bank_cmd >> 4) & 0x0F);
		return FAIL;
	}

	/* step4:enable accessing code */
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__MEM_CD_EN, 0x01);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]enable accessing code fail.\n");
		return FAIL;
	}

	/* step5:burn 8k fw section */
	ret = gt917d_burn_proc(dev, fw_section, start_addr, FW_SECTION_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_section]burn fw_section fail.\n");
		return FAIL;
	}

	/* step6:hold ss51 & release dsp */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x04);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]hold ss51 & release dsp fail.\n");
		return FAIL;
	}
	/* must delay */
	usleep_range(1000, 2000);

	/* step7:send burn cmd to move data to flash from sram */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, bank_cmd & 0x0f);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]send burn cmd fail.\n");
		return FAIL;
	}
	TOUCH_D(FW_UPGRADE, "[burn_fw_section]Wait for the burn is complete......\n");
	do {
		ret = gt917d_get_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TOUCH_E("[burn_fw_section]Get burn state fail\n");
			return FAIL;
		}
		usleep_range(10000, 11000);
	} while (rd_buf[0]);

	/* step8:select bank */
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__SRAM_BANK, (bank_cmd >> 4) & 0x0F);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]select bank %d fail.\n", (bank_cmd >> 4) & 0x0F);
		return FAIL;
	}

	/* step9:enable accessing code */
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__MEM_CD_EN, 0x01);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]enable accessing code fail.\n");
		return FAIL;
	}

	/* step10:recall 8k fw section */
	ret = gt917d_recall_check(dev, fw_section, start_addr, FW_SECTION_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_section]recall check %dk firmware fail.\n", FW_SECTION_LENGTH / 1024);
		return FAIL;
	}

	/* step11:disable accessing code */
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__MEM_CD_EN, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]disable accessing code fail.\n");
		return FAIL;
	}

	return SUCCESS;
}

static u8 gt917d_burn_dsp_isp(struct device *dev)
{
	s32 ret = 0;
	u8 *fw_dsp_isp = NULL;
	u8 retry = 0;

	TOUCH_TRACE();

	TOUCH_I("[burn_dsp_isp]Begin burn dsp isp---->>\n");

	/* step1:alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_dsp_isp = kzalloc(FW_DSP_ISP_LENGTH, GFP_KERNEL);
		if (fw_dsp_isp == NULL) {
			continue;
		} else {
			TOUCH_I("[burn_dsp_isp]Alloc %dk byte memory success.\n", FW_DSP_ISP_LENGTH / 1024);
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_dsp_isp]Alloc memory fail,exit.\n");
		kfree(fw_dsp_isp);
		return FAIL;
	}

	/* step2:load dsp isp file data */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step2:load dsp isp file data\n");
	ret = gt917d_load_section_file(fw_dsp_isp, FW_DSP_ISP_LENGTH,
			FW_DSP_ISP_LENGTH, SEEK_END);
	if (ret == FAIL) {
		TOUCH_E("[burn_dsp_isp]load firmware dsp_isp fail.\n");
		goto exit_burn_dsp_isp;
	}

	/* step3:disable wdt,clear cache enable */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step3:disable wdt, clear cache enable\n");
	ret = gt917d_set_ic_msg(dev, 0x40B0, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]disable wdt fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}
	ret = gt917d_set_ic_msg(dev, 0x404B, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]clear cache enable fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step4:hold ss51 & dsp */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step4:hold ss51 & dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]hold ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step5:set boot from sram */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step5:set boot from sram\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOTCTL_B0_, 0x02);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]set boot from sram fail\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step6:software reboot */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step6:software reboot\n");
	ret = gt917d_set_ic_msg(dev, _bWO_MISCTL__CPU_SWRST_PULSE, 0x01);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]software reboot fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step7:select bank2 */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step7:select bank2\n");
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__SRAM_BANK, 0x02);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]select bank2 fail\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step8:enable accessing code */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step8:enable accessing code\n");
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__MEM_CD_EN, 0x01);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]enable accessing code fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step9:burn 4k dsp_isp */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step9:burn 4k dsp_isp\n");
	ret = gt917d_burn_proc(dev, fw_dsp_isp, 0xC000, FW_DSP_ISP_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_dsp_isp]burn dsp_isp fail.\n");
		goto exit_burn_dsp_isp;
	}

	/* step10:set scramble */
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]step10:set scramble\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_dsp_isp]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}
	update_msg.fw_burned_len += FW_DSP_ISP_LENGTH;
	TOUCH_D(FW_UPGRADE, "[burn_dsp_isp]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_dsp_isp:
	kfree(fw_dsp_isp);

	return ret;
}

static u8 gt917d_burn_fw_ss51(struct device *dev)
{
	u8 *fw_ss51 = NULL;
	u8 retry = 0;
	s32 ret = 0;

	TOUCH_TRACE();

	TOUCH_I("[burn_fw_ss51]Begin burn ss51 firmware---->>\n");

	/* step1:alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_ss51 = kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_ss51 == NULL) {
			continue;
		} else {
			TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]Alloc %dk byte memory success.\n", (FW_SECTION_LENGTH / 1024));
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_fw_ss51]Alloc memory fail, exit.\n");
		kfree(fw_ss51);
		return FAIL;
	}

	TOUCH_I("[burn_fw_ss51]Reset first 8K of ss51 to 0xFF.\n");
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step2: reset bank0 0xC000~0xD000\n");
	memset(fw_ss51, 0xFF, FW_SECTION_LENGTH);

	/* step3:clear control flag */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step3:clear control flag\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_ss51]clear control flag fail.\n");
		ret = FAIL;
		goto exit_burn_fw_ss51;
	}

	/* step4:burn ss51 firmware section 1 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step4:burn ss51 firmware section 1\n");
	ret = gt917d_burn_fw_section(dev, fw_ss51, 0xC000, 0x01);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_ss51]burn ss51 firmware section 1 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step5:load ss51 firmware section 2 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step5:load ss51 firmware section 2 file data\n");
	ret = gt917d_load_section_file(fw_ss51, FW_SECTION_LENGTH, FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_ss51]load ss51 firmware section 2 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step6:burn ss51 firmware section 2 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step6:burn ss51 firmware section 2\n");
	ret = gt917d_burn_fw_section(dev, fw_ss51, 0xE000, 0x02);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_ss51]burn ss51 firmware section 2 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step7:load ss51 firmware section 3 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step7:load ss51 firmware section 3 file data\n");
	ret = gt917d_load_section_file(fw_ss51, 2 * FW_SECTION_LENGTH, FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_ss51]load ss51 firmware section 3 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step8:burn ss51 firmware section 3 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step8:burn ss51 firmware section 3\n");
	ret = gt917d_burn_fw_section(dev, fw_ss51, 0xC000, 0x13);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_ss51]burn ss51 firmware section 3 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step9:load ss51 firmware section 4 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step9:load ss51 firmware section 4 file data\n");
	ret = gt917d_load_section_file(fw_ss51, 3 * FW_SECTION_LENGTH,
			FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_ss51]load ss51 firmware section 4 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step10:burn ss51 firmware section 4 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]step10:burn ss51 firmware section 4\n");
	ret = gt917d_burn_fw_section(dev, fw_ss51, 0xE000, 0x14);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_ss51]burn ss51 firmware section 4 fail.\n");
		goto exit_burn_fw_ss51;
	}

	update_msg.fw_burned_len += (FW_SECTION_LENGTH * 4);
	TOUCH_D(FW_UPGRADE, "[burn_fw_ss51]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_ss51:
	kfree(fw_ss51);
	return ret;
}

static u8 gt917d_burn_fw_dsp(struct device *dev)
{
	s32 ret = 0;
	u8 *fw_dsp = NULL;
	u8 retry = 0;
	u8 rd_buf[5] = {0, };

	TOUCH_TRACE();

	TOUCH_I("[burn_fw_dsp]Begin burn dsp firmware---->>\n");
	/* step1:alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_dsp = kzalloc(FW_DSP_LENGTH, GFP_KERNEL);
		if (fw_dsp == NULL) {
			continue;
		} else {
			TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]Alloc %dk byte memory success.\n", FW_SECTION_LENGTH / 1024);
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_fw_dsp]Alloc memory fail, exit.\n");
		kfree(fw_dsp);
		return FAIL;
	}

	/* step2:load firmware dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step2:load firmware dsp\n");
	ret = gt917d_load_section_file(fw_dsp, 4 * FW_SECTION_LENGTH, FW_DSP_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_dsp]load firmware dsp fail.\n");
		goto exit_burn_fw_dsp;
	}

	/* step3:select bank3 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step3:select bank3\n");
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__SRAM_BANK, 0x03);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_dsp]select bank3 fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}

	/* step4:hold ss51 & dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step4:hold ss51 & dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_dsp]hold ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}

	/* step5:set scramble */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step5:set scramble\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_dsp]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}

	/* step6:release ss51 & dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step6:release ss51 & dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x04);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_dsp]release ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}
	/* must delay */
	touch_msleep(2);

	/* step7:burn 4k dsp firmware */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step7:burn 4k dsp firmware\n");
	ret = gt917d_burn_proc(dev, fw_dsp, 0x9000, FW_DSP_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_dsp]burn fw_section fail.\n");
		goto exit_burn_fw_dsp;
	}

	/* step8:send burn cmd to move data to flash from sram */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step8:send burn cmd to move data to flash from sram\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, 0x05);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_dsp]send burn cmd fail.\n");
		goto exit_burn_fw_dsp;
	}
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]Wait for the burn is complete......\n");
	do {
		ret = gt917d_get_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TOUCH_E("[burn_fw_dsp]Get burn state fail\n");
			goto exit_burn_fw_dsp;
		}
		touch_msleep(11);
	} while (rd_buf[0]);

	/* step9:recall check 4k dsp firmware */
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]step9:recall check 4k dsp firmware\n");
	ret = gt917d_recall_check(dev, fw_dsp, 0x9000, FW_DSP_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_dsp]recall check 4k dsp firmware fail.\n");
		goto exit_burn_fw_dsp;
	}

	update_msg.fw_burned_len += FW_DSP_LENGTH;
	TOUCH_D(FW_UPGRADE, "[burn_fw_dsp]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_dsp:
	kfree(fw_dsp);

	return ret;
}

static u8 gt917d_burn_fw_boot(struct device *dev)
{
	s32 ret = 0;
	u8 *fw_boot = NULL;
	u8 retry = 0;
	u8 rd_buf[5] = {0, };

	TOUCH_TRACE();

	TOUCH_I("[burn_fw_boot]Begin burn bootloader firmware---->>\n");

	/* step1:Alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step1:Alloc memory\n");
	while (retry++ < 5) {
		fw_boot = kzalloc(FW_BOOT_LENGTH, GFP_KERNEL);
		if (fw_boot == NULL) {
			continue;
		} else {
			TOUCH_D(FW_UPGRADE, "[burn_fw_boot]Alloc %dk byte memory success.\n", FW_BOOT_LENGTH / 1024);
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_fw_boot]Alloc memory fail, exit.\n");
		kfree(fw_boot);
		return FAIL;
	}

	/* step2:load firmware bootloader */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step2:load firmware bootloader\n");
	ret = gt917d_load_section_file(fw_boot,
			4 * FW_SECTION_LENGTH + FW_DSP_LENGTH,
			FW_BOOT_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_boot]load firmware bootcode fail.\n");
		goto exit_burn_fw_boot;
	}

	/* step3:hold ss51 & dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step3:hold ss51 & dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot]hold ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}

	/* step4:set scramble */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step4:set scramble\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}

	/* step5:hold ss51 & release dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step5:hold ss51 & release dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x04);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot]release ss51 & dsp fail\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}
	/* must delay */
	touch_msleep(2);

	/* step6:select bank3 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step6:select bank3\n");
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__SRAM_BANK, 0x03);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot]select bank3 fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}

	/* step6:burn 2k bootloader firmware */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step6:burn 2k bootloader firmware\n");
	ret = gt917d_burn_proc(dev, fw_boot, 0x9000, FW_BOOT_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_boot]burn fw_boot fail.\n");
		goto exit_burn_fw_boot;
	}

	/* step7:send burn cmd to move data to flash from sram */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step7:send burn cmd to move data to flash from sram\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, 0x06);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot]send burn cmd fail.\n");
		goto exit_burn_fw_boot;
	}
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]Wait for the burn is complete......\n");
	do {
		ret = gt917d_get_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TOUCH_E("[burn_fw_boot]Get burn state fail\n");
			goto exit_burn_fw_boot;
		}
		touch_msleep(11);
	} while (rd_buf[0]);

	/* step8:recall check 2k bootloader firmware */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]step8:recall check 2k bootloader firmware\n");
	ret = gt917d_recall_check(dev, fw_boot, 0x9000, FW_BOOT_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_boot]recall check 2k bootcode firmware fail\n");
		goto exit_burn_fw_boot;
	}

	update_msg.fw_burned_len += FW_BOOT_LENGTH;
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_boot:
	kfree(fw_boot);

	return ret;
}
static u8 gt917d_burn_fw_boot_isp(struct device *dev)
{
	s32 ret = 0;
	u8 *fw_boot_isp = NULL;
	u8 retry = 0;
	u8 rd_buf[5] = {0, };

	TOUCH_TRACE();

	if (update_msg.fw_burned_len >= update_msg.fw_total_len) {
		TOUCH_D(FW_UPGRADE, "No need to upgrade the boot_isp code!\n");
		return SUCCESS;
	}
	TOUCH_I("[burn_fw_boot_isp]Begin burn boot_isp firmware---->>\n");

	/* step1:Alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step1:Alloc memory\n");
	while (retry++ < 5) {
		fw_boot_isp = kzalloc(FW_BOOT_ISP_LENGTH, GFP_KERNEL);
		if (fw_boot_isp == NULL) {
			continue;
		} else {
			TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]Alloc %dk byte memory success.\n", (FW_BOOT_ISP_LENGTH / 1024));
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_fw_boot_isp]Alloc memory fail, exit.\n");
		kfree(fw_boot_isp);
		return FAIL;
	}

	/* step2:load firmware bootloader */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step2:load firmware bootloader isp\n");
	ret = gt917d_load_section_file(fw_boot_isp, (update_msg.fw_burned_len - FW_DSP_ISP_LENGTH), FW_BOOT_ISP_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_boot_isp]load firmware boot_isp fail.\n");
		goto exit_burn_fw_boot_isp;
	}

	/* step3:hold ss51 & dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step3:hold ss51 & dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot_isp]hold ss51 & dsp fail\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}

	/* step4:set scramble */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step4:set scramble\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot_isp]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}

	/* step5:hold ss51 & release dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step5:hold ss51 & release dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x04);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot_isp]release ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}
	/* must delay */
	touch_msleep(2);

	/* step6:select bank3 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step6:select bank3\n");
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__SRAM_BANK, 0x03);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot_isp]select bank3 fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}

	/* step7:burn 2k bootload_isp firmware */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step7:burn 2k bootloader firmware\n");
	ret = gt917d_burn_proc(dev, fw_boot_isp, 0x9000, FW_BOOT_ISP_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_boot_isp]burn fw_section fail.\n");
		goto exit_burn_fw_boot_isp;
	}

	/* step7:send burn cmd to move data to flash from sram */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step8:send burn cmd to move data to flash from sram\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, 0x07);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_boot_isp]send burn cmd fail.\n");
		goto exit_burn_fw_boot_isp;
	}
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]Wait for the burn is complete......\n");
	do {
		ret = gt917d_get_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TOUCH_E("[burn_fw_boot_isp]Get burn state fail\n");
			goto exit_burn_fw_boot_isp;
		}
		usleep_range(10000, 11000);
	} while (rd_buf[0]);

	/* step8:recall check 2k bootload_isp firmware */
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]step9:recall check 2k bootloader firmware\n");
	ret = gt917d_recall_check(dev, fw_boot_isp, 0x9000, FW_BOOT_ISP_LENGTH);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_boot_isp]recall check 2k bootcode_isp firmware fail.\n");
		goto exit_burn_fw_boot_isp;
	}

	update_msg.fw_burned_len += FW_BOOT_ISP_LENGTH;
	TOUCH_D(FW_UPGRADE, "[burn_fw_boot_isp]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_boot_isp:
	kfree(fw_boot_isp);

	return ret;
}

static u8 gt917d_burn_fw_gwake_section(struct device *dev,
		u8 *fw_section, u16 start_addr, u32 len, u8 bank_cmd)
{
	s32 ret = 0;
	u8 rd_buf[5] = {0, };

	TOUCH_TRACE();

	/* step1:hold ss51 & dsp */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_app_section]hold ss51 & dsp fail.\n");
		return FAIL;
	}

	/* step2:set scramble */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_app_section]set scramble fail.\n");
		return FAIL;
	}

	/* step3:hold ss51 & release dsp */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x04);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_app_section]hold ss51 & release dsp fail.\n");
		return FAIL;
	}
	/* must delay */
	touch_msleep(20);

	/* step4:select bank */
	ret = gt917d_set_ic_msg(dev, _bRW_MISCTL__SRAM_BANK, (bank_cmd >> 4) & 0x0F);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_section]select bank %d fail.\n", (bank_cmd >> 4) & 0x0F);
		return FAIL;
	}

	/* step5:burn fw section */
	ret = gt917d_burn_proc(dev, fw_section, start_addr, len);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_app_section]burn fw_section fail.\n");
		return FAIL;
	}

	/* step6:send burn cmd to move data to flash from sram */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, bank_cmd & 0x0F);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_app_section]send burn cmd fail.\n");
		return FAIL;
	}
	TOUCH_D(FW_UPGRADE, "[burn_fw_section]Wait for the burn is complete......\n");
	do {
		ret = gt917d_get_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TOUCH_E("[burn_fw_app_section]Get burn state fail\n");
			return FAIL;
		}
		touch_msleep(10);
	} while (rd_buf[0]);

	/* step7:recall fw section */
	ret = gt917d_recall_check(dev, fw_section, start_addr, len);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_app_section]recall check %dk firmware fail.\n", len / 1024);
		return FAIL;
	}

	return SUCCESS;
}

static u8 gt917d_burn_fw_link(struct device *dev)
{
	u8 *fw_link = NULL;
	u8 retry = 0;
	s32 ret = 0;
	u32 offset = 0;

	TOUCH_TRACE();

	if (update_msg.fw_burned_len >= update_msg.fw_total_len) {
		TOUCH_D(FW_UPGRADE, "No need to upgrade the link code!\n");
		return SUCCESS;
	}
	TOUCH_I("[burn_fw_link]Begin burn link firmware---->>\n");

	/* step1:Alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_fw_link]step1:Alloc memory\n");
	while (retry++ < 5) {
		fw_link = kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_link == NULL) {
			continue;
		} else {
			TOUCH_D(FW_UPGRADE, "[burn_fw_link]Alloc %dk byte memory success.\n", (FW_SECTION_LENGTH / 1024));
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_fw_link]Alloc memory fail, exit.\n");
		kfree(fw_link);
		return FAIL;
	}

	/* step2:load firmware link section 1 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_link]step2:load firmware link section 1\n");
	offset = update_msg.fw_burned_len - FW_DSP_ISP_LENGTH;
	ret = gt917d_load_section_file(fw_link, offset, FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_link]load firmware link section 1 fail.\n");
		goto exit_burn_fw_link;
	}

	/* step3:burn link firmware section 1 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_link]step3:burn link firmware section 1\n");
	ret = gt917d_burn_fw_gwake_section(dev, fw_link, 0x9000, FW_SECTION_LENGTH, 0x38);

	if (ret == FAIL) {
		TOUCH_E("[burn_fw_link]burn link firmware section 1 fail.\n");
		goto exit_burn_fw_link;
	}

	/* step4:load link firmware section 2 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_link]step4:load link firmware section 2 file data\n");
	offset += FW_SECTION_LENGTH;
	ret = gt917d_load_section_file(fw_link, offset,
			FW_GLINK_LENGTH - FW_SECTION_LENGTH, SEEK_SET);

	if (ret == FAIL) {
		TOUCH_E("[burn_fw_link]load link firmware section 2 fail.\n");
		goto exit_burn_fw_link;
	}

	/* step5:burn link firmware section 2 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_link]step4:burn link firmware section 2\n");
	ret = gt917d_burn_fw_gwake_section(dev, fw_link, 0x9000, FW_GLINK_LENGTH - FW_SECTION_LENGTH, 0x39);

	if (ret == FAIL) {
		TOUCH_E("[burn_fw_link]burn link firmware section 2 fail.\n");
		goto exit_burn_fw_link;
	}

	update_msg.fw_burned_len += FW_GLINK_LENGTH;
	TOUCH_D(FW_UPGRADE, "[burn_fw_link]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_link:
	kfree(fw_link);

	return ret;
}

static u8 gt917d_burn_fw_gwake(struct device *dev)
{
	u8 *fw_gwake = NULL;
	u8 retry = 0;
	s32 ret = 0;
	u16 start_index = 4 * FW_SECTION_LENGTH +
		FW_DSP_LENGTH + FW_BOOT_LENGTH +
		FW_BOOT_ISP_LENGTH + FW_GLINK_LENGTH;/* 32 + 4 + 2 + 4 = 42K */
	/* u16 start_index; */

	TOUCH_TRACE();

	if (start_index >= update_msg.fw_total_len) {
		TOUCH_D(FW_UPGRADE, "No need to upgrade the gwake code!\n");
		return SUCCESS;
	}
	/* start_index = update_msg.fw_burned_len - FW_DSP_ISP_LENGTH; */
	TOUCH_I("[burn_fw_gwake]Begin burn gwake firmware---->>\n");

	/* step1:alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_gwake = kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_gwake == NULL) {
			continue;
		} else {
			TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]Alloc %dk byte memory success.\n",
					(FW_SECTION_LENGTH / 1024));
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_fw_gwake]Alloc memory fail, exit.\n");
		kfree(fw_gwake);
		return FAIL;
	}

	/* clear control flag */
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_finish]clear control flag fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step2:load app_code firmware section 1 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step2:load app_code firmware section 1 file data\n");
	ret = gt917d_load_section_file(fw_gwake, start_index, FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]load app_code firmware section 1 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step3:burn app_code firmware section 1 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step3:burn app_code firmware section 1\n");
	ret = gt917d_burn_fw_gwake_section(dev,
			fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3A);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]burn app_code firmware section 1 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step5:load app_code firmware section 2 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step5:load app_code firmware section 2 file data\n");
	ret = gt917d_load_section_file(fw_gwake, start_index + FW_SECTION_LENGTH,
			FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]load app_code firmware section 2 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step6:burn app_code firmware section 2 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step6:burn app_code firmware section 2\n");
	ret = gt917d_burn_fw_gwake_section(dev,
			fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3B);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]burn app_code firmware section 2 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step7:load app_code firmware section 3 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step7:load app_code firmware section 3 file data\n");
	ret = gt917d_load_section_file(fw_gwake,
			start_index + 2 * FW_SECTION_LENGTH,
			FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]load app_code firmware section 3 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step8:burn app_code firmware section 3 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step8:burn app_code firmware section 3\n");
	ret = gt917d_burn_fw_gwake_section(dev, fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3C);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]burn app_code firmware section 3 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step9:load app_code firmware section 4 file data */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step9:load app_code firmware section 4 file data\n");
	ret = gt917d_load_section_file(fw_gwake,
			start_index + 3 * FW_SECTION_LENGTH,
			FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]load app_code firmware section 4 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step10:burn app_code firmware section 4 */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]step10:burn app_code firmware section 4\n");
	ret = gt917d_burn_fw_gwake_section(dev, fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3D);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_gwake]burn app_code firmware section 4 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* update_msg.fw_burned_len += FW_GWAKE_LENGTH; */
	TOUCH_D(FW_UPGRADE, "[burn_fw_gwake]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_gwake:
	kfree(fw_gwake);

	return ret;
}

static u8 gt917d_burn_fw_finish(struct device *dev)
{
	u8 *fw_ss51 = NULL;
	u8 retry = 0;
	s32 ret = 0;

	TOUCH_TRACE();

	TOUCH_I("[burn_fw_finish]burn first 8K of ss51 and finish update.\n");
	/* step1:alloc memory */
	TOUCH_D(FW_UPGRADE, "[burn_fw_finish]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_ss51 = kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_ss51 == NULL) {
			continue;
		} else {
			TOUCH_D(FW_UPGRADE, "[burn_fw_finish]Alloc %dk byte memory success.\n",
					(FW_SECTION_LENGTH / 1024));
			break;
		}
	}
	if (retry >= 5) {
		TOUCH_E("[burn_fw_finish]Alloc memory fail, exit.\n");
		kfree(fw_ss51);
		return FAIL;
	}

	TOUCH_D(FW_UPGRADE, "[burn_fw_finish]step2: burn ss51 first 8K.\n");
	ret = gt917d_load_section_file(fw_ss51, 0, FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_finish]load ss51 firmware section 1 fail.\n");
		goto exit_burn_fw_finish;
	}

	TOUCH_D(FW_UPGRADE, "[burn_fw_finish]step3:clear control flag\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, 0x00);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_finish]clear control flag fail.\n");
		goto exit_burn_fw_finish;
	}

	TOUCH_D(FW_UPGRADE, "[burn_fw_finish]step4:burn ss51 firmware section 1\n");
	ret = gt917d_burn_fw_section(dev, fw_ss51, 0xC000, 0x01);
	if (ret == FAIL) {
		TOUCH_E("[burn_fw_finish]burn ss51 firmware section 1 fail.\n");
		goto exit_burn_fw_finish;
	}

	/* step11:enable download DSP code */
	TOUCH_D(FW_UPGRADE, "[burn_fw_finish]step5:enable download DSP code\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__BOOT_CTL_, 0x99);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_finish]enable download DSP code fail.\n");
		goto exit_burn_fw_finish;
	}

	/* step12:release ss51 & hold dsp */
	TOUCH_D(FW_UPGRADE, "[burn_fw_finish]step6:release ss51 & hold dsp\n");
	ret = gt917d_set_ic_msg(dev, _rRW_MISCTL__SWRST_B0_, 0x08);
	if (ret <= 0) {
		TOUCH_E("[burn_fw_finish]release ss51 & hold dsp fail.\n");
		goto exit_burn_fw_finish;
	}

	if (fw_ss51 != NULL)
		kfree(fw_ss51);
	return SUCCESS;

exit_burn_fw_finish:
	if (fw_ss51 != NULL)
		kfree(fw_ss51);

	return FAIL;
}

s32 gt917d_update_proc(struct device *dev, void *dir)
{
	s32 ret = 0;
	s32 update_ret = FAIL;
	u8 retry = 0;
	struct st_fw_head fw_head = {{0, }, {0, }, 0};
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = to_i2c_client(dev);

	TOUCH_TRACE();

	TOUCH_D(FW_UPGRADE, "[update_proc]Begin update ......\n");
	show_len = 1;
	total_len = 100;

	ret = gt917d_get_update_file(dev, &fw_head, (u8 *)dir);
	if (ret == FAIL) {
		TOUCH_E("Failed get valied firmware data\n");
		return FAIL;
	}
	if (test_and_set_bit(FW_UPDATE_RUNNING, &d->flags)) {
		TOUCH_E("FW update may already running\n");
		return FAIL;
	}

	ret = gt917d_get_ic_fw_msg(dev);
	if (ret == FAIL) {
		TOUCH_E("[update_proc]get ic message fail.\n");
		goto file_fail;
	}

	if (ts->force_fwup) {
		TOUCH_D(FW_UPGRADE, "Enter force update.\n");
	} else {
		ret = gt917d_enter_update_judge(dev, &fw_head);
		if (ret == FAIL) {
			TOUCH_I("[update_proc]Doesn't meet update condition\n");
			goto file_fail;
		}
	}

	ret = gt917d_enter_update_mode(dev);
	if (ret == FAIL) {
		TOUCH_E("[update_proc]enter update mode fail.\n");
		goto update_fail;
	}

	while (retry++ < 5) {
		show_len = 10;
		total_len = 100;
		update_msg.fw_burned_len = 0;
		ret = gt917d_burn_dsp_isp(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn dsp isp fail.\n");
			continue;
		}

		show_len = 20;
		ret = gt917d_burn_fw_gwake(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn app_code firmware fail.\n");
			continue;
		}

		show_len = 30;
		ret = gt917d_burn_fw_ss51(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn ss51 firmware fail.\n");
			continue;
		}

		show_len = 40;
		ret = gt917d_burn_fw_dsp(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn dsp firmware fail.\n");
			continue;
		}

		show_len = 50;
		ret = gt917d_burn_fw_boot(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn bootloader firmware fail.\n");
			continue;
		}
		show_len = 60;

		ret = gt917d_burn_fw_boot_isp(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn boot_isp firmware fail.\n");
			continue;
		}

		show_len = 70;
		ret = gt917d_burn_fw_link(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn link firmware fail.\n");
			continue;
		}

		show_len = 80;
		ret = gt917d_burn_fw_finish(dev);
		if (ret == FAIL) {
			TOUCH_E("[update_proc]burn finish fail.\n");
			continue;
		}
		show_len = 90;
		TOUCH_I("[update_proc]UPDATE SUCCESS.\n");
		retry = 0;
		break;
	}

	if (retry >= 5) {
		TOUCH_E("[update_proc]retry timeout,UPDATE FAIL.\n");
		update_ret = FAIL;
	} else {
		update_ret = SUCCESS;
	}

update_fail:
	TOUCH_D(FW_UPGRADE, "[update_proc]leave update mode.\n");
	gt917d_hw_reset(dev);

	touch_msleep(100);

	if (client->addr == 0x14) {
		client->addr =  0x5D;
		TOUCH_I("Change slave addr 0x5d\n");
	}

	if (update_ret == SUCCESS) {
		TOUCH_I("firmware auto update success, resent config!\n");
		gt917d_init_panel(dev);
	}
	gt917d_get_fw_info(dev);

file_fail:

	update_msg.fw_data = NULL;
	update_msg.fw_total_len = 0;

	release_firmware(update_msg.fw);

	clear_bit(FW_UPDATE_RUNNING, &d->flags);

	total_len = 100;
	if (update_ret == SUCCESS) {
		show_len = 100;
		clear_bit(FW_ERROR, &d->flags);
		return SUCCESS;
	} else {
		show_len = 200;
		return FAIL;
	}
}

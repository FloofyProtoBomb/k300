/* touch_gt917d.h
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

#ifndef LGE_TOUCH_GT917D_H
#define LGE_TOUCH_GT917D_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/dma-mapping.h>
#include <linux/async.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

/*
 *  Include to touch core Header File
 */
#include <touch_core.h>
#include <touch_hwif.h>

#define GT917D_TOOL_FINGER		2
#define GT917D_ADDR_LENGTH		2
#define GT917D_MAX_TOUCH_ID		16
#define GT917D_DRIVER_VERSION		"V2.8.0.2<2017/12/14>"
#define GT91XX_CONFIG_PROC_FILE		"gt9xx_config"
#define GT917D_CONFIG_MIN_LENGTH	186
#define GT917D_CONFIG_MAX_LENGTH	240
#define GT917D_ESD_CHECK_VALUE		0xAA

#define RETRY_MAX_TIMES		5
#define MASK_BIT_8		0x80
#define FAIL			0
#define SUCCESS			1

#define GT917D_REG_COMMAND		0x8040
#define GT917D_REG_COMMAND_CHECK	0x8046
#define GT917D_REG_CONFIG_DATA		0x8047
#define GT917D_REG_VERSION		0x8140
#define GT917D_REG_SENSOR_ID		0x814A
#define GT917D_REG_DOZE_BUF		0x814B
#define GT917D_REG_KNOCKON_ERR_REASON	0xc33D
#define GT917D_READ_COOR_ADDR		0x814E
#define GT917D_REG_FW_MSG		0x41E4

#define GESTURE_DOUBLECLICK	0xCC
#define GESTURE_UP		0xBA
#define GESTURE_LEFT		0xBB
#define GESTURE_RIGHT		0xAA
#define KNOCKON_ERROR		0xF0
#define SWIPE_ERROR		0xF1

#define GESTURE_DOUBLECLICK_EN	(1 << 4)
#define GESTURE_RIGHT_EN	(1 << 5)
#define GESTURE_UP_EN		(1 << 6)
#define GESTURE_LEFT_EN		(1 << 7)

#define GESTURE_SWITCH		0x80A5

/* swipe common */
#define SWIPE_TIME		0x810A	/* MIN_TIME:bit7~4 , MAX_TIME:bit3~0 */
#define SWIPE_WRONG_DIR		0x80AE
#define SWIPE_INIT_DISTANCE	0x80A2
/* swipe up */
#define SWIPE_U_DISTANCE	0x80A8
#define SWIPE_U_RATIO		0x80AC
#define SWIPE_U_INIT_RATIO	0x80AD
#define SWIPE_U_AREA_X1		0x806F
#define SWIPE_U_AREA_X2		0x8070
#define SWIPE_U_AREA_Y1		0x8071
#define SWIPE_U_AREA_Y2		0x8072
#define SWIPE_U_START_AREA_X1	0x8093
#define SWIPE_U_START_AREA_X2	0x8094
#define SWIPE_U_START_AREA_Y1	0x8095
#define SWIPE_U_START_AREA_Y2	0x8096
/* swipe left,right */
#define SWIPE_LR_DISTANCE	0x80A7
#define SWIPE_LR_RATIO		0x80AB
#define SWIPE_LR_INIT_RATIO	0x80A1
#define SWIPE_LR_AREA_X1	0x8104
#define SWIPE_LR_AREA_X2	0x8105
#define SWIPE_LR_AREA_Y1	0x8106
#define SWIPE_LR_AREA_Y2	0x8107
#define SWIPE_L_START_AREA_X1	0x8097
#define SWIPE_L_START_AREA_X2	0x8098
#define SWIPE_L_START_AREA_Y1	0x8099
#define SWIPE_L_START_AREA_Y2	0x809A
#define SWIPE_R_START_AREA_X1	0x809B
#define SWIPE_R_START_AREA_X2	0x809C
#define SWIPE_R_START_AREA_Y1	0x809D
#define SWIPE_R_START_AREA_Y2	0x80A0

#define _bRW_MISCTL__SRAM_BANK          0x4048
#define _bRW_MISCTL__MEM_CD_EN          0x4049
#define _bRW_MISCTL__CACHE_EN           0x404B
#define _bRW_MISCTL__TMR0_EN            0x40B0
#define _rRW_MISCTL__SWRST_B0_          0x4180
#define _bWO_MISCTL__CPU_SWRST_PULSE    0x4184
#define _rRW_MISCTL__BOOTCTL_B0_        0x4190
#define _rRW_MISCTL__BOOT_OPT_B0_       0x4218
#define _rRW_MISCTL__BOOT_CTL_          0x5094
#define _rRW_MISCTL__SHORT_BOOT_FLAG    0x5095

enum {
	ADD_ON_GT917D = 2,
};

enum {
	WORK_THREAD_ENABLED = 0,
	HRTIMER_USED,
	FW_ERROR,
	DOZE_MODE,
	SLEEP_MODE,
	POWER_OFF_MODE,
	RAW_DATA_MODE,
	FW_UPDATE_RUNNING,
	PANEL_RESETTING,
	PANEL_ITO_TESTING
};

enum {
	TC_STATE_ACTIVE = 0,
	TC_STATE_LPWG,
	TC_STATE_DEEP_SLEEP,
	TC_STATE_POWER_OFF,
};

enum {
	IC_INIT_NEED = 0,
	IC_INIT_DONE,
};

enum {
	CHARGER_MODE = 0x06,
	NOT_CHARGER_MODE = 0x07,
};

enum {
	IME_MODE = 0x0C,
	NOT_IME_MODE = 0x0D,
};

enum {
	PALM_RELEASED = 0,
	PALM_PRESSED,
};

struct gt917d_point_t {
	int id;
	int x;
	int y;
	int w;
	int p;
	int tool_type;
};

struct gt917d_config_data {
	int length;
	u8 data[GT917D_CONFIG_MAX_LENGTH + GT917D_ADDR_LENGTH];
};

struct gt917d_fw_info {
	u8 pid[6];
	u8 ic_major_ver;
	u8 ic_minor_ver;
	u8 sensor_id;
};

struct gt917d_swipe_area {
	u8 x1;
	u8 x2;
	u8 y1;
	u8 y2;
};

struct gt917d_swipe_time {
	union {
		struct {
			u8 max:4;
			u8 min:4;
		} __packed;
		u8 min_max;
	};
};

struct gt917d_swipe_common_ctrl {
	u8 enable;
	struct gt917d_swipe_time time;
	u8 wrong_dir;
	u8 init_distance;
};

struct gt917d_swipe_up_ctrl {
	u8 distance;
	u8 ratio;
	u8 init_ratio;
	struct gt917d_swipe_area area;
	struct gt917d_swipe_area start_area;
};

struct gt917d_swipe_left_right_ctrl {
	u8 distance;
	u8 ratio;
	u8 init_ratio;
	struct gt917d_swipe_area area;
	struct gt917d_swipe_area left_start_area;
	struct gt917d_swipe_area right_start_area;
};

struct gt917d_swipe_ctrl {
	struct gt917d_swipe_common_ctrl common;
	struct gt917d_swipe_up_ctrl u;
	struct gt917d_swipe_left_right_ctrl l_r;
};

struct gt917d_data {
	struct device *dev;
	struct i2c_client *client;
	struct gt917d_config_data config;
	struct gt917d_fw_info fw_info;
	struct gt917d_swipe_ctrl swipe;
	struct mutex rw_lock;
	unsigned long flags; /* This member record the device status */
	u8 state;
	atomic_t init;
	bool palm;
};

static inline struct gt917d_data *to_gt917d_data(struct device *dev)
{
	return (struct gt917d_data *)touch_get_device(to_touch_core(dev));
}

extern int gt917d_reg_read(struct device *dev, u16 addr, void *data, int size);
extern int gt917d_reg_write(struct device *dev, u16 addr, void *data, int size);
extern int gt917d_i2c_read(struct i2c_client *client, u8 *buf, int len);
extern int gt917d_i2c_write(struct i2c_client *client, u8 *buf, int len);
extern s32 gt917d_i2c_read_dbl_check(struct device *dev, u16 addr, u8 *rxbuf, int len);
extern int gt917d_get_fw_info(struct device *dev);
extern void gt917d_hw_reset(struct device *dev);
extern s32 gt917d_send_cfg(struct device *dev);
extern int gt917d_init_panel(struct device *dev);
extern int gt917d_wakeup(struct device *dev);
extern int gt917d_lpwg_control(struct device *dev, u8 mode);

extern int gt917d_prd_register_sysfs(struct device *dev);

extern s32 init_wr_node(struct i2c_client *client);
extern void uninit_wr_node(void);

extern u16 show_len;
extern u16 total_len;
extern s32 gt917d_enter_update_mode(struct device *dev);
extern s32 gt917d_update_proc(struct device *dev, void *dir);

#endif /* LGE_TOUCH_GT917D_H */

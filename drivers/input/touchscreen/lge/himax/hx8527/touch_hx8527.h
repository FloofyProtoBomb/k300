/* Himax Android Driver Sample Code for QCT platform
 *
 * Copyright (C) 2017 Himax Corporation.
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

#ifndef HIMAX_PLATFORM_H
#define HIMAX_PLATFORM_H

#include <linux/delay.h>
#ifndef CONFIG_FB
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#endif
#include <linux/types.h>
#include <linux/interrupt.h>

#include <asm/segment.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include <linux/input.h>

#include <linux/module.h>
#include <linux/async.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/input/mt.h>
#include <linux/firmware.h>
#include <linux/types.h>

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#if 0	// [bring up] fix build error
#include <soc/qcom/lge/lge_laf_mode.h>
#include <soc/qcom/lge/lge_boot_mode.h>
#endif	// [bring up] fix build error

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif

#define HIMAX_common_NAME	"himax_tp"
#define HIMAX_DRIVER_VER	"0.1.78.0_MH5_5"
#define FLASH_DUMP_FILE		"/sdcard/HX_Flash_Dump.bin"

#define HX_TOUCH_DATA_SIZE		128
#define HX_POINT_INFO_SIZE		4
#define HX_RAWDATA_HEADER_SIZE		4
#define HX_RAWDATA_CHECKSUM_SIZE	1

#define SHIFTBITS		5
#define DEFAULT_RETRY_CNT	3
#define HIMAX_REG_RETRY_TIMES	5
#define HIMAX_I2C_RETRY_TIMES	10
#define TS_WAKE_LOCK_TIMEOUT	(2 * HZ)

#define HX_VER_FW_MAJ		0x33
#define HX_VER_FW_MIN		0x32
#define HX_VER_FW_CFG		0x39

#define FW_SIZE_32k		32768
#define FW_SIZE_60k		61440
#define FW_SIZE_64k		65536
#define FW_SIZE_124k		126976
#define FW_SIZE_128k		131072

#define GEST_PTLG_ID_LEN	(4)
#define GEST_PTLG_HDR_LEN	(4)
#define GEST_PTLG_HDR_ID1	(0xCC)
#define GEST_PTLG_HDR_ID2	(0x44)
#define GEST_PT_EXTRA		(6)
#define GEST_PT_STATE_FR	(8)
#define GEST_PT_COUNT		(9)
#define	EV_GESTURE_PWR		0x80
#define EV_GESTURE_FR		0x81

#define NO_ERR			0
#define READY_TO_SERVE		1
#define WORK_OUT		2
#define I2C_FAIL		-1
#define MEM_ALLOC_FAIL		-2
#define CHECKSUM_FAIL		-3
#define ERR_WORK_OUT		-10

#define HX_CMD_TSSLPIN			0x80
#define HX_CMD_TSSLPOUT			0x81
#define HX_CMD_TSSOFF			0x82
#define HX_CMD_TSSON			0x83
#define HX_CMD_MANUALMODE		0x42
#define HX_CMD_FLASH_ENABLE		0x43
#define HX_CMD_FLASH_SET_ADDRESS	0x44
#define HX_CMD_FLASH_WRITE_REGISTER	0x45
#define HX_CMD_4A			0x4A
#define HX_FINGER_ON		1
#define HX_FINGER_LEAVE		2

#define HX_REPORT_COORD		1
#define HX_REPORT_SMWP_EVENT	2

//Himax: Set FW and CFG Flash Address
#define LGE_VER_MAJ_FLASH_ADDR	164
#define LGE_VER_MIN_FLASH_ADDR	166

#define FW_VER_MAJ_FLASH_ADDR 	133
#define FW_VER_MIN_FLASH_ADDR	134
#define FW_VER_MAJ_FLASH_LENG	1
#define FW_VER_MIN_FLASH_LENG	1

#define FW_CFG_VER_FLASH_ADDR	132

#define HX_RX_NUM		14
#define HX_TX_NUM		28
#define HX_RX_NUM_2		14
#define HX_TX_NUM_2		28

enum {
	DEEP_SLEEP_MODE = 0,
	LPWG_MODE,
	NORMAL_MODE,
};

enum {
	HW_RESET = 0,
	HW_ESD_RESET,
};

enum {
	IC_INIT_NEED = 0,
	IC_INIT_DONE,
};
enum {
	PALM_RELEASED = 0,
	PALM_DETECTED,
};
// [bring up] fix build error - satrt
enum {
	CONNECT_INVALID = 0,
	CONNECT_SDP,
	CONNECT_DCP,
	CONNECT_CDP,
	CONNECT_PROPRIETARY,
	CONNECT_FLOATED,
	CONNECT_HUB, /* SHOULD NOT change the value */
};

#if defined(CONFIG_LGE_TOUCH_CORE_MTK)
enum {
	CONNECT_CHARGER_UNKNOWN = 0,
	CONNECT_STANDARD_HOST,		/* USB : 450mA */
	CONNECT_CHARGING_HOST,
	CONNECT_NONSTANDARD_CHARGER,	/* AC : 450mA~1A */
	CONNECT_STANDARD_CHARGER,	/* AC : ~1A */
	CONNECT_APPLE_2_1A_CHARGER,	/* 2.1A apple charger */
	CONNECT_APPLE_1_0A_CHARGER,	/* 1A apple charger */
	CONNECT_APPLE_0_5A_CHARGER,	/* 0.5A apple charger */
	CONNECT_WIRELESS_CHARGER,
	CONNECT_DISCONNECTED,
};
#endif
// [bring up] fix build error - end
struct himax_ic_data {
	int vendor_fw_ver;
	int vendor_config_ver;
	int vendor_sensor_id;

	int lge_ver_major;
	int lge_ver_minor;
	uint8_t lge_checksum[4];
};

struct himax_report_data {
	int touch_all_size;
	int raw_cnt_max;
	int raw_cnt_rmd;
	int touch_info_size;
	int event_size;
	int rawdata_size;

	uint8_t diag_cmd;
	uint8_t	finger_num;
	uint8_t hx_state_info[2];
	uint8_t rawdata_frame_size;

	uint8_t *hx_coord_buf;
	uint8_t *hx_event_buf;
	uint8_t *hx_rawdata_buf;
};

struct himax_ts_data {
	uint8_t useScreenRes;
	uint8_t diag_command;
	uint8_t first_pressed;

	uint8_t point_coord_size;
	uint8_t point_area_size;
	uint8_t point_num_offset;

	int num_of_nodes;
	uint8_t raw_data_frame_size;
	uint8_t raw_data_nframes;

	uint8_t mode_state;
	uint8_t crc_flag;
	uint8_t usb_connected;
	uint8_t gesture_cust_en[16];

	uint8_t cable_config[2];

	uint16_t pre_finger_mask;

	uint32_t debug_log_level;
	uint32_t widthFactor;
	uint32_t heightFactor;

	int pre_finger_data[10][2];

	atomic_t init;
	atomic_t palm;
	uint8_t	ic_status;

	int finger_info_addr;

	struct device *dev;
	struct i2c_client *client;
	struct workqueue_struct *flash_wq;
	struct delayed_work flash_work;
	struct workqueue_struct *reset_wq;
	struct delayed_work reset_work;
	struct mutex rw_lock;
};

static inline struct himax_ts_data *to_himax_data(struct device *dev)
{
	return (struct himax_ts_data *)touch_get_device(to_touch_core(dev));
}

void hx8527_reload_config(struct device *dev);
void hx8527_esd_hw_reset(struct device *dev);
extern int hx8527_bus_read(struct device *dev, uint8_t command, uint8_t *data, uint8_t length);
extern int hx8527_bus_write(struct device *dev, uint8_t command, uint8_t *data, uint8_t length);
extern int hx8527_bus_write_command(struct device *dev, uint8_t command);
extern int hx8527_bus_master_write(struct device *dev, uint8_t *data, uint8_t length);
extern void hx8527_log_touch_data(struct device *dev, uint8_t *buf,struct himax_report_data *hx_touch_data);
void hx8527_log_touch_event(int x, int y ,int w, int loop_i, int touched);
void hx8527_log_touch_event_detail(struct himax_ts_data *d, int x, int y, int w, int loop_i, int touched, uint16_t old_finger);
extern void setMutualBuffer_2(void);
extern bool Is_2T2R;
extern int16_t *getMutualBuffer_2(void);
extern int16_t *getMutualBuffer(void);
extern uint8_t getFlashCommand(void);
extern uint8_t getDiagCommand(void);
extern void setMutualBuffer(void);
extern void hx8527_ts_flash_func(struct device *dev);
extern void setFlashBuffer(void);
extern void setSysOperation(uint8_t operation);
extern int hx8527_set_diag_cmd(struct device *dev, struct himax_ic_data *ic_data,struct himax_report_data *hx_touch_data);
extern int hx8527_register_sysfs(struct device *dev);
int hx8527_reset_ctrl(struct device *dev, int ctrl);
#endif

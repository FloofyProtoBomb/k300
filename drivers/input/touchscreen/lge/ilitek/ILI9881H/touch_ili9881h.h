/* touch_ili9881h.h
 *
 * Copyright (C) 2015 LGE.
 *
 * Author: PH1-BSP-Touch@lge.com
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

#ifndef LGE_TOUCH_ILI9881H_H
#define LGE_TOUCH_ILI9881H_H

#define CHIP_ID				0x9881
#define CHIP_TYPE			0x11
#define CORE_TYPE			0x03
#define CHIP_PID			0x98811103
#define TPD_WIDTH			2048

#define MAX_TOUCH_NUM			10

#define ILI9881_SLAVE_I2C_ADDR		0x41
#define ILI9881_ICE_MODE_ADDR		0x181062
#define ILI9881_PID_ADDR		0x4009C
#define ILI9881_WDT_ADDR		0x5100C
#define ILI9881_IC_RESET_ADDR		0x040050
#define ILI9881_IC_RESET_KEY		0x00019881
#define ILI9881_IC_RESET_GESTURE_ADDR	0x040054
#define ILI9881_IC_RESET_GESTURE_KEY	0xF38A94EF
#define ILI9881_IC_RESET_GESTURE_RUN	0xA67C9DFE

#define W_CMD_HEADER_SIZE		1
#define W_ICE_HEADER_SIZE		4
#define GLASS_ID_SIZE			5

#define CMD_NONE			0x00
#define CMD_GET_FRIMWARE_BUILD_VERSION	0x15
#define CMD_GET_TP_SIGNAL_INFORMATION	0x17
#define CMD_GET_TP_INFORMATION		0x20
#define CMD_GET_FIRMWARE_VERSION	0x21
#define CMD_GET_PROTOCOL_VERSION	0x22
#define CMD_GET_CORE_VERSION		0x23
#define CMD_GET_MCU_STOP_INTERNAL_DATA	0x25
#define CMD_GET_MCU_ON_INTERNAL_DATA	0x1F
#define CMD_GET_KEY_INFORMATION		0x27
#define CMD_GET_IC_STATUS		0x2A
#define CMD_GET_TC_STATUS		0x2B
#define CMD_MODE_CONTROL		0xF0
#define CMD_SET_CDC_INIT		0xF1
#define CMD_GET_CDC_DATA		0xF2
#define CMD_CDC_BUSY_STATE		0xF3
#define CMD_READ_DATA_CTRL		0xF6
#define CMD_I2C_UART			0x40
#define CMD_ICE_MODE_EXIT		0x1B
#define CMD_READ_MP_TEST_CODE_INFO	0xFE

#define CMD_CONTROL_OPTION		0x01
#define CONTROL_SENSE			0x01
#define CONTROL_SLEEP			0x02
#define CONTROL_SCAN_RATE		0x03
#define CONTROL_CALIBRATION		0x04
#define CONTROL_INTERRUPT_OUTPUT	0x05
#define CONTROL_GLOVE			0x06
#define CONTROL_STYLUS			0x07
#define CONTROL_TP_SCAN_MODE		0x08
#define CONTROL_SW_RESET		0x09
#define CONTROL_LPWG			0x0A
#define CONTROL_GESTURE			0x0B
#define CONTROL_PHONE_COVER		0x0C
#define CONTROL_FINGER_SENSE		0x0F
#define CONTROL_PROXIMITY		0x10
#define CONTROL_PLUG			0x11
#define CONTROL_GRIP_SUPPRESSION_X	0x12
#define CONTROL_GRIP_SUPPRESSION_Y	0x13
#define CONTROL_IME			0x14

#define FIRMWARE_UNKNOWN_MODE		0xFF
#define FIRMWARE_DEMO_MODE		0x00
#define FIRMWARE_TEST_MODE		0x01
#define FIRMWARE_DEBUG_MODE		0x02
#define FIRMWARE_I2CUART_MODE		0x03
#define FIRMWARE_GESTURE_MODE		0x04

#define DEMO_PACKET_ID			0x5A
#define DEBUG_PACKET_ID			0xA7
#define TEST_PACKET_ID			0xF2
#define GESTURE_PACKET_ID		0xAA
#define TCI_FAILREASON_PACKET_ID	0xAB
#define SWIPE_FAILREASON_PACKET_ID	0xAD
#define I2CUART_PACKET_ID		0x7A

#define GESTURE_DOUBLECLICK		0x58
#define GESTURE_UP			0x60
#define GESTURE_DOWN			0x61
#define GESTURE_LEFT			0x62
#define GESTURE_RIGHT			0x63
#define GESTURE_M			0x64
#define GESTURE_W			0x65
#define GESTURE_C			0x66
#define GESTURE_E			0x67
#define GESTURE_V			0x68
#define GESTURE_O			0x69
#define GESTURE_S			0x6A
#define GESTURE_Z			0x6B

#define DEBUG_MODE_PACKET_LENGTH	2048 //1308
#define DEBUG_MODE_PACKET_HEADER_LENGTH	5
#define DEBUG_MODE_PACKET_FRAME_LIMIT	1024
#define TEST_MODE_PACKET_LENGTH		1180
#define GESTURE_MODE_PACKET_INFO_LENGTH          170
#define GESTURE_MODE_PACKET_NORMAL_LENGTH        8

/* INT Function Registers */
#define INTR_BASED_ADDR 		0x48000
#define INTR1_ADDR   			INTR_BASED_ADDR+0x4
#define INTR1_REG_FLASH_INT_FLAG	BIT(25)

/* FLASH Control Function Registers */
#define FLASH_BASED_ADDR		0x41000
#define FLASH0_ADDR			FLASH_BASED_ADDR+0x0
#define FLASH0_REG_PRECLK_SEL		BIT(16)|BIT(17)|BIT(18)|BIT(19)|BIT(20)|BIT(21)|BIT(22)|BIT(23)
#define FLASH0_REG_FLASH_CSB		FLASH0_ADDR
#define FLASH0_REG_RX_DUAL          	BIT(24)
#define FLASH3_ADDR			FLASH_BASED_ADDR+0xC
#define FLASH3_REG_RCV_CNT		FLASH3_ADDR
#define FLASH4_ADDR			FLASH_BASED_ADDR+0x10
#define FLASH4_REG_FLASH_DMA_TRIGGER_EN	BIT(24)|BIT(25)|BIT(26)|BIT(27)|BIT(28)|BIT(29)|BIT(30)|BIT(31)
#define FLASH4_REG_RCV_DATA 		FLASH4_ADDR
#define CONNECT_NONE			(0x00)
#define CONNECT_USB			(0x01)
#define CONNECT_TA			(0x02)
#define CONNECT_OTG			(0x03)
#define CONNECT_WIRELESS		(0x10)

#define CHECK_EQUAL(X, Y) 		((X == Y) ? 0 : -1)
#define ERR_ALLOC_MEM(X)		((IS_ERR(X) || X == NULL) ? 1 : 0)
#define MIN(a,b)			(((a)<(b))?(a):(b))
#define MAX(a,b)			(((a)>(b))?(a):(b))

struct project_param {
	u8 touch_power_control_en;
	u8 touch_maker_id_control_en;
	u8 dsv_toggle;
	u8 deep_sleep_power_control;
	u8 dump_packet;
};

enum {
	MCU_ON = 0,
	MCU_STOP,
};

enum {
	SIGNAL_LOW_SAMPLE = 0,
	SIGNAL_HIGH_SAMPLE,
};

enum {
	LCD_MODE_U0 = 0,
	LCD_MODE_U2_UNBLANK,
	LCD_MODE_U2,
	LCD_MODE_U3,
	LCD_MODE_U3_PARTIAL,
	LCD_MODE_U3_QUICKCOVER,
	LCD_MODE_STOP,
};

#if defined(CONFIG_LGE_TOUCH_CORE_MTK)
enum {
	BOE_INCELL_ILI9881H = 0,
	CSOT_INCELL_FT8736,
	MH4_BOE_HX83102D,
};
#endif

enum {
	FUNC_OFF = 0,
	FUNC_ON,
};

enum {
	IC_INIT_NEED = 0,
	IC_INIT_DONE,
};

enum {
	CHANGING_DISPLAY_MODE_READY = 0,
	CHANGING_DISPLAY_MODE_NOT_READY,
};

enum {
	DEBUG_IDLE = 0,
	DEBUG_GET_DATA_DONE,
	DEBUG_GET_DATA,
};

enum {
	SW_RESET = 0,
	HW_RESET_ASYNC,
	HW_RESET_SYNC,
	HW_RESET_ONLY,
};

enum {
	IC_STATE_INIT = 0,
	IC_STATE_RESET,
	IC_STATE_WAKE,
	IC_STATE_SLEEP,
	IC_STATE_STANDBY,
};

enum {
	LPWG_FAILREASON_DISABLE = 0,
	LPWG_FAILREASON_ENABLE,
};

enum {
	SLEEP_IN = 0,
	SLEEP_OUT = 1,
	SLEEP_DEEP = 3,
};

struct ili9881h_touch_abs_data {
	u8 y_high:4;
	u8 x_high:4;
	u8 x_low;
	u8 y_low;
	u8 pressure;
} __packed;
/*
struct ili9881h_touch_shape_data {
	s8 degree;
	u8 width_major_high;
	u8 width_major_low;
	u8 width_minor_high;
	u8 width_minor_low;
} __packed;
*/
/* report packet */
struct ili9881h_touch_info {
	u8 packet_id;
	struct ili9881h_touch_abs_data abs_data[10];
	u8 p_sensor:4;
	u8 key:4;
	//struct ili9881h_touch_shape_data shape_data[10];
	u8 checksum;
} __packed;

struct ili9881h_debug_info {
	u8 data[DEBUG_MODE_PACKET_LENGTH];
	u8 buf[DEBUG_MODE_PACKET_FRAME_LIMIT][2048];
	u16 frame_cnt;
	bool enable;
} __packed;

struct ili9881h_gesture_info {
	u8 data[GESTURE_MODE_PACKET_INFO_LENGTH];
} __packed;

struct ili9881h_chip_info {
	u32 id;
	u32 type;
	u32 core_type;
};

struct ili9881h_fw_info {
	u8 command_id;
	u8 core;
	u8 customer_code;
	u8 major;
	u8 minor;
	// not used below
	u8 mp_major_core;
	u8 mp_core;
	u8 release_code;
	u8 test_code;
} __packed;

struct ili9881h_fw_extra_info {
	u8 build;
} __packed;
struct ili9881h_protocol_info {
	u8 command_id;
	u8 major;
	u8 mid;
	u8 minor;
} __packed;

struct ili9881h_core_info {
	u8 command_id;
	u8 code_base; // for algorithm modify
	u8 minor;
	u8 revision_major; // for tunning parameter change
	u8 revision_minor;
} __packed;

struct ili9881h_ic_info {
	struct ili9881h_chip_info chip_info;
	struct ili9881h_fw_info fw_info;
	struct ili9881h_fw_extra_info fw_extra_info;
	struct ili9881h_protocol_info protocol_info;
	struct ili9881h_core_info core_info;
};

struct ili9881h_panel_info {
	u8 command_id;
	u8 nMinX;
	u8 nMinY;
	u8 nMaxX_Low;
	u8 nMaxX_High;
	u8 nMaxY_Low;
	u8 nMaxY_High;
	u8 nXChannelNum;
	u8 nYChannelNum;
	u8 nMaxTouchPoint;
	u8 nTouchKeyNum;
	/* added for protocol v5 */
	u8 self_tx_channel_num;
	u8 self_rx_channel_num;
	u8 side_touch_type;
} __packed;

struct ili9881h_panel_extra_info {
	u8 glass_id[GLASS_ID_SIZE];
	u8 glass_info; //0: SIGNAL_LOW_SAMPLE, 1: SIGNAL_HIGH_SAMPLE
	u8 signal;
} __packed;


struct ili9881h_tp_info {
	struct ili9881h_panel_info panel_info;
	struct ili9881h_panel_extra_info panel_extra_info;
};

struct ili9881h_data {
	struct device *dev;
	struct kobject kobj;
	struct mutex io_lock;
	struct mutex apk_lock;
	struct mutex debug_lock;
	struct delayed_work fb_notify_work;

	struct project_param p_param;

	struct ili9881h_ic_info ic_info;
	struct ili9881h_tp_info tp_info;
	struct ili9881h_touch_info touch_info;
	struct ili9881h_debug_info debug_info;
	struct ili9881h_gesture_info gesture_info;

	atomic_t init;
	atomic_t reset;
	atomic_t changing_display_mode;
	wait_queue_head_t inq;

	u8 lcd_mode;
	bool charger;
	u16 actual_fw_mode;
	u8 lpwg_failreason_ctrl;
	u8 err_cnt;
};

extern int ili9881h_switch_fw_mode(struct device *dev, u8 mode);
extern int ili9881h_ice_mode_disable(struct device *dev);
extern int ili9881h_ice_mode_enable(struct device *dev, int mcu_status);
extern int ili9881h_ice_reg_write(struct device *dev, u32 addr, u32 data, int size);
extern int ili9881h_ice_reg_read(struct device *dev, u32 addr, void* data, int size);
extern int ili9881h_reg_read(struct device *dev, u8 cmd, void *data, int size);
extern int ili9881h_reg_write(struct device *dev, u8 cmd, void *data, int size);
extern int ili9881h_tp_info(struct device *dev);
extern int ili9881h_reset_ctrl(struct device *dev, int ctrl);
extern int ili9881h_check_cdc_busy(struct device *dev, int conut, int delay);
extern void ili9881h_dump_packet(void *data, int type, int len, int row_len, const char *name);
extern u8 ili9881h_calc_data_checksum(void *pMsg, u32 nLength);
// int ili9881h_ic_info(struct device *dev);
// int ili9881h_tc_driving(struct device *dev, int mode);
// int ili9881h_irq_abs(struct device *dev);
// int ili9881h_irq_lpwg(struct device *dev);
// int ili9881h_irq_handler(struct device *dev);
// int ili9881h_check_status(struct device *dev);
// int ili9881h_debug_info(struct device *dev);

static inline struct ili9881h_data *to_ili9881h_data(struct device *dev)
{
	return (struct ili9881h_data *)touch_get_device(to_touch_core(dev));
}

static inline struct ili9881h_data *to_ili9881h_data_from_kobj(struct kobject *kobj)
{
	return (struct ili9881h_data *)container_of(kobj,
			struct ili9881h_data, kobj);
}
static inline int ili9881h_read_value(struct device *dev,
		u16 addr, u32 *value)
{
	return ili9881h_reg_read(dev, addr, value, sizeof(*value));
}

static inline int ili9881h_write_value(struct device *dev,
		u16 addr, u32 value)
{
	return ili9881h_reg_write(dev, addr, &value, sizeof(value));
}

static inline void ipio_kfree(void **mem)
{
	if(*mem != NULL) {
		kfree(*mem);
		*mem = NULL;
	}
}

/* extern */
#if defined(CONFIG_LGE_TOUCH_CORE_QCT)
extern int check_recovery_boot;
#endif

#if defined(CONFIG_LGE_MODULE_DETECT)
extern int panel_id;
#endif

/* Extern our apk api for LG */
extern int ili9881h_apk_init(struct device *dev);

#endif /* LGE_TOUCH_ILI9881H_H */

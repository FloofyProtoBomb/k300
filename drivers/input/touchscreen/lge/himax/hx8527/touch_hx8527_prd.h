/* Himax Android Driver Sample Code for debug nodes
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
#ifndef H_HIMAX_DEBUG
#define H_HIMAX_DEBUG

#include "touch_hx8527.h"

#define ROW_SIZE		28
#define COL_SIZE		14
#define M1_COL_SIZE		2
#define LOG_BUF_SIZE		500
#define BUF_SIZE (PAGE_SIZE * 2)
#define MAX_LOG_FILE_SIZE	(10 * 1024 * 1024) /* 10 M byte */
#define MAX_LOG_FILE_COUNT	(4)
#define MUTUAL_NUM (ROW_SIZE * COL_SIZE)
#define SELF_NUM (ROW_SIZE + COL_SIZE)
#define MUL_RAW_DATA_THX_UP 240
#define MUL_RAW_DATA_THX_LOW 140
#define SELF_RAW_DATA_THX_UP 140
#define SELF_RAW_DATA_THX_LOW 5
#define JITTER_THX 50

enum {
	RAW_DATA_TEST = 0,
	JITTER_TEST,
	OPEN_SHORT_TEST,
	LPWG_RAW_DATA_TEST,
	LPWG_JITTER_TEST,
	LPWG_OPEN_SHORT_TEST,
};

enum {
	TIME_INFO_SKIP,
	TIME_INFO_WRITE,
};

int handshaking_result = 0;
int g_diag_command = 0;
bool	fw_update_complete = false;

uint8_t register_command[4];
uint8_t RB1H[8] = {0x00, 0x00, 0xf0, 0x0f, 0xc8, 0x30, 0xf8, 0x0f}; //Sync with WingTech code
uint8_t g_os_fail_num[4]; /* mut_o,mut_s,self_o,self_s */
unsigned char debug_level_cmd = 0;
unsigned char upgrade_fw[128*1024];

void setMutualBuffer(void);
void setMutualBuffer_2(void);

uint8_t getDiagCommand(void);
uint8_t getFlashCommand(void);
uint8_t getSysOperation(void);
uint8_t hx_state_info[2] = {0};
uint8_t diag_coor[128];// = {0xFF};
int16_t diag_self[100] = {0};
int16_t *diag_mutual = NULL;
int16_t *getMutualBuffer(void);
int16_t *getMutualBuffer_2(void);
int16_t *getSelfBuffer(void);

static uint16_t *diag_mutual_2 = NULL;

static int Flash_Size = 131072;
static bool flash_dump_going = false;
static void setFlashCommand(uint8_t command);
static void setFlashReadStep(uint8_t step);
static uint8_t *flash_buffer = NULL;
static uint8_t flash_command = 0;
static uint8_t flash_read_step = 0;
static uint8_t flash_progress = 0;
static uint8_t flash_dump_complete = 0;
static uint8_t flash_dump_fail = 0;
static uint8_t sys_operation = 0;
static uint8_t getFlashDumpComplete(void);
static uint8_t getFlashDumpFail(void);
static uint8_t getFlashDumpProgress(void);
static uint8_t getFlashReadStep(void);

void setFlashBuffer(void);
void setFlashDumpComplete(uint8_t complete);
void setFlashDumpFail(uint8_t fail);
void setFlashDumpProgress(uint8_t progress);
void setSysOperation(uint8_t operation);
void setFlashDumpGoing(bool going);
void himax_get_mutual_edge(void);

extern int hx8527_hand_shaking(void);
extern int hx8527_FlashMode(struct device *dev, int enter);
extern void hx8527_sensor_on(struct device *dev);
extern void hx8527_sensor_off(struct device *dev);
extern int fts_ctpm_fw_upgrade_with_sys_fs_32k(struct device *dev, unsigned char *fw, int len);
extern int hx8527_reset_ctrl(struct device *dev, int ctrl);
extern int hx8527_power(struct device *dev, int ctrl);
extern void hx8527_reload_config(struct device *dev);
#endif



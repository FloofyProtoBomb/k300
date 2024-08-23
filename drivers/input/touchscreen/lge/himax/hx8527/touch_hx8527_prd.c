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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/types.h>
#include <touch_core.h>
#include <touch_hwif.h>

#include "touch_hx8527_prd.h"

/*#define USE_LPWG_MUTUAL*/

int g_max_mutual = 0;
int g_min_mutual = 255;
int g_max_self = 0;
int g_min_self = 255;

int mul_min = 9999;
int mul_max = 0;
int self_min = 9999;
int self_max = 0;
int g_self_test_entered = 0;
static int Selftest_flag;

bool cfg_flag = false;
bool Is_2T2R = false;
bool raw_data_chk_arr[20] = {false};

uint16_t *mutual_bank;
uint16_t *self_bank;
uint8_t g_diag_arr_num = 0;
uint8_t byte_length = 0;
uint8_t test_counter = 0;
uint8_t TEST_DATA_TIMES = 3;

struct timespec timeStart, timeEnd, timeDelta;
extern struct himax_ic_data* ic_data;

static char W_Buf[BUF_SIZE];
static u16 Rawdata_buf[ROW_SIZE * COL_SIZE + ROW_SIZE + COL_SIZE];
static u16 open_short_buf[ROW_SIZE * COL_SIZE + ROW_SIZE + COL_SIZE];
static u16 MutualLowerImage[ROW_SIZE][COL_SIZE];
static u16 MutualUpperImage[ROW_SIZE][COL_SIZE];
static u16 SelfLowerImage[ROW_SIZE + COL_SIZE];
static u16 SelfUpperImage[ROW_SIZE + COL_SIZE];

//=============================================================================================================
//
//	Segment : Himax PROC Debug Function
//
//=============================================================================================================

static void hx8527_log_size_check(struct device *dev)
{
	char *fname = NULL;
	struct file *file;
	loff_t file_size = 0;
	int i = 0;
	char buf1[128] = {0};
	char buf2[128] = {0};
	mm_segment_t old_fs = get_fs();
	int ret = 0;
	int boot_mode = 0;

	set_fs(KERNEL_DS);

//	boot_mode = touch_boot_mode_check(dev);	// [bring up] fix build error

	switch (boot_mode) {
		case TOUCH_NORMAL_BOOT:
			fname = "/sdcard/touch_self_test.txt";
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
		file = filp_open(fname, O_RDONLY, 0666);
		sys_chmod(fname, 0666);
	} else {
		TOUCH_E("%s : fname is NULL, can not open FILE\n",
				__func__);
		goto error;
	}

	if (IS_ERR(file)) {
		TOUCH_I("%s : ERR(%ld) Open file error [%s]\n",
				__func__, PTR_ERR(file), fname);
		goto error;
	}

	file_size = vfs_llseek(file, 0, SEEK_END);
	TOUCH_I("%s : [%s] file_size = %lld\n",
			__func__, fname, file_size);

	filp_close(file, 0);

	if (file_size > MAX_LOG_FILE_SIZE) {
		TOUCH_I("%s : [%s] file_size(%lld) > MAX_LOG_FILE_SIZE(%d)\n",
				__func__, fname, file_size, MAX_LOG_FILE_SIZE);

		for (i = MAX_LOG_FILE_COUNT - 1; i >= 0; i--) {
			if (i == 0)
				sprintf(buf1, "%s", fname);
			else
				sprintf(buf1, "%s.%d", fname, i);

			ret = sys_access(buf1, 0);

			if (ret == 0) {
				TOUCH_I("%s : file [%s] exist\n",
						__func__, buf1);

				if (i == (MAX_LOG_FILE_COUNT - 1)) {
					if (sys_unlink(buf1) < 0) {
						TOUCH_E("%s : failed to remove file [%s]\n",
								__func__, buf1);
						goto error;
					}

					TOUCH_I("%s : remove file [%s]\n",
							__func__, buf1);
				} else {
					sprintf(buf2, "%s.%d",
							fname,
							(i + 1));

					if (sys_rename(buf1, buf2) < 0) {
						TOUCH_E("%s : failed to rename file [%s] -> [%s]\n",
								__func__, buf1, buf2);
						goto error;
					}

					TOUCH_I("%s : rename file [%s] -> [%s]\n",
							__func__, buf1, buf2);
				}
			} else {
				TOUCH_I("%s : file [%s] does not exist (ret = %d)\n",
						__func__, buf1, ret);
			}
		}
	}

error:
	set_fs(old_fs);
	return;
}
static void hx8527_write_file(struct device *dev, char *data, int write_time)
{
	int fd = 0;
	char *fname = NULL;
	char time_string[64] = {0};
	struct timespec my_time;
	struct tm my_date;
	mm_segment_t old_fs = get_fs();
	int boot_mode = 0;

	set_fs(KERNEL_DS);

//	boot_mode = touch_boot_mode_check(dev);	// [bring up] fix build error

	switch (boot_mode) {
		case TOUCH_NORMAL_BOOT:
			fname = "/sdcard/touch_self_test.txt";
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
		fd = sys_open(fname, O_WRONLY|O_CREAT|O_APPEND, 0666);
		sys_chmod(fname, 0666);
	} else {
		TOUCH_E("%s : fname is NULL, can not open FILE\n", __func__);
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

static ssize_t hx8527_mutual_data_read(size_t count, char *buf)
{
	int i, j;

	for(j = 0;j < HX_TX_NUM;j++)
	{
		count += snprintf(buf + count, PAGE_SIZE - count, "%c%2d%c%c",'[', j + 1,']',' ');
		for (i = 0; i < HX_RX_NUM; i++)
		{
			count += snprintf(buf + count, PAGE_SIZE - count, "%5d", diag_mutual[ i + j*HX_RX_NUM]);
		}
		count += snprintf(buf + count, PAGE_SIZE - count, "\n");
	}

	return count;
}

static ssize_t hx8527_self_mutual_data_read(size_t count, char *buf)
{
	int i, j;

	for(j = 0;j < HX_TX_NUM;j++)
	{
		count += snprintf(buf + count, PAGE_SIZE - count, "%c%2d%c%c",'[', j,']',' ');
		for (i = 0; i < HX_RX_NUM; i++)
		{
			count += snprintf(buf + count, PAGE_SIZE - count, "%5d", diag_mutual[ i + j*HX_RX_NUM]);
		}
		count += snprintf(buf + count, PAGE_SIZE - count, " %5d\n", diag_self[j + HX_RX_NUM]);
	}
	count += snprintf(buf + count, PAGE_SIZE - count, "%s","[ S] ");
	for (i = 0; i < HX_RX_NUM; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%5d", diag_self[i]);
	count += snprintf(buf + count, PAGE_SIZE - count, "\n");

	return count;
}

void hx8527_diag_register_set(struct device *dev, uint8_t diag_command)
{
	uint8_t command_F1h[2] = {0xF1, 0x00};

	command_F1h[1] = diag_command;

	hx8527_bus_write(dev, command_F1h[0] ,&command_F1h[1], 1);
}

static ssize_t show_rawdata(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int count = 0;

	g_diag_command = 2;
	hx8527_diag_register_set(dev, 2);

	if (ts->lpwg.screen == 0){
		msleep(500);
	} else {
		msleep(250);
	}

	count = hx8527_mutual_data_read(count, buf);
	himax_get_mutual_edge();
	count += snprintf(buf + count, PAGE_SIZE - count, "==================================================\n");
	if (g_max_mutual>MUL_RAW_DATA_THX_UP)
		count += snprintf(buf + count, PAGE_SIZE - count, "Test %s\n", "FAIL : Exceed upper threshold");
	else if (g_min_mutual<MUL_RAW_DATA_THX_LOW)
		count += snprintf(buf + count, PAGE_SIZE - count, "Test %s\n", "FAIL : Exceed lower threshold");
	else
		count += snprintf(buf + count, PAGE_SIZE - count, "Test %s\n", "PASS : No Errors");
	count += snprintf(buf + count, PAGE_SIZE - count, "MAX = %3d, MIN = %3d, Upper = %3d, Lower =%3d\n",g_max_mutual,g_min_mutual,MUL_RAW_DATA_THX_UP,MUL_RAW_DATA_THX_LOW);
	g_max_mutual = 0;
	g_min_mutual = 255;
	hx8527_diag_register_set(dev, 0);
	msleep(50);
	g_diag_command = 0;

	return count;
}

static ssize_t show_delta(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int count = 0;

	g_diag_command = 1;
	hx8527_diag_register_set(dev, 1);

	if (ts->lpwg.screen == 0){
		msleep(500);
	} else {
		msleep(250);
	}

	count = hx8527_mutual_data_read(count, buf);
	himax_get_mutual_edge();
	count += snprintf(buf + count, PAGE_SIZE - count, "==================================================\n");
	count += snprintf(buf + count, PAGE_SIZE - count, "Test %s\n", "PASS : No Errors"); //to be implement pass fail condition
	count += snprintf(buf + count, PAGE_SIZE - count, "MAX = %3d, MIN = %3d, Upper = %3d, Lower =%3d\n",g_max_mutual,g_min_mutual,0,0);
	g_max_mutual = 0;
	g_min_mutual = 255;
	hx8527_diag_register_set(dev, 0);
	msleep(50);
	g_diag_command = 0;

	return count;
}

static ssize_t show_jitter(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int count = 0;

	g_diag_command = 7;
	hx8527_diag_register_set(dev, 0x17);

	if (ts->lpwg.screen == 0){
		msleep(1600);
	} else {
		msleep(800);
	}

	count = hx8527_mutual_data_read(count, buf);
	himax_get_mutual_edge();
	count += snprintf(buf + count, PAGE_SIZE - count, "==================================================\n");
	if (g_max_mutual>JITTER_THX)
		count += snprintf(buf + count, PAGE_SIZE - count, "Test %s\n", "FAIL : Exceed upper threshold");
	else if (g_min_mutual<0)
		count += snprintf(buf + count, PAGE_SIZE - count, "Test %s\n", "FAIL : Exceed lower threshold");
	else
		count += snprintf(buf + count, PAGE_SIZE - count, "Test %s\n", "PASS : No Errors"); //to be implement pass fail condition
	count += snprintf(buf + count, PAGE_SIZE - count, "MAX = %3d, MIN = %3d, Upper = %3d, Lower =%3d\n",g_max_mutual,g_min_mutual,JITTER_THX,0);
	g_max_mutual = 0;
	g_min_mutual = 255;

	hx8527_diag_register_set(dev, 0);
	msleep(50);
	g_diag_command = 0;

	return count;
}

static ssize_t show_rawdata_sm(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int count = 0;

	g_diag_command = 2;
	hx8527_diag_register_set(dev, 2);

	if (ts->lpwg.screen == 0){
		msleep(500);
	} else {
		msleep(250);
	}

	count += snprintf(buf + count, PAGE_SIZE - count, "====== self + mutual rawdata ======\n");
	count = hx8527_self_mutual_data_read(count, buf);

	hx8527_diag_register_set(dev, 0);
	msleep(50);

	g_diag_command = 0;

	return count;
}

static ssize_t show_delta_sm(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int count = 0;

	memset(diag_mutual, 0x00, HX_RX_NUM * HX_TX_NUM * sizeof(int16_t));
	memset(diag_self, 0x00, sizeof(diag_self));

	g_diag_command = 1;
	hx8527_diag_register_set(dev, 1);

	if (ts->lpwg.screen == 0){
		msleep(500);
	} else {
		msleep(250);
	}

	count += snprintf(buf + count, PAGE_SIZE - count, "====== self + mutual delta ======\n");
	count = hx8527_self_mutual_data_read(count, buf);

	hx8527_diag_register_set(dev, 0);
	msleep(50);
	g_diag_command = 0;

	return count;
}

static ssize_t show_jitter_sm(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int count = 0;

	g_diag_command = 7;
	hx8527_diag_register_set(dev, 0x17);

	if (ts->lpwg.screen == 0){
		msleep(1600);
	} else {
		msleep(800);
	}

	count += snprintf(buf + count, PAGE_SIZE - count, "====== self + mutual delta ======\n");
	count = hx8527_self_mutual_data_read(count, buf);

	hx8527_diag_register_set(dev, 0);
	msleep(50);
	g_diag_command = 0;

	return count;
}

int hx8527_read_IC_power_mode(struct device *dev)
{
	char R84H[5] = {0};

	if (hx8527_bus_read(dev, 0x84, R84H, 4) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return -1;
	}

	if ((((R84H[0] & 0x10) >> 4) == 0) && ((R84H[0] & 0x01) == 1) && (((R84H[1] & 0x02) >> 1) == 1)) //Normal Active mode
	{
		TOUCH_I("%s: IC Power mode - Normal\n", __func__);
		return 0;
	}
	else if ((((R84H[0] & 0x10) >> 4) == 1) && ((R84H[0] & 0x01) == 1) && (((R84H[1] & 0x02) >> 1) == 1)) //LPWG mode
	{
		TOUCH_I("%s: IC Power mode - LPWG\n", __func__);
		return 1;
	}
	else if (((R84H[0] & 0x01) == 0) && (((R84H[1] & 0x02) >> 1) == 0)) // ULP mode
	{
		TOUCH_I("%s: IC Power mode - ULP\n", __func__);
		return 2;
	}
	else
	{
		TOUCH_I("%s: IC Power mode - Not defined. (%d), (%d).\n", __func__, R84H[0], R84H[1]);
		return -1;
	}
}

static ssize_t show_himax_power_mode(struct device *dev, char *buf)
{
	ssize_t ret = 0;
	int power_mode = 0;

	power_mode = hx8527_read_IC_power_mode(dev);

	if (power_mode == 0) {
		ret += sprintf(buf, "Normal Mode\n");
	}
	else if (power_mode == 1) {
		ret += sprintf(buf, "LPWG Mode\n");
	}
	else if (power_mode == 2) {
		ret += sprintf(buf, "ULP Mode\n");
	}
	else {
		ret += sprintf(buf, "Fail - Unknown Mode\n");
	}

	return ret;
}


static ssize_t show_himax_debug_level(struct device *dev, char *buf)
{
	struct himax_ts_data *d = to_himax_data(dev);
	size_t count = 0;

	count = sprintf(buf, "%d\n", d->debug_log_level);

	return count;
}

static ssize_t store_himax_debug_level(struct device *dev,const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	char buf_tmp[11];
	int i;

	if (count >= 12)
	{
		TOUCH_I("%s: no command exceeds 12 chars.\n", __func__);
		return -EFAULT;
	}

	memcpy(buf_tmp, buf, count);

	d->debug_log_level = 0;
	for(i=0; i<count-1; i++)
	{
		if( buf_tmp[i]>='0' && buf_tmp[i]<='9' )
			d->debug_log_level |= (buf_tmp[i]-'0');
		else if( buf_tmp[i]>='A' && buf_tmp[i]<='F' )
			d->debug_log_level |= (buf_tmp[i]-'A'+10);
		else if( buf_tmp[i]>='a' && buf_tmp[i]<='f' )
			d->debug_log_level |= (buf_tmp[i]-'a'+10);

		if(i!=count-2)
			d->debug_log_level <<= 4;
	}

	if (d->debug_log_level & BIT(3)) {
		d->widthFactor = (ts->caps.max_x << SHIFTBITS)/(ts->tci.area.x2 - ts->tci.area.x1);
		d->heightFactor = (ts->caps.max_y << SHIFTBITS)/(ts->tci.area.y2 - ts->tci.area.y2);
		d->useScreenRes = 1;
	} else {
		d->widthFactor = 0;
		d->heightFactor = 0;
		d->useScreenRes = 0;
	}

	return count;
}

void hx8527_register_read(struct device *dev, uint8_t *read_addr, int read_length, uint8_t *read_data, bool cfg_flag)
{
	uint8_t outData[4];

	if (cfg_flag)
	{
		outData[0] = 0x14;
		hx8527_bus_write(dev, 0x8C,&outData[0], 1);

		msleep(10);

		outData[0] = 0x00;
		outData[1] = read_addr[0];
		hx8527_bus_write(dev, 0x8B,&outData[0], 2);
		msleep(10);

		hx8527_bus_read(dev, 0x5A, read_data, read_length);
		msleep(10);

		outData[0] = 0x00;
		hx8527_bus_write(dev, 0x8C,&outData[0], 1);
	}
	else
	{
		hx8527_bus_read(dev, read_addr[0], read_data, read_length);
	}
}

static ssize_t show_himax_proc_register(struct device *dev, char *buf)
{
	int ret = 0;
	uint16_t loop_i;
	uint8_t data[128];

	memset(data, 0x00, sizeof(data));

	TOUCH_I("himax_register_show: %02X,%02X,%02X,%02X\n", register_command[3],register_command[2],register_command[1],register_command[0]);
	hx8527_register_read(dev, register_command, 128, data, cfg_flag);

	ret += sprintf(buf, "command:  %02X,%02X,%02X,%02X\n", register_command[3],register_command[2],register_command[1],register_command[0]);

	for (loop_i = 0; loop_i < 128; loop_i++) {
		ret += sprintf(buf + ret, "0x%2.2X ", data[loop_i]);
		if ((loop_i % 16) == 15)
			ret += sprintf(buf + ret, "\n");
	}
	ret += sprintf(buf + ret, "\n");

	return ret;
}

void hx8527_register_write(struct device *dev, uint8_t *write_addr, int write_length, uint8_t *write_data, bool cfg_flag)
{
	uint8_t outData[4];

	if (cfg_flag)
	{
		outData[0] = 0x14;
		hx8527_bus_write(dev, 0x8C, &outData[0], 1);
		msleep(10);

		outData[0] = 0x00;
		outData[1] = write_addr[0];
		hx8527_bus_write(dev, 0x8B, &outData[0], 2);
		msleep(10);

		hx8527_bus_write(dev, 0x40, &write_data[0], write_length);
		msleep(10);

		outData[0] = 0x00;
		hx8527_bus_write(dev, 0x8C, &outData[0], 1);

		TOUCH_I("CMD: FE(%x), %x, %d\n", write_addr[0],write_data[0], write_length);
	}
	else
	{
		hx8527_bus_write(dev, write_addr[0], &write_data[0], write_length);
	}
}

static ssize_t store_himax_proc_register(struct device *dev,const char *buf, size_t count)
{
	char buf_tmp[16];
	uint8_t length = 0;
	unsigned long result    = 0;
	uint8_t loop_i          = 0;
	uint16_t base           = 2;
	char *data_str = NULL;
	uint8_t w_data[20];
	uint8_t x_pos[20];
	uint8_t temp_cnt = 0;

	if (count >= 80)
	{
		TOUCH_I("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}
	TOUCH_I("%s:buf = %s, line = %d\n",__func__,buf,__LINE__);
	memset(buf_tmp, 0x0, sizeof(buf_tmp));
	memset(w_data, 0x0, sizeof(w_data));
	memset(x_pos, 0x0, sizeof(x_pos));

	TOUCH_I("himax %s \n",buf);

	if ((buf[0] == 'r' || buf[0] == 'w') && buf[1] == ':' && buf[2] == 'x') {

		length = strlen(buf);

		for (loop_i = 0;loop_i < length; loop_i++) //find postion of 'x'
		{
			if(buf[loop_i] == 'x')
			{
				x_pos[temp_cnt] = loop_i;
				temp_cnt++;
			}
		}

		data_str = strrchr(buf, 'x');
		TOUCH_I("%s: %s.\n", __func__,data_str);
		length = strlen(data_str+1) - 1;

		if (buf[0] == 'r')
		{
			if (buf[3] == 'F' && buf[4] == 'E' && length == 4)
			{
				length = length - base;
				cfg_flag = true;
				memcpy(buf_tmp, data_str + base +1, length);
			}
			else{
				cfg_flag = false;
				memcpy(buf_tmp, data_str + 1, length);
			}

			byte_length = length/2;
			if (!kstrtoul(buf_tmp, 16, &result))
			{
				for (loop_i = 0 ; loop_i < byte_length ; loop_i++)
				{
					register_command[loop_i] = (uint8_t)(result >> loop_i*8);
				}
			}
		}
		else if (buf[0] == 'w')
		{
			if (buf[3] == 'F' && buf[4] == 'E')
			{
				cfg_flag = true;
				memcpy(buf_tmp, buf + base + 3, length);
			}
			else
			{
				cfg_flag = false;
				memcpy(buf_tmp, buf + 3, length);
			}
			if(temp_cnt < 3)
			{
				byte_length = length/2;
				if (!kstrtoul(buf_tmp, 16, &result)) //command
				{
					for (loop_i = 0 ; loop_i < byte_length ; loop_i++)
					{
						register_command[loop_i] = (uint8_t)(result >> loop_i*8);
					}
				}
				if (!kstrtoul(data_str+1, 16, &result)) //data
				{
					for (loop_i = 0 ; loop_i < byte_length ; loop_i++)
					{
						w_data[loop_i] = (uint8_t)(result >> loop_i*8);
					}
				}
				hx8527_register_write(dev, register_command, byte_length, w_data, cfg_flag);
			}
			else
			{
				byte_length = x_pos[1] - x_pos[0] - 2;
				for (loop_i = 0;loop_i < temp_cnt; loop_i++) //parsing addr after 'x'
				{
					memcpy(buf_tmp, buf + x_pos[loop_i] + 1, byte_length);
					//TOUCH_I("%s: buf_tmp = %s\n", __func__,buf_tmp);
					if (!kstrtoul(buf_tmp, 16, &result))
					{
						if(loop_i == 0)
						{
							register_command[loop_i] = (uint8_t)(result);
							//TOUCH_I("%s: register_command = %X\n", __func__,register_command[0]);
						}
						else
						{
							w_data[loop_i - 1] = (uint8_t)(result);
							//TOUCH_I("%s: w_data[%d] = %2X\n", __func__,loop_i - 1,w_data[loop_i - 1]);
						}
					}
				}

				byte_length = temp_cnt - 1;
				hx8527_register_write(dev, register_command, byte_length, &w_data[0], cfg_flag);
			}
		}
		else
			return count;
	}
	return count;
}

int16_t *getMutualBuffer(void)
{
	return diag_mutual;
}
int16_t *getSelfBuffer(void)
{
	return &diag_self[0];
}

uint8_t getDiagCommand(void)
{
	return g_diag_command;
}

void setMutualBuffer(void)
{
	diag_mutual = kzalloc(HX_RX_NUM * HX_TX_NUM * sizeof(int16_t), GFP_KERNEL);
}
int16_t *getMutualBuffer_2(void)
{
	return diag_mutual_2;
}
void setMutualBuffer_2(void)
{
	diag_mutual_2 = kzalloc(HX_RX_NUM_2 * HX_TX_NUM_2 * sizeof(int16_t), GFP_KERNEL);
}

bool hx8527_diag_check_sum( struct himax_report_data *hx_touch_data )
{
	uint16_t check_sum_cal = 0;
	int i;

	//Check 128th byte CRC
	for (i = 0, check_sum_cal = 0; i < (hx_touch_data->touch_all_size - hx_touch_data->touch_info_size); i++)
	{
		check_sum_cal += hx_touch_data->hx_rawdata_buf[i];
	}
	if (check_sum_cal % 0x100 != 0)
	{
		return 0;
	}
	return 1;
}

void hx8527_diag_raw_data(struct device *dev, struct himax_report_data *hx_touch_data,int mul_num, int self_num,uint8_t diag_cmd, int16_t *mutual_data, int16_t *self_data)
{
	int index = 0;
	int temp1, temp2,i;
	int cnt = 0;
	uint8_t command_F1h_bank[2] = {0xF1, 0x00};

	//Himax: Check Raw-Data Header
	if (hx_touch_data->hx_rawdata_buf[0] == hx_touch_data->hx_rawdata_buf[1]
			&& hx_touch_data->hx_rawdata_buf[1] == hx_touch_data->hx_rawdata_buf[2]
			&& hx_touch_data->hx_rawdata_buf[2] == hx_touch_data->hx_rawdata_buf[3]
			&& hx_touch_data->hx_rawdata_buf[0] > 0)
	{
		index = (hx_touch_data->hx_rawdata_buf[0] - 1) * hx_touch_data->rawdata_size;
		for (i = 0; i < hx_touch_data->rawdata_size; i++)
		{
			temp1 = index + i;

			if (temp1 < mul_num)
			{ //mutual
				mutual_data[index + i] = hx_touch_data->hx_rawdata_buf[i + 4]; //4: RawData Header
				if (Selftest_flag == 1)
				{
					raw_data_chk_arr[hx_touch_data->hx_rawdata_buf[0] - 1] = true;
				}
			}
			else
			{//self
				temp1 = i + index;
				temp2 = self_num + mul_num;

				if (temp1 >= temp2)
				{
					break;
				}

				self_data[i+index-mul_num] = hx_touch_data->hx_rawdata_buf[i + 4]; //4: RawData Header

				if (Selftest_flag == 1)
				{
					raw_data_chk_arr[hx_touch_data->hx_rawdata_buf[0] - 1] = true;
				}
			}
		}

		if (Selftest_flag == 1)
		{
			cnt = 0;
			for(i = 0;i < hx_touch_data->rawdata_frame_size; i++)
			{
				if(raw_data_chk_arr[i] == true)
					cnt++;
			}
			if (cnt == hx_touch_data->rawdata_frame_size)
			{
				test_counter++;
			}
			if(test_counter == TEST_DATA_TIMES)
			{
				memcpy(mutual_bank, mutual_data, mul_num * sizeof(uint16_t));
				memcpy(self_bank, self_data, self_num * sizeof(uint16_t));
				for(i = 0;i < hx_touch_data->rawdata_frame_size; i++)
				{
					raw_data_chk_arr[i] = false;
				}
				test_counter = 0;
				Selftest_flag = 0;
				command_F1h_bank[1] = 0x00;
				hx8527_bus_write(dev, command_F1h_bank[0] ,&command_F1h_bank[1], 1);
				msleep(20);
				g_diag_command = 0;
			}
		}
	}
}

int hx8527_set_diag_cmd(struct device *dev, struct himax_ic_data *ic_data,struct himax_report_data *hx_touch_data)
{
	struct himax_ts_data *d = to_himax_data(dev);
	int16_t *mutual_data;
	int16_t *self_data;
	int 	mul_num;
	int 	self_num;

	hx_touch_data->diag_cmd = getDiagCommand();
	if (d->diag_command)
	{
		//Check event stack CRC
		if(!hx8527_diag_check_sum(hx_touch_data))
		{
			goto bypass_checksum_failed_packet;
		}

		if(Is_2T2R && (hx_touch_data->diag_cmd >= 4 && hx_touch_data->diag_cmd <= 6))
		{
			mutual_data = getMutualBuffer_2();
			self_data 	= getSelfBuffer();

			// initiallize the block number of mutual and self
			mul_num = HX_RX_NUM_2 * HX_TX_NUM_2;
			self_num = HX_RX_NUM_2 + HX_TX_NUM_2;
		}
		else
		{
			mutual_data = getMutualBuffer();
			self_data 	= getSelfBuffer();

			// initiallize the block number of mutual and self
			mul_num = HX_RX_NUM * HX_TX_NUM;
			self_num = HX_RX_NUM + HX_TX_NUM;
		}

		hx8527_diag_raw_data(dev, hx_touch_data,mul_num, self_num,hx_touch_data->diag_cmd, mutual_data,self_data);

	}
	else if (hx_touch_data->diag_cmd == 8)
	{
		memset(diag_coor, 0x00, sizeof(diag_coor));
		memcpy(&(diag_coor[0]), &hx_touch_data->hx_coord_buf[0], hx_touch_data->touch_info_size);
	}
	//assign state info data
	memcpy(&(hx_state_info[0]), &hx_touch_data->hx_state_info[0], 2);

	return NO_ERR;

bypass_checksum_failed_packet:
	return 1;
}

void hx8527_log_touch_data(struct device *dev, uint8_t *buf,struct himax_report_data *hx_touch_data)
{
	struct himax_ts_data *d = to_himax_data(dev);
	int loop_i = 0;
	int print_size = 0;

	if(!d->diag_command)
		print_size = hx_touch_data->touch_info_size;
	else
		print_size = hx_touch_data->touch_all_size;

	for (loop_i = 0;loop_i < print_size; loop_i+=8)
	{
		if((loop_i + 7) >= print_size)
		{
			TOUCH_I("%s: over flow\n",__func__);
			break;
		}
		TOUCH_I("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i, buf[loop_i], loop_i + 1, buf[loop_i + 1]);
		TOUCH_I("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i + 2, buf[loop_i + 2], loop_i + 3, buf[loop_i + 3]);
		TOUCH_I("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i + 4, buf[loop_i + 4], loop_i + 5, buf[loop_i + 5]);
		TOUCH_I("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i + 6, buf[loop_i + 6], loop_i + 7, buf[loop_i + 7]);
		TOUCH_I("\n");
	}
}
void hx8527_log_touch_event(int x, int y, int w, int loop_i, int touched)
{
	if(touched == HX_FINGER_ON)
		TOUCH_I("Finger %d=> X:%d, Y:%d W:%d, Z:%d, F:%d\n", loop_i + 1, x, y, w, w, loop_i + 1);
	else if(touched == HX_FINGER_LEAVE)
		TOUCH_I("All Finger leave\n");
	else
		TOUCH_I("%s : wrong input!\n",__func__);
}

void hx8527_log_touch_event_detail(struct himax_ts_data *d, int x, int y, int w, int loop_i, int touched, uint16_t old_finger)
{

	if (touched == HX_FINGER_ON) {
		if (old_finger >> loop_i == 0) {
			if (d->useScreenRes)
				TOUCH_I("status: Screen:F:%02d Down, X:%d, Y:%d, W:%d\n",
						loop_i+1, x * d->widthFactor >> SHIFTBITS,
						y * d->heightFactor >> SHIFTBITS, w);
			else
				TOUCH_I("status: Raw:F:%02d Down, X:%d, Y:%d, W:%d\n", loop_i+1, x, y, w);
		}
	} else if(touched == HX_FINGER_LEAVE) {
		if (old_finger >> loop_i == 1) {
			if (d->useScreenRes)
				TOUCH_I("status: Screen:F:%02d Up, X:%d, Y:%d\n",
						loop_i+1, d->pre_finger_data[loop_i][0] * d->widthFactor >> SHIFTBITS,
						d->pre_finger_data[loop_i][1] * d->heightFactor >> SHIFTBITS);
			else
				TOUCH_I("status: Raw:F:%02d Up, X:%d, Y:%d\n",
						loop_i+1, d->pre_finger_data[loop_i][0],
						d->pre_finger_data[loop_i][1]);
		}
	} else {
		TOUCH_I("%s : wrong input!\n",__func__);
	}
}

void himax_get_mutual_edge(void)
{
	int i = 0;
	for(i = 0; i < (HX_RX_NUM * HX_TX_NUM);i++)
	{
		if(diag_mutual[i] > g_max_mutual)
			g_max_mutual = diag_mutual[i];
		if(diag_mutual[i] < g_min_mutual)
			g_min_mutual = diag_mutual[i];
	}
}

void himax_get_self_edge(void)
{
	int i = 0;
	for(i = 0; i < (HX_RX_NUM + HX_TX_NUM);i++)
	{
		if(diag_self[i] > g_max_self)
			g_max_self = diag_self[i];
		if(diag_self[i] < g_min_self)
			g_min_self = diag_self[i];
	}
}

/* print first step which is row */

static size_t print_state_info(size_t count, char *buf)
{
	count += sprintf(buf + count, "ReCal = %d\t",hx_state_info[0] & 0x01);
	count += sprintf(buf + count, "Palm = %d\t",hx_state_info[0]>>1 & 0x01);
	count += sprintf(buf + count, "AC mode = %d\t",hx_state_info[0]>>2 & 0x01);
	count += sprintf(buf + count, "Water = %d\n",hx_state_info[0]>>3 & 0x01);
	count += sprintf(buf + count, "Glove = %d\t",hx_state_info[0]>>4 & 0x01);
	count += sprintf(buf + count, "TX Hop = %d\t",hx_state_info[0]>>5 & 0x01 );
	count += sprintf(buf + count, "Base Line = %d\t",hx_state_info[0]>>6 & 0x01);
	count += sprintf(buf + count, "OSR Hop = %d\t",hx_state_info[1]>>3 & 0x01);
	count += sprintf(buf + count, "KEY = %d\n",hx_state_info[1]>>4 & 0x0F);

	return count;
}

static size_t himax_diag_arrange_print(size_t count, char *buf, int i, int j, int transpose)
{

	if(transpose)
		count += sprintf(buf + count, "%6d", diag_mutual[ j + i*HX_RX_NUM]);
	else
		count += sprintf(buf + count, "%6d", diag_mutual[ i + j*HX_RX_NUM]);

	return count;
}

/* ready to print second step which is column*/
static size_t himax_diag_arrange_inloop(size_t count, char *buf, int in_init,int out_init,bool transpose, int j)
{
	int i;
	int in_max = 0;

	if(transpose)
		in_max = HX_TX_NUM;
	else
		in_max = HX_RX_NUM;

	if (in_init > 0) //bit0 = 1
	{
		for(i = in_init-1;i >= 0;i--)
		{
			count = himax_diag_arrange_print(count, buf, i, j, transpose);
		}
		if(transpose)
		{
			if(out_init > 0)
				count += sprintf(buf + count, " %5d\n", diag_self[j]);
			else
				count += sprintf(buf + count, " %5d\n", diag_self[HX_RX_NUM - j - 1]);
		}
	}
	else	//bit0 = 0
	{
		for (i = 0; i < in_max; i++)
		{
			count = himax_diag_arrange_print(count, buf, i, j, transpose);
		}
		if(transpose)
		{
			if(out_init > 0)
				count += sprintf(buf + count, " %5d\n", diag_self[HX_RX_NUM - j - 1]);
			else
				count += sprintf(buf + count, " %5d\n", diag_self[j]);
		}
	}

	return count;
}

/* print first step which is row */
static size_t himax_diag_arrange_outloop(size_t count, char *buf, int transpose, int out_init, int in_init)
{
	int j;
	int out_max = 0;
	int self_cnt = 0;

	if(transpose)
		out_max = HX_RX_NUM;
	else
		out_max = HX_TX_NUM;

	if(out_init > 0) //bit1 = 1
	{
		self_cnt = 1;
		for(j = out_init-1;j >= 0;j--)
		{
			count += sprintf(buf + count, "%3c%02d%c",'[', j + 1,']');
			count = himax_diag_arrange_inloop(count, buf, in_init, out_init, transpose, j);
			if(!transpose)
			{
				count += sprintf(buf + count, " %5d\n", diag_self[HX_TX_NUM + HX_RX_NUM - self_cnt]);
				self_cnt++;
			}
		}
	}
	else	//bit1 = 0
	{
		for(j = 0;j < out_max;j++)
		{
			count += sprintf(buf + count, "%3c%02d%c",'[', j + 1,']');
			count = himax_diag_arrange_inloop(count, buf, in_init, out_init, transpose, j);
			if(!transpose)
			{
				count += sprintf(buf + count, " %5d\n", diag_self[j + HX_RX_NUM]);
			}
		}
	}

	return count;
}

/* determin the output format of diag */
static size_t himax_diag_arrange(size_t count, char *buf)
{
	int bit2,bit1,bit0;
	int i;

	/* rotate bit */
	bit2 = g_diag_arr_num >> 2;
	/* reverse Y */
	bit1 = g_diag_arr_num >> 1 & 0x1;
	/* reverse X */
	bit0 = g_diag_arr_num & 0x1;

	if (g_diag_arr_num < 4)
	{
		for (i = 0 ;i <= HX_RX_NUM; i++)
			count += sprintf(buf + count, "%3c%02d%c",'[', i,']');
		count += sprintf(buf + count, "\n");
		count = himax_diag_arrange_outloop(count, buf, bit2, bit1 * HX_TX_NUM, bit0 * HX_RX_NUM);
		count += sprintf(buf + count, "%6c",' ');
		if(bit0 == 1)
		{
			for (i = HX_RX_NUM - 1; i >= 0; i--)
				count += sprintf(buf + count, "%6d", diag_self[i]);
		}
		else
		{
			for (i = 0; i < HX_RX_NUM; i++)
				count += sprintf(buf + count, "%6d", diag_self[i]);
		}
	}
	else
	{
		for (i = 0 ;i <= HX_TX_NUM; i++)
			count += sprintf(buf + count, "%3c%02d%c",'[', i,']');
		count += sprintf(buf + count, "\n");
		count = himax_diag_arrange_outloop(count, buf, bit2, bit1 * HX_RX_NUM, bit0 * HX_TX_NUM);
		count += sprintf(buf + count, "%6c",' ');
		if(bit1 == 1)
		{
			for (i = HX_RX_NUM + HX_TX_NUM - 1; i >= HX_RX_NUM; i--)
			{
				count += sprintf(buf + count, "%6d", diag_self[i]);
			}
		}
		else
		{
			for (i = HX_RX_NUM; i < HX_RX_NUM + HX_TX_NUM; i++)
			{
				count += sprintf(buf + count, "%6d", diag_self[i]);
			}
		}
	}

	return count;
}

static ssize_t show_himax_diag(struct device *dev, char *buf)
{
	size_t count = 0;
	uint32_t loop_i;
	uint16_t mutual_num, self_num, width;
	int dsram_type = 0;

	dsram_type = g_diag_command/10;

	if(Is_2T2R &&(g_diag_command >= 4 && g_diag_command <= 6))
	{
		mutual_num	= HX_RX_NUM_2 * HX_TX_NUM_2;
		self_num	= HX_RX_NUM_2 + HX_TX_NUM_2; //don't add KEY_COUNT
		width		= HX_RX_NUM_2;
		count += sprintf(buf + count, "ChannelStart: %4d, %4d\n\n", HX_RX_NUM_2, HX_TX_NUM_2);
	}
	else
	{
		mutual_num	= HX_RX_NUM * HX_TX_NUM;
		self_num	= HX_RX_NUM + HX_TX_NUM; //don't add KEY_COUNT
		width		= HX_RX_NUM;
		count += sprintf(buf + count, "ChannelStart: %4d, %4d\n\n", HX_RX_NUM, HX_TX_NUM);
	}

	// start to show out the raw data in adb shell
	if ((g_diag_command >= 1 && g_diag_command <= 3) || (g_diag_command == 7)){
		count = himax_diag_arrange(count, buf);
		count += sprintf(buf + count, "\n");

		count += sprintf(buf + count, "ChannelEnd");
		count += sprintf(buf + count, "\n");
	}
	else if(Is_2T2R && g_diag_command >= 4 && g_diag_command <= 6) {
		for (loop_i = 0; loop_i < mutual_num; loop_i++) {
			count += sprintf(buf + count, "%4d", diag_mutual_2[loop_i]);
			if ((loop_i % width) == (width - 1))
				count += sprintf(buf + count, " %4d\n", diag_self[width + loop_i/width]);
		}
		count += sprintf(buf + count, "\n");
		for (loop_i = 0; loop_i < width; loop_i++) {
			count += sprintf(buf + count, "%4d", diag_self[loop_i]);
			if (((loop_i) % width) == (width - 1))
				count += sprintf(buf + count, "\n");
		}

		count += sprintf(buf + count, "ChannelEnd");
		count += sprintf(buf + count, "\n");
	}

	else if (g_diag_command == 8) {
		for (loop_i = 0; loop_i < 128 ;loop_i++) {
			if ((loop_i % 16) == 0)
				count += sprintf(buf + count, "LineStart:");
			count += sprintf(buf + count, "%4x", diag_coor[loop_i]);
			if ((loop_i % 16) == 15)
				count += sprintf(buf + count, "\n");
		}
	}
	else if (dsram_type > 0 && dsram_type <= 8)
	{
		count = himax_diag_arrange(count, buf);
		count += sprintf(buf + count, "ChannelEnd");
		count += sprintf(buf + count, "\n");
	}
	if((g_diag_command >= 1 && g_diag_command <= 7) || dsram_type > 0)
	{
		/* print Mutual/Slef Maximum and Minimum */
		himax_get_mutual_edge();
		himax_get_self_edge();
		count += sprintf(buf + count, "Mutual Max:%3d, Min:%3d\n",g_max_mutual,g_min_mutual);
		count += sprintf(buf + count, "Self Max:%3d, Min:%3d\n",g_max_self,g_min_self);

		/* recovery status after print*/
		g_max_mutual = 0;
		g_min_mutual = 255;
		g_max_self = 0;
		g_min_self = 255;
	}
	/*pring state info*/
	count = print_state_info(count, buf);
	return count;
}

int hx8527_determin_diag_rawdata(int diag_command)
{
	return (diag_command/10 > 0) ? 0 : diag_command%10;
}

static ssize_t store_himax_diag(struct device *dev,const char *buf, size_t count)
{
	char messages[80] = {0};

	uint8_t command[2] = {0x00, 0x00};
	uint8_t receive[1];

	/* 1:IIR,2:DC,3:Bank,4:IIR2,5:IIR2_N,6:FIR2,7:Baseline,8:dump coord */
	int rawdata_type = 0;

	memset(receive, 0x00, sizeof(receive));

	if (count >= 80)
	{
		TOUCH_I("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}
	memcpy(messages, buf, count);

	if (messages[1] == 0x0A){
		g_diag_command =messages[0] - '0';
	}else{
		g_diag_command =(messages[0] - '0')*10 + (messages[1] - '0');
	}

	rawdata_type = hx8527_determin_diag_rawdata(g_diag_command);

	if(g_diag_command > 0 && rawdata_type == 0)
	{
		TOUCH_I("[Himax]g_diag_command=0x%x, rawdata_type=%d! Maybe no support!\n"
				, g_diag_command, rawdata_type);
		g_diag_command = 0x00;
	}
	else
		TOUCH_I("[Himax]g_diag_command=0x%x, rawdata_type=%d\n", g_diag_command, rawdata_type);

	if (rawdata_type > 0 && rawdata_type < 8)
	{
		TOUCH_I("%s,common\n",__func__);

		command[0] = g_diag_command;
		hx8527_diag_register_set(dev, command[0]);
	}
	else
	{
		if(g_diag_command != 0x00)
		{
			TOUCH_E("[Himax]g_diag_command error!diag_command=0x%x so reset\n",g_diag_command);
			command[0] = 0x00;
			g_diag_command = 0x00;
			hx8527_diag_register_set(dev, command[0]);
		}
		else
		{
			command[0] = 0x00;
			g_diag_command = 0x00;
			hx8527_diag_register_set(dev, command[0]);
			TOUCH_I("return to normal g_diag_command=0x%x\n",g_diag_command);
		}
	}
	return count;
}

static ssize_t store_reset_ctrl(struct device *dev, const char *buf, size_t count)
{
	int value = 0;

	sscanf(buf, "%d", &value);

	switch(value){

		case 0:
			TOUCH_I("%s, POWER_HW_RESET_SYNC\n", __func__);
			hx8527_power(dev, POWER_HW_RESET_SYNC);
			break;

		case 1:
			TOUCH_I("%s, POWER_OFF -> POWER_ON\n", __func__);
			hx8527_power(dev, POWER_OFF);
			hx8527_power(dev, POWER_ON);
			break;

		default:
			TOUCH_I("not supported value = %d\n", value);
			break;
	}

	TOUCH_I("%s() buf:%s",__func__, buf);
	return count;
}

int hx8527_read_ic_trigger_type(struct device *dev)
{
	char data[12] = {0};
	int trigger_type = false;

	hx8527_sensor_off(dev);
	data[0] = 0x8C;
	data[1] = 0x14;
	hx8527_bus_master_write(dev, &data[0], 2);
	msleep(10);
	data[0] = 0x8B;
	data[1] = 0x00;
	data[2] = 0x02;
	hx8527_bus_master_write(dev, &data[0], 3);
	msleep(10);
	hx8527_bus_read(dev, 0x5A, data, 10);

	if((data[1] & 0x01) == 1) {//FE(02)
		trigger_type = true;
	} else {
		trigger_type = false;
	}
	data[0] = 0x8C;
	data[1] = 0x00;
	hx8527_bus_master_write(dev, &data[0], 2);
	hx8527_sensor_on(dev);

	return trigger_type;
}


static ssize_t show_himax_debug(struct device *dev, char *buff)
{
	size_t count = 0;

	if (debug_level_cmd == 't')
	{
		if (fw_update_complete)
		{
			count += sprintf(buff, "FW Update Complete\n");
		}
		else
		{
			count += sprintf(buff, "FW Update Fail\n");
		}
	}
	else if (debug_level_cmd == 'h')
	{
		if (handshaking_result == 0)
		{
			count += sprintf(buff, "Handshaking Result = %d (MCU Running)\n",handshaking_result);
		}
		else if (handshaking_result == 1)
		{
			count += sprintf(buff, "Handshaking Result = %d (MCU Stop)\n",handshaking_result);
		}
		else if (handshaking_result == 2)
		{
			count += sprintf(buff, "Handshaking Result = %d (I2C Error)\n",handshaking_result);
		}
		else
		{
			count += sprintf(buff, "Handshaking Result = error \n");
		}
	}
	else if (debug_level_cmd == 'v')
	{
		count += sprintf(buff + count, "FW_VER = 0x%2.2X \n",ic_data->vendor_fw_ver);

		count += sprintf(buff + count, "CONFIG_VER = 0x%2.2X \n",ic_data->vendor_config_ver);

		count += sprintf(buff + count, "\n");

		count += sprintf(buff + count, "Himax Touch Driver Version:\n");
		count += sprintf(buff + count, "%s \n", HIMAX_DRIVER_VER);
	}
	else if (debug_level_cmd == 'd')
	{
		count += sprintf(buff + count, "Himax Touch IC Information :\n");
		count += sprintf(buff + count, "IC Type : HX852xES\n");
		count += sprintf(buff + count, "RX Num : %d\n", HX_RX_NUM);
		count += sprintf(buff + count, "TX Num : %d\n", HX_TX_NUM);
		if(Is_2T2R)
		{
			count += sprintf(buff + count, "2T2R panel\n");
		}
	}
	else if (debug_level_cmd == 'i')
	{
		count += sprintf(buff + count, "I2C communication is good.\n");
	}
	else if (debug_level_cmd == 'n')
	{
		if(hx8527_read_ic_trigger_type(dev) == 1) //Edgd = 1, Level = 0
			count += sprintf(buff + count, "IC Interrupt type is edge trigger.\n");
		else if(hx8527_read_ic_trigger_type(dev) == 0)
			count += sprintf(buff + count, "IC Interrupt type is level trigger.\n");
		else
			count += sprintf(buff + count, "Unkown IC trigger type.\n");
	}

	return count;
}


static ssize_t store_himax_debug(struct device *dev,const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	const struct firmware *fw = NULL;

	int result = 0;
	char fileName[128];
	int fw_type = 0;

	if (count >= 80) {
		TOUCH_I("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	if ( buf[0] == 'h') {
		/* handshaking */
		debug_level_cmd = buf[0];
		touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
		handshaking_result = hx8527_hand_shaking(); //0:Running, 1:Stop, 2:I2C Fail
		touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
		return count;
	} else if ( buf[0] == 'v') {
		/* firmware version */
		touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
		hx8527_power(dev, POWER_OFF);
		hx8527_power(dev, POWER_ON);
		debug_level_cmd = buf[0];
		hx8527_reload_config(dev);
		touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
		return count;
	} else if ( buf[0] == 'd') {
		/* ic information */
		debug_level_cmd = buf[0];
		return count;
	} else if (buf[0] == 't') {
		touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);

		debug_level_cmd 		= buf[0];
		fw_update_complete		= false;
		memset(fileName, 0, 128);

		// parse the file name
		snprintf(fileName, count-2, "%s", &buf[2]);
		TOUCH_I("%s: upgrade from file(%s) start!\n", __func__, fileName);

		result = request_firmware(&fw, fileName, dev);

		if (result < 0) {
			TOUCH_I("fail to request_firmware fwpath: %s (ret:%d)\n", fileName, result);
			return count;
		} else {
			fw_type = fw->size/1024;
			// start to upgrade
			TOUCH_I("Now FW size is : %dk\n",fw_type);
			switch(fw_type) {
				case 32:
					if (fts_ctpm_fw_upgrade_with_sys_fs_32k(dev, (unsigned char *)fw->data, fw->size) != 0) {
						TOUCH_E("%s: TP upgrade error, line: %d\n", __func__, __LINE__);
						fw_update_complete = false;
					} else {
						TOUCH_I("%s: TP upgrade OK, line: %d\n", __func__, __LINE__);
						fw_update_complete = true;
					}
					break;
				default:
					TOUCH_E("%s: Flash command fail: %d\n", __func__, __LINE__);
					fw_update_complete = false;
					break;
			}
			release_firmware(fw);
			goto firmware_upgrade_done;
		}
	} else if (buf[0] == 'i' && buf[1] == '2' && buf[2] == 'c') {
		/* i2c commutation */
		debug_level_cmd = 'i';
		return count;
	} else if (buf[0] == 'i' && buf[1] == 'n' && buf[2] == 't') {
		/* INT trigger */
		debug_level_cmd = 'n';
		return count;
	} else {
		/* others,do nothing */
		debug_level_cmd = 0;
		return count;
	}

firmware_upgrade_done:

	hx8527_power(dev, POWER_OFF);
	hx8527_power(dev, POWER_ON);
	hx8527_reload_config(dev);

	msleep(120);
	touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);

	return count;
}

uint8_t getFlashCommand(void)
{
	return flash_command;
}

static uint8_t getFlashDumpProgress(void)
{
	return flash_progress;
}

static uint8_t getFlashDumpComplete(void)
{
	return flash_dump_complete;
}

static uint8_t getFlashDumpFail(void)
{
	return flash_dump_fail;
}

uint8_t getSysOperation(void)
{
	return sys_operation;
}

static uint8_t getFlashReadStep(void)
{
	return flash_read_step;
}

void setFlashBuffer(void)
{
	flash_buffer = kzalloc(Flash_Size * sizeof(uint8_t), GFP_KERNEL);
	if (flash_buffer == NULL) {
 		TOUCH_E("%s: flash_buffer Memory allocate fail!\n", __func__);
		return;
	}
	memset(flash_buffer,0x00,Flash_Size);
}

void setSysOperation(uint8_t operation)
{
	sys_operation = operation;
}

void setFlashDumpProgress(uint8_t progress)
{
	flash_progress = progress;
}

void setFlashDumpComplete(uint8_t status)
{
	flash_dump_complete = status;
}

void setFlashDumpFail(uint8_t fail)
{
	flash_dump_fail = fail;
}

static void setFlashCommand(uint8_t command)
{
	flash_command = command;
}

static void setFlashReadStep(uint8_t step)
{
	flash_read_step = step;
}

void setFlashDumpGoing(bool going)
{
	flash_dump_going = going;
}

static ssize_t show_proc_flash(struct device *dev, char *buf)
{
	int ret = 0;
	int loop_i;
	uint8_t local_flash_read_step=0;
	uint8_t local_flash_complete = 0;
	uint8_t local_flash_progress = 0;
	uint8_t local_flash_command = 0;
	uint8_t local_flash_fail = 0;

	local_flash_complete = getFlashDumpComplete();
	local_flash_progress = getFlashDumpProgress();
	local_flash_command = getFlashCommand();
	local_flash_fail = getFlashDumpFail();

	TOUCH_I("flash_progress = %d \n",local_flash_progress);

	if (local_flash_fail)
	{
		ret += sprintf(buf + ret, "FlashStart:Fail \n");
		ret += sprintf(buf + ret, "FlashEnd");
		ret += sprintf(buf + ret, "\n");

		return ret;
	}

	if (!local_flash_complete)
	{
		ret += sprintf(buf + ret, "FlashStart:Ongoing:0x%2.2x \n",flash_progress);
		ret += sprintf(buf + ret, "FlashEnd");
		ret += sprintf(buf + ret, "\n");
		return ret;
	}

	if (local_flash_command == 1 && local_flash_complete)
	{
		ret += sprintf(buf + ret, "FlashStart:Complete \n");
		ret += sprintf(buf + ret, "FlashEnd");
		ret += sprintf(buf + ret, "\n");
		return ret;
	}

	if (local_flash_command == 3 && local_flash_complete)
	{
		ret += sprintf(buf + ret, "FlashStart: \n");
		for(loop_i = 0; loop_i < 128; loop_i++)
		{
			ret += sprintf(buf + ret, "x%2.2x", flash_buffer[loop_i]);
			if ((loop_i % 16) == 15)
			{
				ret += sprintf(buf + ret, "\n");
			}
		}
		ret += sprintf(buf + ret, "FlashEnd");
		ret += sprintf(buf + ret, "\n");
		return ret;
	}

	//flash command == 0 , report the data
	local_flash_read_step = getFlashReadStep();

	ret += sprintf(buf + ret, "FlashStart:%2.2x \n",local_flash_read_step);

	for (loop_i = 0; loop_i < 1024; loop_i++)
	{
		ret += sprintf(buf + ret, "x%2.2X", flash_buffer[local_flash_read_step*1024 + loop_i]);

		if ((loop_i % 16) == 15)
		{
			ret += sprintf(buf + ret, "\n");
		}
	}

	ret += sprintf(buf + ret, "FlashEnd");
	ret += sprintf(buf + ret, "\n");

	return ret;
}

static ssize_t store_proc_flash(struct device *dev,const char *buf, size_t count)
{
	struct himax_ts_data *d = to_himax_data(dev);
	char buf_tmp[6];
	unsigned long result = 0;

	if (count >= 80)
	{
		TOUCH_I("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	memset(buf_tmp, 0x0, sizeof(buf_tmp));

	TOUCH_I("%s: buf = %s\n", __func__, buf);

	if (getSysOperation() == 1)
	{
		TOUCH_E("%s: PROC is busy , return!\n", __func__);
		return count;
	}

	if (buf[0] == '0')
	{
		setFlashCommand(0);
		if (buf[1] == ':' && buf[2] == 'x')
		{
			memcpy(buf_tmp, buf + 3, 2);
			TOUCH_I("%s: read_Step = %s\n", __func__, buf_tmp);
			if (!kstrtoul(buf_tmp, 16, &result))
			{
				TOUCH_I("%s: read_Step = %lu \n", __func__, result);
				setFlashReadStep(result);
			}
		}
	}
	else if (buf[0] == '1')// 1_32,1_60,1_64,1_24,1_28 for flash size 32k,60k,64k,124k,128k
	{
		setSysOperation(1);
		setFlashCommand(1);
		setFlashDumpProgress(0);
		setFlashDumpComplete(0);
		setFlashDumpFail(0);
		if ((buf[1] == '_' ) && (buf[2] == '3' ) && (buf[3] == '2' ))
		{
			Flash_Size = FW_SIZE_32k;
		}
		else if ((buf[1] == '_' ) && (buf[2] == '6' ))
		{
			if (buf[3] == '0')
			{
				Flash_Size = FW_SIZE_60k;
			}
			else if (buf[3] == '4')
			{
				Flash_Size = FW_SIZE_64k;
			}
		}
		else if ((buf[1] == '_' ) && (buf[2] == '2' ))
		{
			if (buf[3] == '4')
			{
				Flash_Size = FW_SIZE_124k;
			}
			else if (buf[3] == '8')
			{
				Flash_Size = FW_SIZE_128k;
			}
		}
		queue_delayed_work(d->flash_wq, &d->flash_work, 0);
	}
	else if (buf[0] == '2') // 2_32,2_60,2_64,2_24,2_28 for flash size 32k,60k,64k,124k,128k
	{
		setSysOperation(1);
		setFlashCommand(2);
		setFlashDumpProgress(0);
		setFlashDumpComplete(0);
		setFlashDumpFail(0);
		if ((buf[1] == '_' ) && (buf[2] == '3' ) && (buf[3] == '2' ))
		{
			Flash_Size = FW_SIZE_32k;
		}
		else if ((buf[1] == '_' ) && (buf[2] == '6' ))
		{
			if (buf[3] == '0')
			{
				Flash_Size = FW_SIZE_60k;
			}
			else if (buf[3] == '4')
			{
				Flash_Size = FW_SIZE_64k;
			}
		}
		else if ((buf[1] == '_' ) && (buf[2] == '2' ))
		{
			if (buf[3] == '4')
			{
				Flash_Size = FW_SIZE_124k;
			}
			else if (buf[3] == '8')
			{
				Flash_Size = FW_SIZE_128k;
			}
		}
		queue_delayed_work(d->flash_wq, &d->flash_work, 0);
	}
	return count;
}

void hx8527_flash_dump_func(struct device *dev, uint8_t local_flash_command, int Flash_Size, uint8_t *flash_buffer)
{
	int i=0, j=0, k=0, l=0;
	int buffer_ptr = 0;
	uint8_t x81_command[2] = {HX_CMD_TSSLPOUT,0x00};
	uint8_t x82_command[2] = {HX_CMD_TSSOFF,0x00};
	uint8_t x43_command[4] = {HX_CMD_FLASH_ENABLE,0x00,0x00,0x00};
	uint8_t x44_command[4] = {HX_CMD_FLASH_SET_ADDRESS,0x00,0x00,0x00};
	uint8_t x59_tmp[4] = {0,0,0,0};
	uint8_t page_tmp[128];

	touch_interrupt_control(dev, INTERRUPT_DISABLE);

	setFlashDumpGoing(true);

	local_flash_command = getFlashCommand();

	hx8527_power(dev, POWER_OFF);
	hx8527_power(dev, POWER_ON);

	if ( hx8527_bus_master_write(dev, x81_command, 1) < 0)//sleep out
	{
		TOUCH_E("%s i2c write 81 fail.\n",__func__);
		goto Flash_Dump_i2c_transfer_error;
	}
	msleep(120);
	if ( hx8527_bus_master_write(dev, x82_command, 1) < 0)
	{
		TOUCH_E("%s i2c write 82 fail.\n",__func__);
		goto Flash_Dump_i2c_transfer_error;
	}
	msleep(100);

	x43_command[1] = 0x01;
	if ( hx8527_bus_write(dev, x43_command[0],&x43_command[1], 1) < 0)
	{
		goto Flash_Dump_i2c_transfer_error;
	}
	msleep(100);

	for( i=0 ; i<8 ;i++)
	{
		for(j=0 ; j<32 ; j++)
		{
			//TOUCH_I(" Step 2 i=%d , j=%d %s\n",i,j,__func__);
			//read page start
			for(k=0; k<128; k++)
			{
				page_tmp[k] = 0x00;
			}
			for(k=0; k<32; k++)
			{
				x44_command[1] = k;
				x44_command[2] = j;
				x44_command[3] = i;
				if ( hx8527_bus_write(dev, x44_command[0], &x44_command[1], 3) < 0)
				{
					TOUCH_E("%s i2c write 44 fail.\n",__func__);
					goto Flash_Dump_i2c_transfer_error;
				}
				if ( hx8527_bus_write_command(dev, 0x46) < 0)
				{
					TOUCH_E("%s i2c write 46 fail.\n",__func__);
					goto Flash_Dump_i2c_transfer_error;
				}
				//msleep(2);
				if ( hx8527_bus_read(dev, 0x59, x59_tmp, 4) < 0)
				{
					TOUCH_E("%s i2c write 59 fail.\n",__func__);
					goto Flash_Dump_i2c_transfer_error;
				}
				//msleep(2);
				for(l=0; l<4; l++)
				{
					page_tmp[k*4+l] = x59_tmp[l];
				}
				//msleep(10);
			}

			for(k=0; k<128; k++)
			{
				flash_buffer[buffer_ptr++] = page_tmp[k];

			}
			setFlashDumpProgress(i*32 + j);
		}
	}

	TOUCH_I("Complete~~~~~~~~~~~~~~~~~~~~~~~\n");
	if(local_flash_command==0x01)
	{
		TOUCH_I(" buffer_ptr = %d \n",buffer_ptr);

		for (i = 0; i < buffer_ptr; i++)
		{
			TOUCH_I("%2.2X ", flash_buffer[i]);

			if ((i % 16) == 15)
			{
				TOUCH_I("\n");
			}
		}
		TOUCH_I("End~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
	}
	else if (local_flash_command == 2)
	{
		struct file *fn;

		fn = filp_open(FLASH_DUMP_FILE,O_CREAT | O_WRONLY ,0);
		if (!IS_ERR(fn))
		{
			fn->f_op->write(fn,flash_buffer,buffer_ptr*sizeof(uint8_t),&fn->f_pos);
			filp_close(fn,NULL);
		}
	}

	hx8527_power(dev, POWER_OFF);
	hx8527_power(dev, POWER_ON);
	hx8527_reload_config(dev);

	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	setFlashDumpGoing(false);

	setFlashDumpComplete(1);
	setSysOperation(0);
	return;

Flash_Dump_i2c_transfer_error:

	hx8527_power(dev, POWER_OFF);
	hx8527_power(dev, POWER_ON);
	hx8527_reload_config(dev);
	touch_interrupt_control(dev, INTERRUPT_ENABLE);

	setFlashDumpGoing(false);
	setFlashDumpComplete(0);
	setFlashDumpFail(1);
	setSysOperation(0);
}

void hx8527_ts_flash_func(struct device *dev)
{
	uint8_t local_flash_command = 0;

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	setFlashDumpGoing(true);

	local_flash_command = getFlashCommand();

	msleep(100);

	TOUCH_I("%s: local_flash_command = %d enter.\n", __func__,local_flash_command);

	if ((local_flash_command == 1 || local_flash_command == 2)|| (local_flash_command==0x0F))
	{
		hx8527_flash_dump_func(dev, local_flash_command,Flash_Size, flash_buffer);
	}

	TOUCH_I("Complete~~~~~~~~~~~~~~~~~~~~~~~\n");

	if (local_flash_command == 2)
	{
		struct file *fn;

		fn = filp_open(FLASH_DUMP_FILE,O_CREAT | O_WRONLY ,0);
		if (!IS_ERR(fn))
		{
			TOUCH_I("%s create file and ready to write\n",__func__);
			fn->f_op->write(fn,flash_buffer,Flash_Size*sizeof(uint8_t),&fn->f_pos);
			filp_close(fn,NULL);
		}
	}

	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	setFlashDumpGoing(false);

	setFlashDumpComplete(1);
	setSysOperation(0);
	return;
}

int hx8527_get_raw_data(struct device *dev, uint8_t diag_command, uint16_t mutual_num, uint16_t self_num, uint16_t *mutual_data, uint16_t *self_data)
{
	uint8_t command_F1h_bank[2] = {0xF1, diag_command};
	int i = 0;
	int result = 0;

	TOUCH_I("Start get raw data\n");

	mutual_bank = kzalloc(mutual_num * sizeof(uint16_t), GFP_KERNEL);
	if (mutual_bank == NULL) {
		TOUCH_E("%s : alloc mutual_bank fail", __func__);
		result = -1;
		goto mutual_mem_alloc_fail;
	}

	self_bank = kzalloc(self_num * sizeof(uint16_t), GFP_KERNEL);
	if (self_bank == NULL) { 
		TOUCH_E("%s : alloc mutual_bank fail", __func__);
		result = -1;
		goto self_mem_alloc_fail;
	}
	memset(mutual_bank, 0xFF, mutual_num * sizeof(uint16_t));
	memset(self_bank, 0xFF, self_num * sizeof(uint16_t));
	Selftest_flag = 1;
	TOUCH_I(" mutual_num = %d, self_num = %d \n",mutual_num, self_num);
	TOUCH_I(" Selftest_flag = %d \n",Selftest_flag);


	hx8527_bus_write(dev, command_F1h_bank[0], &command_F1h_bank[1], 1);
	msleep(100);

	for (i = 0; i < 200; i++) //check for max 200 times
	{
		if(Selftest_flag == 0)
			break;
		msleep(10);

		if(i == 199)
		{
			result = 1;
			TOUCH_E(" Get raw data fail \n");
		}
	}
	memcpy(mutual_data, mutual_bank, mutual_num * sizeof(uint16_t));
	memcpy(self_data, self_bank, self_num * sizeof(uint16_t));

	Selftest_flag = 0;
	command_F1h_bank[1] = 0x00;
	hx8527_bus_write(dev, command_F1h_bank[0], &command_F1h_bank[1], 1);
	msleep(20);
	g_diag_command = 0;
	TOUCH_I(" Write diag cmd = %d \n",command_F1h_bank[1]);
	kfree(self_bank);	
self_mem_alloc_fail:
	kfree(mutual_bank);
mutual_mem_alloc_fail:
	return result;
}

static uint8_t LPWG_Self_Test_Bank(struct device *dev, uint8_t *RB1H, u16 *open_short_buf, uint8_t *os_fail_num)
{
	uint16_t i, m;
	uint16_t mutual_num = 0;
	uint16_t self_num = 0;
	int fail_flag = -1;
	uint8_t bank_val_tmp;
	uint16_t *mutual_bank_data;
	uint16_t *self_bank_data;

	uint8_t set_slf_bnk_ulmt;
	uint8_t set_slf_bnk_dlmt;
	int ret;

	//=============================== Get Bank value Start===============================
	TOUCH_I("Start lpwu_self_test\n");

	mutual_num	= HX_RX_NUM * HX_TX_NUM;
	self_num	= HX_RX_NUM + HX_TX_NUM; //don't add KEY_COUNT

	mutual_bank_data = kzalloc(mutual_num * sizeof(uint16_t), GFP_KERNEL);
	if (mutual_bank_data == NULL) {
		TOUCH_E("%s : alloc mutual_bank_data fail", __func__);
		fail_flag = -1;
		goto mutual_mem_alloc_fail;
	}
	self_bank_data = kzalloc(self_num * sizeof(uint16_t), GFP_KERNEL);
	if (self_bank_data == NULL) {
		TOUCH_E("%s : alloc self_bank_data fail", __func__);
		fail_flag = -1;
		goto self_mem_alloc_fail;
	}
	ret = hx8527_get_raw_data(dev, 0x03, mutual_num, self_num, mutual_bank_data, self_bank_data); //get bank data
	if (ret < 0) {
		TOUCH_E("%s : hx8527_get_raw_data error", __func__);
		fail_flag = -1;
		goto get_raw_data_fail;
	}

	TOUCH_I(" Enter Test flow \n");

	set_slf_bnk_ulmt = RB1H[6]; //Increase @ 2012/05/24 for weak open/short
	set_slf_bnk_dlmt = RB1H[7];

	if(ret == 0)
	{
		fail_flag = 0;

		//======Condition 2======Check every self channel bank
		for (m = 0; m < self_num; m++)
		{
			bank_val_tmp = self_bank_data[m];
			if (bank_val_tmp < set_slf_bnk_dlmt) //Self open
			{
				fail_flag |= 0x01;
				os_fail_num[2]++;
			}
			else if(bank_val_tmp > set_slf_bnk_ulmt) //Self short
			{
				fail_flag |= 0x02;
				os_fail_num[3]++;
			}
		}
		if (fail_flag)
		{
			RB1H[0] = 0xF2; //Fail ID for Condition 2
			for (i = 0;i < 8; i++){
				TOUCH_I(" RB1H[%d] = %X \n", i, RB1H[i]);
			}
			for (m = 0; m < self_num; m++){
				TOUCH_I(" self_bank_data[%d] = %X \n", m, self_bank_data[m]);
			}
		}
		else
		{
			fail_flag = 0;
			RB1H[0] = 0xAA; ////PASS ID
		}
	}

	//memcpy(open_short_buf, mutual_bank_data, mutual_num * sizeof(uint16_t));
	memset(open_short_buf, 0x00, mutual_num * sizeof(uint16_t));
	memcpy(&open_short_buf[mutual_num], self_bank_data, self_num * sizeof(uint16_t));


get_raw_data_fail:

	kfree(self_bank_data);
self_mem_alloc_fail:
	kfree(mutual_bank_data);
mutual_mem_alloc_fail:
	return fail_flag;
}

static uint8_t Self_Test_Bank(struct device *dev, uint8_t *RB1H, u16 *open_short_buf, uint8_t *os_fail_num)
{
	uint16_t i, m;
	uint16_t mutual_num = 0;
	uint16_t self_num = 0;
	uint8_t bank_ulmt, bank_dlmt;
	uint8_t bank_min, bank_max;
	int fail_flag = -1;
	uint8_t bank_val_tmp;
	uint16_t *mutual_bank_data;
	uint16_t *self_bank_data;

	uint8_t set_bnk_ulmt;
	uint8_t set_bnk_dlmt;
	uint8_t set_avg_bnk_ulmt;
	uint8_t set_avg_bnk_dlmt;
	uint8_t set_slf_bnk_ulmt;
	uint8_t set_slf_bnk_dlmt;
	int ret;

	//=============================== Get Bank value Start===============================
	TOUCH_I("Start self_test\n");

	mutual_num	= HX_RX_NUM * HX_TX_NUM;
	self_num	= HX_RX_NUM + HX_TX_NUM; //don't add KEY_COUNT

	mutual_bank_data = kzalloc(mutual_num * sizeof(uint16_t), GFP_KERNEL);
	if (mutual_bank_data == NULL) {
		TOUCH_E("%s : alloc mutual_bank_data fail", __func__);
		fail_flag = -1;
		goto mutual_mem_alloc_fail;
	}
	self_bank_data = kzalloc(self_num * sizeof(uint16_t), GFP_KERNEL);
	if (self_bank_data == NULL) {
		TOUCH_E("%s : alloc self_bank_data fail", __func__);
		fail_flag = -1;
		goto self_mem_alloc_fail;
	}
	ret = hx8527_get_raw_data(dev, 0x03, mutual_num, self_num, mutual_bank_data, self_bank_data); //get bank data

	TOUCH_I(" Enter Test flow \n");
	set_bnk_ulmt = RB1H[2];
	set_bnk_dlmt = RB1H[3];
	set_avg_bnk_ulmt = RB1H[4];
	set_avg_bnk_dlmt = RB1H[5];
	set_slf_bnk_ulmt = RB1H[6]; //Increase @ 2012/05/24 for weak open/short
	set_slf_bnk_dlmt = RB1H[7];

	if(ret == 0)
	{
		fail_flag = 0;
		//======Condition 1======Check every block's bank with average value
		bank_ulmt = set_bnk_ulmt;
		bank_dlmt = set_bnk_dlmt;

		bank_min = 0xFF;
		bank_max = 0x00;
		TOUCH_I(" bank_ulmt = %d, bank_dlmt = %d \n", bank_ulmt, bank_dlmt);
		for (m = 0; m < mutual_num; m++)
		{
			bank_val_tmp = mutual_bank_data[m];
			if (bank_val_tmp < bank_dlmt)
			{
				fail_flag |= 0x01; //Mutual open
				os_fail_num[0]++;
			}
			else if(bank_val_tmp > bank_ulmt)
			{
				fail_flag |= 0x02; //Mutual short
				os_fail_num[1]++;
			}

			//Bank information record
			if (bank_val_tmp > bank_max)
				bank_max = bank_val_tmp;
			else if (bank_val_tmp < bank_min)
				bank_min = bank_val_tmp;
		}
		if (fail_flag)
		{
			RB1H[0] = 0xF1; //Fail ID for Condition 1

			for (i = 0;i < 8; i++){
				TOUCH_I(" RB1H[%d] = %X \n", i, RB1H[i]);
			}
			for (m = 0; m < mutual_num; m++){
				TOUCH_I(" mutual_bank_data[%d] = %X \n", m, mutual_bank_data[m]);
			}
		}
		//======Condition 2======Check every self channel bank
		for (m = 0; m < self_num; m++)
		{
			bank_val_tmp = self_bank_data[m];
			if (bank_val_tmp < set_slf_bnk_dlmt) //Self open
			{
				fail_flag |= 0x01;
				os_fail_num[2]++;
			}
			else if(bank_val_tmp > set_slf_bnk_ulmt) //Self short
			{
				fail_flag |= 0x02;
				os_fail_num[3]++;
			}
		}
		if (fail_flag)
		{
			RB1H[0] = 0xF2; //Fail ID for Condition 2
			for (i = 0;i < 8; i++){
				TOUCH_I(" RB1H[%d] = %X \n", i, RB1H[i]);
			}
			for (m = 0; m < self_num; m++){
				TOUCH_I(" self_bank_data[%d] = %X \n", m, self_bank_data[m]);
			}
		}
		else
		{
			fail_flag = 0;
			RB1H[0] = 0xAA; ////PASS ID
		}
	}

	memcpy(open_short_buf, mutual_bank_data, mutual_num * sizeof(uint16_t));
	memcpy(&open_short_buf[mutual_num], self_bank_data, self_num * sizeof(uint16_t));


	kfree(self_bank_data);
self_mem_alloc_fail:
	kfree(mutual_bank_data);
mutual_mem_alloc_fail:
	return fail_flag;
}

int hx8527_chip_self_test(struct device *dev, uint8_t *RB1H, u16 *open_short_buf, uint8_t *os_fail_num, u8 type)
{
	uint8_t valuebuf[16];
	int pf_value=0x00;
	int retry_times=3;

test_retry:

	memset(valuebuf, 0x00, sizeof(valuebuf));
	memset(os_fail_num, 0x00, 4 * sizeof(uint8_t));

	Selftest_flag = 1;
	g_diag_command = 0x03;

	if (type == OPEN_SHORT_TEST)
		valuebuf[0] = Self_Test_Bank(dev, &RB1H[0], open_short_buf, os_fail_num);
	else
		valuebuf[0] = LPWG_Self_Test_Bank(dev, &RB1H[0], open_short_buf, os_fail_num);

	g_diag_command = 0x00;
	Selftest_flag = 0;

	if (valuebuf[0]==0x0) {
		TOUCH_I("[Himax]: self-test pass\n");
		pf_value = valuebuf[0];
	}
	else {
		TOUCH_E("[Himax]: self-test fail : 0x%x\n",valuebuf[0]);
		if(retry_times>0)
		{
			retry_times--;
			goto test_retry;
		}
		pf_value = valuebuf[0];
	}

	return pf_value;
}


static ssize_t store_sensor_state(struct device *dev,const char *buf, size_t count)
{

	if (count >= 80)
	{
		TOUCH_I("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	if(buf[0] == '0')
	{
		hx8527_sensor_off(dev);
		TOUCH_I("Sense off \n");
	}
	else if(buf[0] == '1')
	{
		if(buf[1] == 's'){
			hx8527_sensor_on(dev);
			TOUCH_I("Sense on re-map on, run sram \n");
		}else{
			hx8527_sensor_on(dev);
			TOUCH_I("Sense on re-map off, run flash \n");
		}
	}
	else
	{
		TOUCH_I("Do nothing \n");
	}
	return count;
}


static ssize_t show_gpio_control(struct device *dev, char *buf)
{
	int ret = 0;
	struct touch_core_data *ts = to_touch_core(dev);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, " GPIO Reset [%d] : %s\n", ts->reset_pin, (gpio_get_value(ts->reset_pin)? "High" : "Low"));
	ret += snprintf(buf + ret, PAGE_SIZE - ret, " echo 0  > gpio_control : Reset = Low \n");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, " echo 1  > gpio_control : Reset = High \n");

	ret += snprintf(buf + ret, PAGE_SIZE - ret, " GPIO Interrupt [%d] : %s\n", ts->int_pin, (gpio_get_value(ts->int_pin)? "High" : "Low"));
	ret += snprintf(buf + ret, PAGE_SIZE - ret, " echo 2  > gpio_control : Interrupt = Low \n");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, " echo 3  > gpio_control : Interrupt = High \n");

	return ret;
}

static ssize_t store_gpio_control(struct device *dev, const char *buf, size_t count)
{
	int value = 0;
	struct touch_core_data *ts = to_touch_core(dev);

	sscanf(buf, "%d", &value);

	switch(value){
		case 0:
			touch_gpio_direction_output(ts->reset_pin, 0);
			break;
		case 1:
			touch_gpio_direction_output(ts->reset_pin, 1);
			break;
		case 2:
			touch_gpio_direction_output(ts->int_pin, 0);
			break;
		case 3:
			touch_gpio_direction_output(ts->int_pin, 1);
			usleep_range(100, 100);
			touch_gpio_direction_input(ts->int_pin);
			break;
		default:
			TOUCH_I("not supported value = %d\n", value);
			break;
	}

	TOUCH_I("%s() buf:%s",__func__, buf);
	return count;
}

static ssize_t show_irq_control(struct device *dev, char *buf)
{
	int ret = 0;
	struct touch_core_data *ts = to_touch_core(dev);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, " IRQ enable state : [%s]\n", (atomic_read(&ts->state.irq_enable) ? "Enable" : "Disable"));
	ret += snprintf(buf + ret, PAGE_SIZE - ret, " echo 0  > irq_control : Disable \n");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, " echo 1  > irq_control : Enable \n");

	return ret;
}

static ssize_t store_irq_control(struct device *dev, const char *buf, size_t count)
{
	int value = 0;

	sscanf(buf, "%d", &value);

	switch(value){
		case 0:
			touch_interrupt_control(dev, INTERRUPT_DISABLE);
			break;
		case 1:
			touch_interrupt_control(dev, INTERRUPT_ENABLE);
			break;
		default:
			TOUCH_I("not supported value = %d\n", value);
			break;
	}

	TOUCH_I("%s() buf:%s",__func__, buf);

	return count;
}

static int himax_set_limit(struct device *dev, u8 type, u16 MutualUpper, u16 SelfUpper)
{
	int i,j;

	if(type == RAW_DATA_TEST || type == LPWG_RAW_DATA_TEST
			|| type == JITTER_TEST || type == LPWG_JITTER_TEST)
	{
		for (i = 0; i < ROW_SIZE; i++) {
			for (j = 0; j < COL_SIZE; j++) {
				MutualUpperImage[i][j] = MutualUpper;
			}
		}
		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			SelfUpperImage[i] = SelfUpper;
		}
	}
	if (type == RAW_DATA_TEST || type == LPWG_RAW_DATA_TEST)
	{
		for (i = 0; i < ROW_SIZE; i++) {
			for (j = 0; j < COL_SIZE; j++) {
				MutualLowerImage[i][j] = MUL_RAW_DATA_THX_LOW;
			}
		}
		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			SelfLowerImage[i] = SELF_RAW_DATA_THX_LOW;
		}
	}

	return 0;
}

static int prd_print_data(struct device *dev, char *buf, u8 type)
{
	int i = 0, j = 0;
	int ret = 0;
	u16 *rawdata_buf = NULL;
	int col_size = COL_SIZE;
	char test_type[32] = {0, };
	int log_ret = 0;

	/* print a frame data */
	ret = snprintf(buf, PAGE_SIZE, "\n   : ");

	switch (type) {
		case RAW_DATA_TEST:
			rawdata_buf = Rawdata_buf;
			snprintf(test_type, sizeof(test_type),
					"\n[RAWDATA_TEST]\n");
			break;
		case JITTER_TEST:
			rawdata_buf = Rawdata_buf;
			snprintf(test_type, sizeof(test_type),
					"\n[JITTER_TEST]\n");
			break;
		case LPWG_RAW_DATA_TEST:
			rawdata_buf = Rawdata_buf;
			snprintf(test_type, sizeof(test_type),
					"\n[LPWG_RAW_DATA_TEST]\n");
			break;
		case LPWG_JITTER_TEST:
			rawdata_buf = Rawdata_buf;
			snprintf(test_type, sizeof(test_type),
					"\n[LPWG_JITTER_TEST]\n");
			break;
		case OPEN_SHORT_TEST:
			rawdata_buf = open_short_buf;
			snprintf(test_type, sizeof(test_type),
					"\n[OPEN_SHORT_TEST]\n");
			break;
		case LPWG_OPEN_SHORT_TEST:
			rawdata_buf = open_short_buf;
			snprintf(test_type, sizeof(test_type),
					"\n[LPWG_OPEN_SHORT_TEST]\n");
			break;
		default:
			snprintf(test_type, sizeof(test_type),
					"\n[NOT TEST ITEM]\n");
			return 1;
	}
	/* Test Type Write */
	hx8527_write_file(dev, test_type, TIME_INFO_SKIP);

	for (i = 0; i < col_size + 1; i++)
	{
		if( i < col_size )
			ret += snprintf(buf + ret, PAGE_SIZE - ret, " [%2d] ", i);
		else
			ret += snprintf(buf + ret, PAGE_SIZE - ret, " [ S] ");
	}



	for (i = 0; i < ROW_SIZE; i++) {
		char log_buf[LOG_BUF_SIZE] = {0, };
		log_ret = 0;
		ret += snprintf(buf + ret, PAGE_SIZE - ret,  "\n[%2d] ", i);
		log_ret += snprintf(log_buf + log_ret,
				LOG_BUF_SIZE - log_ret,  "[%2d]  ", i);

		for (j = 0; j < col_size; j++) {
			ret += snprintf(buf + ret, PAGE_SIZE - ret,
					"%5d ", rawdata_buf[i*col_size+j]);
			log_ret += snprintf(log_buf + log_ret,
					LOG_BUF_SIZE - log_ret,
					"%5d ", rawdata_buf[i*col_size+j]);
			if(j % col_size == col_size - 1)
			{
				ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"%5d ", rawdata_buf[MUTUAL_NUM + COL_SIZE + i]);
				log_ret += snprintf(log_buf + log_ret,
						LOG_BUF_SIZE - log_ret,
						"%5d ", rawdata_buf[MUTUAL_NUM + COL_SIZE + i]);
				if (rawdata_buf[MUTUAL_NUM + COL_SIZE + i] < self_min)
					self_min = rawdata_buf[MUTUAL_NUM + COL_SIZE + i];
				if (rawdata_buf[MUTUAL_NUM + COL_SIZE + i] > self_max)
					self_max = rawdata_buf[MUTUAL_NUM + COL_SIZE + i];
			}

			if (rawdata_buf[i*col_size+j] < mul_min)
				mul_min = rawdata_buf[i*col_size+j];
			if (rawdata_buf[i*col_size+j] > mul_max)
				mul_max = rawdata_buf[i*col_size+j];
		}
		TOUCH_I("%s\n", log_buf);
	}

	for (i = 0; i < COL_SIZE; i++) {
		char log_buf[LOG_BUF_SIZE] = {0, };
		if (i == 0) {
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n[ S] ");
			log_ret += snprintf(log_buf + log_ret, LOG_BUF_SIZE - log_ret,"\n[ S] ");
		}
		log_ret = 0;
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"%5d ", rawdata_buf[MUTUAL_NUM + i]);
		log_ret += snprintf(log_buf + log_ret,
				LOG_BUF_SIZE - log_ret,
				"%5d ", rawdata_buf[MUTUAL_NUM + i]);

		if (rawdata_buf[MUTUAL_NUM + i] < self_min)
			self_min = rawdata_buf[MUTUAL_NUM + i];
		if (rawdata_buf[MUTUAL_NUM + i] > self_max)
			self_max = rawdata_buf[MUTUAL_NUM + i];
	}

	ret += snprintf(buf + ret, PAGE_SIZE - ret,
			"\n==================================================\n");

	return ret;
}

static int prd_compare_rawdata(struct device *dev, char *buf, u8 type, uint16_t *mutual_data, uint16_t *self_data)
{
	int i, j;
	int ret = 0;
	int result = 0;
	uint16_t mutual_upper = 0;
	uint16_t mutual_lower = 0;
	uint16_t self_upper = 0;
	uint16_t self_lower = 0;
	int fail_num = 0;

	switch (type) {
		case RAW_DATA_TEST:
			mutual_upper = MUL_RAW_DATA_THX_UP;
			mutual_lower = MUL_RAW_DATA_THX_LOW;
			self_upper = SELF_RAW_DATA_THX_UP;
			self_lower = SELF_RAW_DATA_THX_LOW;
			break;
		case JITTER_TEST:
			mutual_upper = JITTER_THX;
			mutual_lower = 0;
			self_upper = JITTER_THX;
			self_lower = 0;
			break;
		case LPWG_RAW_DATA_TEST:
			mutual_upper = MUL_RAW_DATA_THX_UP;
			mutual_lower = MUL_RAW_DATA_THX_LOW;
			self_upper = SELF_RAW_DATA_THX_UP;
			self_lower = SELF_RAW_DATA_THX_LOW;
			break;
		case LPWG_JITTER_TEST:
			mutual_upper = JITTER_THX;
			mutual_lower = 0;
			self_upper = JITTER_THX;
			self_lower = 0;
			break;
		default:
			TOUCH_I("Test Type not defined\n");
			return 1;
	}

	himax_set_limit(dev, type, mutual_upper, self_upper);

	if(type == RAW_DATA_TEST)
	{
		for (i = 0; i < ROW_SIZE; i++) {
			for (j = 0; j < COL_SIZE; j++) {
				if ((mutual_data[i * COL_SIZE + j] < MutualLowerImage[i][j]) ||
						(mutual_data[i * COL_SIZE + j] > MutualUpperImage[i][j])) {
					result = 1;
					fail_num++;
					ret += snprintf(W_Buf + ret, BUF_SIZE - ret,
							"F Mutual[%d][%d] = %d\n", i, j, mutual_data[i * COL_SIZE + j]);
					TOUCH_I("F Type%d Mutual[%d][%d] = %d\n", type, i, j, mutual_data[i * COL_SIZE + j]);
				}
			}
		}

		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			if ((self_data[i] < SelfLowerImage[i]) ||
					(self_data[i] > SelfUpperImage[i])) {
				result = 1;
				fail_num++;
				ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"F Self[%d] = %d\n", i, self_data[i]);
				TOUCH_I("F Type%d Self[%d] = %d\n", type, i, self_data[i]);
			}
		}
	}
#ifdef USE_LPWG_MUTUAL
	if(type == LPWG_RAW_DATA_TEST)
	{
		for (i = 0; i < ROW_SIZE; i++) {
			for (j = 0; j < COL_SIZE; j++) {
				if ((mutual_data[i * COL_SIZE + j] < MutualLowerImage[i][j]) ||
						(mutual_data[i * COL_SIZE + j] > MutualUpperImage[i][j])) {
					result = 1;
					fail_num++;
					ret += snprintf(W_Buf + ret, BUF_SIZE - ret,
							"F Mutual[%d][%d] = %d\n", i, j, mutual_data[i * COL_SIZE + j]);
					TOUCH_I("F Type%d Mutual[%d][%d] = %d\n", type, i, j, mutual_data[i * COL_SIZE + j]);
				}
			}
		}

		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			if ((self_data[i] < SelfLowerImage[i]) ||
					(self_data[i] > SelfUpperImage[i])) {
				result = 1;
				fail_num++;
				ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"F Self[%d] = %d\n", i, self_data[i]);
				TOUCH_I("F Type%d Self[%d] = %d\n", type, i, self_data[i]);
			}
		}
	}
	if(type == JITTER_TEST || type == LPWG_JITTER_TEST)
	{
		for (i = 0; i < ROW_SIZE; i++) {
			for (j = 0; j < COL_SIZE; j++) {
				if (mutual_data[i * COL_SIZE + j] > MutualUpperImage[i][j]) {
					result = 1;
					fail_num++;
					ret += snprintf(buf + ret, PAGE_SIZE - ret,
							"F Mutual[%d][%d] = %d\n", i, j, mutual_data[i * COL_SIZE + j]);
					TOUCH_I("F Type%d Mutual[%d][%d] = %d, MutualUpperImage[%d][%d]= %d \n", type, i, j, mutual_data[i * COL_SIZE + j], i, j, MutualUpperImage[i][j]);
				}
			}
		}

		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			if (self_data[i] > SelfUpperImage[i]) {
				result = 1;
				fail_num++;
				ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"F Self[%d] = %d\n", i, self_data[i]);
				TOUCH_I("F Type%d Self[%d] = %d, SelfUpperImage[%d] = %d\n", type, i, self_data[i], i, SelfUpperImage[i]);
			}
		}
	}
#else
	if(type == JITTER_TEST)
	{
		for (i = 0; i < ROW_SIZE; i++) {
			for (j = 0; j < COL_SIZE; j++) {
				if (mutual_data[i * COL_SIZE + j] > MutualUpperImage[i][j]) {
					result = 1;
					fail_num++;
					ret += snprintf(buf + ret, PAGE_SIZE - ret,
							"F Mutual[%d][%d] = %d\n", i, j, mutual_data[i * COL_SIZE + j]);
					TOUCH_I("F Type%d Mutual[%d][%d] = %d, MutualUpperImage[%d][%d]= %d \n", type, i, j, mutual_data[i * COL_SIZE + j], i, j, MutualUpperImage[i][j]);
				}
			}
		}

		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			if (self_data[i] > SelfUpperImage[i]) {
				result = 1;
				fail_num++;
				ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"F Self[%d] = %d\n", i, self_data[i]);
				TOUCH_I("F Type%d Self[%d] = %d, SelfUpperImage[%d] = %d\n", type, i, self_data[i], i, SelfUpperImage[i]);
			}
		}
	}
	if(type == LPWG_RAW_DATA_TEST)
	{
		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			if ((self_data[i] < SelfLowerImage[i]) ||
					(self_data[i] > SelfUpperImage[i])) {
				result = 1;
				fail_num++;
				ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"F Self[%d] = %d\n", i, self_data[i]);
				TOUCH_I("F Type%d Self[%d] = %d\n", type, i, self_data[i]);
			}
		}
	}
	if(type == LPWG_JITTER_TEST)
	{
		for (i = 0; i < ROW_SIZE + COL_SIZE; i++) {
			if (self_data[i] > SelfUpperImage[i]) {
				result = 1;
				fail_num++;
				ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"F Self[%d] = %d\n", i, self_data[i]);
				TOUCH_I("F Type%d Self[%d] = %d, SelfUpperImage[%d] = %d\n", type, i, self_data[i], i, SelfUpperImage[i]);
			}
		}
	}
#endif /* USE_LPWG_MUTUAL */
	if(result == 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Test Fail : %d\n", fail_num);
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Test PASS : No Errors\n");
	ret += snprintf(buf + ret, PAGE_SIZE - ret,
			"mutual MAX = %d, MIN = %d, Upper = %d, Lower = %d\n",
			mul_max, mul_min, mutual_upper, mutual_lower);
	ret += snprintf(buf + ret, PAGE_SIZE - ret,
			"Self   MAX = %d, MIN = %d, Upper = %d, Lower = %d\n",
			self_max, self_min, self_upper, self_lower);

	mul_min = 9999;
	mul_max = 0;
	self_min = 9999;
	self_max = 0;
	return result;
}

void hx8527_control_reK(struct device *dev, uint8_t OnOff)
{
	uint8_t buf[4];

	TOUCH_I("[Himax]: ReK status = %x\n", OnOff);

	if(OnOff)
	{
		buf[0] = 0x00;
		TOUCH_I("[Himax]: enable reK buf[0]=%x\n",buf[0]);
	}
	else
	{
		buf[0] = 0x80;
		TOUCH_I("[Himax]: disable reK buf[0]=%x\n",buf[0]);
	}
	hx8527_bus_write(dev, 0xF4, &buf[0], 1);
	msleep(10);

	return;
}

static int prd_read_rawdata(struct device *dev, char *buf, u8 type, uint16_t *mutual_data, uint16_t *self_data)
{
	uint8_t diag_cmd = 0;
	int ret = 0;

	if(type == JITTER_TEST || type == LPWG_JITTER_TEST)
		diag_cmd = 0x17;
	else
		diag_cmd = g_diag_command;

	g_self_test_entered = 1;

	ret = hx8527_get_raw_data(dev, diag_cmd, MUTUAL_NUM, SELF_NUM, mutual_data, self_data);

	g_self_test_entered = 0;

	return 0;
}

static int prd_rawdata_test(struct device *dev, char *buf, u8 type)
{
	uint16_t *mutual_data = NULL;
	uint16_t *self_data = NULL;
	int ret = 0;

	switch (type) {
		case RAW_DATA_TEST:
			g_diag_command = 2;
			break;
		case JITTER_TEST:
			g_diag_command = 7;
			break;
		case LPWG_RAW_DATA_TEST:
			g_diag_command = 2;
			break;
		case LPWG_JITTER_TEST:
			g_diag_command = 7;
			break;
		default:
			TOUCH_I("Test Type not defined\n");
			return 1;
	}

	mutual_data = kzalloc(MUTUAL_NUM * sizeof(uint16_t), GFP_KERNEL);
	if (mutual_data == NULL) {
		TOUCH_E("%s : alloc mutual_data fail", __func__);
		ret = -1;
		goto mutual_mem_alloc_fail;
	}	
	
	self_data = kzalloc(SELF_NUM * sizeof(uint16_t), GFP_KERNEL);
	if (self_data == NULL) {
		TOUCH_E("%s : alloc self_data fail", __func__);
		ret = -1;
		goto self_mem_alloc_fail;
	}

	prd_read_rawdata(dev, buf, type, mutual_data, self_data);

	memcpy(Rawdata_buf, mutual_data, MUTUAL_NUM * sizeof(uint16_t));
	memcpy(&Rawdata_buf[MUTUAL_NUM], self_data, SELF_NUM * sizeof(uint16_t));

	ret = prd_print_data(dev, W_Buf, type);
	hx8527_write_file(dev, W_Buf, TIME_INFO_SKIP);
	memset(W_Buf, 0, BUF_SIZE);

	ret = prd_compare_rawdata(dev, W_Buf, type, mutual_data, self_data);
	hx8527_write_file(dev, W_Buf, TIME_INFO_SKIP);
	memset(W_Buf, 0, BUF_SIZE);


	kfree(self_data);
self_mem_alloc_fail:
	kfree(mutual_data);
mutual_mem_alloc_fail:

	return ret;
}
static int prd_print_os_result(struct device *dev, char *buf, uint16_t result, uint8_t *os_fail_num)
{
	int ret = 0;
	uint16_t mutual_upper = RB1H[2];
	uint16_t mutual_lower = RB1H[3];
	uint16_t self_upper = RB1H[6];
	uint16_t self_lower = RB1H[7];

	if(result == 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Test PASS : No Errors\n");
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Test Fail : %d open, %d short \n",
				os_fail_num[0] + os_fail_num[2],os_fail_num[1] + os_fail_num[3]);

	ret += snprintf(buf + ret, PAGE_SIZE - ret,
			"mutual MAX = %d, MIN = %d, Upper = %d, Lower = %d\n",
			mul_max, mul_min, mutual_upper, mutual_lower);
	ret += snprintf(buf + ret, PAGE_SIZE - ret,
			"Self   MAX = %d, MIN = %d, Upper = %d, Lower = %d\n",
			self_max, self_min, self_upper, self_lower);

	mul_min = 9999;
	mul_max = 0;
	self_min = 9999;
	self_max = 0;

	return ret;
}

static int prd_open_short_test(struct device *dev, char *buf, u8 type)
{
	int result = 0;

	g_self_test_entered = 1;
	memset(open_short_buf, 0x00, (MUTUAL_NUM + SELF_NUM) * sizeof(u16));
	result = hx8527_chip_self_test(dev, RB1H, open_short_buf, g_os_fail_num, type);
	g_self_test_entered = 0;


	/* Open Short Test Data */
	prd_print_data(dev, W_Buf, type);
	hx8527_write_file(dev, W_Buf, TIME_INFO_SKIP);
	memset(W_Buf, 0, BUF_SIZE);

	/* Open Short Test Result */
	prd_print_os_result(dev, W_Buf, result, g_os_fail_num);
	hx8527_write_file(dev, W_Buf, TIME_INFO_SKIP);
	memset(W_Buf, 0, BUF_SIZE);

	return result;
}

static ssize_t show_sd(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	int rawdata_ret = 0;
	int blu_jitter_ret = 0;
	int openshort_ret = 0;

	//---Check the LCD state---
	if (ts->lpwg.screen == 0){
		TOUCH_E("LCD is OFF, Please turn on.\n");
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "LCD is OFF, Please turn on.\n");
		return ret;
	}

	hx8527_control_reK(dev, 0);

	/* file create , time log */
	hx8527_write_file(dev, "\nShow_sd Test Start", TIME_INFO_SKIP);
	hx8527_write_file(dev, "\n", TIME_INFO_WRITE);
	TOUCH_I("Show_sd Test Start\n");

	openshort_ret = prd_open_short_test(dev, buf, OPEN_SHORT_TEST);

	rawdata_ret = prd_rawdata_test(dev, buf, RAW_DATA_TEST);

	blu_jitter_ret = prd_rawdata_test(dev, buf, JITTER_TEST);

	ret = snprintf(buf, PAGE_SIZE,
			"\n========RESULT=======\n");
	if (rawdata_ret == 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Raw Data : Pass\n");
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Raw Data : Fail\n");

	if (openshort_ret == 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Channel Status : Pass\n");
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Channel Status : Fail\n");

	if (blu_jitter_ret == 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Jitter : Pass\n");
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"Jitter : Fail\n");

	hx8527_write_file(dev, buf, TIME_INFO_SKIP);
	hx8527_write_file(dev, "Show_sd Test End\n", TIME_INFO_WRITE);
	hx8527_log_size_check(dev);
	hx8527_control_reK(dev, 1);


	TOUCH_I("%s Waiting for 1s delay time\n", __func__);
	msleep(50);
	TOUCH_I("Show_sd Test End\n");


	return ret;
}

static ssize_t show_lpwg_sd(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	int lpwg_openshort_ret = 0;
	int lpwg_rawdata_ret = 0;
	int lpwg_jitter_ret = 0;
	//uint8_t buf_cmd[4];

	//---Check the LCD state---
	if (!(ts->mfts_lpwg) && ts->lpwg.screen){
		TOUCH_E("LCD is ON, Please turn off.\n");
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "LCD is ON, Please turn off.\n");
		return ret;
	}

	if (ts->mfts_lpwg){
		TOUCH_I("%s Waiting for 800ms delay time\n", __func__);
		msleep(800);
	}
	hx8527_control_reK(dev, 0);

	/* file create , time log */
	hx8527_write_file(dev, "\nShow_lpwg_sd Test Start", TIME_INFO_SKIP);
	hx8527_write_file(dev, "\n", TIME_INFO_WRITE);
	TOUCH_I("Show_lpwg_sd Test Start\n");

	lpwg_openshort_ret = prd_open_short_test(dev, buf, LPWG_OPEN_SHORT_TEST);

	lpwg_rawdata_ret = prd_rawdata_test(dev, buf, LPWG_RAW_DATA_TEST);

	lpwg_jitter_ret = prd_rawdata_test(dev, buf, LPWG_JITTER_TEST);

	ret = snprintf(buf + ret, PAGE_SIZE, "========RESULT=======\n");

	if (!lpwg_rawdata_ret)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"LPWG RawData : Pass\n");
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"LPWG RawData : Fail\n");

	if (!lpwg_openshort_ret)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"LPWG Channel Status : Pass\n");
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"LPWG Channel Status : Fail\n");

	if (!lpwg_jitter_ret)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"LPWG Jitter : Pass\n");
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				"LPWG Jitter : Fail\n");

	hx8527_write_file(dev, buf, TIME_INFO_SKIP);

	hx8527_write_file(dev, "Show_lpwg_sd Test End\n", TIME_INFO_WRITE);
	hx8527_log_size_check(dev);

	hx8527_control_reK(dev, 1);
	TOUCH_I("Show_lpwg_sd Test End\n");

	return ret;
}

static TOUCH_ATTR(sd, show_sd, NULL);
static TOUCH_ATTR(lpwg_sd, show_lpwg_sd, NULL);
static TOUCH_ATTR(rawdata, show_rawdata, NULL);
static TOUCH_ATTR(delta, show_delta, NULL);
static TOUCH_ATTR(jitter, show_jitter, NULL);
static TOUCH_ATTR(rawdata_sm, show_rawdata_sm, NULL);
static TOUCH_ATTR(delta_sm, show_delta_sm, NULL);
static TOUCH_ATTR(jitter_sm, show_jitter_sm, NULL);
static TOUCH_ATTR(power_mode, show_himax_power_mode, NULL);
static TOUCH_ATTR(debug_level, show_himax_debug_level, store_himax_debug_level);
static TOUCH_ATTR(reg, show_himax_proc_register, store_himax_proc_register);
static TOUCH_ATTR(diag, show_himax_diag, store_himax_diag);
static TOUCH_ATTR(hw_reset, NULL, store_reset_ctrl);
static TOUCH_ATTR(debug, show_himax_debug, store_himax_debug);
static TOUCH_ATTR(flash_dump, show_proc_flash, store_proc_flash);
static TOUCH_ATTR(sensor, NULL, store_sensor_state);
static TOUCH_ATTR(gpio_control, show_gpio_control, store_gpio_control);
static TOUCH_ATTR(irq_control, show_irq_control, store_irq_control);

static struct attribute *himax_attribute_list[] = {
	&touch_attr_sd.attr,
	&touch_attr_lpwg_sd.attr,
	&touch_attr_rawdata.attr,
	&touch_attr_delta.attr,
	&touch_attr_jitter.attr,
	&touch_attr_rawdata_sm.attr,
	&touch_attr_delta_sm.attr,
	&touch_attr_jitter_sm.attr,
	&touch_attr_power_mode.attr,
	&touch_attr_debug_level.attr,
	&touch_attr_reg.attr,
	&touch_attr_diag.attr,
	&touch_attr_hw_reset.attr,
	&touch_attr_debug.attr,
	&touch_attr_flash_dump.attr,
	&touch_attr_sensor.attr,
	&touch_attr_gpio_control.attr,
	&touch_attr_irq_control.attr,
	NULL,
};

static const struct attribute_group himax_attribute_group = {
	.attrs = himax_attribute_list,
};

int hx8527_register_sysfs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	TOUCH_TRACE();

	ret = sysfs_create_group(&ts->kobj, &himax_attribute_group);
	if (ret < 0)
		TOUCH_E("himax sysfs register failed\n");

	return 0;
}

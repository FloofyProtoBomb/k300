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

#include <touch_core.h>
#include <touch_hwif.h>
#include <touch_common.h>
#include <linux/regulator/consumer.h>

#include "touch_hx8527.h"

extern int g_self_test_entered;
extern int check_recovery_boot;

struct himax_ic_data *ic_data;
struct himax_report_data *hx_touch_data;

static const char const *tci_debug_str[8] = {
	"NONE",
	"DISTANCE_INTER_TAP",
	"DISTANCE_TOUCHSLOP",
	"TIMEOUT_INTER_TAP",
	"MULTI_FINGER",
	"DELAY_TIME", /* It means Over Tap */
	"PALM_STATE",
	"Reserved"
};

int hx8527_FlashMode(struct device *dev, int enter)
{
	uint8_t cmd[2];
	cmd[0] = enter;
	if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 1) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}
	return 0;
}

static bool hx8527_calculateChecksum(struct device *dev)
{
	uint8_t cmd[10];

	memset(cmd, 0x00, sizeof(cmd));

	//Sleep out
	if( hx8527_bus_write(dev, HX_CMD_TSSLPOUT ,&cmd[0], 0) < 0)
	{
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}
	msleep(120);

	while(true){
		cmd[0] = 0x00;
		cmd[1] = 0x04;
		cmd[2] = 0x0A;
		cmd[3] = 0x02;

		if (hx8527_bus_write(dev, 0xED ,&cmd[0], 4) < 0) {
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return 0;
		}

		//Enable Flash
		cmd[0] = 0x01;
		cmd[1] = 0x00;
		cmd[2] = 0x02;

		if (hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 3) < 0) {
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return 0;
		}
		cmd[0] = 0x05;
		if (hx8527_bus_write(dev, 0xD2 ,&cmd[0], 1) < 0) {
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return 0;
		}

		cmd[0] = 0x01;
		if (hx8527_bus_write(dev, 0x53 ,&cmd[0], 1) < 0) {
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return 0;
		}

		msleep(200);

		if (hx8527_bus_read(dev, 0xAD, cmd, 4) < 0) {
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return -1;
		}

		TOUCH_I("%s 0xAD[0,1,2,3] = %d,%d,%d,%d \n",__func__,cmd[0],cmd[1],cmd[2],cmd[3]);

		if (cmd[0] == 0 && cmd[1] == 0 && cmd[2] == 0 && cmd[3] == 0 ) {
			hx8527_FlashMode(dev, 0);
			return 1;
		} else {
			hx8527_FlashMode(dev, 0);
			return 0;
		}

	}
	return 0;
}

static int hx8527_read_Sensor_ID(struct device *dev)
{
	uint8_t val_high[1], val_low[1], ID0=0, ID1=0;
	char data[3];
	int sensor_id;

	data[0] = 0x56; data[1] = 0x02; data[2] = 0x02;/*ID pin PULL High*/
	hx8527_bus_master_write(dev, &data[0], 3);
	msleep(1);

	//read id pin high
	hx8527_bus_read(dev, 0x57, val_high, 1);

	data[0] = 0x56; data[1] = 0x01; data[2] = 0x01;/*ID pin PULL Low*/
	hx8527_bus_master_write(dev, &data[0], 3);
	msleep(1);

	//read id pin low
	hx8527_bus_read(dev, 0x57, val_low, 1);

	if((val_high[0] & 0x01) ==0)
		ID0=0x02;/*GND*/
	else if((val_low[0] & 0x01) ==0)
		ID0=0x01;/*Floating*/
	else
		ID0=0x04;/*VCC*/

	if((val_high[0] & 0x02) ==0)
		ID1=0x02;/*GND*/
	else if((val_low[0] & 0x02) ==0)
		ID1=0x01;/*Floating*/
	else
		ID1=0x04;/*VCC*/
	if((ID0==0x04)&&(ID1!=0x04))
	{
		data[0] = 0x56; data[1] = 0x02; data[2] = 0x01;/*ID pin PULL High,Low*/
		hx8527_bus_master_write(dev, &data[0], 3);
		msleep(1);

	}
	else if((ID0!=0x04)&&(ID1==0x04))
	{
		data[0] = 0x56; data[1] = 0x01; data[2] = 0x02;/*ID pin PULL Low,High*/
		hx8527_bus_master_write(dev, &data[0], 3);
		msleep(1);

	}
	else if((ID0==0x04)&&(ID1==0x04))
	{
		data[0] = 0x56; data[1] = 0x02; data[2] = 0x02;/*ID pin PULL High,High*/
		hx8527_bus_master_write(dev, &data[0], 3);
		msleep(1);

	}
	sensor_id=(ID1<<4)|ID0;

	data[0] = 0xE4;
	data[1] = sensor_id;
	hx8527_bus_master_write(dev, &data[0], 2);/*Write to MCU*/
	msleep(1);

	return sensor_id;

}

void hx8527_read_FW_ver(struct device *dev)
{
	uint8_t data[64];

	//read fw version
	if (hx8527_bus_read(dev, HX_VER_FW_MAJ, data, 1) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return;
	}
	ic_data->vendor_fw_ver =  data[0]<<8;

	if (hx8527_bus_read(dev, HX_VER_FW_MIN, data, 1) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return;
	}
	ic_data->vendor_fw_ver = data[0] | ic_data->vendor_fw_ver;

	//read config version
	if (hx8527_bus_read(dev, HX_VER_FW_CFG, data, 1) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return;
	}
	ic_data->vendor_config_ver = data[0];
	//read sensor ID
	ic_data->vendor_sensor_id = hx8527_read_Sensor_ID(dev);

	TOUCH_I("sensor_id=%x.\n",ic_data->vendor_sensor_id);
	TOUCH_I("fw_ver=%x.\n",ic_data->vendor_fw_ver);
	TOUCH_I("config_ver=%x.\n",ic_data->vendor_config_ver);

	return;
}

void hx8527_touch_information(struct device *dev)
{
	char data[20] = {0};
	Is_2T2R = true;

	TOUCH_I("%s:HX_RX_NUM =%d,HX_TX_NUM =%d\n", __func__,HX_RX_NUM, HX_TX_NUM);

	if (hx8527_bus_read(dev, 0xB1, data, 17) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
	}

	ic_data->lge_ver_major = data[15];
	ic_data->lge_ver_minor = data[16];
	TOUCH_I("%s:lge_ver_major = %d,lge_ver_minor = %d \n", __func__,data[15], data[16]);

	if (hx8527_bus_read(dev, HX_VER_FW_CFG, data, 1) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
	}
	ic_data->vendor_config_ver = data[0];
	TOUCH_I("config_ver=%x.\n",ic_data->vendor_config_ver);
}

void hx8527_usb_detect_set(struct device *dev, uint8_t *cable_config)
{
	uint8_t tmp_data[4];
	uint8_t retry_cnt = 0;

	do
	{
		hx8527_bus_master_write(dev, cable_config, 2);

		hx8527_bus_read(dev, cable_config[0], tmp_data, 1);
		retry_cnt++;
	}while(tmp_data[0] != cable_config[1] && retry_cnt < HIMAX_REG_RETRY_TIMES);

}

void hx8527_sensor_on(struct device *dev)
{
	hx8527_bus_write_command(dev, HX_CMD_TSSON);
	msleep(30);

	hx8527_bus_write_command(dev, HX_CMD_TSSLPOUT);
	msleep(50);

	TOUCH_I("%s\n", __func__);
}

void hx8527_sensor_off(struct device *dev)
{
	hx8527_bus_write_command(dev, HX_CMD_TSSOFF);
	msleep(50);

	hx8527_bus_write_command(dev, HX_CMD_TSSLPIN);
	msleep(50);

	TOUCH_I("%s\n", __func__);
}

int hx8527_hand_shaking(struct device *dev)    //0:Running, 1:Stop, 2:I2C Fail
{
	struct himax_ts_data *d = to_himax_data(dev);
	int ret, result;
	uint8_t hw_reset_check[1];
	uint8_t hw_reset_check_2[1];
	uint8_t buf0[2];


	memset(hw_reset_check, 0x00, sizeof(hw_reset_check));
	memset(hw_reset_check_2, 0x00, sizeof(hw_reset_check_2));

	buf0[0] = 0xF2;
	if (d->ic_status == 0xAA) {
		buf0[1] = 0xAA;
		d->ic_status = 0x55;
	} else {
		buf0[1] = 0x55;
		d->ic_status = 0xAA;
	}

	ret = hx8527_bus_master_write(dev, buf0, 2);
	if (ret < 0) {
		TOUCH_E("[Himax]:write 0xF2 failed line: %d \n",__LINE__);
		goto work_func_send_i2c_msg_fail;
	}
	msleep(50);

	buf0[0] = 0xF2;
	buf0[1] = 0x00;
	ret = hx8527_bus_master_write(dev, buf0, 2);
	if (ret < 0) {
		TOUCH_E("[Himax]:write 0x92 failed line: %d \n",__LINE__);
		goto work_func_send_i2c_msg_fail;
	}
	msleep(2);

	ret = hx8527_bus_read(dev, 0x90, hw_reset_check, 1);
	if (ret < 0) {
		TOUCH_E("[Himax]:i2c_himax_read 0x90 failed line: %d \n",__LINE__);
		goto work_func_send_i2c_msg_fail;
	}

	if ((d->ic_status != hw_reset_check[0])) {
		msleep(2);
		ret = hx8527_bus_read(dev, 0x90, hw_reset_check_2, 1);
		if (ret < 0) {
			TOUCH_E("[Himax]:i2c_himax_read 0x90 failed line: %d \n",__LINE__);
			goto work_func_send_i2c_msg_fail;
		}

		if (hw_reset_check[0] == hw_reset_check_2[0]) {
			result = 1;
		} else {
			result = 0;
		}
	} else {
		result = 0;
	}

	return result;

work_func_send_i2c_msg_fail:
	return 2;
}

int hx8527_ic_esd_recovery(struct device *dev, int hx_esd_event,int hx_zero_event,int length)
{
	int shaking_ret = 0;

	/* hand shaking status: 0:Running, 1:Stop, 2:I2C Fail */
	shaking_ret = hx8527_hand_shaking(dev);
	if(shaking_ret == 2)
	{
		TOUCH_I("I2C Fail.\n");
		goto err_workqueue_out;
	}
	if(hx_esd_event == length)
	{
		goto checksum_fail;
	}
	else if(shaking_ret == 1 && hx_zero_event == length)
	{
		TOUCH_I("ESD event checked - ALL Zero.\n");
		goto checksum_fail;
	}
	else
		goto workqueue_out;

checksum_fail:
	return CHECKSUM_FAIL;
err_workqueue_out:
	return ERR_WORK_OUT;
workqueue_out:
	return WORK_OUT;
}

int hx8527_bus_read(struct device *dev, uint8_t command, uint8_t *data, uint8_t length)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	struct touch_bus_msg msg = {0, };
	int ret = 0;

	mutex_lock(&d->rw_lock);

	ts->tx_buf[0] = command;
	msg.tx_buf = ts->tx_buf;
	msg.tx_size = 1;
	msg.rx_buf = ts->rx_buf;
	msg.rx_size = length;

	ret = touch_bus_read(dev, &msg);
	if (ret < 0) {
		TOUCH_E("touch bus read error : %d\n", ret);
		mutex_unlock(&d->rw_lock);
		return ret;
	}

	memcpy(data, &ts->rx_buf[0], length);

	mutex_unlock(&d->rw_lock);

	return 0;
}

int hx8527_bus_write(struct device *dev, uint8_t command, uint8_t *data, uint8_t length)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	struct touch_bus_msg msg = {0, };
	int ret = 0;

	mutex_lock(&d->rw_lock);

	ts->tx_buf[0] = command;
	memcpy(&ts->tx_buf[1], data, length);
	msg.tx_buf = ts->tx_buf;
	msg.tx_size = length + 1;
	msg.rx_buf = NULL;
	msg.rx_size = 0;

	ret = touch_bus_write(dev, &msg);
	if (ret < 0) {
		TOUCH_E("touch bus write error : %d\n", ret);
		mutex_unlock(&d->rw_lock);
		return ret;
	}

	mutex_unlock(&d->rw_lock);

	return 0;
}

int hx8527_bus_write_command(struct device *dev, uint8_t command)
{
	return hx8527_bus_write(dev, command, NULL, 0);
}

int hx8527_bus_master_write(struct device *dev, uint8_t *data, uint8_t length)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	struct touch_bus_msg msg = {0, };
	int ret = 0;

	mutex_lock(&d->rw_lock);

	memcpy(&ts->tx_buf[0], data, length);
	msg.tx_buf = ts->tx_buf;
	msg.tx_size = length;
	msg.rx_buf = NULL;
	msg.rx_size = 0;

	ret = touch_bus_write(dev, &msg);
	if (ret < 0) {
		TOUCH_E("touch bus write error : %d\n", ret);
		mutex_unlock(&d->rw_lock);
		return ret;
	}

	mutex_unlock(&d->rw_lock);

	return 0;
}

static int hx8527_deep_sleep(struct device *dev, bool onoff)
{
	struct touch_core_data *ts = to_touch_core(dev);

	/* on = deep sleep in, off = deep sleep out */
	if (onoff) {
		touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
		hx8527_sensor_off(dev);
		atomic_set(&ts->state.sleep, IC_DEEP_SLEEP);
		TOUCH_I("IC status = IC_DEEP_SLEEP");
	} else {
		hx8527_sensor_on(dev);
		touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
		atomic_set(&ts->state.sleep, IC_NORMAL);
		TOUCH_I("IC status = IC_NORMAL");
	}

	return 0;
}

static int hx8527_lpwg_control(struct device *dev, int mode)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);

	uint8_t buf[4];

	hx8527_bus_read(dev, 0x8F, buf, 1);

	if (mode == LPWG_MODE)
		buf[0] |= 0x20;
	else
		buf[0] &= 0xDF;

	if (hx8527_bus_write(dev, 0x8F ,buf, 1) < 0)
		TOUCH_E("%s i2c write fail.\n",__func__);

	TOUCH_I("%s() lpwg.mode=%d, mode_state=%d",
			__func__, ts->lpwg.mode, d->mode_state);

	return 0;
}

static int himax_lpwg_mode(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);

	/* FB_SUSPEND */
	if (atomic_read(&ts->state.fb) == FB_SUSPEND) {
		if (ts->mfts_lpwg){
			TOUCH_I("FB_SUSPEND == mfts_lpwg\n");
			d->mode_state = LPWG_MODE;
			hx8527_lpwg_control(dev, d->mode_state);
			d->mode_state = NORMAL_MODE;
			return 0;
		}

		if (ts->lpwg.sensor == PROX_NEAR) {
			TOUCH_I("FB_SUSPEND == PROX_NEAR, %s\n", atomic_read(&ts->state.sleep) ? "IC_DEEP_SLEEP" : "IC_NORMAL" );
			if (atomic_read(&ts->state.sleep) != IC_DEEP_SLEEP) {
				d->mode_state = DEEP_SLEEP_MODE;
				hx8527_deep_sleep(dev, IC_DEEP_SLEEP);
				hx8527_lpwg_control(dev, d->mode_state);
			}
		} else if (ts->lpwg.mode == LPWG_NONE) {
			TOUCH_I("FB_SUSPEND == LPWG_NONE, %s\n", atomic_read(&ts->state.sleep) ? "IC_DEEP_SLEEP" : "IC_NORMAL" );
			if (atomic_read(&ts->state.sleep) != IC_DEEP_SLEEP) {
				d->mode_state = DEEP_SLEEP_MODE;
				hx8527_deep_sleep(dev, IC_DEEP_SLEEP);
				hx8527_lpwg_control(dev, d->mode_state);
			}
		} else if (ts->lpwg.qcover == HALL_NEAR) {
			/* set qcover action */
		} else {
			TOUCH_I("FB_SUSPEND == set LPWG\n");
			d->mode_state = LPWG_MODE;
			hx8527_lpwg_control(dev, d->mode_state);
			if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
				hx8527_deep_sleep(dev, IC_NORMAL);
		}
		return 0;
	}

	touch_report_all_event(ts);

	/* FB_RESUME */
	if (ts->lpwg.screen) {
		TOUCH_I("FB_RESUME == screen on\n");
		d->mode_state = NORMAL_MODE;
		hx8527_lpwg_control(dev, d->mode_state);
		if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
			hx8527_deep_sleep(dev, IC_NORMAL);
			return 0;
		}
		if (ts->lpwg.qcover == HALL_NEAR){
			/* set qcover action */
		}
	} else if (ts->lpwg.sensor == PROX_NEAR) {
		TOUCH_I("FB_RESUME == PROX_NEAR, %s\n", atomic_read(&ts->state.sleep) ? "IC_DEEP_SLEEP" : "IC_NORMAL");
		if (atomic_read(&ts->state.sleep) != IC_DEEP_SLEEP) {
			d->mode_state = DEEP_SLEEP_MODE;
			hx8527_deep_sleep(dev, IC_DEEP_SLEEP);
			hx8527_lpwg_control(dev, d->mode_state);
		}
	}
	return 0;
}

int hx8527_cal_data_len(struct device *dev, int raw_cnt_rmd, int raw_cnt_max)
{
	struct touch_core_data *ts = to_touch_core(dev);

	int RawDataLen;
	if (raw_cnt_rmd != 0x00) {
		RawDataLen = 128 - ((ts->caps.max_id+raw_cnt_max+3)*4) - 1;
	}else{
		RawDataLen = 128 - ((ts->caps.max_id+raw_cnt_max+2)*4) - 1;
	}
	return RawDataLen;
}

void hx8527_power_on_init(struct device *dev)
{
	TOUCH_I("%s:\n", __func__);
	hx8527_sensor_on(dev);
	hx8527_sensor_off(dev);
	hx8527_read_FW_ver(dev);
	hx8527_touch_information(dev);
	hx8527_sensor_on(dev);

	return;
}

int hx8527_report_data_init(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	if(hx_touch_data->hx_coord_buf!=NULL)
	{
		kfree(hx_touch_data->hx_coord_buf);
	}
	if(hx_touch_data->hx_rawdata_buf!=NULL)
	{
		kfree(hx_touch_data->hx_rawdata_buf);
	}
	hx_touch_data->event_size = HX_TOUCH_DATA_SIZE;
	if(hx_touch_data->hx_event_buf!=NULL)
	{
		kfree(hx_touch_data->hx_event_buf);
	}

	hx_touch_data->touch_all_size = HX_TOUCH_DATA_SIZE;
	hx_touch_data->raw_cnt_max = ts->caps.max_id/4;
	hx_touch_data->raw_cnt_rmd = ts->caps.max_id%4;

//
	if (hx_touch_data->raw_cnt_rmd != 0x00) //more than 4 fingers
	{
		hx_touch_data->rawdata_size = hx8527_cal_data_len(dev, hx_touch_data->raw_cnt_rmd, hx_touch_data->raw_cnt_max);
		hx_touch_data->touch_info_size = (ts->caps.max_id+hx_touch_data->raw_cnt_max+2)*4;
	}
	else //less than 4 fingers
	{
		hx_touch_data->rawdata_size = hx8527_cal_data_len(dev, hx_touch_data->raw_cnt_rmd, hx_touch_data->raw_cnt_max);
		hx_touch_data->touch_info_size = (ts->caps.max_id+hx_touch_data->raw_cnt_max+1)*4;
	}
	if((HX_TX_NUM * HX_RX_NUM + HX_TX_NUM + HX_RX_NUM) % hx_touch_data->rawdata_size == 0)
		hx_touch_data->rawdata_frame_size = (HX_TX_NUM * HX_RX_NUM + HX_TX_NUM + HX_RX_NUM) / hx_touch_data->rawdata_size;
	else
		hx_touch_data->rawdata_frame_size = (HX_TX_NUM * HX_RX_NUM + HX_TX_NUM + HX_RX_NUM) / hx_touch_data->rawdata_size + 1;
	TOUCH_I("%s: rawdata_frame_size = %d \n",__func__,hx_touch_data->rawdata_frame_size);
	TOUCH_I("%s: hx_raw_cnt_max:%d,hx_raw_cnt_rmd:%d,g_hx_rawdata_size:%d,hx_touch_data->touch_info_size:%d\n",__func__,hx_touch_data->raw_cnt_max,hx_touch_data->raw_cnt_rmd,hx_touch_data->rawdata_size,hx_touch_data->touch_info_size);
//

	hx_touch_data->hx_coord_buf = kzalloc(sizeof(uint8_t)*(hx_touch_data->touch_info_size),GFP_KERNEL);
	if(hx_touch_data->hx_coord_buf == NULL)
		goto coord_mem_alloc_fail;

	hx_touch_data->hx_rawdata_buf = kzalloc(sizeof(uint8_t)*(hx_touch_data->touch_all_size - hx_touch_data->touch_info_size),GFP_KERNEL);
	if(hx_touch_data->hx_rawdata_buf == NULL)
		goto rawdata_mem_alloc_fail;

	hx_touch_data->hx_event_buf = kzalloc(sizeof(uint8_t)*(hx_touch_data->event_size),GFP_KERNEL);
	if(hx_touch_data->hx_event_buf == NULL)
		goto event_mem_alloc_fail;

	return NO_ERR;

event_mem_alloc_fail:
	kfree(hx_touch_data->hx_rawdata_buf);
rawdata_mem_alloc_fail:
	kfree(hx_touch_data->hx_coord_buf);
coord_mem_alloc_fail:
	TOUCH_I("%s: Memory allocate fail!\n",__func__);
	return MEM_ALLOC_FAIL;

}

void hx8527_reload_config(struct device *dev)
{
	hx8527_power_on_init(dev);
	if(hx8527_report_data_init(dev))
		TOUCH_E("%s: allocate data fail\n",__func__);;
}

int hx8527_power(struct device *dev, int ctrl)
{
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();
	TOUCH_I("%s\n", __func__);

	switch (ctrl) {
		case POWER_OFF:
			touch_gpio_direction_output(ts->reset_pin, 0);
			touch_msleep(ts->caps.hw_reset_delay);
			break;

		case POWER_ON:
			touch_gpio_direction_output(ts->reset_pin, 1);
			touch_msleep(ts->caps.hw_reset_delay);
			break;

		case POWER_HW_RESET_ASYNC:
		case POWER_HW_RESET_SYNC:
			TOUCH_I("%s, POWER_HW_RESET\n", __func__);
			hx8527_reset_ctrl(dev, HW_RESET);
			break;
		default:
			TOUCH_I("%s, not supported power control\n", __func__);
			break;
	}

	return 0;
}

int hx8527_reset_ctrl(struct device *dev, int ctrl)
{
	struct touch_core_data *ts = to_touch_core(dev);
	uint8_t read_R36[2] = {0};
	u8 r36_retry_count = 0;

	TOUCH_TRACE();
	TOUCH_I("%s\n", __func__);

	switch (ctrl) {
		case HW_RESET:
			touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
			touch_gpio_direction_output(ts->reset_pin, 0);
			touch_msleep(ts->caps.hw_reset_delay);
			touch_gpio_direction_output(ts->reset_pin, 1);
			touch_msleep(ts->caps.hw_reset_delay);
			hx8527_sensor_on(dev);
			touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
			break;

		case HW_ESD_RESET:
			if (g_self_test_entered == 1) {
				TOUCH_I("HW_ESD_RESET - skip, self testing...\n");
				break;
			} else {
				TOUCH_I("HW_ESD_RESET - start\n");
			}
			touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);

			while (r36_retry_count < 3) {
				hx8527_power(dev, POWER_OFF);
				hx8527_power(dev, POWER_ON);

				r36_retry_count++;
				if (hx8527_bus_read(dev, 0x36, read_R36, 2) < 0) {
					TOUCH_E("ESD R36 read Fail. retry count = %d\n", r36_retry_count);
					continue;
				}

				if (read_R36[0] != 0x0F || read_R36[1] != 0x53) {
					TOUCH_E("ESD R36 Fail : R36[0]=%d, R36[1]=%d, retry count = %d\n",
						read_R36[0], read_R36[1], r36_retry_count);
					continue;
				}

				hx8527_reload_config(dev);
				break;
			}

			touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
			TOUCH_I("HW_ESD_RESET - end\n");
			break;
	}

	return 0;
}

static int hx8527_suspend(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	int mfts_mode = 0;
	int boot_mode = TOUCH_NORMAL_BOOT;

	TOUCH_I("%s Start\n", __func__);

        boot_mode = touch_check_boot_mode(dev);
        if (boot_mode == TOUCH_CHARGER_MODE
                        || boot_mode == TOUCH_LAF_MODE
                        || boot_mode == TOUCH_RECOVERY_MODE) {
                TOUCH_I("skip [%s]: boot_mode = [%d]\n", __func__, boot_mode);
                return -EPERM;
        }

	mfts_mode = touch_check_boot_mode(dev);
	if ((mfts_mode >= TOUCH_MINIOS_MFTS_FOLDER) && !ts->mfts_lpwg) {
		TOUCH_I("%s - MFTS\n", __func__);
		hx8527_deep_sleep(dev, IC_DEEP_SLEEP);
		return -EPERM;
	} else {
		TOUCH_I("%s : touch_suspend start\n", __func__);
	}

	d->pre_finger_mask = 0;
	himax_lpwg_mode(dev);

	TOUCH_I("%s: END \n", __func__);
	return 0;
}

static int hx8527_resume(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	int boot_mode = TOUCH_NORMAL_BOOT;

	TOUCH_I("%s Start\n", __func__);

        boot_mode = touch_check_boot_mode(dev);
        if (boot_mode == TOUCH_CHARGER_MODE
                        || boot_mode == TOUCH_LAF_MODE
                        || boot_mode == TOUCH_RECOVERY_MODE) {
                TOUCH_I("skip [%s]: boot_mode = [%d]\n", __func__, boot_mode);
                return -EPERM;
        }

	if (atomic_read(&ts->state.core) == CORE_UPGRADE) {
		TOUCH_I("%s - skip resume during the FW_upgrade\n",__func__);
		return 1;
	}

	d->mode_state = NORMAL_MODE;
	himax_lpwg_mode(dev);

	hx8527_reset_ctrl(dev, HW_RESET);
	hx8527_usb_detect_set(d->dev, d->cable_config);

	TOUCH_I("%s: END \n", __func__);

	return 1;
}

static int hx8527_wake_event(struct device *dev, uint8_t *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	int tap_count;

	int i=0, j=0, check_FC = 0, gesture_flag = 0;
	u8 tci_1_fr, extra;

	for(i=0;i<GEST_PTLG_ID_LEN;i++)
	{
		if (check_FC==0)
		{
			if((buf[0]!=0x00)&&((buf[0]<=0x0F)||(buf[0]==EV_GESTURE_PWR)||(buf[0]==EV_GESTURE_FR)))
			{
				check_FC = 1;
				gesture_flag = buf[i];
			}
			else
			{
				check_FC = 0;
				TOUCH_I("ID START at %x , value = %x skip the event\n", i, buf[i]);
				break;
			}
		}
		else
		{
			if(buf[i]!=gesture_flag)
			{
				check_FC = 0;
				TOUCH_I("ID NOT the same %x != %x So STOP parse event\n", buf[i], gesture_flag);
				break;
			}
		}
	}

	if (check_FC == 0)
		return 0;
	if(buf[GEST_PTLG_ID_LEN] != GEST_PTLG_HDR_ID1 ||
			buf[GEST_PTLG_ID_LEN+1] != GEST_PTLG_HDR_ID2)
		return 0;

	if((gesture_flag != EV_GESTURE_PWR)&&(gesture_flag != EV_GESTURE_FR))
	{
		if(!d->gesture_cust_en[gesture_flag])
		{
			TOUCH_I("%s NOT report customer key \n ",__func__);
			return 0;//NOT report customer key
		}
	}
	else
	{
		if(!d->gesture_cust_en[0])
		{
			TOUCH_I("%s NOT report report double click \n",__func__);
			return 0;//NOT report power key
		}
	}

	extra = buf[GEST_PT_EXTRA];
	if (extra == 0)
	{
		TOUCH_I("%s No Extra packet \n",__func__);
		return gesture_flag;
	}

	// add coordinate flag
	tap_count = buf[GEST_PT_COUNT];

	//TOUCH_I("%s Tap Count %d \n",__func__, tap_count);
	ts->lpwg.code_num = tap_count;
	for (i = 0; i < tap_count; i++) {
		j = i*4+2;
		ts->lpwg.code[i].x = (int)(buf[GEST_PT_COUNT+1+i*4]*(ts->tci.area.x2)/255);
		ts->lpwg.code[i].y = (int)(buf[GEST_PT_COUNT+2+i*4]*(ts->tci.area.y2)/255);

		if ( (ts->lpwg.mode >= LPWG_PASSWORD) && (ts->role.hide_coordinate) )
			TOUCH_I("LPWG data xxxx, xxxx\n");
		else
			TOUCH_I("LPWG data %d, %d\n", ts->lpwg.code[i].x, ts->lpwg.code[i].y);
	}
	ts->lpwg.code[tap_count].x = -1;
	ts->lpwg.code[tap_count].y = -1;

	if (gesture_flag == EV_GESTURE_FR)
	{
		tci_1_fr = buf[GEST_PT_STATE_FR];
		TOUCH_I("Knock-on Failure Reason Reported : [%s]\n",tci_debug_str[tci_1_fr]);
	}

	return gesture_flag;
}

static int hx8527_fw_compare(struct device *dev, const struct firmware *fw)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	char buf_version[5];
	unsigned long bin_lge_major_version = 0;
	unsigned long bin_lge_minor_version = 0;
	u32 bin_fw_version = 0;
	u32 bin_cfg_version = 0;
	int update = 0;
	u16 bin_checksum_start = 32 * 1024 - 4; //last 4 bytes of 32k
	u8 bin_checksum[4] = {0, };
	int ret, i;

	memset(buf_version, 0x0, sizeof(buf_version));

	buf_version[0] = fw->data[LGE_VER_MAJ_FLASH_ADDR];
	ret = kstrtoul(buf_version, 10, &bin_lge_major_version);

	buf_version[0] = fw->data[LGE_VER_MIN_FLASH_ADDR];
	buf_version[1] = fw->data[LGE_VER_MIN_FLASH_ADDR+1];
	ret = kstrtoul(buf_version, 10, &bin_lge_minor_version);

	bin_fw_version = fw->data[FW_VER_MAJ_FLASH_ADDR]<<8 | fw->data[FW_VER_MIN_FLASH_ADDR];
	bin_cfg_version = fw->data[FW_CFG_VER_FLASH_ADDR];

	for(i = 0; i < 4; i++){
		bin_checksum[i] = fw->data[bin_checksum_start + i];
		if(bin_checksum[i] != ic_data->lge_checksum[i])
			update = 1;
		TOUCH_I("bin_checksum[%d] = %2X, ic_checksum[%d] = %2X update = %d\n", i, bin_checksum[i], i, ic_data->lge_checksum[i], update);
	}

	if (ts->force_fwup || (ic_data->vendor_fw_ver != bin_fw_version) \
			|| (ic_data->lge_ver_major != bin_lge_major_version) \
			|| (ic_data->lge_ver_minor != bin_lge_minor_version) \
			|| (ic_data->vendor_config_ver != bin_cfg_version) || d->crc_flag == 1)
	{
		update = 1;
	}

	TOUCH_I("%s : bin_ver(fw,cfg) = %d,%02d, ic_ver(fw,cfg) = %d,%02d\n", __func__,
			bin_fw_version, bin_cfg_version, ic_data->vendor_fw_ver, ic_data->vendor_config_ver);
	TOUCH_I("%s : bin_lge_ver = %lu.%02lu, ic_lge_ver = %d.%02d\n", __func__,
			bin_lge_major_version, bin_lge_minor_version, ic_data->lge_ver_major, ic_data->lge_ver_minor);
	TOUCH_I("%s : update = %d, force_fwup = %d\n", __func__, update, ts->force_fwup);

	return update;
}

void hx8527_touch_data(struct device *dev, uint8_t *buf,int ts_status)
{
	struct himax_ts_data *d = to_himax_data(dev);
	uint8_t hx_state_info_pos = hx_touch_data->touch_info_size - 3;

	if(ts_status == HX_REPORT_COORD)
	{
		memcpy(hx_touch_data->hx_coord_buf,&buf[0],hx_touch_data->touch_info_size);
		if(buf[hx_state_info_pos] != 0xFF && buf[hx_state_info_pos + 1] != 0xFF )
			memcpy(hx_touch_data->hx_state_info,&buf[hx_state_info_pos],2);
		else
			memset(hx_touch_data->hx_state_info, 0x00, sizeof(hx_touch_data->hx_state_info));
	}
	else
		memcpy(hx_touch_data->hx_event_buf,buf,hx_touch_data->event_size);

	if(d->diag_command)
		memcpy(hx_touch_data->hx_rawdata_buf,&buf[hx_touch_data->touch_info_size],hx_touch_data->touch_all_size - hx_touch_data->touch_info_size);
}

bool hx8527_read_event_stack(struct device *dev, uint8_t *buf, int length)
{
	uint8_t cmd[4];

	cmd[0] = 0x31;
	if ( hx8527_bus_read(dev, 0x86, buf, length) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		goto err_workqueue_out;
	}

	return 1;

err_workqueue_out:
	return 0;
}

int hx8527_touch_get(struct device *dev, uint8_t *buf, int ts_status)
{
	struct himax_ts_data *d = to_himax_data(dev);
	int ret = 0;
	int info_size = hx_touch_data->touch_info_size;
	int all_size = hx_touch_data->touch_all_size;

	switch(ts_status)
	{
		/*normal*/
		case 1:
			hx_touch_data->diag_cmd = getDiagCommand();

			ret = hx8527_read_event_stack(dev, buf, info_size);

			d->diag_command = buf[info_size - 4] >> 4 & 0x0F;

			if(d->diag_command)
				ret = hx8527_read_event_stack(dev, &buf[info_size], all_size - info_size);

			if (!ret)
			{
				TOUCH_E("%s: can't read data from chip!\n", __func__);
				goto err_workqueue_out;
			}
			break;
			/*SMWP*/
		case 2:
			if(!hx8527_read_event_stack(dev, buf, hx_touch_data->event_size))
			{
				TOUCH_E("%s: can't read data from chip!\n", __func__);
				goto err_workqueue_out;
			}
			break;
		default:
			break;
	}
	return NO_ERR;

err_workqueue_out:
	return I2C_FAIL;

}

void hx8527_report_points(struct device *dev, u8 finger_number)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	struct touch_data *tdata;

	int x = 0;
	int y = 0;
	int w = 0;
	int base = 0;
	int32_t	loop_i = 0;
	uint16_t old_finger = 0;
	uint8_t finger_index = 0;

	ts->new_mask = 0;

	/* finger press */
	if (finger_number != 0)
	{
		old_finger = d->pre_finger_mask;
		d->pre_finger_mask = 0;
		hx_touch_data->finger_num = hx_touch_data->hx_coord_buf[d->point_num_offset - 4] & 0x0F;
		for (loop_i = 0 ; loop_i < ts->caps.max_id ; loop_i++)
		{
			base = loop_i * 4;
			x = hx_touch_data->hx_coord_buf[base] << 8 | hx_touch_data->hx_coord_buf[base + 1];
			y = (hx_touch_data->hx_coord_buf[base + 2] << 8 | hx_touch_data->hx_coord_buf[base + 3]);
			w = hx_touch_data->hx_coord_buf[(ts->caps.max_id * 4) + loop_i];
			if(x >= 0 && x <= ts->tci.area.x2 && y >= 0 && y <= ts->tci.area.y2)
			{
				ts->new_mask |= (1 << loop_i);
				hx_touch_data->finger_num--;
				if ((d->debug_log_level & BIT(3)) > 0)
				{
					hx8527_log_touch_event_detail(d, x, y, w, loop_i, HX_FINGER_ON, old_finger);
				}
				tdata = ts->tdata;
				tdata[loop_i].id = loop_i;
				tdata[loop_i].type = MT_TOOL_FINGER;
				tdata[loop_i].x = x;
				tdata[loop_i].y = y;
				tdata[loop_i].pressure = w;
				tdata[loop_i].width_major = 0;
				tdata[loop_i].width_minor = 0;
				tdata[loop_i].orientation = 0;

				if (!d->first_pressed)
				{
					d->first_pressed = 1;
					TOUCH_I("S1@%d, %d\n", x, y);
				}

				d->pre_finger_data[loop_i][0] = x;
				d->pre_finger_data[loop_i][1] = y;
				if (d->debug_log_level & BIT(1))
					hx8527_log_touch_event(x, y, w, loop_i, HX_FINGER_ON);

				d->pre_finger_mask = d->pre_finger_mask + (1 << loop_i);
				finger_index++;

				TOUCH_D(ABS, "tdata [id:%d x:%d y:%d z:%d - wM:%d wm:%d o:%d)]\n",
						tdata[loop_i].id,
						tdata[loop_i].x, tdata[loop_i].y, tdata[loop_i].pressure,
						tdata[loop_i].width_major, tdata[loop_i].width_minor, tdata[loop_i].orientation);
			}
			/* report coordinates */
			else
			{
				if (loop_i == 0 && d->first_pressed == 1)
				{
					d->first_pressed = 2;
					TOUCH_I("E1@%d, %d\n",
							d->pre_finger_data[0][0] , d->pre_finger_data[0][1]);
				}
				if ((d->debug_log_level & BIT(3)) > 0)
				{
					hx8527_log_touch_event_detail(d, x, y, w, loop_i, HX_FINGER_LEAVE, old_finger);
				}
			}
		}
	}

	/* finger release */
	else
	{
		for (loop_i = 0; loop_i < ts->caps.max_id; loop_i++)
		{
			if(d->pre_finger_mask > 0 && (d->debug_log_level & BIT(3)) > 0)
			{
				if (((d->pre_finger_mask >> loop_i) & 1) == 1)
				{
					if (d->useScreenRes)
					{
						TOUCH_I("status:%X, Screen:F:%02d Up, X:%d, Y:%d\n",
								0, loop_i+1, d->pre_finger_data[loop_i][0] * d->widthFactor >> SHIFTBITS,
								d->pre_finger_data[loop_i][1] * d->heightFactor >> SHIFTBITS);
					}
					else
					{
						TOUCH_I("status:%X, Raw:F:%02d Up, X:%d, Y:%d\n",
								0, loop_i+1, d->pre_finger_data[loop_i][0], d->pre_finger_data[loop_i][1]);
					}
				}
			}
		}
		if (d->pre_finger_mask > 0)
		{
			d->pre_finger_mask = 0;
		}

		if (d->first_pressed == 1)
		{
			d->first_pressed = 2;
			TOUCH_I("E1@%d, %d\n",d->pre_finger_data[0][0] , d->pre_finger_data[0][1]);
		}

		if (d->debug_log_level & BIT(1))
			hx8527_log_touch_event(x, y, w, loop_i, HX_FINGER_LEAVE);
	}
	ts->intr_status = TOUCH_IRQ_FINGER;
	ts->tcount = finger_index;
}

int hx8527_checksum_cal(struct device *dev,uint8_t *buf,int ts_status)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	int hx_EB_event = 0;
	int hx_EC_event = 0;
	int hx_ED_event = 0;
	int hx_esd_event = 0;
	int hx_zero_event = 0;
	int shaking_ret = 0;
	uint16_t check_sum_cal = 0;
	int32_t	loop_i = 0;
	int length = 0;
	int hx_lpwg_ED_event = 0;

	/* Normal */
	if(ts_status == HX_REPORT_COORD)
		length = hx_touch_data->touch_info_size;
	/* SMWP */
	else if(ts_status == HX_REPORT_SMWP_EVENT)
		length = (GEST_PTLG_ID_LEN+GEST_PTLG_HDR_LEN);
	else
	{
		TOUCH_I("%s, Neither Normal Nor SMWP error!\n",__func__);
	}

	if((ts_status == HX_REPORT_SMWP_EVENT) && (buf[d->point_num_offset] == 0xFF))
	{
		goto workqueue_out;
	}

	//TOUCH_I("Now status=%d,length=%d\n",ts_status,length);
	for (loop_i = 0; loop_i < length; loop_i++)
	{
		check_sum_cal+=buf[loop_i];

		if (d->debug_log_level & BIT(0))
		{
			TOUCH_I("P %d = 0x%2.2X ", loop_i, buf[loop_i]);
			if (loop_i % 8 == 7)
				TOUCH_I("\n");
		}
		if(ts_status == HX_REPORT_COORD)
		{
			/* case 1 ESD recovery flow */
			if(buf[loop_i] == 0xEB)
				hx_EB_event++;
			else if(buf[loop_i] == 0xEC)
				hx_EC_event++;
			else if(buf[loop_i] == 0xED)
				hx_ED_event++;
			/* case 2 ESD recovery flow-Disable */
			else if(buf[loop_i] == 0x00)
				hx_zero_event++;
			else
			{
				hx_EB_event = 0;
				hx_EC_event = 0;
				hx_ED_event = 0;
				hx_zero_event = 0;
			}

			if(hx_EB_event == length)
			{
				hx_esd_event = length;
				TOUCH_I("ESD event checked - ALL 0xEB.\n");
			}
			else if(hx_EC_event == length)
			{
				hx_esd_event = length;
				TOUCH_I("ESD event checked - ALL 0xEC.\n");
			}
			else if(hx_ED_event == length)
			{
				hx_esd_event = length;
				TOUCH_I("ESD event checked - ALL 0xED.\n");
			}
			else
			{
				hx_esd_event = 0;
			}
		}
	}
	if(ts_status == HX_REPORT_SMWP_EVENT && ts->lpwg.screen == 0)
	{
		for (loop_i = 0; loop_i < length; loop_i++)
		{
			if(buf[loop_i] == 0xED)
				hx_lpwg_ED_event++;
		}
		if(hx_lpwg_ED_event == length)
		{
			d->mode_state = LPWG_MODE;
			hx8527_lpwg_control(dev, d->mode_state);
		}
	}

	if ((hx_esd_event == length || hx_zero_event == length)
			&& (hx_touch_data->diag_cmd == 0)
			&& (g_self_test_entered == 0) ) {
		shaking_ret = hx8527_ic_esd_recovery(dev, hx_esd_event,hx_zero_event,length);
		if(shaking_ret == CHECKSUM_FAIL)
		{
			hx8527_reset_ctrl(dev, HW_ESD_RESET);
			goto workqueue_out;
		}
		else if(shaking_ret == ERR_WORK_OUT)
			goto err_workqueue_out;
		else
		{
			//TOUCH_I("I2C running. Nothing to be done!\n");
			goto workqueue_out;
		}
	}

	if ((check_sum_cal % 0x100 != 0) )
	{
		goto checksum_fail;
	}

	return NO_ERR;

checksum_fail:
	return CHECKSUM_FAIL;
err_workqueue_out:
	return ERR_WORK_OUT;
workqueue_out:
	return WORK_OUT;
}

int hx8527_ts_status(struct himax_ts_data *d)
{
	/* 1: normal, 2:SMWP */
	int result = HX_REPORT_COORD;
	uint8_t diag_cmd = 0;

	diag_cmd = getDiagCommand();

	if ((d->mode_state == LPWG_MODE)&&(!diag_cmd)) {
		result = HX_REPORT_SMWP_EVENT;
	}
	return result;
}

int hx8527_coord_report(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	u8 palm = 0;
	u8 is_release_event = 0;
	u8 finger_number = 0;

	if (atomic_read(&d->init) == IC_INIT_NEED)
		return 0;

	//touch monitor raw data fetch
	if(hx8527_set_diag_cmd(dev, ic_data, hx_touch_data))
		TOUCH_I("%s: coordinate dump fail and bypass with checksum err\n", __func__);

	palm = (hx_touch_data->hx_state_info[0] >> 3 & 0x01);

	if (hx_touch_data->hx_coord_buf[d->point_num_offset] == 0xff) {
		is_release_event = 1;
		finger_number = 0;
	} else {
		finger_number = hx_touch_data->hx_coord_buf[d->point_num_offset] & 0x0f;
	}

	if ((atomic_read(&d->palm) == PALM_RELEASED) && palm) {
		atomic_set(&d->palm, PALM_DETECTED);
		TOUCH_I("palm detected!!\n");
		ts->is_cancel = 1;
	} else if ((atomic_read(&d->palm) == PALM_DETECTED) && (!palm || is_release_event)) {
		atomic_set(&d->palm, PALM_RELEASED);
		TOUCH_I("palm released\n");
	}

	hx8527_report_points(dev, finger_number);

	return 0;
}

static void hx8527_ts_flash_work_func(struct work_struct *flash_work)
{
	struct himax_ts_data *d = container_of(to_delayed_work(flash_work), struct himax_ts_data, flash_work);
	hx8527_ts_flash_func(d->dev);
}

static void hx8527_ts_reset_work_func(struct work_struct *reset_work)
{
	struct himax_ts_data *d = container_of(to_delayed_work(reset_work), struct himax_ts_data, reset_work);
	struct touch_core_data *ts = to_touch_core(d->dev);

	TOUCH_I("%s srart\n", __func__);
	mutex_lock(&ts->lock);
	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
	touch_report_all_event(ts);
	touch_gpio_direction_output(ts->reset_pin, 0);
	touch_msleep(ts->caps.hw_reset_delay);
	touch_gpio_direction_output(ts->reset_pin, 1);
	touch_msleep(ts->caps.hw_reset_delay);
	hx8527_sensor_on(d->dev);
	touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	hx8527_usb_detect_set(d->dev, d->cable_config);
	TOUCH_I("%s end\n", __func__);
}

static int hx8527_ManualMode(struct device *dev, int enter)
{
	uint8_t cmd[2];
	cmd[0] = enter;
	if ( hx8527_bus_write(dev, HX_CMD_MANUALMODE ,&cmd[0], 1) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}
	return 0;
}

static int hx8527_lock_flash(struct device *dev, int enable)
{
	uint8_t cmd[5];

	if (hx8527_bus_write(dev, 0xAA ,&cmd[0], 0) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}

	/* lock sequence start */
	cmd[0] = 0x01;cmd[1] = 0x00;cmd[2] = 0x06;
	if (hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 3) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}

	cmd[0] = 0x03;cmd[1] = 0x00;cmd[2] = 0x00;
	if (hx8527_bus_write(dev, HX_CMD_FLASH_SET_ADDRESS ,&cmd[0], 3) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}

	if(enable!=0){
		cmd[0] = 0x63;cmd[1] = 0x02;cmd[2] = 0x70;cmd[3] = 0x03;
	}
	else{
		cmd[0] = 0x63;cmd[1] = 0x02;cmd[2] = 0x30;cmd[3] = 0x00;
	}

	if (hx8527_bus_write(dev, HX_CMD_FLASH_WRITE_REGISTER ,&cmd[0], 4) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}

	if (hx8527_bus_write_command(dev, HX_CMD_4A) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}
	msleep(50);

	if (hx8527_bus_write(dev, 0xA9 ,&cmd[0], 0) < 0) {
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return 0;
	}

	return 0;
	/* lock sequence stop */
}

int fts_ctpm_fw_upgrade_with_sys_fs_32k(struct device *dev, unsigned char *fw, int len)
{
	unsigned char* ImageBuffer = fw;
	int fullFileLength = len;
	int i;
	uint8_t cmd[5], last_byte, prePage;
	int FileLength = 0;
	uint8_t checksumResult = 0;
	int Flashing_Flag = 0;

	TOUCH_I("Enter %s\n", __func__);
	if (len != 0x8000)   //32k
	{
		TOUCH_E("%s: The file size is not 32K bytes, len = %d \n", __func__, fullFileLength);
		return -EUPGRADE;
	}

	hx8527_power(dev, POWER_OFF);
	hx8527_power(dev, POWER_ON);

	FileLength = fullFileLength;

	if ( hx8527_bus_write(dev, HX_CMD_TSSLPOUT ,&cmd[0], 0) < 0)
	{
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return -EUPGRADE;
	}

	msleep(120);

	hx8527_lock_flash(dev, 0);

	cmd[0] = 0x05;cmd[1] = 0x00;cmd[2] = 0x02;
	if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 3) < 0)
	{
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return -EUPGRADE;
	}

	if ( hx8527_bus_write(dev, 0x4F ,&cmd[0], 0) < 0)
	{
		TOUCH_E("%s: i2c access fail!\n", __func__);
		return -EUPGRADE;
	}
	msleep(50);

	hx8527_ManualMode(dev, 1);
	hx8527_FlashMode(dev, 1);

	FileLength = (FileLength + 3) / 4;
	for (i = 0, prePage = 0; i < FileLength; i++)
	{
		last_byte = 0;
		cmd[0] = i & 0x1F;
		if (cmd[0] == 0x1F || i == FileLength - 1)
		{
			last_byte = 1;
		}
		cmd[1] = (i >> 5) & 0x1F;
		cmd[2] = (i >> 10) & 0x1F;
		if ( hx8527_bus_write(dev, HX_CMD_FLASH_SET_ADDRESS ,&cmd[0], 3) < 0)
		{
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return -EUPGRADE;
		}

		if (prePage != cmd[1] || i == 0)
		{
			prePage = cmd[1];
			cmd[0] = 0x01;cmd[1] = 0x09;//cmd[2] = 0x02;
			if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
			{
				TOUCH_E("%s: i2c access fail!\n", __func__);
				return -EUPGRADE;
			}

			cmd[0] = 0x01;cmd[1] = 0x0D;//cmd[2] = 0x02;
			if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
			{
				TOUCH_E("%s: i2c access fail!\n", __func__);
				return -EUPGRADE;
			}

			cmd[0] = 0x01;cmd[1] = 0x09;//cmd[2] = 0x02;
			if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
			{
				TOUCH_E("%s: i2c access fail!\n", __func__);
				return -EUPGRADE;
			}
		}

		memcpy(&cmd[0], &ImageBuffer[4*i], 4);
		if ( hx8527_bus_write(dev, HX_CMD_FLASH_WRITE_REGISTER ,&cmd[0], 4) < 0)
		{
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return -EUPGRADE;
		}

		cmd[0] = 0x01;cmd[1] = 0x0D;//cmd[2] = 0x02;
		if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
		{
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return -EUPGRADE;
		}

		cmd[0] = 0x01;cmd[1] = 0x09;//cmd[2] = 0x02;
		if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
		{
			TOUCH_E("%s: i2c access fail!\n", __func__);
			return -EUPGRADE;
		}

		if(Flashing_Flag == i)
		{
			TOUCH_I("FW Flashing... (%02d%%)\n", ((i*100)/FileLength)+1);
			Flashing_Flag += 82;
		}
		if (last_byte == 1)
		{
			cmd[0] = 0x01;cmd[1] = 0x01;//cmd[2] = 0x02;
			if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
			{
				TOUCH_E("%s: i2c access fail!\n", __func__);
				return -EUPGRADE;
			}

			cmd[0] = 0x01;cmd[1] = 0x05;//cmd[2] = 0x02;
			if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
			{
				TOUCH_E("%s: i2c access fail!\n", __func__);
				return -EUPGRADE;
			}

			cmd[0] = 0x01;cmd[1] = 0x01;//cmd[2] = 0x02;
			if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
			{
				TOUCH_E("%s: i2c access fail!\n", __func__);
				return -EUPGRADE;
			}

			cmd[0] = 0x01;cmd[1] = 0x00;//cmd[2] = 0x02;
			if ( hx8527_bus_write(dev, HX_CMD_FLASH_ENABLE ,&cmd[0], 2) < 0)
			{
				TOUCH_E("%s: i2c access fail!\n", __func__);
				return -EUPGRADE;
			}

			msleep(5);
			if (i == (FileLength - 1))
			{
				hx8527_FlashMode(dev, 0);
				hx8527_ManualMode(dev, 0);
				checksumResult = hx8527_calculateChecksum(dev);
				hx8527_lock_flash(dev, 1);

				if (checksumResult) //Success
				{
					return 0;
				}
				else //Fail
				{
					TOUCH_E("%s: checksumResult fail!\n", __func__);
					return -EUPGRADE;
				}
			}
		}
	}
	return 0;
}

int hx8527_probe(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d;
	struct i2c_client *client = to_i2c_client(dev);

	int err = 0;
	int ret = 0;
	int boot_mode = TOUCH_NORMAL_BOOT;

	d = kzalloc(sizeof(struct himax_ts_data), GFP_KERNEL);
	if (d == NULL) {
		TOUCH_E("%s: allocate himax_ts_data failed\n", __func__);
		err = -ENOMEM;
		goto err_alloc_data_failed;
	}
	d->dev = dev;
	d->client = client;

	touch_set_device(ts, d);

	ret = touch_gpio_init(ts->reset_pin, "touch_reset");
        if (ret < 0) {
                TOUCH_E("failed to touch gpio init\n");
                return ret;
        }

	ret = touch_gpio_direction_output(ts->reset_pin, 1);
        if (ret < 0) {
                TOUCH_E("failed to touch gpio direction_output\n");
                return ret;
        }

	ret = touch_gpio_init(ts->int_pin, "touch_int");
        if (ret < 0) {
                TOUCH_E("failed to touch gpio init\n");
                return ret;
        }

	ret = touch_gpio_direction_input(ts->int_pin);
        if (ret < 0) {
                TOUCH_E("failed to touch gpio direction_input\n");
                return ret;
        }

	touch_power_init(dev);
	touch_bus_init(dev, MAX_BUF_SIZE);

	mutex_init(&d->rw_lock);

	boot_mode = touch_check_boot_mode(dev);
	if (boot_mode == TOUCH_CHARGER_MODE
			|| boot_mode == TOUCH_LAF_MODE
			|| boot_mode == TOUCH_RECOVERY_MODE) {
		TOUCH_I("skip [%s]: boot_mode = [%d]\n", __func__, boot_mode);
		touch_gpio_init(ts->reset_pin, "touch_reset");
		touch_gpio_direction_output(ts->reset_pin, 1);
		/* Deep Sleep */
		touch_msleep(100);
		hx8527_deep_sleep(dev,IC_DEEP_SLEEP);
		return 0;
	}

	ic_data = kzalloc(sizeof(*ic_data), GFP_KERNEL);
	if (ic_data == NULL) { /*Allocate IC data space*/
		err = -ENOMEM;
		goto err_dt_ic_data_fail;
	}

	/* allocate report data */
	hx_touch_data = kzalloc(sizeof(struct himax_report_data),GFP_KERNEL);
	if(hx_touch_data == NULL)
	{
		err = -ENOMEM;
		goto err_alloc_data_failed;
	}

	TOUCH_I("%s: int : %2.2x\n", __func__, ts->int_pin);
	TOUCH_I("%s: rst : %2.2x\n", __func__, ts->reset_pin);

	d->flash_wq = create_singlethread_workqueue("himax_flash_wq");
	if (!d->flash_wq)
	{
		TOUCH_E("%s: create flash workqueue failed\n", __func__);
		err = -ENOMEM;
		goto err_create_wq_failed;
	}
	d->reset_wq = create_singlethread_workqueue("reset_wq");
		if (!d->reset_wq)
		{
			TOUCH_E("%s: create flash workqueue failed\n", __func__);
			err = -ENOMEM;
			goto err_create_wq_failed;
		}

	INIT_DELAYED_WORK(&d->flash_work, hx8527_ts_flash_work_func);
	INIT_DELAYED_WORK(&d->reset_work, hx8527_ts_reset_work_func);

	setSysOperation(0);
	setFlashBuffer();

	d->usb_connected = 0x00;
	d->cable_config[0] = 0xF0;
	d->cable_config[1] = 0x00;

	d->mode_state = NORMAL_MODE;
	d->gesture_cust_en[0]= 1;

	d->ic_status = 0xAA;

	ts->tci.area.x1 = 0;
	ts->tci.area.x2 = ts->caps.max_x;
	ts->tci.area.y1 = 0;
	ts->tci.area.y2 = ts->caps.max_y;

	atomic_set(&d->palm, PALM_RELEASED);

	d->point_coord_size = ts->caps.max_id * 4;
	d->point_area_size = ((ts->caps.max_id - 1)/4 + 1) * 4;
	d->point_num_offset = d->point_coord_size + d->point_area_size;

	d->num_of_nodes = HX_RX_NUM * HX_TX_NUM + HX_RX_NUM + HX_TX_NUM;
	d->raw_data_frame_size = HX_TOUCH_DATA_SIZE - d->point_num_offset - HX_POINT_INFO_SIZE - HX_RAWDATA_HEADER_SIZE - HX_RAWDATA_CHECKSUM_SIZE;
	d->raw_data_nframes = (d->num_of_nodes / d->raw_data_frame_size) + ((d->num_of_nodes % d->raw_data_frame_size)? 1 : 0);

	TOUCH_I("point_coord_size : %d, point_area_size : %d, point_num_offset : %d\n",
			d->point_coord_size, d->point_area_size, d->point_num_offset);
	TOUCH_I("num_of_nodes : %d, raw_data_frame_size : %d, raw_data_nframes : %d\n",
			d->num_of_nodes, d->raw_data_frame_size, d->raw_data_nframes);

	return 0;

err_create_wq_failed:
	kfree(ic_data);

err_dt_ic_data_fail:
	kfree(d);

err_alloc_data_failed:
	kfree(hx_touch_data);
	return err;

}


int hx8527_remove(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = ts->touch_device_data;

	kfree(d);

	return 0;

}
static int hx8527_shutdown(struct device *dev)
{
	TOUCH_TRACE();

	return 0;
}

static int hx8527_esd_recovery(struct device *dev)
{
	TOUCH_TRACE();

	return 0;
}

static int hx8527_swipe_enable(struct device *dev, bool enable)
{
	TOUCH_TRACE();

	return 0;
}

static int hx8527_init_pm(struct device *dev)
{
	TOUCH_TRACE();

	return 0;
}

bool hx8527_ic_package_check(struct device *dev)
{
	uint8_t cmd[3];

	memset(cmd, 0x00, sizeof(cmd));

	if (hx8527_bus_read(dev, 0xD1, cmd, 3) < 0)
		return false ;

	if (cmd[0] == 0x05 && cmd[1] == 0x85 && (cmd[2] == 0x25 || cmd[2] == 0x26 || cmd[2] == 0x27 || cmd[2] == 0x28)) {
		TOUCH_I("Himax IC package 852x ES\n");
	} else {
		TOUCH_E("Himax IC package incorrect!!PKG[0]=%x,PKG[1]=%x,PKG[2]=%x\n",cmd[0],cmd[1],cmd[2]);
		return false ;
	}
	return true;
}

void hx8527_flash_dump_128_func(struct device *dev, uint8_t *flash_buffer)
{
	struct touch_core_data *ts = to_touch_core(dev);

	int k = 0;
	uint8_t x81_command[2] = {HX_CMD_TSSLPOUT,0x00};
	uint8_t x82_command[2] = {HX_CMD_TSSOFF,0x00};
	uint8_t x43_command[4] = {HX_CMD_FLASH_ENABLE,0x00,0x00,0x00};
	uint8_t x44_command[4] = {HX_CMD_FLASH_SET_ADDRESS,0x00,0x00,0x00};
	uint8_t x59_tmp[4] = {0,0,0,0};
	uint8_t page_tmp[128];

	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);
	hx8527_power(dev, POWER_OFF);
	hx8527_power(dev, POWER_ON);

	if ( hx8527_bus_master_write(dev, x81_command, 1) < 0) //sleep out
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

	//read page start

	memset(page_tmp, 0x00, 128 * sizeof(uint8_t));

	for(k=0; k<32; k++)
	{
		x44_command[1] = k;
		x44_command[2] = 31;
		x44_command[3] = 7;
		if ( hx8527_bus_write(dev, x44_command[0],&x44_command[1], 3) < 0 )
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
		memcpy(&page_tmp[k*4], x59_tmp, 4 * sizeof(uint8_t));
		//msleep(10);
	}
	//read page end
	memcpy(flash_buffer, page_tmp, 128 * sizeof(uint8_t));

	TOUCH_I("%s - Complete\n",__func__);
	for(k = 0; k < 4 ; k ++)
	{
		ic_data->lge_checksum[k] = flash_buffer[k + 124];
	}

Flash_Dump_i2c_transfer_error:
	hx8527_power(dev, POWER_OFF);
	hx8527_power(dev, POWER_ON);

	return;
}

int hx8527_init(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	uint8_t flash_tmp[128];
	int err = 0;


	TOUCH_I("Enter %s\n", __func__);
	if (atomic_read(&ts->state.core) == CORE_PROBE) {
		if (hx8527_ic_package_check(dev) == false)
		{
			TOUCH_E("Himax chip doesn NOT EXIST");
			goto err_ic_package_failed;
		}

		if(!hx8527_calculateChecksum(dev))
		{
			d->crc_flag = 1;
			TOUCH_E("IC CRC check sum wrong.");
		}
		hx8527_flash_dump_128_func(dev, flash_tmp);
	}

	hx8527_power_on_init(dev);

	if (atomic_read(&ts->state.core) == CORE_PROBE) {
		setMutualBuffer();
		if (getMutualBuffer() == NULL) {
			TOUCH_E("%s: mutual buffer allocate fail failed\n", __func__);
			return -1;
		}

		if(Is_2T2R){
			setMutualBuffer_2();

			if (getMutualBuffer_2() == NULL) {
				TOUCH_E("%s: mutual buffer 2 allocate fail failed\n", __func__);
				return -1;
			}
		}

		//touch data init
		hx8527_report_data_init(dev);
		if(err)
			goto err_report_data_init_failed;
	}

	atomic_set(&d->init, IC_INIT_DONE);
	return 0;

err_report_data_init_failed:
err_ic_package_failed:
	return I2C_FAIL;
}

int hx8527_irq_handler(struct device *dev)
{
	struct himax_ts_data *d = to_himax_data(dev);
	uint8_t buf[128];
	int check_sum_cal = 0;
	int ts_status = 0;
	int ret = NO_ERR;
	struct touch_core_data *ts = to_touch_core(dev);
	struct timespec timeStart = {0, };
	struct timespec timeEnd = {0, };
	struct timespec timeDelta = {0, };
	struct sched_param param = { .sched_priority = 51 };
	sched_setscheduler(current, SCHED_RR, &param);

	if (d->debug_log_level & BIT(2)) {
		getnstimeofday(&timeStart);
	}

	ts_status = hx8527_ts_status(d);
	if(ts_status > HX_REPORT_SMWP_EVENT || ts_status < HX_REPORT_COORD)
		goto bail;

	memset(buf, 0x00, sizeof(buf));

	if(hx8527_touch_get(dev, buf,ts_status))
		goto err_workqueue_out;

	if (d->debug_log_level & BIT(0))
	{
		hx8527_log_touch_data(dev, buf,hx_touch_data);
	}

	check_sum_cal = hx8527_checksum_cal(dev,buf,ts_status);
	if (check_sum_cal == CHECKSUM_FAIL)
		goto bail;
	else if (check_sum_cal == READY_TO_SERVE)
		goto bail;
	else if (check_sum_cal == ERR_WORK_OUT)
		goto err_workqueue_out;
	else if (check_sum_cal == WORK_OUT)
		goto bail;
	/* checksum calculate pass and assign data to global touch data*/
	else
		hx8527_touch_data(dev,buf,ts_status);

	if(ts_status == HX_REPORT_COORD)
	{
		ret = hx8527_coord_report(dev);
	}
	else
	{
		ret = hx8527_wake_event(dev, buf);
		if (ret == EV_GESTURE_PWR)
		{
			TOUCH_I(" %s KEY event = %x \n",__func__,EV_GESTURE_PWR);
			ts->intr_status = TOUCH_IRQ_KNOCK;
		}
	}
	if (d->debug_log_level & BIT(2)) {
		getnstimeofday(&timeEnd);
#if 0	// [bring up] fix build error
		timeDelta.tv_nsec = (timeEnd.tv_sec*1000000000+timeEnd.tv_nsec)
			-(timeStart.tv_sec*1000000000+timeStart.tv_nsec);
#endif	// [bring up] fix build error
		TOUCH_I("Touch latency = %ld us\n", timeDelta.tv_nsec/1000);
	}

bail:
	return ret;

err_workqueue_out:
	TOUCH_I("%s: Now reset the Touch chip.\n", __func__);
	return -ERESTART;
}

int hx8527_upgrade(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	char fwpath[256] = {0};
	const struct firmware *fw = NULL;
	int upgrade_times = 0;
	int ret = 0;
	int boot_mode = TOUCH_NORMAL_BOOT;

// [bring up] fix build error - start
	if (0) {
		TOUCH_I("%s: temporarily skip fw upgrade for bring up\n", __func__);
			return -EPERM;
	}
//[bring up] fix build error - end
	if (atomic_read(&ts->state.fb) >= FB_SUSPEND) {
		TOUCH_I("state.fb is not FB_RESUME\n");
		return -EPERM;
	}

        boot_mode = touch_check_boot_mode(dev);
        if (boot_mode == TOUCH_CHARGER_MODE
                        || boot_mode == TOUCH_LAF_MODE
                        || boot_mode == TOUCH_RECOVERY_MODE) {
                TOUCH_I("skip [%s]: boot_mode = [%d]\n", __func__, boot_mode);
                return -EPERM;
        }

	if (ts->test_fwpath[0] != 0) {
		memcpy(fwpath, &ts->test_fwpath[0], sizeof(fwpath));
		TOUCH_I("get fwpath from test_fwpath:%s\n", &ts->test_fwpath[0]);
	} else if (ts->def_fwcnt) {
		memcpy(fwpath, ts->def_fwpath[0], sizeof(fwpath)); // 0 : pramboot bin, 1 : 8607 all bin, 2: 8606 all bin
		TOUCH_I("get fwpath from def_fwpath : %s\n", fwpath);
	} else {
		TOUCH_E("no firmware file\n");
		return -EPERM;
	}

	fwpath[sizeof(fwpath)-1] = '\0';

	if (strlen(fwpath) <= 0) {
		TOUCH_E("error get fw path\n");
		return -EPERM;
	}

	TOUCH_I("fwpath[%s]\n", fwpath);

	ret = request_firmware(&fw, fwpath, dev);

	if (ret < 0) {
		TOUCH_I("fail to request_firmware fwpath: %s (ret:%d)\n", fwpath, ret);
		return ret;
	}

	TOUCH_I("fw size:%zu, data: %p\n", fw->size, fw->data);

	if (hx8527_fw_compare(dev, fw)) {
		ret = -EPERM;
		while (upgrade_times < 3) {
			ret = fts_ctpm_fw_upgrade_with_sys_fs_32k(dev, (unsigned char *)fw->data, fw->size);
			if (ret < 0) {
				upgrade_times++;
				TOUCH_E("FW upgrade fail, retry = %d\n", upgrade_times);
				continue;
			}
			TOUCH_I("FW upgrade OK\n");
			ret = 0;
			break;

		}
	} else {
		release_firmware(fw);
		return -EPERM;
	}

	release_firmware(fw);
	return 0;
}

static int hx8527_lpwg(struct device *dev, u32 code, void *param)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int *value = (int *)param;
	int prev_screen = 0;

	switch (code) {
		case LPWG_ACTIVE_AREA:
			ts->tci.area.x1 = value[0];
			ts->tci.area.x2 = value[1];
			ts->tci.area.y1 = value[2];
			ts->tci.area.y2 = value[3];
			TOUCH_I("LPWG_ACTIVE_AREA: x0[%d], x1[%d], x2[%d], x3[%d]\n",
					value[0], value[1], value[2], value[3]);
			break;

		case LPWG_TAP_COUNT:
			ts->tci.info[TCI_2].tap_count = value[0];
			TOUCH_I("LPWG_TAP_COUNT: [%d]\n", value[0]);
			break;

		case LPWG_DOUBLE_TAP_CHECK:
			ts->tci.double_tap_check = value[0];
			TOUCH_I("LPWG_DOUBLE_TAP_CHECK: [%d]\n", value[0]);
			break;

		case LPWG_UPDATE_ALL:
			prev_screen = ts->lpwg.screen;

			ts->lpwg.mode = value[0];
			ts->lpwg.screen = value[1];
			ts->lpwg.sensor = value[2];
			ts->lpwg.qcover = value[3];

			TOUCH_I("LPWG_UPDATE_ALL: mode[%d], screen[%s], sensor[%s], qcover[%s]\n",
					value[0],
					value[1] ? "ON" : "OFF",
					value[2] ? "FAR" : "NEAR",
					value[3] ? "CLOSE" : "OPEN");

			if(prev_screen == ts->lpwg.screen)
				himax_lpwg_mode(dev);
			else
				TOUCH_I("screen %s -> %s, skip lpwg mode setting\n",
					prev_screen ? "ON" : "OFF",
					ts->lpwg.screen ? "ON" : "OFF");

			break;

		case LPWG_REPLY:
			break;

	}

	return 0;
}

int hx8527_notify_etc(struct device *dev, ulong event, void *data)
{
        int ret = 0;

        TOUCH_TRACE();

        return ret;
}

int hx8527_notify_normal(struct device *dev, ulong event, void *data)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct himax_ts_data *d = to_himax_data(dev);
	int charger_state = atomic_read(&ts->state.connect);
	int ret = 0;

	TOUCH_TRACE();
	TOUCH_I("%s event=0x%x\n", __func__, (unsigned int)event);

	if (charger_state == CONNECT_INVALID)
		d->cable_config[1] = 0x00;
	else
		d->cable_config[1] = 0x01;

	switch (event) {
		case NOTIFY_CONNECTION:
			TOUCH_I("%s USB detection!!\n",__func__);
			hx8527_usb_detect_set(dev, d->cable_config);
			break;
		case NOTIFY_IME_STATE:
			TOUCH_I("NOTIFY_IME_STATE!!\n");
			break;

		case LCD_EVENT_TOUCH_RESET_START:
			TOUCH_I("LCD_EVENT_TOUCH_RESET_START!!\n");
			//queue_delayed_work(d->reset_wq, &d->reset_work, msecs_to_jiffies(0));
			break;
		case NOTIFY_CALL_STATE:
			TOUCH_I("NOTIFY_CALL_STATE!\n");
			break;
		default:
			TOUCH_E("%lu is not supported\n", event);
			break;
	}

	return ret;
}


int hx8527_notify(struct device *dev, ulong event, void *data)
{
        int boot_mode = TOUCH_NORMAL_BOOT;

        TOUCH_TRACE();

        boot_mode = touch_check_boot_mode(dev);
        if (boot_mode == TOUCH_CHARGER_MODE
                        || boot_mode == TOUCH_LAF_MODE
                        || boot_mode == TOUCH_RECOVERY_MODE) {
                TOUCH_I("Notify Skip MODE notify. %d\n", boot_mode);
                return hx8527_notify_etc(dev,event,data);
        }

        return hx8527_notify_normal(dev,event,data);

}

static int hx8527_get_cmd_version(struct device *dev, char *buf)
{
	int offset = 0;

	offset = snprintf(buf + offset, PAGE_SIZE - offset, "Firmware info\n");
	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Version : V%d.%02d\n\n",
			ic_data->lge_ver_major, ic_data->lge_ver_minor);

	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Config info\n");
	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Version : V%02d\n\n",
			ic_data->vendor_config_ver);

	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Product_id : [HX8527]\n\n");

	return offset;

}

static int hx8527_get_cmd_atcmd_version(struct device *dev, char *buf)
{
	int offset = 0;

	offset = snprintf(buf, PAGE_SIZE, "V%d.%02d\n",
			ic_data->lge_ver_major, ic_data->lge_ver_minor);

	return offset;
}

int hx8527_set(struct device *dev, u32 cmd, void *input, void *output)
{
	TOUCH_TRACE();

	return 0;
}

int hx8527_get(struct device *dev, u32 cmd, void *input, void *output)
{
	int ret = 0;

	TOUCH_D(BASE_INFO, "%s : cmd %d\n", __func__, cmd);

	switch (cmd) {
		case CMD_VERSION:
			ret = hx8527_get_cmd_version(dev, (char *)output);
			break;

		case CMD_ATCMD_VERSION:
			ret = hx8527_get_cmd_atcmd_version(dev, (char *)output);
			break;

		default:
			break;
	}

	return ret;
}

static struct touch_driver touch_driver = {
	.probe = hx8527_probe,
	.remove = hx8527_remove,
	.shutdown = hx8527_shutdown,
	.suspend = hx8527_suspend,
	.resume = hx8527_resume,
	.init = hx8527_init,
	.irq_handler = hx8527_irq_handler,
	.power = hx8527_power,
	.upgrade = hx8527_upgrade,
	.esd_recovery = hx8527_esd_recovery,
	.lpwg = hx8527_lpwg,
	.swipe_enable = hx8527_swipe_enable,
	.notify = hx8527_notify,
	.init_pm = hx8527_init_pm,
	.register_sysfs = hx8527_register_sysfs,
	.set = hx8527_set,
	.get = hx8527_get,
};

#define MATCH_NAME			"himax,hx8527"
static struct of_device_id touch_match_ids[] = {
	{ .compatible = MATCH_NAME, },
	{},
};

static struct touch_hwif hwif = {
	.bus_type = HWIF_I2C,
	.name = LGE_TOUCH_NAME,
	.owner = THIS_MODULE,
	.of_match_table = of_match_ptr(touch_match_ids),
};

static int __init touch_device_init(void)
{
    TOUCH_I("touch_device_init func\n");
    TOUCH_TRACE();

    if (is_lcm_name("TXD-ILI9881C")) {
	    TOUCH_I("%s, HX8527 found!lcm_name = %s\n",__func__,lge_get_lcm_name());
	    return touch_bus_device_init(&hwif, &touch_driver);
    }

    TOUCH_I("%s, HX8527 not found!\n", __func__);
    return 0;
}


static void __exit touch_device_exit(void)
{
	TOUCH_TRACE();
	touch_bus_device_exit(&hwif);
}

module_init(touch_device_init);
module_exit(touch_device_exit);

MODULE_DESCRIPTION("Himax for LGE touch driver v1");
MODULE_LICENSE("GPL");

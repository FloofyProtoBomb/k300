/* touch_gt917d.c
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

static struct i2c_client *i2c_connect_client;
static struct proc_dir_entry *gt917d_config_proc;
static struct gt917d_point_t points[GT917D_MAX_TOUCH_ID];
static u8 touch_cnt;

#define LPWG_FAILREASON_TCI_NUM 8
static const char * const lpwg_failreason_tci_str[LPWG_FAILREASON_TCI_NUM] = {
	[0] = "NONE",
	[1] = "DISTANCE_INTER_TAP",
	[2] = "DISTANCE_TOUCHSLOP",
	[3] = "TIMEOUT_INTER_TAP",
	[4] = "MULTI_FINGER",
	[5] = "DELAY_TIME", //it means Over Tap
	[6] = "PALM_STATE",
	[7] = "OUTOF_AREA",
};

#define SWIPE_FAILREASON_NUM 12
static const char * const swipe_debug_str[SWIPE_FAILREASON_NUM + 1] = {
	[0] = "Reserved",
	[1] = "NORMAL_ERROR",
	[2] = "FINGER_FAST_RELEASE",
	[3] = "MULTI_FINGER",
	[4] = "FAST_SWIPE",
	[5] = "SLOW_SWIPE",
	[6] = "WRONG_DIRECTION",
	[7] = "RATIO_FAIL",
	[8] = "OUT_OF_START_AREA",
	[9] = "OUT_OF_ACTIVE_AREA",
	[10] = "INITAIL_RATIO_FAIL",
	[11] = "PALM_STATE",
	[12] = "Reserved" // Invalid data
};

int gt917d_reg_read(struct device *dev, u16 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct touch_bus_msg msg = {0,};
	int ret = 0;

	TOUCH_TRACE();

	mutex_lock(&d->rw_lock);

    ts->tx_buf[0] = (addr >> 8) & 0xFF;
    ts->tx_buf[1] = addr & 0xFF;

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = GT917D_ADDR_LENGTH;

    msg.rx_buf = ts->rx_buf;
    msg.rx_size = size;

    ret = touch_bus_read(dev, &msg);

    if (ret < 0) {
        TOUCH_E("touch bus read error : %d\n", ret);
        mutex_unlock(&d->rw_lock);
        return ret;
    }

    memcpy(data, &ts->rx_buf[0], size);
    mutex_unlock(&d->rw_lock);
    return 0;
}


int gt917d_reg_write(struct device *dev, u16 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct touch_bus_msg msg = {0,};
	int ret = 0;

    TOUCH_TRACE();
    mutex_lock(&d->rw_lock);

    ts->tx_buf[0] = (addr >> 8) & 0xFF;
    ts->tx_buf[1] = addr & 0xFF;

	memcpy(&ts->tx_buf[GT917D_ADDR_LENGTH], data, size);

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = size + GT917D_ADDR_LENGTH;
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

int gt917d_i2c_read(struct i2c_client *client, u8 *buf, int len)
{
    unsigned int address = (buf[0] << 8) + buf[1];
    int ret = 0;

	TOUCH_TRACE();

	ret = gt917d_reg_read(&client->dev, address, &buf[GT917D_ADDR_LENGTH], len - GT917D_ADDR_LENGTH);

	return ret;
}

int gt917d_i2c_write(struct i2c_client *client, u8 *buf, int len)
{
    unsigned int address = (buf[0] << 8) + buf[1];
    int ret = 0;

	TOUCH_TRACE();

	ret = gt917d_reg_write(&client->dev, address, &buf[GT917D_ADDR_LENGTH], len - GT917D_ADDR_LENGTH);

    return ret;
}

s32 gt917d_i2c_read_dbl_check(struct device *dev, u16 addr, u8 *rxbuf, int len)
{
    u8 buf[16] = {0};
    u8 confirm_buf[16] = {0};
    u8 retry = 0;

	TOUCH_TRACE();

	if (len + 2 > sizeof(buf)) {
		TOUCH_E("only support length less then %zu\n", sizeof(buf) - 2);
		return FAIL;
	}

	while (retry++ < 3) {
		memset(buf, 0xAA, 16);
		gt917d_reg_read(dev, addr, buf, len);

        memset(confirm_buf, 0xAB, 16);
        gt917d_reg_read(dev, addr, confirm_buf, len);

		if (!memcmp(buf, confirm_buf, len)) {
			memcpy(rxbuf, confirm_buf, len);
			return SUCCESS;
		}
	}

	return FAIL;
}

static ssize_t gt917d_config_read_proc(struct file *file, char __user *page,
		size_t size, loff_t *ppos)
{
	int i = 0;
	int ret = 0;
	char *ptr = NULL;
	size_t data_len = 0;
	char temp_data[GT917D_CONFIG_MAX_LENGTH] = {0, };
	struct touch_core_data *ts = to_touch_core(&i2c_connect_client->dev);
	struct gt917d_data *d = to_gt917d_data(&i2c_connect_client->dev);

	TOUCH_TRACE();

    ptr = kzalloc(4096, GFP_KERNEL);
    if (!ptr) {
        TOUCH_E("Failed alloc memory for config\n");
        return -ENOMEM;
    }

	data_len += snprintf(ptr + data_len, 4096 - data_len,
			"====init value====\n");
	for (i = 0 ; i < GT917D_CONFIG_MAX_LENGTH ; i++) {
		data_len += snprintf(ptr + data_len, 4096 - data_len,
				"0x%02X ", d->config.data[i + 2]);

        if (i % 8 == 7)
            data_len += snprintf(ptr + data_len,
                    4096 - data_len, "\n");
    }
    data_len += snprintf(ptr + data_len, 4096 - data_len, "\n");

	data_len += snprintf(ptr + data_len, 4096 - data_len,
			"====real value====\n");

	mutex_lock(&ts->lock);
	ret = gt917d_reg_read(&i2c_connect_client->dev,
			GT917D_REG_CONFIG_DATA,
			temp_data, sizeof(temp_data));
	mutex_unlock(&ts->lock);

	if (ret < 0) {
		data_len += snprintf(ptr + data_len, 4096 - data_len,
				"Failed read real config data\n");
	} else {
		for (i = 0; i < GT917D_CONFIG_MAX_LENGTH; i++) {
			data_len += snprintf(ptr + data_len, 4096 - data_len,
					"0x%02X ", temp_data[i]);

            if (i % 8 == 7)
                data_len += snprintf(ptr + data_len,
                        4096 - data_len, "\n");
        }
    }
    data_len = simple_read_from_buffer(page, size, ppos, ptr, data_len);
    kfree(ptr);
    ptr = NULL;
    return data_len;
}

static u8 ascii2hex(u8 a)
{
    s8 value = 0;

	TOUCH_TRACE();

	if (a >= '0' && a <= '9')
		value = a - '0';
	else if (a >= 'A' && a <= 'F')
		value = a - 'A' + 0x0A;
	else if (a >= 'a' && a <= 'f')
		value = a - 'a' + 0x0A;
	else
		value = 0xff;

    return value;
}

static int gt917d_ascii_to_array(const u8 *src_buf, int src_len, u8 *dst_buf)
{
	int i = 0;
	int ret = 0;
	int cfg_len = 0;
	u8 high = 0;
	u8 low = 0;

	TOUCH_TRACE();

	for (i = 0; i < src_len;) {
		if (src_buf[i] == ' ' || src_buf[i] == '\r' || src_buf[i] == '\n') {
			i++;
			continue;
		}

		if ((src_buf[i] == '0') && ((src_buf[i + 1] == 'x') || (src_buf[i + 1] == 'X'))) {
			high = ascii2hex(src_buf[i + 2]);
			low = ascii2hex(src_buf[i + 3]);

            if ((high == 0xFF) || (low == 0xFF)) {
                ret = -1;
                goto convert_failed;
            }

			if (cfg_len < GT917D_CONFIG_MAX_LENGTH) {
                dst_buf[cfg_len++] = (high << 4) + low;
                i += 5;
            } else {
                ret = -2;
                goto convert_failed;
            }
        } else {
            ret = -3;
            goto convert_failed;
        }
    }
    return cfg_len;

convert_failed:
    return ret;
}

static ssize_t gt917d_config_write_proc(struct file *filp,
		const char __user *buffer, size_t count, loff_t *off)
{
	u8 *temp_buf = NULL;
	u8 *file_config = NULL;
	int file_cfg_len = 0;
	s32 ret = 0;
	s32 i = 0;
	struct touch_core_data *ts = to_touch_core(&i2c_connect_client->dev);

	TOUCH_TRACE();

	TOUCH_I("write count %zu\n", count);

    if (count > PAGE_SIZE) {
        TOUCH_E("config to long %zu\n", count);
        return -EFAULT;
    }

	temp_buf = kzalloc(count, GFP_KERNEL);
	if (!temp_buf) {
		TOUCH_E("failed alloc temp memory\n");
		return -ENOMEM;
	}

	file_config = kzalloc(GT917D_CONFIG_MAX_LENGTH, GFP_KERNEL);
	if (!file_config) {
		TOUCH_E("failed alloc config memory\n");
		kfree(temp_buf);
		return -ENOMEM;
	}

    if (copy_from_user(temp_buf, buffer, count)) {
        TOUCH_E("Failed copy from user\n");
        ret = -EFAULT;
        goto send_cfg_err;
    }

	file_cfg_len = gt917d_ascii_to_array(temp_buf, (int)count, &file_config[0]);
	if (file_cfg_len < 0) {
		TOUCH_E("failed covert ascii to hex\n");
		ret = -EFAULT;
		goto send_cfg_err;
	}
	if (file_cfg_len > GT917D_CONFIG_MAX_LENGTH) {
		TOUCH_E("file_cfg_len(%d) > GT917D_CONFIG_MAX_LENGTH(%d)\n",
				file_cfg_len, GT917D_CONFIG_MAX_LENGTH);
		ret = -EFAULT;
		goto send_cfg_err;
	}

	i = 0;
	while (i++ < 5) {
		mutex_lock(&ts->lock);
		ret = gt917d_reg_write(&i2c_connect_client->dev,
				GT917D_REG_CONFIG_DATA,
				file_config, file_cfg_len);
		mutex_unlock(&ts->lock);

		if (ret == 0) {
			TOUCH_I("Send config SUCCESS.\n");
			break;
		}
		TOUCH_E("Send config i2c error.\n");
		ret = -EFAULT;
		goto send_cfg_err;
	}

    ret = count;
send_cfg_err:
    kfree(temp_buf);
    kfree(file_config);
    return ret;
}

static const struct file_operations config_proc_ops = {
	.owner = THIS_MODULE,
	.read = gt917d_config_read_proc,
	.write = gt917d_config_write_proc,
};

static ssize_t gt917d_workmode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t data_len = 0;
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
		data_len = scnprintf(buf, PAGE_SIZE, "%s\n",
				"sleep_mode");
	else
		data_len = scnprintf(buf, PAGE_SIZE, "%s\n",
				"normal_mode");

    return data_len;
}
static DEVICE_ATTR(workmode, 0444, gt917d_workmode_show, NULL);

static ssize_t gt917d_drv_irq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long value = 0;
    int err = 0;

	TOUCH_TRACE();

	err = kstrtoul(buf, 10, &value);
	if (err < 0) {
		TOUCH_E("Failed to convert value\n");
		return -EINVAL;
	}

    switch (value) {
        case 0:
            /* Disable irq */
            touch_interrupt_control(dev, INTERRUPT_DISABLE);
            break;
        case 1:
            /* Enable irq */
            touch_interrupt_control(dev, INTERRUPT_ENABLE);
            break;
        default:
            TOUCH_E("Invalid value\n");
            return -EINVAL;
    }

    return count;
}

static ssize_t gt917d_drv_irq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gt917d_data *d = to_gt917d_data(dev);

	TOUCH_TRACE();

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			test_bit(WORK_THREAD_ENABLED, &d->flags)
			? "enabled" : "disabled");
}
static DEVICE_ATTR(drv_irq, 0664, gt917d_drv_irq_show, gt917d_drv_irq_store);

static ssize_t gt917d_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();

	if ('1' != buf[0]) {
		TOUCH_E("Invalid argument for reset\n");
		return -EINVAL;
	}

	mutex_lock(&ts->lock);
	gt917d_hw_reset(dev);
	mutex_unlock(&ts->lock);

    return count;
}

static DEVICE_ATTR(reset, 0220, NULL, gt917d_reset_store);

static struct attribute *gt917d_attrs[] = {
	&dev_attr_workmode.attr,
	&dev_attr_drv_irq.attr,
	&dev_attr_reset.attr,
	NULL
};

static const struct attribute_group gt917d_attr_group = {
	.attrs = gt917d_attrs,
};

static int gt917d_create_file(struct gt917d_data *d)
{
	int ret = 0;
	struct i2c_client *client = d->client;

	TOUCH_TRACE();

	/* Create proc file system */
	gt917d_config_proc = NULL;
	gt917d_config_proc = proc_create(GT91XX_CONFIG_PROC_FILE, 0664,
			NULL, &config_proc_ops);
	if (!gt917d_config_proc)
		TOUCH_E("create_proc_entry %s failed\n",
				GT91XX_CONFIG_PROC_FILE);
	else
		TOUCH_I("create proc entry %s success\n",
				GT91XX_CONFIG_PROC_FILE);

	ret = sysfs_create_group(&client->dev.kobj, &gt917d_attr_group);
	if (ret) {
		TOUCH_E("Failure create sysfs group %d\n", ret);
		/*TODO: debug change */
		goto exit_free_config_proc;
	}

	return 0;

exit_free_config_proc:
	remove_proc_entry(GT91XX_CONFIG_PROC_FILE, gt917d_config_proc);
	return -ENODEV;
}

static void gt917d_remove_file(struct gt917d_data *d)
{
	struct i2c_client *client = d->client;

	TOUCH_TRACE();

	sysfs_remove_group(&client->dev.kobj, &gt917d_attr_group);
	remove_proc_entry(GT91XX_CONFIG_PROC_FILE, gt917d_config_proc);
}

static void gt917d_trans_swipe_info(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	int max_time_trans_numerator = 1;	/* max_time/10 */
	int max_time_trans_denominator = 10;
	int mm_trans_numerator = 100;		/* mm_value/1.25 */
	int mm_trans_denominator = 125;
	int pixel_trans_numerator = 10000;	/* pixel_value/(10.55*1.25) */
	int pixel_trans_denominator = 131875;
	int temp = 0;

	TOUCH_TRACE();

	temp = 0x00;
	if (ts->swipe[SWIPE_U].enable)
		temp |= GESTURE_UP_EN;
	if (ts->swipe[SWIPE_L].enable)
		temp |= GESTURE_LEFT_EN;
	if (ts->swipe[SWIPE_R].enable)
		temp |= GESTURE_RIGHT_EN;
	d->swipe.common.enable = (u8)temp;

	temp = ts->swipe[SWIPE_U].max_time * max_time_trans_numerator / max_time_trans_denominator;
	d->swipe.common.time.max = (u8)(temp & 0x0F);
	temp = ts->swipe[SWIPE_U].min_time;
	d->swipe.common.time.min = (u8)(temp & 0x0F);

	temp = ts->swipe[SWIPE_U].wrong_dir_thres * mm_trans_numerator / mm_trans_denominator;
	d->swipe.common.wrong_dir = (u8)temp;
	temp = ts->swipe[SWIPE_U].init_ratio_chk_dist * mm_trans_numerator / mm_trans_denominator;
	d->swipe.common.init_distance = (u8)temp;

	temp = ts->swipe[SWIPE_U].distance * mm_trans_numerator / mm_trans_denominator;
	d->swipe.u.distance = (u8)temp;
	temp = ts->swipe[SWIPE_U].ratio_thres;
	d->swipe.u.ratio = (u8)temp;
	temp = ts->swipe[SWIPE_U].init_ratio_thres;
	d->swipe.u.init_ratio = (u8)temp;

	temp = ts->swipe[SWIPE_U].area.x1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.area.x1 = (u8)temp;
	temp = ts->swipe[SWIPE_U].area.x2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.area.x2 = (u8)temp;
	temp = ts->swipe[SWIPE_U].area.y1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.area.y1 = (u8)temp;
	temp = ts->swipe[SWIPE_U].area.y2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.area.y2 = (u8)temp;

	temp = ts->swipe[SWIPE_U].start_area.x1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.start_area.x1 = (u8)temp;
	temp = ts->swipe[SWIPE_U].start_area.x2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.start_area.x2 = (u8)temp;
	temp = ts->swipe[SWIPE_U].start_area.y1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.start_area.y1 = (u8)temp;
	temp = ts->swipe[SWIPE_U].start_area.y2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.u.start_area.y2 = (u8)temp;

	temp = ts->swipe[SWIPE_L].distance * mm_trans_numerator / mm_trans_denominator;
	d->swipe.l_r.distance = (u8)temp;
	temp = ts->swipe[SWIPE_L].ratio_thres;
	d->swipe.l_r.ratio = (u8)temp;
	temp = ts->swipe[SWIPE_L].init_ratio_thres;
	d->swipe.l_r.init_ratio = (u8)temp;

	temp = ts->swipe[SWIPE_L].area.x1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.area.x1 = (u8)temp;
	temp = ts->swipe[SWIPE_L].area.x2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.area.x2 = (u8)temp;
	temp = ts->swipe[SWIPE_L].area.y1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.area.y1 = (u8)temp;
	temp = ts->swipe[SWIPE_L].area.y2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.area.y2 = (u8)temp;

	temp = ts->swipe[SWIPE_L].start_area.x1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.left_start_area.x1 = (u8)temp;
	temp = ts->swipe[SWIPE_L].start_area.x2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.left_start_area.x2 = (u8)temp;
	temp = ts->swipe[SWIPE_L].start_area.y1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.left_start_area.y1 = (u8)temp;
	temp = ts->swipe[SWIPE_L].start_area.y2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.left_start_area.y2 = (u8)temp;

	temp = ts->swipe[SWIPE_R].start_area.x1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.right_start_area.x1 = (u8)temp;
	temp = ts->swipe[SWIPE_R].start_area.x2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.right_start_area.x2 = (u8)temp;
	temp = ts->swipe[SWIPE_R].start_area.y1 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.right_start_area.y1 = (u8)temp;
	temp = ts->swipe[SWIPE_R].start_area.y2 * pixel_trans_numerator / pixel_trans_denominator;
	d->swipe.l_r.right_start_area.y2 = (u8)temp;
}

static void gt917d_get_swipe_info(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();

	ts->swipe[SWIPE_U].enable = false;
	ts->swipe[SWIPE_U].distance = 20;
	ts->swipe[SWIPE_U].ratio_thres = 150;
	ts->swipe[SWIPE_U].min_time = 4;
	ts->swipe[SWIPE_U].max_time = 150;
	ts->swipe[SWIPE_U].wrong_dir_thres = 5;
	ts->swipe[SWIPE_U].init_ratio_chk_dist = 4;
	ts->swipe[SWIPE_U].init_ratio_thres = 100;
	ts->swipe[SWIPE_U].area.x1 = 42;
	ts->swipe[SWIPE_U].area.y1 = 0;
	ts->swipe[SWIPE_U].area.x2 = 677;
	ts->swipe[SWIPE_U].area.y2 = 1519;
	ts->swipe[SWIPE_U].start_area.x1 = 228;
	ts->swipe[SWIPE_U].start_area.y1 = 1367;
	ts->swipe[SWIPE_U].start_area.x2 = 490;
	ts->swipe[SWIPE_U].start_area.y2 = 1519;

	ts->swipe[SWIPE_L].enable = false;
	ts->swipe[SWIPE_L].distance = 12;
	ts->swipe[SWIPE_L].ratio_thres = 150;
	ts->swipe[SWIPE_L].min_time = 4;
	ts->swipe[SWIPE_L].max_time = 150;
	ts->swipe[SWIPE_L].wrong_dir_thres = 5;
	ts->swipe[SWIPE_L].init_ratio_chk_dist = 4;
	ts->swipe[SWIPE_L].init_ratio_thres = 100;
	ts->swipe[SWIPE_L].area.x1 = 0;
	ts->swipe[SWIPE_L].area.y1 = 0;
	ts->swipe[SWIPE_L].area.x2 = 719;
	ts->swipe[SWIPE_L].area.y2 = 1519;
	ts->swipe[SWIPE_L].start_area.x1 = 614;
	ts->swipe[SWIPE_L].start_area.y1 = 204;
	ts->swipe[SWIPE_L].start_area.x2 = 719;
	ts->swipe[SWIPE_L].start_area.y2 = 1105;

	ts->swipe[SWIPE_R].enable = false;
	ts->swipe[SWIPE_R].distance = 12;
	ts->swipe[SWIPE_R].ratio_thres = 150;
	ts->swipe[SWIPE_R].min_time = 4;
	ts->swipe[SWIPE_R].max_time = 150;
	ts->swipe[SWIPE_R].wrong_dir_thres = 5;
	ts->swipe[SWIPE_R].init_ratio_chk_dist = 4;
	ts->swipe[SWIPE_R].init_ratio_thres = 100;
	ts->swipe[SWIPE_R].area.x1 = 0;
	ts->swipe[SWIPE_R].area.y1 = 0;
	ts->swipe[SWIPE_R].area.x2 = 719;
	ts->swipe[SWIPE_R].area.y2 = 1519;
	ts->swipe[SWIPE_R].start_area.x1 = 0;
	ts->swipe[SWIPE_R].start_area.y1 = 204;
	ts->swipe[SWIPE_R].start_area.x2 = 105;
	ts->swipe[SWIPE_R].start_area.y2 = 1105;

	gt917d_trans_swipe_info(dev);
}

int gt917d_get_fw_info(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	s32 ret = -1;
	u8 buf[6] = {0,};

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return 0;
	}

	ret = gt917d_reg_read(dev, GT917D_REG_VERSION, buf, sizeof(buf));
	if (ret < 0) {
		TOUCH_E("Failed read fw_info\n");
		return ret;
	}

    /* product id */
    memcpy(d->fw_info.pid, buf, 4);

    /* current firmware version */
    d->fw_info.ic_major_ver = buf[5];
    d->fw_info.ic_minor_ver = buf[4];

	/* read sensor id */
	d->fw_info.sensor_id = 0xff;
	ret = gt917d_i2c_read_dbl_check(dev, GT917D_REG_SENSOR_ID,
			&d->fw_info.sensor_id, 1);
	if (ret != SUCCESS || d->fw_info.sensor_id >= 0x06) {
		TOUCH_E("Failed get valid sensor_id(0x%02X), No Config Sent\n",
				d->fw_info.sensor_id);

        d->fw_info.sensor_id = 0xff;
        return -1;
    }

	TOUCH_I("==================== Version Info ====================\n");
	TOUCH_I("Product-id : [GT%s]\n", d->fw_info.pid);
	TOUCH_I("IC firmware version: v%d.%02d\n", d->fw_info.ic_major_ver, d->fw_info.ic_minor_ver);
	TOUCH_I("Config Version : v%d\n", d->config.data[GT917D_ADDR_LENGTH]);
	TOUCH_I("Sensor ID : %d\n", d->fw_info.sensor_id);
	TOUCH_I("======================================================\n");

    return 0;
}

static int gt917d_i2c_test(struct device *dev)
{
    u8 test = 0;
    u8 retry = 0;
    int ret = -1;

	TOUCH_TRACE();

	while (retry++ < 3) {
		ret = gt917d_reg_read(dev, GT917D_REG_CONFIG_DATA, &test, 1);

		if (ret == 0)
			return ret;
		TOUCH_E("GT917D i2c test failed time %d\n", retry);
		touch_msleep(10);
	}

    return -EAGAIN;
}

void gt917d_hw_reset(struct device *dev)
{
    struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();

	touch_gpio_direction_output(ts->reset_pin, 0);
	touch_msleep(20);

    touch_gpio_direction_output(ts->reset_pin, 1);
    touch_msleep(50);
}

static int gt917d_parse_dt_cfg(struct device *dev, u8 *cfg, int *cfg_len, u8 sid)
{
	struct device_node *np = dev->of_node;
	struct property *prop = NULL;
	char cfg_name[18] = {0, };
	int ret = 0;

	TOUCH_TRACE();

	snprintf(cfg_name, sizeof(cfg_name), "cfg-group%d", sid);
	prop = of_find_property(np, cfg_name, cfg_len);
	if (!prop || !prop->value || *cfg_len == 0 ||
			*cfg_len > GT917D_CONFIG_MAX_LENGTH) {
		*cfg_len = 0;
		ret = -EPERM;
	} else {
		memcpy(cfg, prop->value, *cfg_len);
		ret = 0;
	}

    return ret;
}

static int gt917d_find_valid_cfg_data(struct device *dev)
{
	struct gt917d_data *d = to_gt917d_data(dev);
	int ret = -1;
	u8 sensor_id = 0;
	struct gt917d_config_data *cfg = &d->config;

	TOUCH_TRACE();

	cfg->length = 0;

	/* read sensor id */
	ret = gt917d_i2c_read_dbl_check(dev, GT917D_REG_SENSOR_ID, &sensor_id, 1);
	if (ret != SUCCESS || sensor_id >= 0x06) {
		TOUCH_E("Failed get valid sensor_id(0x%02X), No Config Sent\n", sensor_id);
		return -EINVAL;
	}
	TOUCH_I("Sensor_ID: %d\n", sensor_id);

	/* parse config data */
	TOUCH_I("Get config data from device tree\n");
	ret = gt917d_parse_dt_cfg(dev,
			&cfg->data[GT917D_ADDR_LENGTH],
			&cfg->length, sensor_id);
	if (ret < 0) {
		TOUCH_E("Failed to parse config data form device tree\n");

		cfg->length = 0;
		return -EPERM;
	}

	if (cfg->length < GT917D_CONFIG_MIN_LENGTH) {
		TOUCH_E("Failed get valid config data with sensor id %d\n", sensor_id);
		cfg->length = 0;
		return -EPERM;
	}
	TOUCH_I("Config group%d used,length: %d\n", sensor_id, cfg->length);

	return 0;
}

int gt917d_send_cfg(struct device *dev)
{
	s32 ret = 0;
	s32 i = 0;
	u8 check_sum = 0;
	s32 retry = 0;
	struct gt917d_data *d = to_gt917d_data(dev);
	struct gt917d_config_data *cfg = &d->config;

	TOUCH_TRACE();

	if (!cfg->length) {
		TOUCH_I("No config data or error occurred in panel_init\n");
		return 0;
	}

	check_sum = 0;
	for (i = GT917D_ADDR_LENGTH; i < cfg->length; i++)
		check_sum += cfg->data[i];
	cfg->data[cfg->length] = (~check_sum) + 1;

	TOUCH_I("Driver send config\n");
	for (retry = 0; retry < RETRY_MAX_TIMES; retry++) {
		ret = gt917d_reg_write(dev, GT917D_REG_CONFIG_DATA,
				&cfg->data[GT917D_ADDR_LENGTH], GT917D_CONFIG_MAX_LENGTH);
		if (ret == 0)
			break;
	}

    return ret;
}

int gt917d_init_panel(struct device *dev)
{
	struct gt917d_data *d = to_gt917d_data(dev);
	s32 ret = -1;
	u8 opr_buf[16] = {0};
	u8 drv_cfg_version = 0;
	u8 flash_cfg_version = 0;
	struct gt917d_config_data *cfg = &d->config;

	TOUCH_TRACE();

	gt917d_find_valid_cfg_data(dev);

	/* check firmware */
	ret = gt917d_i2c_read_dbl_check(dev, GT917D_REG_FW_MSG, opr_buf, 1);
	if (ret == SUCCESS) {
		if (opr_buf[0] != 0xBE) {
			set_bit(FW_ERROR, &d->flags);
			TOUCH_E("Firmware error, no config sent!\n");
			return -EINVAL;
		}
	}

	ret = gt917d_i2c_read_dbl_check(dev, GT917D_REG_CONFIG_DATA,
			&opr_buf[0], 1);
	if (ret == SUCCESS) {
		TOUCH_I("Config Version: %d; IC Config Version: %d\n",
				cfg->data[GT917D_ADDR_LENGTH], opr_buf[0]);
		flash_cfg_version = opr_buf[0];
		drv_cfg_version = cfg->data[GT917D_ADDR_LENGTH];

		if (flash_cfg_version < 90 &&
				flash_cfg_version > drv_cfg_version)
			cfg->data[GT917D_ADDR_LENGTH] = 0x00;
	} else {
		TOUCH_E("Failed to get ic config version!No config sent\n");
		return -EPERM;
	}

	ret = gt917d_send_cfg(dev);
	if (ret < 0)
		TOUCH_E("Send config error\n");
	else
		touch_msleep(250);

	/* restore config version */
	cfg->data[GT917D_ADDR_LENGTH] = drv_cfg_version;

    return 0;
}

static int gt917d_gesture_mode(struct device *dev)
{
    int ret = -1;
    int retry = 0;
    u8 i2c_control_buf = 8;

	TOUCH_TRACE();

	TOUCH_I("Entering gesture mode.\n");
	while (retry++ < 5) {
		ret = gt917d_reg_write(dev, GT917D_REG_COMMAND_CHECK, &i2c_control_buf, 1);
		if (ret < 0) {
			TOUCH_I("failed to set doze flag into 0x8046, %d\n", retry);
			continue;
		}
		ret = gt917d_reg_write(dev, GT917D_REG_COMMAND, &i2c_control_buf, 1);
		if (ret == 0) {
			TOUCH_I("Gesture mode enabled\n");
			goto exit;
		}
		touch_msleep(10);
	}
	TOUCH_E("Failed enter doze mode\n");
exit:
	touch_msleep(100);
	return ret;
}

int gt917d_wakeup(struct device *dev)
{
	u8 retry = 0;
	int ret = -1;

	TOUCH_TRACE();

	while (retry++ < 10) {
		gt917d_hw_reset(dev);
		ret = gt917d_i2c_test(dev);
		if (!ret) {
			TOUCH_I("%s: Success wakeup\n", __func__);
			return ret;
		} else {
			continue;
		}
	}

	TOUCH_E("Failed wakeup\n");

	return -EINVAL;
}

static int gt917d_swipe_enable(struct device *dev, bool enable)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	u16 addr_offset = GT917D_REG_CONFIG_DATA - GT917D_ADDR_LENGTH;

	TOUCH_TRACE();

	TOUCH_I("%s: swipe %s - U(%d),L(%d),R(%d)\n", __func__,
			(enable ? "enable" : "disable"),
			ts->swipe[SWIPE_U].enable,
			ts->swipe[SWIPE_L].enable,
			ts->swipe[SWIPE_R].enable);

	if (enable) {
		d->config.data[GESTURE_SWITCH - addr_offset] &= ~((u8)(GESTURE_UP_EN | GESTURE_LEFT_EN | GESTURE_RIGHT_EN));
		d->swipe.common.enable = 0x00;
		if (ts->swipe[SWIPE_U].enable)
			d->swipe.common.enable |= GESTURE_UP_EN;
		if (ts->swipe[SWIPE_L].enable)
			d->swipe.common.enable |= GESTURE_LEFT_EN;
		if (ts->swipe[SWIPE_R].enable)
			d->swipe.common.enable |= GESTURE_RIGHT_EN;
		d->config.data[GESTURE_SWITCH - addr_offset] |= d->swipe.common.enable;

		d->config.data[SWIPE_TIME - addr_offset] = d->swipe.common.time.min_max;
		d->config.data[SWIPE_WRONG_DIR - addr_offset] = d->swipe.common.wrong_dir;
		d->config.data[SWIPE_INIT_DISTANCE - addr_offset] = d->swipe.common.init_distance;

		d->config.data[SWIPE_U_DISTANCE - addr_offset] = d->swipe.u.distance;
		d->config.data[SWIPE_U_RATIO - addr_offset] = d->swipe.u.ratio;
		d->config.data[SWIPE_U_INIT_RATIO - addr_offset] = d->swipe.u.init_ratio;
		d->config.data[SWIPE_U_AREA_X1 - addr_offset] = d->swipe.u.area.x1;
		d->config.data[SWIPE_U_AREA_X2 - addr_offset] = d->swipe.u.area.x2;
		d->config.data[SWIPE_U_AREA_Y1 - addr_offset] = d->swipe.u.area.y1;
		d->config.data[SWIPE_U_AREA_Y2 - addr_offset] = d->swipe.u.area.y2;
		d->config.data[SWIPE_U_START_AREA_X1 - addr_offset] = d->swipe.u.start_area.x1;
		d->config.data[SWIPE_U_START_AREA_X2 - addr_offset] = d->swipe.u.start_area.x2;
		d->config.data[SWIPE_U_START_AREA_Y1 - addr_offset] = d->swipe.u.start_area.y1;
		d->config.data[SWIPE_U_START_AREA_Y2 - addr_offset] = d->swipe.u.start_area.y2;

		d->config.data[SWIPE_LR_DISTANCE - addr_offset] = d->swipe.l_r.distance;
		d->config.data[SWIPE_LR_RATIO - addr_offset] = d->swipe.l_r.ratio;
		d->config.data[SWIPE_LR_INIT_RATIO - addr_offset] = d->swipe.l_r.init_ratio;
		d->config.data[SWIPE_LR_AREA_X1 - addr_offset] = d->swipe.l_r.area.x1;
		d->config.data[SWIPE_LR_AREA_X2 - addr_offset] = d->swipe.l_r.area.x2;
		d->config.data[SWIPE_LR_AREA_Y1 - addr_offset] = d->swipe.l_r.area.y1;
		d->config.data[SWIPE_LR_AREA_Y2 - addr_offset] = d->swipe.l_r.area.y2;
		d->config.data[SWIPE_L_START_AREA_X1 - addr_offset] = d->swipe.l_r.left_start_area.x1;
		d->config.data[SWIPE_L_START_AREA_X2 - addr_offset] = d->swipe.l_r.left_start_area.x2;
		d->config.data[SWIPE_L_START_AREA_Y1 - addr_offset] = d->swipe.l_r.left_start_area.y1;
		d->config.data[SWIPE_L_START_AREA_Y2 - addr_offset] = d->swipe.l_r.left_start_area.y2;
		d->config.data[SWIPE_R_START_AREA_X1 - addr_offset] = d->swipe.l_r.right_start_area.x1;
		d->config.data[SWIPE_R_START_AREA_X2 - addr_offset] = d->swipe.l_r.right_start_area.x2;
		d->config.data[SWIPE_R_START_AREA_Y1 - addr_offset] = d->swipe.l_r.right_start_area.y1;
		d->config.data[SWIPE_R_START_AREA_Y2 - addr_offset] = d->swipe.l_r.right_start_area.y2;
	} else {
		d->config.data[GESTURE_SWITCH - addr_offset] &= ~((u8)(GESTURE_UP_EN | GESTURE_LEFT_EN | GESTURE_RIGHT_EN));
	}

	return 0;
}

int gt917d_lpwg_control(struct device *dev, u8 mode)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	u16 addr_offset = GT917D_REG_CONFIG_DATA - GT917D_ADDR_LENGTH;
	int ret = 0;

	TOUCH_TRACE();

	switch (mode) {
	case LPWG_NONE:
	case LPWG_DOUBLE_TAP:
		touch_interrupt_control(dev, INTERRUPT_DISABLE);
		ret = gt917d_gesture_mode(dev);
		touch_interrupt_control(dev, INTERRUPT_ENABLE);

		if (ret < 0)
			TOUCH_E("Failed enter suspend\n");
		/* to avoid waking up while not sleeping */
		/* delay 48 + 10ms to ensure reliability */
		touch_msleep(58);
		break;
	default:
		break;
	}

	if (mode == LPWG_DOUBLE_TAP)
		d->config.data[GESTURE_SWITCH - addr_offset] |= (u8)GESTURE_DOUBLECLICK_EN;
	else
		d->config.data[GESTURE_SWITCH - addr_offset] &= ~((u8)GESTURE_DOUBLECLICK_EN);

	gt917d_swipe_enable(dev, true);

	gt917d_send_cfg(dev);

	ts->tci.mode = mode;
	TOUCH_I("%s: tci = %d\n", __func__, ts->tci.mode);

	return ret;
}

static int gt917d_deep_sleep(struct device *dev, bool enable)
{
	struct touch_core_data *ts = to_touch_core(dev);
	s32 ret = -1;
	u8 buf = 5;

	TOUCH_TRACE();

	TOUCH_I("%s: enable = %d\n", __func__, enable);

	if (enable) {
		TOUCH_I("%s: Enter to deep sleep mode\n", __func__);

		if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
			TOUCH_I("%s: Already in deep sleep state\n", __func__);
			return 0;
		}

		atomic_set(&ts->state.sleep, IC_DEEP_SLEEP);

		touch_interrupt_control(dev, INTERRUPT_DISABLE);

		gt917d_reg_write(dev, GT917D_REG_COMMAND, &buf, 1);

		/* to avoid waking up while not sleeping */
		/* delay 48 + 10ms to ensure reliability */
		touch_msleep(58);

		return ret;
	} else {
		TOUCH_I("%s: Awake from deep sleep mode\n", __func__);

		if (atomic_read(&ts->state.sleep) == IC_NORMAL) {
			TOUCH_I("%s: Already in awake state\n", __func__);
			return 0;
		}

		touch_interrupt_control(dev, INTERRUPT_DISABLE);

		ret = gt917d_wakeup(dev);

		touch_interrupt_control(dev, INTERRUPT_ENABLE);

		atomic_set(&ts->state.sleep, IC_NORMAL);

		return ret;
	}
}

static int gt917d_lpwg_mode(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	u8 next_state = TC_STATE_ACTIVE;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return 0;
	}

	if (atomic_read(&d->init) == IC_INIT_NEED) {
		TOUCH_I("%s: Not Ready, Need IC init\n", __func__);
		return 0;
	}

	if (d->palm == PALM_PRESSED) {
		d->palm = PALM_RELEASED;
		TOUCH_I("%s: Palm released\n", __func__);
	}

	if ((ts->mfts_lpwg) && (atomic_read(&ts->state.fb) == FB_SUSPEND)) {
		TOUCH_I("%s: mfts_lpwg & FB_SUSPEND => TC_STATE_LPWG\n", __func__);
		gt917d_lpwg_control(dev, LPWG_DOUBLE_TAP);
		d->state = TC_STATE_LPWG;
		return 0;
	}

	if (ts->lpwg.screen) {
		next_state = TC_STATE_ACTIVE;
	} else {	/* screen off  */
		if (ts->lpwg.sensor == PROX_NEAR) {
			next_state = TC_STATE_DEEP_SLEEP;
		} else {	/* sensor far */
			if (ts->lpwg.mode == LPWG_DOUBLE_TAP) {
				next_state = TC_STATE_LPWG;
			} else {	/* LPWG_NONE */
				next_state = TC_STATE_DEEP_SLEEP;
				if (ts->swipe[SWIPE_U].enable
						|| ts->swipe[SWIPE_L].enable
						|| ts->swipe[SWIPE_R].enable) {
					next_state = TC_STATE_LPWG;
					TOUCH_I("%s: LPWG_NONE & swipe enable => TC_STATE_LPWG\n", __func__);
				}
			}
		}
	}

	TOUCH_I("%s: state changed from [%d] to [%d]\n", __func__,  d->state, next_state);

	if (d->state == TC_STATE_ACTIVE) {
		if (next_state == TC_STATE_ACTIVE) {
			goto skip;
		} else if (next_state == TC_STATE_LPWG) {
			gt917d_lpwg_control(dev, ts->lpwg.mode);
		} else if (next_state == TC_STATE_DEEP_SLEEP) {
			gt917d_deep_sleep(dev, true);
		} else {
			goto invalid_state;
		}
	} else if (d->state == TC_STATE_LPWG) {
		if (next_state == TC_STATE_ACTIVE) {
			touch_report_all_event(ts);
			gt917d_wakeup(dev);
		} else if (next_state == TC_STATE_LPWG) {
			goto skip;
		} else if (next_state == TC_STATE_DEEP_SLEEP) {
			gt917d_hw_reset(dev);
			gt917d_deep_sleep(dev, true);
		} else {
			goto invalid_state;
		}
	} else if (d->state == TC_STATE_DEEP_SLEEP) {
		if (next_state == TC_STATE_ACTIVE) {
			gt917d_deep_sleep(dev, false);
		} else if (next_state == TC_STATE_LPWG) {
			gt917d_deep_sleep(dev, false);
			gt917d_lpwg_control(dev, ts->lpwg.mode);
		} else if (next_state == TC_STATE_DEEP_SLEEP) {
			goto skip;
		} else {
			goto invalid_state;
		}
	} else {
		goto invalid_state;
	}

	d->state = next_state;
	return 0;
skip:
	TOUCH_I("%s: skip\n", __func__);
	return 0;
invalid_state:
	TOUCH_E("invalid state\n");
	return -EINVAL;
}

static int gt917d_lpwg(struct device *dev, u32 code, void *param)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int *value = (int *)param;

	TOUCH_TRACE();

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
		ts->lpwg.mode = value[0];
		ts->lpwg.screen = value[1];
		ts->lpwg.sensor = value[2];
		ts->lpwg.qcover = value[3];

		TOUCH_I("LPWG_UPDATE_ALL: mode[%d], screen[%s], sensor[%s], qcover[%s]\n",
				value[0],
				value[1] ? "ON" : "OFF",
				value[2] ? "FAR" : "NEAR",
				value[3] ? "CLOSE" : "OPEN");

            gt917d_lpwg_mode(dev);
            break;

        case LPWG_REPLY:
            break;

    }

    return 0;
}

static int gt917d_usb_status(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int charger_state = atomic_read(&ts->state.connect);
	u8 charger_mode = CHARGER_MODE;

	TOUCH_TRACE();

	TOUCH_I("%s: TA Type: %d\n", __func__, charger_state);

	if (charger_state)
		charger_mode = CHARGER_MODE;
	else
		charger_mode = NOT_CHARGER_MODE;

	if (ts->lpwg.screen == 0) {
		TOUCH_I("%s: ts->lpwg.screen is off - Don't try write\n", __func__);
		return -EAGAIN;
	}

	if (atomic_read(&ts->state.pm) > DEV_PM_RESUME) {
		TOUCH_I("%s: DEV_PM_SUSPEND - Don't try write\n", __func__);
		return -EAGAIN;
	}

	TOUCH_I("%s: write charger_mode = 0x%02X\n", __func__, charger_mode);
	gt917d_reg_write(dev, GT917D_REG_COMMAND, &charger_mode, sizeof(charger_mode));
	touch_msleep(20);

	return 0;
}

static int gt917d_ime_status(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ime_state = atomic_read(&ts->state.ime);
	u8 ime_mode = IME_MODE;

	TOUCH_TRACE();

	TOUCH_I("%s: IME: %d\n", __func__, ime_state);

	if (ime_state)
		ime_mode = IME_MODE;
	else
		ime_mode = NOT_IME_MODE;

	if (ts->lpwg.screen == 0) {
		TOUCH_I("%s: ts->lpwg.screen is off - Don't try write\n", __func__);
		return -EAGAIN;
	}

	if (atomic_read(&ts->state.pm) > DEV_PM_RESUME) {
		TOUCH_I("%s: DEV_PM_SUSPEND - Don't try write\n", __func__);
		return -EAGAIN;
	}

	TOUCH_I("%s: write ime_mode = 0x%02X\n", __func__, ime_mode);
	gt917d_reg_write(dev, GT917D_REG_COMMAND, &ime_mode, sizeof(ime_mode));
	touch_msleep(20);

	return 0;
}

static int gt917d_notify(struct device *dev, ulong event, void *data)
{
	int ret = 0;

	TOUCH_TRACE();

	TOUCH_I("%s event=0x%x\n", __func__, (unsigned int)event);
	switch (event) {
	case NOTIFY_CONNECTION:
		TOUCH_I("NOTIFY_CONNECTION!\n");
		ret = gt917d_usb_status(dev);
		break;
	case NOTIFY_IME_STATE:
		TOUCH_I("NOTIFY_IME_STATE!\n");
		ret = gt917d_ime_status(dev);
		break;
	case NOTIFY_CALL_STATE:
		TOUCH_I("NOTIFY_CALL_STATE!\n");
		break;
	default:
		TOUCH_E("%lx is not supported\n", event);
		break;
	}

    return ret;
}

static int gt917d_fw_recovery(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	struct i2c_client *client = to_i2c_client(dev);
	int ret = 0;
	char fwpath[256] = {0};
	int boot_mode = TOUCH_NORMAL_BOOT;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);
	if ((boot_mode == TOUCH_CHARGER_MODE)
			|| (boot_mode == TOUCH_LAF_MODE)
			|| (boot_mode == TOUCH_RECOVERY_MODE)) {
		TOUCH_I("%s: boot_mode = %d(CHARGER:5/LAF:6/RECOVER_MODE:7)\n", __func__, boot_mode);
		return -EPERM;
	}

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return -EPERM;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return -EPERM;
	}

	client->addr = 0x14;
	TOUCH_I("%s: Change Slave Addr(0x14) for recovery\n", __func__);
	ret = gt917d_i2c_test(dev);
	if (ret) {
		TOUCH_I("%s: Failed communicate with IC use I2C address 0x14\n", __func__);
		return -EPERM;
	}

	if (ts->test_fwpath[0] && (ts->test_fwpath[0] != 't' && ts->test_fwpath[1] != 0)) {
		memcpy(fwpath, &ts->test_fwpath[0], sizeof(fwpath));
		TOUCH_I("%s: get fwpath from test_fwpath:%s\n", __func__, &ts->test_fwpath[0]);
	} else if (ts->def_fwcnt) {
		memcpy(fwpath, ts->def_fwpath[0], sizeof(fwpath)); // 0 : normal bin, 1 : shorttest bin
		TOUCH_I("%s: get fwpath from def_fwpath : %s\n", __func__, fwpath);
	} else {
		TOUCH_E("no firmware file\n");
		return -EPERM;
	}

	fwpath[sizeof(fwpath) - 1] = '\0';
	if (strlen(fwpath) <= 0) {
		TOUCH_E("error get fw path\n");
		return -EPERM;
	}

	TOUCH_I("%s: fwpath[%s]\n", __func__, fwpath);
	ret = gt917d_update_proc(dev, fwpath);
	if (ret == FAIL) {
		TOUCH_E("fail to gt917d_fw_upgrade,fwpath: %s\n", ts->def_fwpath[0]);
		return -EPERM;
	}
	return 0;
}

static int gt917d_init(struct device *dev)
{
	struct gt917d_data *d = to_gt917d_data(dev);
	int ret = 0;

	TOUCH_TRACE();

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return -EPERM;
	}

	ret = gt917d_get_fw_info(dev);
	if (ret < 0)
		TOUCH_E("failed to get fw_info, ret:%d\n", ret);

	gt917d_usb_status(dev);
	gt917d_ime_status(dev);

	if (d->palm == PALM_PRESSED) {
		d->palm = PALM_RELEASED;
		TOUCH_I("%s: Palm released\n", __func__);
	}

	atomic_set(&d->init, IC_INIT_DONE);

	gt917d_lpwg_mode(dev);

	return 0;
}

static int gt917d_power(struct device *dev, int ctrl)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);

	TOUCH_TRACE();

	switch (ctrl) {
	case POWER_OFF:
		TOUCH_I("%s, off\n", __func__);

		touch_interrupt_control(dev, INTERRUPT_DISABLE);
		touch_report_all_event(ts);

		atomic_set(&d->init, IC_INIT_NEED);
		d->state = TC_STATE_POWER_OFF;

		touch_power_1_8_vdd(dev, 0);
		touch_power_3_3_vcl(dev, 0);
		touch_gpio_direction_output(ts->reset_pin, 0);
		break;
	case POWER_ON:
		TOUCH_I("%s, on\n", __func__);

		touch_power_3_3_vcl(dev, 1);
		touch_power_1_8_vdd(dev, 1);
		touch_msleep(15);
		touch_gpio_direction_output(ts->reset_pin, 1);
		touch_msleep(50);

		atomic_set(&ts->state.sleep, IC_NORMAL);
		d->state = TC_STATE_ACTIVE;
		break;
	case POWER_SLEEP:
		TOUCH_I("%s, sleep\n", __func__);
		break;
	case POWER_WAKE:
		TOUCH_I("%s, wake\n", __func__);
		break;
	case POWER_HW_RESET_ASYNC:
		TOUCH_I("%s, HW Reset(%d)\n", __func__, ctrl);

		touch_interrupt_control(dev, INTERRUPT_DISABLE);
		touch_report_all_event(ts);

		atomic_set(&d->init, IC_INIT_NEED);
		d->state = TC_STATE_POWER_OFF;

		gt917d_hw_reset(dev);

		atomic_set(&ts->state.sleep, IC_NORMAL);
		d->state = TC_STATE_ACTIVE;

		queue_delayed_work(ts->wq, &ts->init_work, 0);
		break;
	case POWER_HW_RESET_SYNC:
		TOUCH_I("%s, HW Reset(%d)\n", __func__, ctrl);

		touch_interrupt_control(dev, INTERRUPT_DISABLE);
		touch_report_all_event(ts);

		atomic_set(&d->init, IC_INIT_NEED);
		d->state = TC_STATE_POWER_OFF;

		gt917d_hw_reset(dev);

		atomic_set(&ts->state.sleep, IC_NORMAL);
		d->state = TC_STATE_ACTIVE;

		gt917d_init(dev);
		touch_interrupt_control(dev, INTERRUPT_ENABLE);
		break;
	case POWER_SW_RESET:
		TOUCH_I("%s, SW Reset\n", __func__);

		touch_interrupt_control(dev, INTERRUPT_DISABLE);
		touch_report_all_event(ts);

		atomic_set(&d->init, IC_INIT_NEED);
		d->state = TC_STATE_POWER_OFF;

		gt917d_hw_reset(dev);

		atomic_set(&ts->state.sleep, IC_NORMAL);
		d->state = TC_STATE_ACTIVE;

		gt917d_init(dev);
		touch_interrupt_control(dev, INTERRUPT_ENABLE);
		break;
	default:
		TOUCH_I("%s, Unknown Power Ctrl!!!!\n", __func__);
		break;
	}

	return 0;
}

static int gt917d_probe(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct i2c_client *client = to_i2c_client(dev);
	struct gt917d_data *d = NULL;
	int boot_mode = TOUCH_NORMAL_BOOT;
	int ret = -1;

	TOUCH_TRACE();

	i2c_connect_client = client;

	TOUCH_I("GT917D Driver Version: %s\n", GT917D_DRIVER_VERSION);
	TOUCH_I("GT917D I2C Address: 0x%02x\n", client->addr);

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d) {
		TOUCH_E("failed to allocate gt917d_data\n");
		return -ENOMEM;
	}

	d->dev = dev;
	d->client = client;

	mutex_init(&d->rw_lock);

	touch_set_device(ts, d);

    touch_gpio_init(ts->reset_pin, "touch_reset");
    touch_gpio_direction_output(ts->reset_pin, 0);

    touch_gpio_init(ts->int_pin, "touch_int");
    touch_gpio_direction_input(ts->int_pin);

	touch_power_init(dev);
	touch_bus_init(dev, MAX_BUF_SIZE);

	gt917d_get_swipe_info(dev);

	d->config.data[0] = GT917D_REG_CONFIG_DATA >> 8;
	d->config.data[1] = GT917D_REG_CONFIG_DATA & 0XFF;

	gt917d_power(dev, POWER_ON);

	ret = gt917d_i2c_test(dev);
	if (ret) {
		TOUCH_E("Failed communicate with IC use I2C\n\n");
		ret = gt917d_fw_recovery(dev);
		if (ret) {
			TOUCH_I("Failed communicate with IC use I2C address 0x14\n");
			return -EPERM;
		}
	}

	ret = gt917d_init_panel(dev);
	if (ret < 0)
		TOUCH_E("Panel un-initialize\n");

	ret = gt917d_create_file(d);
	if (ret)
		TOUCH_I("Failed create attributes file\n");

	init_wr_node(client);

	boot_mode = touch_check_boot_mode(dev);
	if ((boot_mode == TOUCH_CHARGER_MODE)
			|| (boot_mode == TOUCH_LAF_MODE)
			|| (boot_mode == TOUCH_RECOVERY_MODE)) {
		TOUCH_I("%s: boot_mode = %d -> power off\n", __func__, boot_mode);
		gt917d_power(dev, POWER_OFF);
	}

    return 0;
}

static int gt917d_remove(struct device *dev)
{
	struct gt917d_data *d = to_gt917d_data(dev);

	TOUCH_TRACE();

	uninit_wr_node();
	gt917d_remove_file(d);
	/*
	touch_bus_init(dev, MAX_BUF_SIZE);		=> touch_bus_remove() is needed
	touch_power_init(dev);				=> touch_power_remove() is needed
	touch_gpio_init(ts->int_pin, "touch_int");	=> touch_gpio_remove() is needed
	touch_gpio_init(ts->reset_pin, "touch_reset");	=> touch_gpio_remove() is needed
	*/
	devm_kfree(dev, d);
	i2c_connect_client = NULL;

	return 0;
}

static int gt917d_shutdown(struct device *dev)
{
	TOUCH_TRACE();

	gt917d_power(dev, POWER_OFF);

    return 0;
}

static int gt917d_upgrade(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	char fwpath[256] = {0};
	int ret = 0;
	int boot_mode = TOUCH_NORMAL_BOOT;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);
	if ((boot_mode == TOUCH_CHARGER_MODE)
			|| (boot_mode == TOUCH_LAF_MODE)
			|| (boot_mode == TOUCH_RECOVERY_MODE)) {
		TOUCH_I("%s: boot_mode = %d(CHARGER:5/LAF:6/RECOVER_MODE:7)\n", __func__, boot_mode);
		return -EPERM;
	}

	if (atomic_read(&ts->state.fb) >= FB_SUSPEND) {
		TOUCH_I("%s: state.fb is not FB_RESUME\n", __func__);
		return -EPERM;
	}

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return -EPERM;
	}

	if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP) {
		TOUCH_I("%s: ts->state.sleep is IC_DEEP_SLEEP\n", __func__);
		return -EPERM;
	}

	if (ts->test_fwpath[0] && (ts->test_fwpath[0] != 't' && ts->test_fwpath[1] != 0)) {
		memcpy(fwpath, &ts->test_fwpath[0], sizeof(fwpath));
		TOUCH_I("%s: get fwpath from test_fwpath:%s\n", __func__, &ts->test_fwpath[0]);
	} else if (ts->def_fwcnt) {
		memcpy(fwpath, ts->def_fwpath[0], sizeof(fwpath)); // 0 : normal bin, 1 : shorttest bin
		TOUCH_I("%s: get fwpath from def_fwpath : %s\n", __func__, fwpath);
	} else {
		TOUCH_E("no firmware file\n");
		return -EPERM;
	}

	fwpath[sizeof(fwpath) - 1] = '\0';
	if (strlen(fwpath) <= 0) {
		TOUCH_E("error get fw path\n");
		return -EPERM;
	}

	TOUCH_I("%s: fwpath[%s]\n", __func__, fwpath);
	ret = gt917d_update_proc(dev, fwpath);
	if (ret == FAIL) {
		TOUCH_E("fail to gt917d_fw_upgrade,fwpath: %s\n", ts->def_fwpath[0]);
		return -EPERM;
	}

    return 0;
}

static int gt917d_suspend(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	int boot_mode = TOUCH_NORMAL_BOOT;
	int ret = 0;

	TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);

	switch (boot_mode) {
	case TOUCH_NORMAL_BOOT:
	case TOUCH_MINIOS_AAT:
		break;
	case TOUCH_MINIOS_MFTS_FOLDER:
	case TOUCH_MINIOS_MFTS_FLAT:
	case TOUCH_MINIOS_MFTS_CURVED:
		if (!ts->mfts_lpwg) {
			TOUCH_I("%s: touch_suspend - MFTS\n", __func__);
			gt917d_power(dev, POWER_OFF);
			return -EPERM;
		}
		break;
	case TOUCH_CHARGER_MODE:
	case TOUCH_LAF_MODE:
	case TOUCH_RECOVERY_MODE:
		TOUCH_I("%s: Etc boot_mode(%d)!!!\n", __func__, boot_mode);
		return -EPERM;
	default:
		TOUCH_E("invalid boot_mode = %d\n", boot_mode);
		return -EPERM;
	}

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return -EPERM;
	}

	if (atomic_read(&d->init) == IC_INIT_DONE) {
		gt917d_lpwg_mode(dev);
	} else {
		TOUCH_I("%s: d->init is IC_INIT_NEED\n", __func__);
		ret = 1;
	}

    return ret;
}

static int gt917d_resume(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);
	int boot_mode = TOUCH_NORMAL_BOOT;

    TOUCH_TRACE();

	boot_mode = touch_check_boot_mode(dev);

	switch (boot_mode) {
	case TOUCH_NORMAL_BOOT:
	case TOUCH_MINIOS_AAT:
		break;
	case TOUCH_MINIOS_MFTS_FOLDER:
	case TOUCH_MINIOS_MFTS_FLAT:
	case TOUCH_MINIOS_MFTS_CURVED:
		if (!ts->mfts_lpwg) {
			TOUCH_I("%s: touch_resume - MFTS\n", __func__);
			gt917d_power(dev, POWER_ON);
		}
		break;
	case TOUCH_CHARGER_MODE:
	case TOUCH_LAF_MODE:
	case TOUCH_RECOVERY_MODE:
		TOUCH_I("%s: Etc boot_mode(%d)!!!\n", __func__, boot_mode);
		return -EPERM;
	default:
		TOUCH_E("invalid boot_mode = %d\n", boot_mode);
		return -EPERM;
	}

	if (d->state == TC_STATE_POWER_OFF) {
		TOUCH_I("%s: d->state is TC_STATE_POWER_OFF\n", __func__);
		return -EPERM;
	}

	return 0;
}

static int gt917d_irq_abs_data(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct touch_data *tdata = NULL;
	u32 touch_count = 0;
	u8 finger_index = 0;
	int i = 0;
	u8 touch_id = 0;

    TOUCH_TRACE();
    touch_count = touch_cnt;
    ts->new_mask = 0;

    if(touch_count == 0) {
        ts->tcount = 0;
        ts->intr_status = TOUCH_IRQ_FINGER;
        return 0;
    }
    for (i = 0; i < touch_count; i++) {
        touch_id = points[i].id;
        if (touch_id >= ts->caps.max_id) {
            break; // ??
        }

        ts->new_mask |= (1 << touch_id);
        tdata = ts->tdata + touch_id;

		tdata->id = touch_id;
		tdata->type = MT_TOOL_FINGER;
		tdata->x = points[i].x;
		tdata->y = points[i].y;
		tdata->pressure = points[i].p;
		tdata->width_major = 0;
		tdata->width_minor = 0;
		tdata->orientation = 0;

		finger_index++;

		TOUCH_D(ABS, "tdata [id:%2d x:%4d y:%4d z:%3d - %3d,%3d,%3d]\n",
				tdata->id,
				tdata->x, tdata->y, tdata->pressure,
				tdata->width_major,
				tdata->width_minor,
				tdata->orientation);

    }
    ts->tcount = finger_index;
    ts->intr_status = TOUCH_IRQ_FINGER;

    return 0;

}

static u8 CalCheckSum(u8 *s, u8 len)
{
	u8 sum = 0;

	TOUCH_TRACE();

	do {
		sum += s[--len];
	} while (len != 0);

	return sum;
}

static int gt917d_irq_abs(struct device *dev)
{
	int ret = 0;
	int i = 0;
	u8 *coor_data = NULL;
	u8 finger_state = 0;
	u8 touch_num = 0;
	u8 calCheckSum = 0;
	u8 point_data[1 + 8 * GT917D_MAX_TOUCH_ID + 1 + 1] = {0,};
	struct touch_core_data *ts = to_touch_core(dev);
	struct gt917d_data *d = to_gt917d_data(dev);

    TOUCH_TRACE();

	memset(points, 0, sizeof(*points) * GT917D_MAX_TOUCH_ID);
	ret = gt917d_reg_read(dev, GT917D_READ_COOR_ADDR, point_data, 11);
	if (ret < 0) {
		TOUCH_E("I2C transfer error. errno:%d\n", ret);
		return 0;
	}

	finger_state = point_data[0];
	touch_num = finger_state & 0x0f;
	if (finger_state == 0x00)
		return 0;

	touch_cnt = touch_num;
	if ((finger_state & MASK_BIT_8) == 0 ||
			touch_num > ts->caps.max_id) {
		TOUCH_E("Invalid touch state: 0x%x\n", finger_state);

		finger_state = 0;
		goto exit_get_point;
	}

	if (finger_state & 0x40) {
		if (d->palm == PALM_RELEASED) {
			d->palm = PALM_PRESSED;
			TOUCH_I("%s: Palm pressed\n", __func__);
		}
	} else {
		if (d->palm == PALM_PRESSED) {
			d->palm = PALM_RELEASED;
			TOUCH_I("%s: Palm released\n", __func__);
		}
	}

	if (touch_num > 1) {
		u8 buf[8 * GT917D_MAX_TOUCH_ID] = {0, };

		ret = gt917d_reg_read(dev, GT917D_READ_COOR_ADDR + 11, buf, 8 * (touch_num - 1));
		if (ret < 0) {
			TOUCH_E("I2C error. %d\n", ret);
			finger_state = 0;
			goto exit_get_point;
		}
		memcpy(&point_data[11], &buf[0], 8 * (touch_num - 1));
	}

	for (i = 0; i < touch_num; i++) {
		coor_data = &point_data[i * 8 + 1];
		points[i].id = coor_data[0];
		points[i].x = coor_data[1] | (coor_data[2] << 8);
		points[i].y = coor_data[3] | (coor_data[4] << 8);
		points[i].w = coor_data[5] | (coor_data[6] << 8);
		/* if pen hover points[].p must set to zero */
		points[i].p = coor_data[5] | (coor_data[6] << 8);
		points[i].tool_type = GT917D_TOOL_FINGER;
	}

	calCheckSum = 0 - CalCheckSum(&point_data[0], 1 + 8 * touch_num + 1);
	if (calCheckSum != point_data[1 + 8 * touch_num + 1]) {
		TOUCH_E("CalCheckSum:%d\n", calCheckSum);
		TOUCH_E("Receive CheckSum:%d\n", point_data[1 + 8 * touch_num + 1]);
		TOUCH_E("CalCheckSum error\n");
		return 0;
	}
exit_get_point:
	return gt917d_irq_abs_data(dev);
}

static int gt917d_get_tci_data(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	u8 doze_buf[10] = {0, };
	int tapcount = 2;	/* double tap */
	int i = 0;
	int offset = 0;
	int ret = 0;

	TOUCH_TRACE();

	ret = gt917d_reg_read(dev, GT917D_REG_DOZE_BUF, doze_buf, sizeof(doze_buf));
	if (ret < 0) {
		TOUCH_E("Failed read doze buf\n");
		return -EINVAL;
	}

	ts->lpwg.code_num = tapcount;

	for (i = 0; i < tapcount; i++) {
		offset = 4 * i;
		ts->lpwg.code[i].x = doze_buf[offset + 2] | doze_buf[offset + 3] << 8;
		ts->lpwg.code[i].y = doze_buf[offset + 4] | doze_buf[offset + 5] << 8;
		TOUCH_I("LPWG data (%4d, %4d)\n", ts->lpwg.code[i].x, ts->lpwg.code[i].y);
	}

	ts->lpwg.code[tapcount].x = -1;
	ts->lpwg.code[tapcount].y = -1;

	ts->intr_status = TOUCH_IRQ_KNOCK;

	return ret;
}

static int gt917d_get_swipe_data(struct device *dev, int swipe)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;
	u8 swipe_data_buf[10] = {0, };
	char direction = '?';
	u16 x_start_point = 0;
	u16 x_end_point = 0;
	u16 y_start_point = 0;
	u16 y_end_point = 0;

	TOUCH_TRACE();

	switch (swipe) {
	case GESTURE_UP:
		direction = 'U';
		break;
	case GESTURE_LEFT:
		direction = 'L';
		break;
	case GESTURE_RIGHT:
		direction = 'R';
		break;
	default:
		break;
	}

	ret = gt917d_reg_read(dev, GT917D_REG_DOZE_BUF, swipe_data_buf, sizeof(swipe_data_buf));
	if (ret < 0) {
		TOUCH_E("Failed read doze buf\n");
		return -EINVAL;
	}

    x_start_point = swipe_data_buf[2]|swipe_data_buf[3]<<8;
    y_start_point = swipe_data_buf[4]|swipe_data_buf[5]<<8;
    x_end_point = swipe_data_buf[6]|swipe_data_buf[7]<<8;
    y_end_point = swipe_data_buf[8]|swipe_data_buf[9]<<8;

	TOUCH_I("SWIPE Data : SWIPE_[%c], start(%d, %d), end(%d, %d)\n",
			direction, x_start_point, y_start_point, x_end_point, y_end_point);

	ts->intr_status = TOUCH_IRQ_SWIPE_UP;

	return 0;
}

static int gt917d_irq_lpwg(struct device *dev)
{
	int ret = 0;
	u8 doze_buf = 0;
	u8 knockon_fail_reason = 0;
	u8 swipe_fail_reason = 0;

	TOUCH_TRACE();

	ret = gt917d_reg_read(dev, GT917D_REG_DOZE_BUF, &doze_buf, sizeof(doze_buf));
	if (ret < 0) {
		TOUCH_E("Failed read doze buf\n");
		return -EINVAL;
	}
	TOUCH_I("doze_buf = 0x%02x\n", doze_buf);

	switch (doze_buf) {
	case GESTURE_DOUBLECLICK:
		ret = gt917d_get_tci_data(dev);
		break;
	case GESTURE_UP:
	case GESTURE_RIGHT:
	case GESTURE_LEFT:
		ret = gt917d_get_swipe_data(dev, doze_buf);
		break;
	case KNOCKON_ERROR:
		gt917d_reg_read(dev, GT917D_REG_KNOCKON_ERR_REASON, &knockon_fail_reason, 1);
		if (knockon_fail_reason >= 0 && knockon_fail_reason <= 7)
			TOUCH_I("GT917D_REG_KNOCKON_ERR_REASON:%s\n", lpwg_failreason_tci_str[knockon_fail_reason]);
		else
			TOUCH_I("knockon_fail_reason = %d\n", knockon_fail_reason);
		break;
	case SWIPE_ERROR:
		gt917d_reg_read(dev, GT917D_REG_KNOCKON_ERR_REASON, &swipe_fail_reason, 1);
		if (swipe_fail_reason >= 0 && swipe_fail_reason <= 12)
			TOUCH_I("swipe failreason : %s\n", swipe_debug_str[swipe_fail_reason]);
		else
			TOUCH_I("swipe_fail_reason = %d\n", swipe_fail_reason);
		break;
	default:
		TOUCH_I("%s : not support type(0x%02x)\n", __func__, doze_buf);
		break;
	}

	doze_buf = 0x00;
	gt917d_reg_write(dev, GT917D_REG_DOZE_BUF, &doze_buf, 1);

	return ret;
}

static int gt917d_irq_handler(struct device *dev)
{
    int ret = 0;
    u8 int_status = 0;

	TOUCH_TRACE();

	gt917d_reg_read(dev, GT917D_REG_SENSOR_ID, &int_status, 1);
	if (ret < 0)
		return ret;

	if (int_status < 6) {
		ret = gt917d_irq_abs(dev);
	} else if (int_status == 0xa) {
		ret = gt917d_irq_lpwg(dev);
		if (ret) {
			TOUCH_E("Failed handler gesture event %d\n", ret);
			return ret;
		}
	} else {
		TOUCH_E("Invalid interrupt status : %d\n", int_status);
#if defined(CONFIG_LGE_DISPLAY_RECOVERY_ESD)
        ret = -ERESTART;
#endif
    }

    return ret;
}

static ssize_t store_reg_ctrl(struct device *dev, const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	char command[6] = {0};
	u32 cmd32 = 0;
	int value = 0;
	u16 cmd = 0;
	u32 data = 0;

	TOUCH_TRACE();

	if (sscanf(buf, "%5s %x %d", command, &cmd32, &value) <= 0)
		return count;

	mutex_lock(&ts->lock);

	cmd = (u16)cmd32;
	if (!strcmp(command, "write")) {
		data = value;
		if (gt917d_reg_write(dev, cmd, &data, sizeof(u32)) < 0)
			TOUCH_E("reg addr 0x%x write fail\n", cmd);
		else
			TOUCH_I("reg[%x] = 0x%x\n", cmd, data);
	} else if (!strcmp(command, "read")) {
		if (gt917d_reg_read(dev, cmd, &data, sizeof(u32)) < 0)
			TOUCH_E("reg addr 0x%x read fail\n", cmd);
		else
			TOUCH_I("reg[%x] = 0x%x\n", cmd, data);
	} else {
		TOUCH_I("Usage\n");
		TOUCH_I("Write reg value\n");
		TOUCH_I("Read reg\n");
	}

	mutex_unlock(&ts->lock);

	return count;
}

static ssize_t store_reset_ctrl(struct device *dev, const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int value = 0;

	TOUCH_TRACE();

	if (kstrtos32(buf, 10, &value) < 0)
		return count;

	mutex_lock(&ts->lock);

    if (value == 0) {
		gt917d_power(dev, POWER_SW_RESET);
	} else if (value == 1) {
		gt917d_power(dev, POWER_HW_RESET_ASYNC);
	} else if (value == 2) {
		gt917d_power(dev, POWER_HW_RESET_SYNC);
	} else if (value == 3) {
		gt917d_power(dev, POWER_OFF);
		gt917d_power(dev, POWER_ON);
		queue_delayed_work(ts->wq, &ts->init_work, 0);
	} else {
		TOUCH_I("Unsupported command %d\n", value);
	}

	mutex_unlock(&ts->lock);

	return count;
}

static ssize_t show_pinstate(struct device *dev, char *buf)
{
    int ret = 0;
    struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();

	ret = snprintf(buf, PAGE_SIZE, "RST:%d, INT:%d\n",
			gpio_get_value(ts->reset_pin), gpio_get_value(ts->int_pin));
	TOUCH_I("%s() buf:%s\n", __func__, buf);
	return ret;
}

static TOUCH_ATTR(reg_ctrl, NULL, store_reg_ctrl);
static TOUCH_ATTR(reset_ctrl, NULL, store_reset_ctrl);
static TOUCH_ATTR(pinstate, show_pinstate, NULL);


static struct attribute *gt917d_attribute_list[] = {
	&touch_attr_reg_ctrl.attr,
	&touch_attr_reset_ctrl.attr,
	&touch_attr_pinstate.attr,
	NULL,
};

static const struct attribute_group gt917d_attribute_group = {
    .attrs = gt917d_attribute_list,
};

static int gt917d_register_sysfs(struct device *dev)
{
    struct touch_core_data *ts = to_touch_core(dev);
    int ret = 0;
    TOUCH_TRACE();

    ret = sysfs_create_group(&ts->kobj, &gt917d_attribute_group);
    if (ret) {
        TOUCH_E("ili9881h sysfs register failed\n");
        goto error;
    }

	ret = gt917d_prd_register_sysfs(dev);
	if (ret) {
		TOUCH_E("ili9881h register failed\n");
		goto error;
	}

    return 0;

error:
    kobject_del(&ts->kobj);

    return ret;
}

static int gt917d_get_cmd_version(struct device *dev, char *buf)
{
	struct gt917d_data *d = to_gt917d_data(dev);
	int offset = 0;

    TOUCH_TRACE();

	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"==================== Version Info ====================\n");
	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Product-id : [GT%s]\n",
			d->fw_info.pid);
	offset += snprintf(buf + offset, PAGE_SIZE - offset, "IC firmware version: v%d.%02d\n",
			d->fw_info.ic_major_ver, d->fw_info.ic_minor_ver);
	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Sensor ID : %d\n",
			d->fw_info.sensor_id);
	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Config Version: v%d\n",
			d->config.data[GT917D_ADDR_LENGTH]);

    offset += snprintf(buf + offset, PAGE_SIZE - offset,
            "======================================================\n");

    return offset;
}

static int gt917d_get_cmd_atcmd_version(struct device *dev, char *buf)
{
	struct gt917d_data *d = to_gt917d_data(dev);
	int offset = 0;

    TOUCH_TRACE();

	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"IC firmware version: v%d.%02d\n",
			d->fw_info.ic_major_ver, d->fw_info.ic_minor_ver);

    return offset;
}

static int gt917d_init_pm(struct device *dev)
{
    TOUCH_TRACE();
    return 0;
}

static int gt917d_set(struct device *dev, u32 cmd, void *input, void *output)
{
    TOUCH_TRACE();

    return 0;
}

static int gt917d_get(struct device *dev, u32 cmd, void *input, void *output)
{
    int ret = 0;

	TOUCH_I("%s : cmd %d\n", __func__, cmd);

	switch (cmd) {
	case CMD_VERSION:
		ret = gt917d_get_cmd_version(dev, (char *)output);
		break;

	case CMD_ATCMD_VERSION:
		ret = gt917d_get_cmd_atcmd_version(dev, (char *)output);
		break;

        default:
            break;
    }

    return ret;
}

static int gt917d_esd_recovery(struct device *dev)
{
    TOUCH_TRACE();

	return 0;
}

static struct touch_driver touch_driver = {
	.probe = gt917d_probe,
	.remove = gt917d_remove,
	.suspend = gt917d_suspend,
	.shutdown = gt917d_shutdown,
	.resume = gt917d_resume,
	.init = gt917d_init,
	.irq_handler = gt917d_irq_handler,
	.power = gt917d_power,
	.upgrade = gt917d_upgrade,
	.esd_recovery = gt917d_esd_recovery,
	.lpwg = gt917d_lpwg,
	.swipe_enable = gt917d_swipe_enable,
	.notify = gt917d_notify,
	.init_pm = gt917d_init_pm,
	.register_sysfs = gt917d_register_sysfs,
	.set = gt917d_set,
	.get = gt917d_get,
};

#define MATCH_NAME			"goodix,gt917d"

static const struct of_device_id touch_match_ids[] = {
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
	TOUCH_TRACE();

	TOUCH_I("%s func\n", __func__);

	if(is_lcm_name("SKI-JD9365DA") || is_lcm_name("LCE-ST7703") || is_lcm_name("LCE-ILI9881C"))
	{
		TOUCH_I("%s, gt917d found!!lcm_name = %s\n",__func__,lge_get_lcm_name());
		return touch_bus_device_init(&hwif, &touch_driver);
	}

	TOUCH_I("%s, gt917d not found!!\n",__func__);

	return 0;
}

static void __exit touch_device_exit(void)
{
    TOUCH_TRACE();
    touch_bus_device_exit(&hwif);
}

module_init(touch_device_init);
module_exit(touch_device_exit);

MODULE_AUTHOR("BSP-TOUCH@lge.com");
MODULE_DESCRIPTION("LGE touch driver v5");
MODULE_LICENSE("GPL");

/* touch_gt917d_tool.c
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

#include "touch_gt917d.h"

#define DATA_LENGTH_UINT	512
#define CMD_HEAD_LENGTH		(sizeof(struct st_cmd_head) - sizeof(u8 *))
static char procname[20] = {0};

#pragma pack(1)
struct st_cmd_head {
	u8	wr;		/*write read flag 0:R 1:W 2:PID 3:*/
	u8	flag;		/*0:no need flag/int 1: need flag 2:need int*/
	u8	flag_addr[2];	/*flag address*/
	u8	flag_val;	/*flag val*/
	u8	flag_relation;	/*flag_val:flag 0:not equal 1:equal 2:> 3:<*/
	u16	circle;		/*polling cycle*/
	u8	times;		/*plling times*/
	u8	retry;		/*I2C retry times*/
	u16	delay;		/*delay before read or after write*/
	u16	data_len;	/*data length*/
	u8	addr_len;	/*address length*/
	u8	addr[2];	/*address*/
	u8	res[3];		/*reserved*/
	u8	*data; };	/*data pointer*/
#pragma pack()
static struct st_cmd_head cmd_head;

static struct i2c_client *gt_client;
static struct proc_dir_entry *gt917d_proc_entry;

static ssize_t gt917d_tool_read(struct file *file, char __user *page, size_t size, loff_t *ppos);
static ssize_t gt917d_tool_write(struct file *filp, const char __user *buff, size_t len, loff_t *off);
static const struct file_operations gt917d_proc_ops = {
	.owner = THIS_MODULE,
	.read = gt917d_tool_read,
	.write = gt917d_tool_write,
};

static s32 (*tool_i2c_read)(u8 *buf, u16 len);
static s32 (*tool_i2c_write)(u8 *buf, u16 len);

static s32 DATA_LENGTH;
static s8 IC_TYPE[16] = "GT9XX";

static void tool_set_proc_name(char *procname)
{
	TOUCH_TRACE();

	snprintf(procname, 20, "gmnode");
}

static s32 tool_i2c_read_no_extra(u8 *buf, u16 len)
{
	s32 ret = -1;
	s32 i = 0;

	TOUCH_TRACE();

	for (i = 0; i < cmd_head.retry; i++) {
		ret = gt917d_i2c_read(gt_client, buf, len + GT917D_ADDR_LENGTH);
		if (ret == 0)
			break;
	}
	return ret;
}

static s32 tool_i2c_write_no_extra(u8 *buf, u16 len)
{
	s32 ret = -1;
	s32 i = 0;

	TOUCH_TRACE();

	for (i = 0; i < cmd_head.retry; i++) {
		ret = gt917d_i2c_write(gt_client, buf, len);
		if (ret == 0)
			break;
	}

	return ret;
}

static s32 tool_i2c_read_with_extra(u8 *buf, u16 len)
{
	s32 ret = -1;
	u8 pre[2] = {0x0f, 0xff};
	u8 end[2] = {0x80, 0x00};

	TOUCH_TRACE();

	tool_i2c_write_no_extra(pre, 2);
	ret = tool_i2c_read_no_extra(buf, len);
	tool_i2c_write_no_extra(end, 2);

	return ret;
}

static s32 tool_i2c_write_with_extra(u8 *buf, u16 len)
{
	s32 ret = -1;
	u8 pre[2] = {0x0f, 0xff};
	u8 end[2] = {0x80, 0x00};

	TOUCH_TRACE();

	tool_i2c_write_no_extra(pre, 2);
	ret = tool_i2c_write_no_extra(buf, len);
	tool_i2c_write_no_extra(end, 2);

	return ret;
}

static void register_i2c_func(void)
{
	TOUCH_TRACE();

	if (strncmp(IC_TYPE, "GT8110", 6) &&
			strncmp(IC_TYPE, "GT8105", 6) &&
			strncmp(IC_TYPE, "GT801", 5) &&
			strncmp(IC_TYPE, "GT800", 5) &&
			strncmp(IC_TYPE, "GT801PLUS", 9) &&
			strncmp(IC_TYPE, "GT811", 5) &&
			strncmp(IC_TYPE, "GTxxx", 5) &&
			strncmp(IC_TYPE, "GT9XX", 5)) {
		tool_i2c_read = tool_i2c_read_with_extra;
		tool_i2c_write = tool_i2c_write_with_extra;
		TOUCH_I("I2C function: with pre and end cmd!\n");
	} else {
		tool_i2c_read = tool_i2c_read_no_extra;
		tool_i2c_write = tool_i2c_write_no_extra;
		TOUCH_I("I2C function: without pre and end cmd!\n");
	}
}

static void unregister_i2c_func(void)
{
	TOUCH_TRACE();

	tool_i2c_read = NULL;
	tool_i2c_write = NULL;
	dev_info(&gt_client->dev, "I2C function: unregister i2c transfer function!");
}

s32 init_wr_node(struct i2c_client *client)
{
	s32 i = 0;

	TOUCH_TRACE();

	gt_client = client;
	memset(&cmd_head, 0, sizeof(cmd_head));
	cmd_head.data = NULL;

	i = 6;
	while ((!cmd_head.data) && i) {
		cmd_head.data = kzalloc(i * DATA_LENGTH_UINT, GFP_KERNEL);
		if (cmd_head.data)
			break;
		i--;
	}
	if (i) {
		DATA_LENGTH = i * DATA_LENGTH_UINT - GT917D_ADDR_LENGTH;
		dev_info(&gt_client->dev, "Alloc memory size:%d.", DATA_LENGTH);
	} else {
		dev_info(&gt_client->dev, "Apply for memory failed.");
		return FAIL;
	}

	cmd_head.addr_len = 2;
	cmd_head.retry = 5;

	register_i2c_func();

	tool_set_proc_name(procname);
	gt917d_proc_entry = proc_create(procname, 0666, NULL, &gt917d_proc_ops);
	if (!gt917d_proc_entry) {
		dev_info(&gt_client->dev, "Couldn't create proc entry!");
		return FAIL;
	}

	dev_info(&gt_client->dev, "Create proc entry success!");
	return SUCCESS;
}

void uninit_wr_node(void)
{
	kfree(cmd_head.data);
	cmd_head.data = NULL;
	unregister_i2c_func();
	remove_proc_entry(procname, NULL);
}

static u8 relation(u8 src, u8 dst, u8 rlt)
{
	u8 ret = 0;

	TOUCH_TRACE();

	switch (rlt) {
	case 0:
		ret = (src != dst) ? true : false;
		break;
	case 1:
		ret = (src == dst) ? true : false;
		TOUCH_I("equal:src:0x%02x  dst:0x%02x  ret:%d.\n", src, dst, (s32)ret);
		break;
	case 2:
		ret = (src > dst) ? true : false;
		break;
	case 3:
		ret = (src < dst) ? true : false;
		break;
	case 4:
		ret = (src & dst) ? true : false;
		break;
	case 5:
		ret = (!(src | dst)) ? true : false;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static u8 comfirm(void)
{
	s32 i = 0;
	u8 buf[32] = {0, };

	TOUCH_TRACE();

	memcpy(buf, cmd_head.flag_addr, cmd_head.addr_len);

	for (i = 0; i < cmd_head.times; i++) {
		if (tool_i2c_read(buf, 1) < 0) {
			dev_info(&gt_client->dev, "Read flag data failed!");
			return FAIL;
		}

		if (true == relation(buf[GT917D_ADDR_LENGTH], cmd_head.flag_val, cmd_head.flag_relation)) {
			TOUCH_I("value at flag addr:0x%02x.\n", buf[GT917D_ADDR_LENGTH]);
			TOUCH_I("flag value:0x%02x.\n", cmd_head.flag_val);
			break;
		}

		msleep(cmd_head.circle);
	}

	if (i >= cmd_head.times) {
		dev_info(&gt_client->dev, "Can't get the continue flag!");
		return FAIL;
	}

	return SUCCESS;
}

static ssize_t gt917d_tool_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	s32 ret = 0;
	struct gt917d_data *d = to_gt917d_data(&gt_client->dev);

	TOUCH_TRACE();

	ret = copy_from_user(&cmd_head, buff, CMD_HEAD_LENGTH);
	if (ret) {
		dev_info(&gt_client->dev, "copy_from_user failed.");
		return -EPERM;
	}

	TOUCH_I("[Operation]wr: %02X\n", cmd_head.wr);
	TOUCH_I("[Flag]flag: %02X, addr: %02X%02X, value: %02X, relation: %02X\n",
			cmd_head.flag, cmd_head.flag_addr[0],
			cmd_head.flag_addr[1], cmd_head.flag_val,
			cmd_head.flag_relation);
	TOUCH_I("[Retry]circle: %d, times: %d, retry: %d, delay: %d\n",
			(s32)cmd_head.circle,
			(s32)cmd_head.times, (s32)cmd_head.retry,
			(s32)cmd_head.delay);

	if (cmd_head.wr == 1) {
		if (cmd_head.data_len > DATA_LENGTH) {
			dev_info(&gt_client->dev, "Tool write failed data too long");
			return -EPERM;
		}
		ret = copy_from_user(&cmd_head.data[GT917D_ADDR_LENGTH],
				&buff[CMD_HEAD_LENGTH],
				cmd_head.data_len);
		if (ret) {
			dev_info(&gt_client->dev, "copy_from_user failed.");
			return -EPERM;
		}
		memcpy(&cmd_head.data[GT917D_ADDR_LENGTH - cmd_head.addr_len],
				cmd_head.addr, cmd_head.addr_len);

		if (cmd_head.flag == 1) {
			if (comfirm() == FAIL) {
				dev_info(&gt_client->dev, "[WRITE]Comfirm fail!");
				return -EPERM;
			}
		} else if (cmd_head.flag == 2) {
			/*Need interrupt!*/
		}
		if (tool_i2c_write(&cmd_head.data[GT917D_ADDR_LENGTH -
					cmd_head.addr_len], cmd_head.data_len +
					cmd_head.addr_len) < 0) {
			dev_info(&gt_client->dev, "[WRITE]Write data failed!");
			return -EPERM;
		}

		if (cmd_head.delay)
			msleep(cmd_head.delay);
	} else if (cmd_head.wr == 3) {
		if (cmd_head.data_len > DATA_LENGTH) {
			dev_info(&gt_client->dev, "Tool write failed data too long");
			return -EPERM;
		}
		ret = copy_from_user(&cmd_head.data[0], &buff[CMD_HEAD_LENGTH],
				cmd_head.data_len);
		if (ret) {
			dev_info(&gt_client->dev, "copy_from_user failed.");
			return -EPERM;
		}
		memcpy(IC_TYPE, cmd_head.data, cmd_head.data_len);

		register_i2c_func();
	} else if (cmd_head.wr == 7) {/*disable irq!*/
		touch_interrupt_control(&gt_client->dev, INTERRUPT_DISABLE);
	} else if (cmd_head.wr == 9) {/*enable irq!*/
		touch_interrupt_control(&gt_client->dev, INTERRUPT_ENABLE);
	} else if (cmd_head.wr == 17) {
		if (cmd_head.data_len > DATA_LENGTH) {
			dev_info(&gt_client->dev, "Tool write failed data too long");
			return -EPERM;
		}
		ret = copy_from_user(&cmd_head.data[GT917D_ADDR_LENGTH],
				&buff[CMD_HEAD_LENGTH],
				cmd_head.data_len);
		if (ret) {
			TOUCH_I("copy_from_user failed.\n");
			return -EPERM;
		}
		if (cmd_head.data[GT917D_ADDR_LENGTH]) {
			dev_info(&gt_client->dev, "gt917d enter rawdiff.");
			set_bit(RAW_DATA_MODE, &d->flags);
		} else {
			clear_bit(RAW_DATA_MODE, &d->flags);
			dev_info(&gt_client->dev, "gt917d leave rawdiff.");
		}
	} else if (cmd_head.wr == 19) {
		gt917d_hw_reset(&gt_client->dev);
	} else if (cmd_head.wr == 11) {/*Enter update mode!*/
		if (gt917d_enter_update_mode(&gt_client->dev) == FAIL)
			return -EPERM;
	} else if (cmd_head.wr == 13) {/*Leave update mode!*/
		gt917d_hw_reset(&gt_client->dev);
	} else if (cmd_head.wr == 15) {/*Update firmware!*/
		show_len = 0;
		total_len = 0;
		if (cmd_head.data_len > DATA_LENGTH) {
			dev_info(&gt_client->dev,
					"Tool write failed data too long");
			return -EPERM;
		}
		memset(cmd_head.data, 0, DATA_LENGTH);
		ret = copy_from_user(cmd_head.data,
				&buff[CMD_HEAD_LENGTH],
				cmd_head.data_len);
		if (ret) {
			TOUCH_I("copy_from_user failed.\n");
			return -EPERM;
		}

		if (gt917d_update_proc(&gt_client->dev, (void *)cmd_head.data) == FAIL)
			return -EPERM;
	}

	return len;
}

static ssize_t gt917d_tool_read(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	s32 ret = 0;

	TOUCH_TRACE();

	if (*ppos) {
		/* ADB call again
		   TOUCH_I( "[HEAD]wr: %d\n", cmd_head.wr);
		   TOUCH_I( "[PARAM]size: %d, *ppos: %d\n", size, (int)*ppos);
		   TOUCH_I( "[TOOL_READ]ADB call again, return it.\n");
		 */
		*ppos = 0;

		return 0;
	}

	if (cmd_head.wr % 2) {
		return -EPERM;
	} else if (!cmd_head.wr) {
		u16 len, data_len, loc, addr;

		if (cmd_head.flag == 1) {
			if (comfirm() == FAIL) {
				dev_info(&gt_client->dev, "[READ]Comfirm fail!");
				return -EPERM;
			}
		} else if (cmd_head.flag == 2) {
			/*Need interrupt!*/
		}

		if (cmd_head.delay)
			msleep(cmd_head.delay);

		data_len = cmd_head.data_len;
		addr = (cmd_head.addr[0] << 8) + cmd_head.addr[1];
		loc = 0;

		while (data_len > 0) {
			len = data_len > DATA_LENGTH ? DATA_LENGTH : data_len;
			cmd_head.data[0] = (addr >> 8) & 0xFF;
			cmd_head.data[1] = (addr & 0xFF);
			if (tool_i2c_read(cmd_head.data, len) < 0) {
				dev_info(&gt_client->dev, "[READ]Read data failed!");
				return -EPERM;
			}
			ret = simple_read_from_buffer(&page[loc], size, ppos,
					&cmd_head.data[GT917D_ADDR_LENGTH], len);
			if (ret < 0)
				return ret;
			loc += len;
			addr += len;
			data_len -= len;
		}
		return cmd_head.data_len;
	} else if (cmd_head.wr == 2) {
		ret = simple_read_from_buffer(page, size, ppos,
				IC_TYPE, sizeof(IC_TYPE));
		return ret;
	} else if (cmd_head.wr == 4) {
		u8 progress_buf[4];

		progress_buf[0] = show_len >> 8;
		progress_buf[1] = show_len & 0xff;
		progress_buf[2] = total_len >> 8;
		progress_buf[3] = total_len & 0xff;

		ret = simple_read_from_buffer(page, size, ppos,
				progress_buf, 4);
		return ret;
	} else if (cmd_head.wr == 6) {
		/*Read error code!*/
	} else if (cmd_head.wr == 8) {	/*Read driver version*/
		ret = simple_read_from_buffer(page, size, ppos,
				GT917D_DRIVER_VERSION,
				strlen(GT917D_DRIVER_VERSION));
		return ret;
	}

	return -EPERM;
}

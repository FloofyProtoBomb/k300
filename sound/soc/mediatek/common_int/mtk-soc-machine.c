/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 *
 * Filename:
 * ---------
 *   mt_soc_machine.c
 *
 * Project:
 * --------
 *   Audio soc machine driver
 *
 * Description:
 * ------------
 *   Audio machine driver
 *
 * Author:
 * -------
 * Chipeng Chang
 *
 *------------------------------------------------------------------------------
 *
 ******************************************************************************
 */

/*****************************************************************************
 *                     C O M P I L E R   F L A G S
 *****************************************************************************/

/*****************************************************************************
 *                E X T E R N A L   R E F E R E N C E S
 *****************************************************************************/
#include "mtk-auddrv-afe.h"
#include "mtk-auddrv-ana.h"
#include "mtk-auddrv-clk.h"
#include "mtk-auddrv-common.h"
#include "mtk-auddrv-def.h"
#include "mtk-auddrv-kernel.h"
#include "mtk-soc-afe-control.h"

#include <asm/div64.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <stdarg.h>

#include "mtk-soc-codec-63xx.h"
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "mtk-soc-speaker-amp.h"

#include "mtk-hw-component.h"
#if defined(CONFIG_SND_SOC_CS43130)
#include "mtk-cs43130-machine-ops.h"
#endif
#if defined(CONFIG_SND_SOC_CS35L35)
#include "mtk-cs35l35-machine-ops.h"
#endif

#if defined(CONFIG_LGE_HIFI_HWINFO)
#include <soc/mediatek/lge/board_lge.h>
#endif

#ifdef CONFIG_LGE_HP_POP_CANCEL_SWITCH_USE_LDO
#include    <linux/regulator/consumer.h>
static struct regulator *ldo_vout;
#endif

static struct dentry *mt_sco_audio_debugfs;
#define DEBUG_FS_NAME "mtksocaudio"
#define DEBUG_ANA_FS_NAME "mtksocanaaudio"

static int mt_soc_ana_debug_open(struct inode *inode, struct file *file)
{
	pr_debug("mt_soc_ana_debug_open\n");
	return 0;
}

static ssize_t mt_soc_ana_debug_read(struct file *file, char __user *buf,
				     size_t count, loff_t *pos)
{
	const int size = 8192;
	/* char buffer[size]; */
	char *buffer = NULL; /* for reduce kernel stack */
	int n = 0;
	int ret = 0;

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer) {
		kfree(buffer);
		return -ENOMEM;
	}

	AudDrv_Clk_On();
	audckbufEnable(true);

	n = Ana_Debug_Read(buffer, size);

	pr_debug("mt_soc_ana_debug_read len = %d\n", n);

	audckbufEnable(false);
	AudDrv_Clk_Off();

	ret = simple_read_from_buffer(buf, count, pos, buffer, n);
	kfree(buffer);
	return ret;
}

static int mt_soc_debug_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t mt_soc_debug_read(struct file *file, char __user *buf,
				 size_t count, loff_t *pos)
{
	const int size = 12288;
	/* char buffer[size]; */
	char *buffer = NULL; /* for reduce kernel stack */
	int n = 0;
	int ret = 0;

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer) {
		kfree(buffer);
		return -ENOMEM;
	}

	AudDrv_Clk_On();

	n = AudDrv_Reg_Dump(buffer, size);
	pr_debug("mt_soc_debug_read len = %d\n", n);

	AudDrv_Clk_Off();

	ret = simple_read_from_buffer(buf, count, pos, buffer, n);
	kfree(buffer);
	return ret;
}

static char const ParSetkeyAfe[] = "Setafereg";
static char const ParSetkeyAna[] = "Setanareg";
static char const PareGetkeyAfe[] = "Getafereg";
static char const PareGetkeyAna[] = "Getanareg";

static ssize_t mt_soc_debug_write(struct file *f, const char __user *buf,
				  size_t count, loff_t *offset)
{
#define MAX_DEBUG_WRITE_INPUT 256
	int ret = 0;
	char InputBuf[MAX_DEBUG_WRITE_INPUT];
	char *token1 = NULL;
	char *token2 = NULL;
	char *token3 = NULL;
	char *token4 = NULL;
	char *token5 = NULL;
	char *temp = NULL;
	char *str_begin = NULL;

	unsigned long regaddr = 0;
	unsigned long regvalue = 0;
	char delim[] = " ,";

	if (!count) {
		pr_debug("%s(), count is 0, return directly\n", __func__);
		goto exit;
	}

	if (count > MAX_DEBUG_WRITE_INPUT)
		count = MAX_DEBUG_WRITE_INPUT;

	memset_io((void *)InputBuf, 0, MAX_DEBUG_WRITE_INPUT);

	if (copy_from_user((InputBuf), buf, count)) {
		pr_debug("%s(), copy_from_user fail, mt_soc_debug_write count = %zu\n",
			 __func__, count);
		goto exit;
	}

	str_begin = kstrndup(InputBuf, MAX_DEBUG_WRITE_INPUT - 1,
			     GFP_KERNEL);
	if (!str_begin) {
		pr_warn("%s(), kstrdup fail\n", __func__);
		goto exit;
	}
	temp = str_begin;

	pr_debug(
		"copy_from_user mt_soc_debug_write count = %zu, temp = %s, pointer = %p\n",
		count, str_begin, str_begin);
	token1 = strsep(&temp, delim);
	token2 = strsep(&temp, delim);
	token3 = strsep(&temp, delim);
	token4 = strsep(&temp, delim);
	token5 = strsep(&temp, delim);
	pr_debug("token1 = %s token2 = %s token3 = %s token4 = %s token5 = %s\n",
		token1, token2, token3, token4, token5);

	AudDrv_Clk_On();
	if (strcmp(token1, ParSetkeyAfe) == 0) {
		if ((token3 != NULL) && (token5 != NULL)) {
			ret = kstrtoul(token3, 16, &regaddr);
			ret = kstrtoul(token5, 16, &regvalue);
			pr_debug("%s, regaddr = 0x%x, regvalue = 0x%x\n",
				 ParSetkeyAfe, (unsigned int)regaddr,
				 (unsigned int)regvalue);
			Afe_Set_Reg(regaddr, regvalue, 0xffffffff);
			regvalue = Afe_Get_Reg(regaddr);
			pr_debug("%s, regaddr = 0x%x, regvalue = 0x%x\n",
				 ParSetkeyAfe, (unsigned int)regaddr,
				 (unsigned int)regvalue);
		} else {
			pr_debug("token3 or token5 is NULL!\n");
		}
	}

	if (strcmp(token1, ParSetkeyAna) == 0) {
		if ((token3 != NULL) && (token5 != NULL)) {
			ret = kstrtoul(token3, 16, &regaddr);
			ret = kstrtoul(token5, 16, &regvalue);
			pr_debug("%s, regaddr = 0x%x, regvalue = 0x%x\n",
				 ParSetkeyAna, (unsigned int)regaddr,
				 (unsigned int)regvalue);
			audckbufEnable(true);
			Ana_Set_Reg(regaddr, regvalue, 0xffffffff);
			regvalue = Ana_Get_Reg(regaddr);
			audckbufEnable(false);
			pr_debug("%s, regaddr = 0x%x, regvalue = 0x%x\n",
				 ParSetkeyAna, (unsigned int)regaddr,
				 (unsigned int)regvalue);
		} else {
			pr_debug("token3 or token5 is NULL!\n");
		}
	}

	if (strcmp(token1, PareGetkeyAfe) == 0) {
		if (token3 != NULL) {
			ret = kstrtoul(token3, 16, &regaddr);
			regvalue = Afe_Get_Reg(regaddr);
			pr_debug("%s, regaddr = 0x%x, regvalue = 0x%x\n",
				 PareGetkeyAfe, (unsigned int)regaddr,
				 (unsigned int)regvalue);
		} else {
			pr_debug("token3 is NULL!\n");
		}
	}

	if (strcmp(token1, PareGetkeyAna) == 0) {
		if (token3 != NULL) {
			ret = kstrtoul(token3, 16, &regaddr);
			regvalue = Ana_Get_Reg(regaddr);
			pr_debug("%s, regaddr = 0x%x, regvalue = 0x%x\n",
				 PareGetkeyAna, (unsigned int)regaddr,
				 (unsigned int)regvalue);
		} else {
			pr_debug("token3 is NULL!\n");
		}
	}
	AudDrv_Clk_Off();

	kfree(str_begin);
exit:
	return count;
}

static const struct file_operations mtaudio_debug_ops = {
	.open = mt_soc_debug_open,
	.read = mt_soc_debug_read,
	.write = mt_soc_debug_write,
};

static const struct file_operations mtaudio_ana_debug_ops = {
	.open = mt_soc_ana_debug_open, .read = mt_soc_ana_debug_read,
};

/* snd_soc_ops */
static int mt_machine_trigger(struct snd_pcm_substream *substream, int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		EnableAfe(true);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		EnableAfe(false);
		return 0;
	}
	return -EINVAL;
}

#ifdef CONFIG_SND_SOC_CS35L41
#include <linux/clk.h>
#define CLK_SRC_SCLK 0
#define CLK_SRC_LRCLK 1
#define CLK_SRC_PDM 2
#define CLK_SRC_SELF 3
#define CLK_SRC_MCLK 4
#define CLK_SRC_SWIRE 5
#define CLK_SRC_DAI 0
#define CLK_SRC_CODEC 1

static unsigned int codec_clk_src = CLK_SRC_MCLK;
static const char *const codec_src_clocks[] = {"SCLK", "LRCLK", "PDM",
						"MCLK", "SELF", "SWIRE"};

static unsigned int dai_clks = SND_SOC_DAIFMT_CBS_CFS;
static const char *const dai_sub_clocks[] = {"Codec Slave", "Codec Master",
					"CODEC BMFS", "CODEC BSFM"
};

static unsigned int dai_bit_fmt = SND_SOC_DAIFMT_NB_NF;
static const char *const dai_bit_config[] = {"NormalBF", "NormalB INVF",
					"INVB NormalF", "INVB INVF"
};

static unsigned int dai_mode_fmt = SND_SOC_DAIFMT_I2S;
static const char *const dai_mode_config[] = {"I2S", "Right J",
					"Left J", "DSP A", "DSP B",
					"PDM"
};

#ifdef CONFIG_SND_SOC_CS35L41_LGE
static unsigned int sys_clk_static = 3072000;
static const char *const static_clk_mode[] = {"Off", "5P6", "6P1", "11P2",
			"12", "12P2", "13", "22P5", "24", "24P5", "26", "30P72", "20P48"
};
#else
static unsigned int sys_clk_static = 0;
static const char *const static_clk_mode[] = {"Off", "5P6", "6P1", "11P2",
			"12", "12P2", "13", "22P5", "24", "24P5", "26"
};
#endif

unsigned int dai_force_frame32 = 0;
static const char *const dai_force_frame32_config[] = {"Off", "On"};

/* Cirrus Zynq Machine Driver Codes */
/*
struct cirrus_zynq_data {
	bool need_sclk;
	struct snd_soc_card snd_card;
	struct cirrus_zynq_dai_props {
		struct cirrus_zynq_dai cpu_dai;
		struct cirrus_zynq_dai codec_dai;
	} *dai_props;
	struct snd_soc_dai_link dai_link[];
};
*/

static int cs35l41_codec_clk_src_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int codec_clk_src_val = 0;

	switch (codec_clk_src) {
	case CLK_SRC_SCLK:
		codec_clk_src_val = 0;
		break;
	case CLK_SRC_LRCLK:
		codec_clk_src_val = 1;
		break;
	case CLK_SRC_PDM:
		codec_clk_src_val = 2;
		break;
	case CLK_SRC_MCLK:
		codec_clk_src_val = 3;
		break;
	case CLK_SRC_SELF:
		codec_clk_src_val = 4;
		break;
	case CLK_SRC_SWIRE:
		codec_clk_src_val = 5;
		break;
	}

	ucontrol->value.integer.value[0] = codec_clk_src_val;

	return 0;
}

static int cs35l41_codec_clk_src_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_info("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		codec_clk_src = CLK_SRC_SCLK;
		break;
	case 1:
		codec_clk_src = CLK_SRC_LRCLK;
		break;
	case 2:
		codec_clk_src = CLK_SRC_PDM;
		break;
	case 3:
		codec_clk_src = CLK_SRC_MCLK;
		break;
	case 4:
		codec_clk_src = CLK_SRC_SELF;
		break;
	case 5:
		codec_clk_src = CLK_SRC_SWIRE;
		break;
	}

	return 0;
}

static int cs35l41_codec_dai_clks_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int dai_clks_val = 0;

	switch (dai_clks) {
	case SND_SOC_DAIFMT_CBS_CFS:
		dai_clks_val = 0;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		dai_clks_val = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		dai_clks_val = 2;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		dai_clks_val = 3;
		break;
	}

	ucontrol->value.integer.value[0] = dai_clks_val;

	return 0;
}

static int cs35l41_codec_dai_clks_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_info("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		dai_clks = SND_SOC_DAIFMT_CBS_CFS;
		break;
	case 1:
		dai_clks = SND_SOC_DAIFMT_CBM_CFM;
		break;
	case 2:
		dai_clks = SND_SOC_DAIFMT_CBM_CFS;
		break;
	case 3:
		dai_clks = SND_SOC_DAIFMT_CBS_CFM;
		break;
	}

	return 0;
}

static int cs35l41_codec_dai_bitfmt_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int dai_bits_val = 0;

	switch (dai_bit_fmt) {
	case SND_SOC_DAIFMT_NB_NF:
		dai_bits_val = 0;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		dai_bits_val = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		dai_bits_val = 2;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		dai_bits_val = 3;
		break;
	}

	ucontrol->value.integer.value[0] = dai_bits_val;

	return 0;
}

static int cs35l41_codec_dai_bitfmt_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_info("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		dai_bit_fmt = SND_SOC_DAIFMT_NB_NF;
		break;
	case 1:
		dai_bit_fmt = SND_SOC_DAIFMT_NB_IF;
		break;
	case 2:
		dai_bit_fmt = SND_SOC_DAIFMT_IB_NF;
		break;
	case 3:
		dai_bit_fmt = SND_SOC_DAIFMT_IB_IF;
		break;
	}

	return 0;
}

static int cs35l41_codec_dai_mode_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int dai_mode_val = 0;

	switch (dai_mode_fmt) {
	case SND_SOC_DAIFMT_I2S:
		dai_mode_val = 0;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		dai_mode_val = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dai_mode_val = 2;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		dai_mode_val = 3;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		dai_mode_val = 4;
		break;
	case SND_SOC_DAIFMT_PDM:
		dai_mode_val = 5;
		break;
	}

	ucontrol->value.integer.value[0] = dai_mode_val;

	return 0;
}

static int cs35l41_codec_dai_mode_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_info("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		dai_mode_fmt = SND_SOC_DAIFMT_I2S;
		break;
	case 1:
		dai_mode_fmt = SND_SOC_DAIFMT_RIGHT_J;
		break;
	case 2:
		dai_mode_fmt = SND_SOC_DAIFMT_LEFT_J;
		break;
	case 3:
		dai_mode_fmt = SND_SOC_DAIFMT_DSP_A;
		break;
	case 4:
		dai_mode_fmt = SND_SOC_DAIFMT_DSP_B;
		break;
	case 5:
		dai_mode_fmt = SND_SOC_DAIFMT_PDM;
		break;
	}

	return 0;
}

static int cs35l41_codec_static_clk_mode_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	int static_mode_val = 0;

	switch (sys_clk_static) {
	case 0:
		static_mode_val = 0;
		break;
	case 5644800:
		static_mode_val = 1;
		break;
	case 6144000:
		static_mode_val = 2;
		break;
	case 11289600:
		static_mode_val = 3;
		break;
	case 12000000:
		static_mode_val = 4;
		break;
	case 12288000:
		static_mode_val = 5;
		break;
	case 13000000:
		static_mode_val = 6;
		break;
	case 22579200:
		static_mode_val = 7;
		break;
	case 24000000:
		static_mode_val = 8;
		break;
	case 24576000:
		static_mode_val = 9;
		break;
	case 26000000:
		static_mode_val = 10;
		break;
#ifdef CONFIG_SND_SOC_CS35L41_LGE
	case 3072000:
		static_mode_val = 11;
		break;
	case 2048000:
		static_mode_val = 12;
		break;
	default :
		pr_info("%s wrong value is set...\n", __func__);
		break;
#endif
	}

	ucontrol->value.integer.value[0] = static_mode_val;

	return 0;
}

static int cs35l41_codec_static_clk_mode_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		sys_clk_static = 0;
		break;
	case 1:
		sys_clk_static = 5644800;
		break;
	case 2:
		sys_clk_static = 6144000;
		break;
	case 3:
		sys_clk_static = 11289600;
		break;
	case 4:
		sys_clk_static = 12000000;
		break;
	case 5:
		sys_clk_static = 12288000;
		break;
	case 6:
		sys_clk_static = 13000000;
		break;
	case 7:
		sys_clk_static = 22579200;
		break;
	case 8:
		sys_clk_static = 24000000;
		break;
	case 9:
		sys_clk_static = 24576000;
		break;
	case 10:
		sys_clk_static = 26000000;
		break;
#ifdef CONFIG_SND_SOC_CS35L41_LGE
	case 11:
		sys_clk_static = 3072000;
		break;
	case 12:
		sys_clk_static = 2048000;
		break;
	default :
		pr_info("%s wrong value is set...\n", __func__);
		break;
#endif
	}

	return 0;
}

static int cs35l41_codec_dai_force_frame32_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int dai_force_frame32_val = 0;

	switch (dai_force_frame32) {
	case 0:
		dai_force_frame32_val = 0;
		break;
	case 1:
		dai_force_frame32_val = 1;
		break;
	}

	ucontrol->value.integer.value[0] = dai_force_frame32_val;

	return 0;
}

static int cs35l41_codec_dai_force_frame32_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		dai_force_frame32 = 0;
		break;
	case 1:
		dai_force_frame32 = 1;
		break;
	}

	return 0;
}

static const struct soc_enum cirrus_snd_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(dai_sub_clocks), dai_sub_clocks),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(dai_bit_config), dai_bit_config),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(dai_mode_config), dai_mode_config),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(static_clk_mode), static_clk_mode),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(codec_src_clocks), codec_src_clocks),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(dai_force_frame32_config), dai_force_frame32_config),
};
static int cs35l41_codec_startup(struct snd_pcm_substream *substream)
{
	/* Cirrus Zynq Machine Driver Codes */
	/*
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct cirrus_zynq_data *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct cirrus_zynq_dai_props *dai_props;
	int ret;
	unsigned int fmt;

	dai_props = &priv->dai_props[rtd->num];

	fmt = dai_mode_fmt | dai_bit_fmt |
			  dai_clks;

	snd_soc_runtime_set_dai_fmt(rtd, fmt);

	ret = clk_prepare_enable(dai_props->cpu_dai.clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dai_props->codec_dai.clk);
	if (ret)
		clk_disable_unprepare(dai_props->cpu_dai.clk);
	*/
	pr_info("%s \n", __func__);
	return 0;
}

static void cs35l41_codec_shutdown(struct snd_pcm_substream *substream)
{
	/* Cirrus Zynq Machine Driver Codes */
	/*
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct cirrus_zynq_data *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct cirrus_zynq_dai_props *dai_props;

	dai_props = &priv->dai_props[rtd->num];

	clk_disable_unprepare(dai_props->cpu_dai.clk);

	clk_disable_unprepare(dai_props->codec_dai.clk);
	*/
	pr_info("%s\n", __func__);
}

static int cs35l41_codec_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	unsigned int mclk, sclk, fs, clk = 0, swidth;
	unsigned int num_codecs = rtd->num_codecs;
	int ret = 0, i;

	pr_info("%s start\n", __func__);

	if (sys_clk_static) {
		mclk = sys_clk_static;
	} else {
		fs = params_rate(params);
		switch (fs) {
		//12.288 MHz friendly Fs values
		case 384000:
			mclk = 49152000;
			sclk = 12288000;
		break;
		case 192000:
			mclk = 24576000;
			sclk = 6144000;
		break;
		case 96000:
		case 48000:
		case 32000:
		case 24000:
		case 16000:
		case 12000:
		case  8000:
			mclk = 12288000;
			sclk = 3072000;
		break;
		//12.000 MHz friendly Fs values
		case 44118:
		case 22058:
		case 11029:
			mclk = 12000000;
			sclk = 2823000;
		break;
		// 11.2896 MHz friendly Fs values
		case 88200:
		case 44100:
		case 22050:
		case 11025:
			mclk = 11289600;
			sclk = 2822400;
		break;
		default:
			mclk = 11289600;
			sclk = 2822400;
		break; //44.100K
		}
	}

	swidth = params_width(params);
	fs = params_rate(params);
	switch (swidth) {
	// 16-bit I2S frames
	case 8:
	case 16:
		sclk = fs * 32;
		break;
	//32-bit I2S frames
	case 24:
	case 32:
		sclk = fs * 64;
		break;
	default:
		dev_err(codec_dais[0]->dev, "%s invalid params_width error\n", __func__);
		return -EINVAL;
	}

	switch (codec_clk_src) {
	case CLK_SRC_SCLK:
		clk = sclk;
		break;
	case CLK_SRC_LRCLK:
		clk = params_rate(params);
		break;
	case CLK_SRC_PDM:
		clk = sclk;
		break;
	case CLK_SRC_MCLK:
		clk = mclk;
		break;
	case CLK_SRC_SELF:
		clk = mclk;
		break;
	case CLK_SRC_SWIRE:
		break;
	}

#ifdef CONFIG_SND_SOC_CS35L41_LGE
	for (i = 0; i < num_codecs; i++) {
		if(sys_clk_static) {
			ret = snd_soc_codec_set_sysclk(codec_dais[i]->codec,
						0, 0, clk,
						SND_SOC_CLOCK_IN);
		} else {
			ret = snd_soc_codec_set_sysclk(codec_dais[i]->codec,
						codec_clk_src, 0, clk,
						SND_SOC_CLOCK_IN);
		}
	}
#else
	for (i = 0; i < num_codecs; i++)
		ret = snd_soc_codec_set_sysclk(codec_dais[i]->codec,
					codec_clk_src, 0, clk,
					SND_SOC_CLOCK_IN);
#endif

	pr_info("%s end\n", __func__);
	return ret;
}
#endif

static struct snd_soc_ops mt_machine_audio_ops = {
	.trigger = mt_machine_trigger,
#ifdef CONFIG_SND_SOC_CS35L41
	.startup = cs35l41_codec_startup,
	.shutdown = cs35l41_codec_shutdown,
	.hw_params = cs35l41_codec_hw_params,
#endif
};

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt_soc_dai_common[] = {
	/* FrontEnd DAI Links */
	{
		.name = "MultiMedia1",
		.stream_name = MT_SOC_DL1_STREAM_NAME,
		.cpu_dai_name = MT_SOC_DL1DAI_NAME,
		.platform_name = MT_SOC_DL1_PCM,
		.codec_dai_name = MT_SOC_CODEC_TXDAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "MultiMedia2",
		.stream_name = MT_SOC_UL1_STREAM_NAME,
		.cpu_dai_name = MT_SOC_UL1DAI_NAME,
		.platform_name = MT_SOC_UL1_PCM,
		.codec_dai_name = MT_SOC_CODEC_RXDAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "Voice_MD1",
		.stream_name = MT_SOC_VOICE_MD1_STREAM_NAME,
		.cpu_dai_name = MT_SOC_VOICE_MD1_NAME,
		.platform_name = MT_SOC_VOICE_MD1,
		.codec_dai_name = MT_SOC_CODEC_VOICE_MD1DAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
#ifdef CONFIG_MTK_HDMI_TDM
	{
		.name = "HDMI_OUT",
		.stream_name = MT_SOC_HDMI_STREAM_NAME,
		.cpu_dai_name = MT_SOC_HDMI_NAME,
		.platform_name = MT_SOC_HDMI_PCM,
		.codec_dai_name = MT_SOC_CODEC_HDMI_DUMMY_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
#endif
	{
		.name = "ULDLOOPBACK",
		.stream_name = MT_SOC_ULDLLOOPBACK_STREAM_NAME,
		.cpu_dai_name = MT_SOC_ULDLLOOPBACK_NAME,
		.platform_name = MT_SOC_ULDLLOOPBACK_PCM,
		.codec_dai_name = MT_SOC_CODEC_ULDLLOOPBACK_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "I2S0OUTPUT",
		.stream_name = MT_SOC_I2S0_STREAM_NAME,
		.cpu_dai_name = MT_SOC_I2S0_NAME,
		.platform_name = MT_SOC_I2S0_PCM,
		.codec_dai_name = MT_SOC_CODEC_I2S0_DUMMY_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "MRGRX",
		.stream_name = MT_SOC_MRGRX_STREAM_NAME,
		.cpu_dai_name = MT_SOC_MRGRX_NAME,
		.platform_name = MT_SOC_MRGRX_PCM,
		.codec_dai_name = MT_SOC_CODEC_MRGRX_DAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "MRGRXCAPTURE",
		.stream_name = MT_SOC_MRGRX_CAPTURE_STREAM_NAME,
		.cpu_dai_name = MT_SOC_MRGRX_NAME,
		.platform_name = MT_SOC_MRGRX_AWB_PCM,
		.codec_dai_name = MT_SOC_CODEC_MRGRX_DUMMY_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "I2S0DL1OUTPUT",
		.stream_name = MT_SOC_I2SDL1_STREAM_NAME,
		.cpu_dai_name = MT_SOC_I2S0DL1_NAME,
		.platform_name = MT_SOC_I2S0DL1_PCM,
		.codec_dai_name = MT_SOC_CODEC_I2S0TXDAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "DEEP_BUFFER_DL_OUTPUT",
		.stream_name = MT_SOC_DEEP_BUFFER_DL_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = MT_SOC_DEEP_BUFFER_DL_PCM,
		.codec_dai_name = MT_SOC_CODEC_DEEPBUFFER_TX_DAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "DL1AWBCAPTURE",
		.stream_name = MT_SOC_DL1_AWB_RECORD_STREAM_NAME,
		.cpu_dai_name = MT_SOC_DL1AWB_NAME,
		.platform_name = MT_SOC_DL1_AWB_PCM,
		.codec_dai_name = MT_SOC_CODEC_DL1AWBDAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "Voice_MD1_BT",
		.stream_name = MT_SOC_VOICE_MD1_BT_STREAM_NAME,
		.cpu_dai_name = MT_SOC_VOICE_MD1_BT_NAME,
		.platform_name = MT_SOC_VOICE_MD1_BT,
		.codec_dai_name = MT_SOC_CODEC_VOICE_MD1_BTDAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "VOIP_CALL_BT_PLAYBACK",
		.stream_name = MT_SOC_VOIP_BT_OUT_STREAM_NAME,
		.cpu_dai_name = MT_SOC_VOIP_CALL_BT_OUT_NAME,
		.platform_name = MT_SOC_VOIP_BT_OUT,
		.codec_dai_name = MT_SOC_CODEC_VOIPCALLBTOUTDAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "VOIP_CALL_BT_CAPTURE",
		.stream_name = MT_SOC_VOIP_BT_IN_STREAM_NAME,
		.cpu_dai_name = MT_SOC_VOIP_CALL_BT_IN_NAME,
		.platform_name = MT_SOC_VOIP_BT_IN,
		.codec_dai_name = MT_SOC_CODEC_VOIPCALLBTINDAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "TDM_Debug_CAPTURE",
		.stream_name = MT_SOC_TDM_CAPTURE_STREAM_NAME,
		.cpu_dai_name = MT_SOC_TDMRX_NAME,
		.platform_name = MT_SOC_TDMRX_PCM,
		.codec_dai_name = MT_SOC_CODEC_TDMRX_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "FM_MRG_TX",
		.stream_name = MT_SOC_FM_MRGTX_STREAM_NAME,
		.cpu_dai_name = MT_SOC_FM_MRGTX_NAME,
		.platform_name = MT_SOC_FM_MRGTX_PCM,
		.codec_dai_name = MT_SOC_CODEC_FMMRGTXDAI_DUMMY_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "MultiMedia3",
		.stream_name = MT_SOC_UL1DATA2_STREAM_NAME,
		.cpu_dai_name = MT_SOC_UL2DAI_NAME,
		.platform_name = MT_SOC_UL2_PCM,
		.codec_dai_name = MT_SOC_CODEC_RXDAI2_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "I2S0_AWB_CAPTURE",
		.stream_name = MT_SOC_I2S0AWB_STREAM_NAME,
		.cpu_dai_name = MT_SOC_I2S0AWBDAI_NAME,
		.platform_name = MT_SOC_I2S0_AWB_PCM,
		.codec_dai_name = MT_SOC_CODEC_I2S0AWB_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "Voice_MD2",
		.stream_name = MT_SOC_VOICE_MD2_STREAM_NAME,
		.cpu_dai_name = MT_SOC_VOICE_MD2_NAME,
		.platform_name = MT_SOC_VOICE_MD2,
		.codec_dai_name = MT_SOC_CODEC_VOICE_MD2DAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "PLATOFRM_CONTROL",
		.stream_name = MT_SOC_ROUTING_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = MT_SOC_ROUTING_PCM,
		.codec_dai_name = MT_SOC_CODEC_DUMMY_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "Voice_MD2_BT",
		.stream_name = MT_SOC_VOICE_MD2_BT_STREAM_NAME,
		.cpu_dai_name = MT_SOC_VOICE_MD2_BT_NAME,
		.platform_name = MT_SOC_VOICE_MD2_BT,
		.codec_dai_name = MT_SOC_CODEC_VOICE_MD2_BTDAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "HP_IMPEDANCE",
		.stream_name = MT_SOC_HP_IMPEDANCE_STREAM_NAME,
		.cpu_dai_name = MT_SOC_HP_IMPEDANCE_NAME,
		.platform_name = MT_SOC_HP_IMPEDANCE_PCM,
		.codec_dai_name = MT_SOC_CODEC_HP_IMPEDANCE_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "FM_I2S_RX_Playback",
		.stream_name = MT_SOC_FM_I2S_PLAYBACK_STREAM_NAME,
		.cpu_dai_name = MT_SOC_FM_I2S_NAME,
		.platform_name = MT_SOC_FM_I2S_PCM,
		.codec_dai_name = MT_SOC_CODEC_FM_I2S_DAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "FM_I2S_RX_Capture",
		.stream_name = MT_SOC_FM_I2S_CAPTURE_STREAM_NAME,
		.cpu_dai_name = MT_SOC_FM_I2S_NAME,
		.platform_name = MT_SOC_FM_I2S_AWB_PCM,
		.codec_dai_name = MT_SOC_CODEC_FM_I2S_DUMMY_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
	{
		.name = "MultiMedia_DL2",
		.stream_name = MT_SOC_DL2_STREAM_NAME,
		.cpu_dai_name = MT_SOC_DL2DAI_NAME,
		.platform_name = MT_SOC_DL2_PCM,
		.codec_dai_name = MT_SOC_CODEC_TXDAI2_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "MultiMedia_DL3",
		.stream_name = MT_SOC_DL3_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
		.codec_dai_name = MT_SOC_CODEC_OFFLOAD_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
#ifdef _NON_COMMON_FEATURE_READY
	{
		.name = "MOD_DAI_CAPTURE",
		.stream_name = MT_SOC_MODDAI_STREAM_NAME,
		.cpu_dai_name = MT_SOC_MOD_DAI_NAME,
		.platform_name = MT_SOC_MOD_DAI_PCM,
		.codec_dai_name = MT_SOC_CODEC_MOD_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
#endif
#ifdef CONFIG_MTK_AUDIO_TUNNELING_SUPPORT
	{
		.name = "OFFLOAD",
		.stream_name = MT_SOC_OFFLOAD_STREAM_NAME,
		.cpu_dai_name = MT_SOC_OFFLOAD_PLAYBACK_DAI_NAME,
		.platform_name = MT_SOC_PLAYBACK_OFFLOAD,
		.codec_dai_name = MT_SOC_CODEC_OFFLOAD_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
#endif
#ifdef _NON_COMMON_FEATURE_READY
	{
		.name = "PCM_ANC",
		.stream_name = MT_SOC_ANC_STREAM_NAME,
		.cpu_dai_name = MT_SOC_ANC_NAME,
		.platform_name = MT_SOC_ANC_PCM,
		.codec_dai_name = MT_SOC_CODEC_ANC_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
#endif
	{
		.name = "ANC_RECORD",
		.stream_name = MT_SOC_ANC_RECORD_STREAM_NAME,
		.cpu_dai_name = MT_SOC_ANC_RECORD_DAI_NAME,
		.platform_name = MT_SOC_I2S2_ADC2_PCM,
		.codec_dai_name = MT_SOC_CODEC_DUMMY_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
		.ops = &mt_machine_audio_ops,
	},
#ifdef _NON_COMMON_FEATURE_READY
	{
		.name = "Voice_Ultrasound",
		.stream_name = MT_SOC_VOICE_ULTRA_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = MT_SOC_VOICE_ULTRA,
		.codec_dai_name = MT_SOC_CODEC_VOICE_ULTRADAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
#endif
	{
		.name = "Voice_USB",
		.stream_name = MT_SOC_VOICE_USB_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = MT_SOC_VOICE_USB,
		.codec_dai_name = MT_SOC_CODEC_VOICE_USBDAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "Voice_USB_ECHOREF",
		.stream_name = MT_SOC_VOICE_USB_ECHOREF_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = MT_SOC_VOICE_USB_ECHOREF,
		.codec_dai_name = MT_SOC_CODEC_VOICE_USB_ECHOREF_DAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
		.playback_only = true,
	},
#ifdef CONFIG_MTK_AUDIO_SCP_SPKPROTECT_SUPPORT
	{
		.name = "DL1SCPSPKOUTPUT",
		.stream_name = MT_SOC_DL1SCPSPK_STREAM_NAME,
		.cpu_dai_name = MT_SOC_DL1SCPSPK_NAME,
		.platform_name = MT_SOC_DL1SCPSPK_PCM,
		.codec_dai_name = MT_SOC_CODEC_SPKSCPTXDAI_NAME,
		.codec_name = MT_SOC_CODEC_NAME,
	},
	{
		.name = "VOICE_SCP",
		.stream_name = MT_SOC_SCPVOICE_STREAM_NAME,
		.cpu_dai_name = MT_SOC_SCPVOICE_NAME,
		.platform_name = MT_SOC_SCP_VOICE_PCM,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
#endif
};

#ifdef CONFIG_SND_SOC_MTK_BTCVSD
static struct snd_soc_dai_link mt_soc_btcvsd_dai[] = {
	{
		.name = "BTCVSD",
		.stream_name = "BTCVSD",
		.cpu_dai_name   = MT_SOC_BTCVSD_DAI_NAME,
		.codec_dai_name = MT_SOC_CODEC_BTCVSD_DAI_NAME,
		.codec_name = MT_SOC_CODEC_DUMMY_NAME,
	},
};
#endif

#ifdef CONFIG_SND_SOC_ES9218P
static struct snd_soc_dai_link mt_soc_dai_es9218[] = {
         {
          .name = "HIFI_DAC_OUTPUT",
          .stream_name = "Playback",
          .cpu_dai_name = "es9218-hifi",
          .platform_name = MT_SOC_ROUTING_PCM,
          .codec_dai_name = "es9218-hifi",
          .codec_name = "es9218-codec.6-0048",
          .ops = &mt_machine_audio_ops,
         },
};
#endif

#ifdef CONFIG_SND_SOC_AK4376
static struct snd_soc_dai_link mt_soc_dai_ak4376[] = {
         {
          .name = "HIFI_DAC_OUTPUT",
          .stream_name = "Playback",
          .cpu_dai_name = "ak4376-AIF1",
          .platform_name = MT_SOC_ROUTING_PCM,
          .codec_dai_name = "ak4376-AIF1",
          .codec_name = "ak4376.3-0010",
          .ops = &mt_machine_audio_ops,
         },
};
#endif

static struct snd_soc_dai_link mt_soc_exthp_dai[] = {
	{
		.name = "ext_Headphone_Multimedia",
		.stream_name = MT_SOC_HEADPHONE_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
#ifdef CONFIG_SND_SOC_CS43130
		.codec_dai_name = "cs43130-hifi",
		.codec_name = "cs43130.2-0030",
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS |
			   SND_SOC_DAIFMT_NB_NF,
		.ops = &cs43130_ops,
#else
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
#endif
	},
};

#ifdef CONFIG_SND_SOC_CS35L41

#ifndef CONFIG_SND_SOC_CS35L41_SINGLE
static struct snd_soc_codec_conf cs35l41_codec_conf[] = {
	{
		.dev_name = "cs35l41.6-0040",
		.name_prefix = "Left",
	},
	{
		.dev_name = "cs35l41.6-0041",
		.name_prefix = "Right",
	},
};

static struct snd_soc_dai_link_component cs35l41_codecs[] =
{
	{
		.dai_name = "cs35l41-pcm",
		.name = "cs35l41.6-0040",
	},
	{
		.dai_name = "cs35l41-pcm",
		.name = "cs35l41.6-0041",
	},
};
#endif

static const struct snd_kcontrol_new cs35l41_controls[] = {
#ifndef CONFIG_SND_SOC_CS35L41_SINGLE
	SOC_DAPM_PIN_SWITCH("Left Speaker"),
	SOC_DAPM_PIN_SWITCH("Right Speaker"),
#endif
	SOC_ENUM_EXT("CS35L41 DAI Clocks", cirrus_snd_enum[0], cs35l41_codec_dai_clks_get,
			cs35l41_codec_dai_clks_put),
	SOC_ENUM_EXT("CS35L41 DAI Polarity", cirrus_snd_enum[1], cs35l41_codec_dai_bitfmt_get,
			cs35l41_codec_dai_bitfmt_put),
	SOC_ENUM_EXT("CS35L41 DAI Mode", cirrus_snd_enum[2], cs35l41_codec_dai_mode_get,
			cs35l41_codec_dai_mode_put),
	SOC_ENUM_EXT("CS35L41 Static MCLK Mode", cirrus_snd_enum[3],
			cs35l41_codec_static_clk_mode_get, cs35l41_codec_static_clk_mode_put),
	SOC_ENUM_EXT("CS35L41 Codec CLK Source", cirrus_snd_enum[4], cs35l41_codec_clk_src_get,
			cs35l41_codec_clk_src_put),
	SOC_ENUM_EXT("CS35L41 Force Frame32", cirrus_snd_enum[5], cs35l41_codec_dai_force_frame32_get,
			cs35l41_codec_dai_force_frame32_put),
};

#ifndef CONFIG_SND_SOC_CS35L41_SINGLE
static struct snd_soc_dapm_widget cs35l41_widgets[] = {
            SND_SOC_DAPM_SPK("Left Speaker", NULL),
            SND_SOC_DAPM_SPK("Right Speaker", NULL),
};

static struct snd_soc_dapm_route cs35l41_audio_paths[] = {
     { "Left Speaker", NULL, "Left SPK" },
     { "Right Speaker", NULL, "Right SPK" },
};
#endif
static int cs35l41_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	struct snd_soc_card *card = rtd->card;

	pr_info("%s: cs35l41_codec_init enter\n", __func__);

	ret = snd_soc_add_card_controls(card, cs35l41_controls, ARRAY_SIZE(cs35l41_controls));
	if (ret < 0) {
		pr_err("%s: snd_soc_add_card_controls failed, err %d\n",
			__func__, ret);
		return ret;
	}
#ifndef CONFIG_SND_SOC_CS35L41_SINGLE
	ret = snd_soc_dapm_new_controls(&card->dapm, cs35l41_widgets,ARRAY_SIZE(cs35l41_widgets));
	if (ret < 0) {
		pr_err("%s: snd_soc_dapm_new_controls failed, err %d\n",
			__func__, ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, cs35l41_audio_paths,ARRAY_SIZE(cs35l41_audio_paths));
	if (ret < 0) {
		pr_err("%s: snd_soc_dapm_add_routes failed, err %d\n",
			__func__, ret);
		return ret;
	}
	snd_soc_dapm_sync(&card->dapm);
#endif
	return ret;
}
#endif

//#ifdef CONFIG_SND_SOC_TAS2563
#if 0
static const struct snd_kcontrol_new tas2563_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Speaker"),
	SOC_DAPM_PIN_SWITCH("Right Speaker"),
};

static struct snd_soc_dapm_widget tas2563_widgets[] = {
            SND_SOC_DAPM_SPK("Left Speaker", NULL),
            SND_SOC_DAPM_SPK("Right Speaker", NULL),
};

static struct snd_soc_dapm_route tas2563_audio_paths[] = {
     { "Left Speaker", NULL, "Left OUT" },
     { "Right Speaker", NULL, "Right OUT" },
};

static struct snd_soc_codec_conf tas2563_codec_conf[] = {
	{
		.dev_name = "tas2563.6-004c",
		.name_prefix = "Left",
	},
	{
		.dev_name = "tas2563.6-004d",
		.name_prefix = "Right",
	},
};

static int tas2563_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	struct snd_soc_card *card = rtd->card;

	pr_info("%s: enter\n", __func__);

	ret = snd_soc_add_card_controls(card, tas2563_controls, ARRAY_SIZE(tas2563_controls));
	if (ret < 0) {
		pr_err("%s: snd_soc_add_card_controls failed, err %d\n",
			__func__, ret);
		return ret;
	}
	ret = snd_soc_dapm_new_controls(&card->dapm, tas2563_widgets,ARRAY_SIZE(tas2563_widgets));
	if (ret < 0) {
		pr_err("%s: snd_soc_dapm_new_controls failed, err %d\n",
			__func__, ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, tas2563_audio_paths,ARRAY_SIZE(tas2563_audio_paths));
	if (ret < 0) {
		pr_err("%s: snd_soc_dapm_add_routes failed, err %d\n",
			__func__, ret);
		return ret;
	}
	snd_soc_dapm_sync(&card->dapm);
	return ret;
}

static struct snd_soc_dai_link_component tas2563_codecs[] =
{
	{
		.dai_name = "tas2563 ASI1",
		.name = "tas2563.6-004c",
	},
	{
		.dai_name = "tas2563 ASI1",
		.name = "tas2563.6-004d",
	},
};
#endif

static struct snd_soc_dai_link mt_soc_extspk_dai[] = {
	{
		.name = "ext_Speaker_Multimedia",
		.stream_name = MT_SOC_SPEAKER_STREAM_NAME,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
#ifdef CONFIG_SND_SOC_MAX98926
		.codec_dai_name = "max98926-aif1",
		.codec_name = "MAX98926_MT",
#elif defined(CONFIG_SND_SOC_CS35L35)
		.codec_dai_name = "cs35l35-pcm",
		.codec_name = "cs35l35.2-0040",
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS |
			   SND_SOC_DAIFMT_NB_NF,
		.ops = &cs35l35_ops,
#elif defined(CONFIG_SND_SMARTPA_AW8898)
		.codec_dai_name = "aw8898-aif",
		.codec_name = "aw8898_smartpa",
#elif defined(CONFIG_SND_SOC_FS16XX)
		.codec_dai_name = "foursemi-aif",
		.codec_name = "fs16xx.6-0034",
		.ops = &mt_machine_audio_ops,
#elif defined(CONFIG_SND_SOC_CS35L41)
#ifdef CONFIG_SND_SOC_CS35L41_SINGLE
		.codec_dai_name = "cs35l41-pcm",
		.codec_name = "cs35l41.6-0040",
#else
		.num_codecs = 2,
		.codecs = cs35l41_codecs,
#endif
		.ops = &mt_machine_audio_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |SND_SOC_DAIFMT_CBS_CFS,
		.init = &cs35l41_codec_init,
#elif defined(CONFIG_SND_SOC_TAS2563)
		.codec_dai_name = "tas2563 ASI1",
		.codec_name = "tas2563.6-004c",
		.ops = &mt_machine_audio_ops,
#else
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
#endif
	},
	{
		.name = "I2S1_AWB_CAPTURE",
		.stream_name = MT_SOC_I2S2ADC2_STREAM_NAME,
		.cpu_dai_name = MT_SOC_I2S2ADC2DAI_NAME,
		.platform_name = MT_SOC_I2S2_ADC2_PCM,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ops = &mt_machine_audio_ops,
	},
};

static struct snd_soc_dai_link
	mt_soc_dai_component[ARRAY_SIZE(mt_soc_dai_common) +
#ifdef CONFIG_SND_SOC_MTK_BTCVSD
			     ARRAY_SIZE(mt_soc_btcvsd_dai) +
#endif
			     ARRAY_SIZE(mt_soc_exthp_dai) +
#ifdef CONFIG_SND_SOC_AK4376
                 ARRAY_SIZE(mt_soc_dai_ak4376) +
#endif
#ifdef CONFIG_SND_SOC_ES9218P
                 ARRAY_SIZE(mt_soc_dai_es9218) +
#endif
			     ARRAY_SIZE(mt_soc_extspk_dai)];

static struct snd_soc_card mt_snd_soc_card_mt = {
	.name = "mt-snd-card",
	.dai_link = mt_soc_dai_common,
	.num_links = ARRAY_SIZE(mt_soc_dai_common),
};

#ifdef CONFIG_SND_SOC_ES9218P
static struct snd_soc_card *populate_sndcard_dailinks(struct snd_soc_card *card)
{
	int len = card->num_links;

#if defined(CONFIG_LGE_HIFI_HWINFO)
	if (lge_get_hifi_hwinfo()) {
#else
	if (of_find_compatible_node(NULL, NULL, "dac,es9218-codec")) {
#endif
		memcpy(mt_soc_dai_component + len, mt_soc_dai_es9218, sizeof(mt_soc_dai_es9218));
		len += ARRAY_SIZE(mt_soc_dai_es9218);
	}
	else {
		pr_err("es9218 audio support not present\n");
	}
	card->dai_link = mt_soc_dai_component;
	card->num_links = len;
	return card;
}
#endif

#ifdef CONFIG_SND_SOC_AK4376
static struct snd_soc_card *populate_sndcard_dailinks(struct snd_soc_card *card)
{
	int len = card->num_links;

#if defined(CONFIG_LGE_HIFI_HWINFO)
	if (lge_get_hifi_hwinfo()) {
#else
	if (of_find_compatible_node(NULL, NULL, "akm,ak4376")) {
#endif
		memcpy(mt_soc_dai_component + len, mt_soc_dai_ak4376, sizeof(mt_soc_dai_ak4376));
		len += ARRAY_SIZE(mt_soc_dai_ak4376);
	}
	else {
		pr_err("ak4376 audio support not present\n");
	}
	card->dai_link = mt_soc_dai_component;
	card->num_links = len;
	return card;
}
#endif

static void get_ext_dai_codec_name(void)
{
	get_extspk_dai_codec_name(mt_soc_extspk_dai);
	get_exthp_dai_codec_name(mt_soc_exthp_dai);
}

#if defined(CONFIG_SND_SOC_CS35L41) && !defined(CONFIG_SND_SOC_CS35L41_SINGLE)
static int cirrus_codec_update_dai_link(struct snd_soc_card *card)
{
	int i, j, index, ret = 0;
	struct device *cdev = card->dev;
	struct snd_soc_dai_link *dai_link = card->dai_link;
	struct device_node *np;
	if (!cdev) {
		pr_err("%s: Sound card device memory NULL\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < card->num_links; i++) {
		/* populate codec_of_node for snd card dai links */
		if (dai_link[i].codec_name && !dai_link[i].codec_of_node) {
			pr_info("%s: retrieving phandle for codec %s \n", __func__, dai_link[i].codec_name);
			index = of_property_match_string(cdev->of_node, "cirrus-codec-names", dai_link[i].codec_name);
			if (index < 0) {
				pr_info("%s: continue(1) index = %d, name = %s \n", __func__, index, dai_link[i].codec_name);
				continue;
			}
			np = of_parse_phandle(cdev->of_node, "cirrus-codec", index);
			if (!np) {
				pr_err("%s: retrieving phandle for codec %s failed\n", __func__, dai_link[i].codec_name);
				ret = -ENODEV;
				goto err;
			}
			dai_link[i].codec_of_node = np;
			dai_link[i].codec_name = NULL;
		}
		if (dai_link[i].codecs && (dai_link[i].num_codecs > 0)) {
			for (j = 0; j < dai_link[i].num_codecs; j++) {
				pr_info("dai_link[%d].codecs[%d].name = %s\n",i, j, dai_link[i].codecs[j].name);
				index = of_property_match_string(cdev->of_node, "cirrus-codec-names", dai_link[i].codecs[j].name);
				if (index < 0) {
					pr_info("%s: continue(2) index = %d, name = %s, \n", __func__, index, dai_link[i].codecs[j].name);
					continue;
				}
				np = of_parse_phandle(cdev->of_node, "cirrus-codec", index);
				if (!np) {
					pr_err("%s: retrieving phandle for codec %s failed\n",
						__func__, dai_link[i].codecs[j].name);
					ret = -ENODEV;
					goto err;
				}
				dai_link[i].codecs[j].of_node = np;
				dai_link[i].codecs[j].name = NULL;
			}
		}
	}
err:
	return ret;
}
#endif

static int mt_soc_snd_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt_snd_soc_card_mt;
	struct device_node *btcvsd_node;
	int ret;
#ifdef CONFIG_LGE_HP_POP_CANCEL_SWITCH_USE_LDO
	int err = 0;
#endif
	int daiLinkNum = 0;

#if !defined(CONFIG_SND_SOC_CS35L41) && !defined(CONFIG_SND_SOC_TAS2563) && !defined(CONFIG_SND_SMARTPA_AW8898)
	ret = mtk_spk_update_dai_link(mt_soc_extspk_dai, pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s(), mtk_spk_update_dai_link error\n",
			__func__);
		return -EINVAL;
	}
#endif

	get_ext_dai_codec_name();

	pr_debug("mt_soc_snd_probe dai_link = %p\n",
		mt_snd_soc_card_mt.dai_link);

	/* DEAL WITH DAI LINK */
	memcpy(mt_soc_dai_component, mt_soc_dai_common,
	       sizeof(mt_soc_dai_common));
	daiLinkNum += ARRAY_SIZE(mt_soc_dai_common);

#ifdef CONFIG_SND_SOC_MTK_BTCVSD
	/* assign btcvsd platform_node */
	btcvsd_node = of_parse_phandle(pdev->dev.of_node,
				       "mediatek,btcvsd_snd", 0);
	if (!btcvsd_node) {
		dev_err(&pdev->dev, "Property 'btcvsd_snd' missing or invalid\n");
		return -EINVAL;
	}
	mt_soc_btcvsd_dai[0].platform_of_node = btcvsd_node;

	memcpy(mt_soc_dai_component + daiLinkNum,
	mt_soc_btcvsd_dai, sizeof(mt_soc_btcvsd_dai));
	daiLinkNum += ARRAY_SIZE(mt_soc_btcvsd_dai);
#endif

	memcpy(mt_soc_dai_component + daiLinkNum, mt_soc_exthp_dai,
	       sizeof(mt_soc_exthp_dai));
	daiLinkNum += ARRAY_SIZE(mt_soc_exthp_dai);

	memcpy(mt_soc_dai_component + daiLinkNum, mt_soc_extspk_dai,
	       sizeof(mt_soc_extspk_dai));
	daiLinkNum += ARRAY_SIZE(mt_soc_extspk_dai);

	mt_snd_soc_card_mt.dai_link = mt_soc_dai_component;
	mt_snd_soc_card_mt.num_links = daiLinkNum;

#if defined(CONFIG_SND_SOC_AK4376) || defined(CONFIG_SND_SOC_ES9218P)
	populate_sndcard_dailinks(card);
#endif

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
#if defined(CONFIG_SND_SOC_CS35L41) && !defined(CONFIG_SND_SOC_CS35L41_SINGLE)
	cirrus_codec_update_dai_link(card);
	card->codec_conf = cs35l41_codec_conf;
	card->num_configs = ARRAY_SIZE(cs35l41_codec_conf);
#endif

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);

	/* create debug file */
	mt_sco_audio_debugfs =
		debugfs_create_file(DEBUG_FS_NAME, S_IFREG | 0444, NULL,
				    (void *)DEBUG_FS_NAME, &mtaudio_debug_ops);

	/* create analog debug file */
	mt_sco_audio_debugfs = debugfs_create_file(
		DEBUG_ANA_FS_NAME, S_IFREG | 0444, NULL,
		(void *)DEBUG_ANA_FS_NAME, &mtaudio_ana_debug_ops);

#ifdef CONFIG_LGE_HP_POP_CANCEL_SWITCH_USE_LDO
	ldo_vout = regulator_get(NULL, "irtx_ldo");
	if (IS_ERR(ldo_vout)) {
		pr_err("%s: Can't get irtx_ldo regulator\n", __func__);
		ldo_vout = NULL;
	} else {
		pr_info("%s: Found irtx_ldo regulator.\n",__func__ );
		err = regulator_set_voltage(ldo_vout, 2800000, 2800000);
		if (err) {
			pr_info("%s: Can't set irtx_ldo voltage, ret = %d\n", __func__, err);
		}
		err = regulator_enable(ldo_vout);
		if (err) {
			pr_info("%s: Failed to enable irtx_ldo, ret=%d\n", __func__, err);
		}
	}
#endif

	return ret;
}

static int mt_soc_snd_remove(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mt_audio_driver_dt_match[] = {
	{
		.compatible = "mediatek,audio",
	},
	{} };
#endif

static struct platform_driver mt_audio_driver = {
	.driver = {

			.name = "mtk-audio",
			.owner = THIS_MODULE,
#ifdef CONFIG_OF
			.of_match_table = mt_audio_driver_dt_match,
#endif
		},
	.probe = mt_soc_snd_probe,
	.remove = mt_soc_snd_remove,
};

#ifndef CONFIG_OF
static struct platform_device *mtk_soc_snd_dev;
#endif

static int __init mt_soc_snd_init(void)
{
	int ret;

	pr_debug("%s\n", __func__);
#ifndef CONFIG_OF
	mtk_soc_snd_dev = platform_device_alloc("mtk-audio", -1);
	if (!mtk_soc_snd_dev)
		return -ENOMEM;

	ret = platform_device_add(mtk_soc_snd_dev);
	if (ret != 0) {
		platform_device_put(mtk_soc_snd_dev);
		return ret;
	}
#endif
	ret = platform_driver_register(&mt_audio_driver);
	pr_debug("-%s\n", __func__);

	return ret;
}
module_init(mt_soc_snd_init);

static void __exit mt_soc_snd_exit(void)
{
	platform_driver_unregister(&mt_audio_driver);
}
module_exit(mt_soc_snd_exit);

/* Module information */
MODULE_AUTHOR("ChiPeng <chipeng.chang@mediatek.com>");
MODULE_DESCRIPTION("ALSA SoC driver ");
MODULE_LICENSE("GPL");

/*****************************************************************************
*  Copyright Statement:
*  --------------------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of MediaTek Inc. (C) 2008
*
*  BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
*  THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
*  RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO BUYER ON
*  AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
*  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
*  NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
*  SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
*  SUPPLIED WITH THE MEDIATEK SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH
*  THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO
*  NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S
*  SPECIFICATION OR TO CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
*  BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
*  LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
*  AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
*  OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY BUYER TO
*  MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
*  THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
*  WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT OF
*  LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING THEREOF AND
*  RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN FRANCISCO, CA, UNDER
*  THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE (ICC).
*
*****************************************************************************/


#ifdef BUILD_LK
#include <string.h>
#include <mt_gpio.h>
#include <platform/mt_pmic.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
#include "upmu_common.h"
#include <linux/string.h>
#endif

#include "lcm_drv.h"
#if defined(BUILD_LK)
#define LCM_PRINT printf
#elif defined(BUILD_UBOOT)
#define LCM_PRINT printf
#else
#define LCM_PRINT printk
#endif
#if defined(BUILD_LK)
#include <boot_mode.h>
#else
#include <linux/types.h>
#include <upmu_hw.h>
#endif
#include <linux/input/lge_touch_notify.h>
#include <soc/mediatek/lge/board_lge.h>
#include "mt6370_pmu_dsv.h"
#include "lcd_bias.h"
#include "disp_recovery.h"

#define FRAME_WIDTH              (720)
#define FRAME_HEIGHT             (1440)

#define PHYSICAL_WIDTH        (62)
#define PHYSICAL_HEIGHT         (124)

static struct LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)                                    (lcm_util.set_reset_pin((v)))
#define UDELAY(n)                                             (lcm_util.udelay(n))
#define MDELAY(n)                                             (lcm_util.mdelay(n))

/* --------------------------------------------------------------------------- */
/* Local Functions */
/* --------------------------------------------------------------------------- */

#define dsi_set_cmdq_V3(para_tbl, size, force_update)\
lcm_util.dsi_set_cmdq_V3(para_tbl, size, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)\
lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)\
lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)\
lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)\
lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)\
lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size)\
lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

struct LCM_setting_table {
    unsigned char cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3[] = {
       {0x15, 0x00, 1,   {0x00}},
       {0x29, 0xFF, 3,   {0x87, 0x36, 0x01}},
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xFF, 2,   {0x87, 0x36}},
       {0x15, 0x00, 1,   {0x86}},
       {0x15, 0xB0, 1,   {0x0B}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xB3, 3,   {0x01, 0x00, 0x40}},
       {0x15, 0x00, 1,   {0x8D}},
       {0x15, 0xF5, 1,   {0x11}},
       {0x15, 0x00, 1,   {0xD5}},
       {0x15, 0xF5, 1,   {0x11}},
       {0x15, 0x00, 1,   {0xC1}},
       {0x15, 0xC0, 1,   {0x01}},
       {0x15, 0x00, 1,   {0xE2}},
       {0x15, 0xCE, 1,   {0xBA}},
       {0x15, 0x00, 1,   {0xA0}},
       {0x29, 0xF3, 8,   {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03}},
       {0x15, 0x00, 1,   {0xE4}},
       {0x15, 0xC3, 1,   {0x40}},
       {0x15, 0x00, 1,   {0xB0}},
       {0x15, 0xF3, 1,   {0x01}},
       {0x15, 0x00, 1,   {0x00}},
       {0x15, 0x1C, 1,   {0x04}},
       {0x15, 0x00, 1,   {0xA0}},
       {0x29, 0xB3, 7,   {0x32, 0x02, 0xD0, 0x05, 0xA0, 0x00, 0x58}},
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xC0, 14,  {0x00, 0xBA, 0x06, 0x38, 0x00, 0xBA, 0x00, 0xBA, 0x06, 0x06, 0x01, 0x15, 0x01, 0x15}},
       {0x15, 0x00, 1,   {0x80}},
       {0x15, 0xA5, 1,   {0xCF}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xC0, 14,  {0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0x03, 0x31, 0x31, 0x09, 0x09}},
       {0x15, 0x00, 1,   {0xB0}},
       {0x29, 0xCE, 14,  {0x00, 0x01, 0x01, 0x01, 0x03, 0x31, 0x09, 0x00, 0x01, 0x01, 0x01, 0x03, 0x31, 0x09}},
       {0x15, 0x00, 1,   {0x88}},
       {0x29, 0xC3, 2,   {0x22, 0x22}},
       {0x15, 0x00, 1,   {0x98}},
       {0x29, 0xC3, 2,   {0x22, 0x22}},
       {0x15, 0x00, 1,   {0xA3}},
       {0x29, 0xC1, 2,   {0x01, 0x10}},
       {0x15, 0x00, 1,   {0x82}},
       {0x29, 0xA5, 3,   {0x00, 0x00, 0x00}},
       {0x15, 0x00, 1,   {0x87}},
       {0x29, 0xA5, 6,   {0x00, 0x07, 0x77, 0x00, 0x07, 0x07}},
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xC2, 4,   {0x82, 0x00, 0x00, 0x81}},
       {0x15, 0x00, 1,   {0x84}},
       {0x29, 0xC2, 4,   {0x81, 0x00, 0x00, 0x81}},
       {0x15, 0x00, 1,   {0x88}},
       {0x29, 0xC2, 4,   {0x83, 0x00, 0x32, 0xBC}},
       {0x15, 0x00, 1,   {0xB0}},
       {0x29, 0xC2, 5,   {0x82, 0x00, 0x00, 0x07, 0x88}},
       {0x15, 0x00, 1,   {0xB5}},
       {0x29, 0xC2, 5,   {0x81, 0x01, 0x00, 0x07, 0x88}},
       {0x15, 0x00, 1,   {0xBA}},
       {0x29, 0xC2, 5,   {0x00, 0x02, 0x00, 0x07, 0x88}},
       {0x15, 0x00, 1,   {0xC0}},
       {0x29, 0xC2, 5,   {0x01, 0x03, 0x00, 0x07, 0x88}},
       {0x15, 0x00, 1,   {0xDA}},
       {0x29, 0xC2, 2,   {0x33, 0x33}},
       {0x15, 0x00, 1,   {0xC0}},
       {0x29, 0xC3, 3,   {0x01, 0x99, 0x9C}},
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xCC, 12,  {0x02, 0x03, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xCC, 12,  {0x03, 0x02, 0x09, 0x08, 0x07, 0x06, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A}},
       {0x15, 0x00, 1,   {0xA0}},
       {0x29, 0xCC, 15,  {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x18, 0x19, 0x20, 0x21, 0x14, 0x15, 0x16, 0x17, 0x04}},
       {0x15, 0x00, 1,   {0xB0}},
       {0x29, 0xCC, 5,   {0x22, 0x22, 0x22, 0x22, 0x22}},
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xCB, 8,   {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xCB, 15,  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
       {0x15, 0x00, 1,   {0xA0}},
       {0x29, 0xCB, 15,  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
       {0x15, 0x00, 1,   {0xB0}},
       {0x29, 0xCB, 2,   {0x00, 0x00}},
       {0x15, 0x00, 1,   {0xC0}},
       {0x29, 0xCB, 15,  {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55}},
       {0x15, 0x00, 1,   {0xD0}},
       {0x29, 0xCB, 15,  {0x00, 0x00, 0x00, 0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x00, 0x00, 0x00}},
       {0x15, 0x00, 1,   {0xE0}},
       {0x29, 0xCB, 2,   {0x00, 0x00}},
       {0x15, 0x00, 1,   {0xF0}},
       {0x29, 0xCB, 8,   {0xFF, 0x0F, 0x00, 0x3F, 0x30, 0x30, 0x30, 0x00}},
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xCD, 15,  {0x22, 0x22, 0x22, 0x22, 0x22, 0x01, 0x13, 0x14, 0x1B, 0x05, 0x03, 0x17, 0x18, 0x18, 0x22}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xCD, 3,   {0x0F, 0x0E, 0x0D}},
       {0x15, 0x00, 1,   {0xA0}},
       {0x29, 0xCD, 15,  {0x22, 0x22, 0x22, 0x22, 0x22, 0x02, 0x13, 0x14, 0x1B, 0x06, 0x04, 0x17, 0x18, 0x18, 0x22}},
       {0x15, 0x00, 1,   {0xB0}},
       {0x29, 0xCD, 3,   {0x0F, 0x0E, 0x0D}},
       {0x15, 0x00, 1,   {0x81}},
       {0x29, 0xF3, 12,  {0x40, 0x89, 0xC0, 0x40, 0x89, 0xC0, 0x40, 0x01, 0x00, 0x40, 0x01, 0x00}},//
       {0x15, 0x00, 1,   {0x80}},
       {0x15, 0xCE, 1,   {0x93}},
       {0x15, 0x00, 1,   {0x83}},
       {0x29, 0xCE, 3,   {0x55, 0x00, 0x18}},
       {0x15, 0x00, 1,   {0x86}},
       {0x29, 0xCE, 2,   {0x00, 0x00}},
       {0x15, 0x00, 1,   {0x88}},
       {0x29, 0xCE, 2,   {0x00, 0x00}},
       {0x15, 0x00, 1,   {0x8A}},
       {0x29, 0xCE, 2,   {0x82, 0x6A}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xCE, 5,   {0xF2, 0xFF, 0xF6, 0x0F, 0xFF}},
       {0x15, 0x00, 1,   {0xF0}},
       {0x29, 0xC3, 6,   {0x01, 0x00, 0x02, 0x10, 0x00, 0x6A}},
       {0x15, 0x00, 1,   {0xF7}},
       {0x29, 0xC3, 6,   {0x02, 0x00, 0x30, 0x08, 0x00, 0x02}},
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xCF, 9,   {0x10, 0x05, 0x9E, 0x00, 0x03, 0x05, 0xA0, 0x00, 0x03}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xCF, 9,   {0x10, 0x03, 0xC0, 0x00, 0x03, 0x03, 0xC2, 0x00, 0x03}},
       {0x15, 0x00, 1,   {0xA0}},
       {0x29, 0xCF, 9,   {0x10, 0x02, 0x60, 0x00, 0x03, 0x02, 0x62, 0x00, 0x03}},
       {0x15, 0x00, 1,   {0xB0}},
       {0x29, 0xCF, 9,   {0x10, 0x01, 0x40, 0x00, 0x03, 0x01, 0x42, 0x00, 0x03}},
       {0x15, 0x00, 1,   {0xC0}},
       {0x29, 0xCF, 9,   {0x11, 0x00, 0x00, 0x00, 0x03, 0x07, 0x80, 0x00, 0x03}},
       {0x15, 0x00, 1,   {0xD0}},
       {0x29, 0xCF, 9,   {0x10, 0x07, 0x6C, 0x00, 0x03, 0x07, 0x6E, 0x00, 0x03}},
       {0x15, 0x00, 1,   {0xE0}},
       {0x29, 0xCF, 12,  {0x01, 0x00, 0x02, 0x02, 0x00, 0x31, 0x08, 0x00, 0x01, 0x10, 0x00, 0x6A}},
       {0x15, 0x00, 1,   {0xE0}},
       {0x15, 0xFF, 1,   {0x00}},
       {0x15, 0x00, 1,   {0x90}},
       {0x29, 0xC5, 3,   {0x77, 0x1E, 0x14}},//
       {0x15, 0x00, 1,   {0x89}},//
       {0x15, 0xC5, 1,   {0x05}},//
       {0x15, 0x00, 1,   {0x00}},
       {0x29, 0xD8, 2,   {0x31, 0x31}},
       {0x15, 0x00, 1,   {0x00}},
       {0x29, 0xD9, 5,   {0x80, 0xB9, 0xB9, 0xB9, 0xB9}},
       {0x15, 0x00, 1,   {0x00}},
       {0x29, 0xE1, 24,  {0x03, 0x09, 0x1A, 0x2B, 0x36, 0x44, 0x57, 0x66, 0x6C, 0x76, 0x7F, 0x89, 0x6E, 0x67, 0x64, 0x5D, 0x50, 0x44, 0x36, 0x2C, 0x24, 0x18, 0x09, 0x06}},
       {0x15, 0x00, 1,   {0x00}},
       {0x29, 0xE2, 24,  {0x03, 0x09, 0x1A, 0x2B, 0x36, 0x44, 0x57, 0x66, 0x6C, 0x76, 0x7F, 0x89, 0x6E, 0x67, 0x64, 0x5D, 0x50, 0x44, 0x36, 0x2C, 0x24, 0x18, 0x09, 0x06}},
       {0x15, 0x00, 1,   {0xB3}},
       {0x29, 0xF3, 2,   {0x02, 0xFD}},//
       {0x15, 0x00, 1,   {0x80}},
       {0x29, 0xFF, 2,   {0x00, 0x00}},
       {0x15, 0x00, 1,   {0x00}},
       {0x29, 0xFF, 3,   {0x00, 0x00, 0x00}},
       //{0x15, 0x35, 1,   {0x00}},
       {0x05, 0x11, 1,   {0x00}},
};
#if 0
static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Exit_Sleep[] = {
	{0x05, 0x11, 1, {0x00}},
};
#endif
static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Disp_On[] = {
	{0x05, 0x29, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Display_Off[] = {
	{0x05, 0x28, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Enter_Sleep[] = {
	{0x05, 0x10, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Deep_standby_cmd[] = {
	{0x15, 0x00, 1, {0x00}},
	{0x29, 0xF7, 4, {0x5A, 0xA5, 0x87, 0x36}},
};


static struct LCM_setting_table __attribute__ ((unused)) lcm_backlight_level_setting[] = {
	{0x51, 1, {0xFF}},
};


static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
    memcpy((void*)&lcm_util, (void*)util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS * params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type   = LCM_TYPE_DSI;

	params->width  = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	// physical size
	params->physical_width = PHYSICAL_WIDTH;
	params->physical_height = PHYSICAL_HEIGHT;

	params->dsi.mode   = BURST_VDO_MODE; //SYNC_EVENT_VDO_MODE; //BURST_VDO_MODE;//SYNC_PULSE_VDO_MODE;
	// enable tearing-free
	params->dbi.te_mode                 = LCM_DBI_TE_MODE_DISABLED;
	params->dbi.te_edge_polarity        = LCM_POLARITY_RISING;

	// DSI
	/* Command mode setting */
	params->dsi.LANE_NUM                    = LCM_FOUR_LANE;
	//The following defined the fomat for data coming from LCD engine.
	params->dsi.data_format.color_order     = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq       = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding         = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format          = LCM_DSI_FORMAT_RGB888;

	// Highly depends on LCD driver capability.
	params->dsi.packet_size=256;
	//params->dsi.intermediat_buffer_num = 0;

	params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;
	params->dsi.cont_clock = 1;

	params->dsi.vertical_sync_active = 2;
	params->dsi.vertical_backporch = 16;
	params->dsi.vertical_frontporch = 16;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 4;
	params->dsi.horizontal_backporch = 32;
	params->dsi.horizontal_frontporch = 32;
	params->dsi.horizontal_active_pixel	= FRAME_WIDTH;

	params->dsi.PLL_CLOCK = 220;
	params->dsi.ssc_disable	= 1;

#if defined(CONFIG_LGE_DISPLAY_ESD_RECOVERY)
	params->dsi.esd_check_enable = 0;
	params->dsi.customization_esd_check_enable = 1;

	params->dsi.lcm_esd_check_table[1].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[1].count = 1;
	params->dsi.lcm_esd_check_table[1].para_list[0] = 0x14;

/*
	params->dsi.lcm_esd_check_table[0].cmd = 0x05;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x00;

	params->dsi.lcm_esd_check_table[2].cmd = 0x0E;
	params->dsi.lcm_esd_check_table[2].count = 1;
	params->dsi.lcm_esd_check_table[2].para_list[0] = 0x00;
*/

	set_disp_esd_check_lcm(true);
#endif

	params->lcm_seq_suspend = LCM_MIPI_VIDEO_FRAME_DONE_SUSPEND;
	params->lcm_seq_shutdown = LCM_SHUTDOWN_AFTER_DSI_OFF;
	params->lcm_seq_resume = LCM_MIPI_READY_VIDEO_FRAME_RESUME;
	params->lcm_seq_power_on = NOT_USE_RESUME;

	params->esd_powerctrl_support = false;
}

static void init_lcm_registers(void)
{
	LCM_PRINT("[LCD] %s : +\n", __func__);

	dsi_set_cmdq_V3(lcm_initialization_setting_V3_Disp_On, sizeof(lcm_initialization_setting_V3_Disp_On) / sizeof(struct LCM_setting_table_V3), 1);
	MDELAY(5);
	LCM_PRINT("[LCD] %s : -\n", __func__);
}

static void init_lcm_registers_sleep(void)
{
	LCM_PRINT("[LCD] %s : +\n", __func__);

	dsi_set_cmdq_V3(lcm_initialization_setting_V3_Display_Off, sizeof(lcm_initialization_setting_V3_Display_Off) / sizeof(struct LCM_setting_table_V3), 1);
	MDELAY(50);
	dsi_set_cmdq_V3(lcm_initialization_setting_V3_Enter_Sleep, sizeof(lcm_initialization_setting_V3_Enter_Sleep) / sizeof(struct LCM_setting_table_V3), 1);
	MDELAY(120);
	dsi_set_cmdq_V3(lcm_initialization_setting_V3_Deep_standby_cmd, sizeof(lcm_initialization_setting_V3_Deep_standby_cmd) / sizeof(struct LCM_setting_table_V3), 1);

	LCM_PRINT("[LCD] %s : -\n", __func__);
}

#if 0
static void ldo_1v8io_ctrl(unsigned int enable)
{
	if(enable)
	    lcd_bias_set_gpio_ctrl(LCD_LDO_EN, 1);
	else
	    lcd_bias_set_gpio_ctrl(LCD_LDO_EN, 0);

    LCM_PRINT("[LCD] %s : %s\n",__func__,(enable)? "ON":"OFF");
}
#endif

static void lcm_dsv_vpos_ctrl(unsigned int enable)
{
	if(enable)
	    lcd_bias_set_gpio_ctrl(DSV_VPOS_EN, 1);
	else
	    lcd_bias_set_gpio_ctrl(DSV_VPOS_EN, 0);

	LCM_PRINT("[LCD] %s : %s\n",__func__,(enable)? "ON":"OFF");
}

static void lcm_dsv_vneg_ctrl(unsigned int enable)
{
	if(enable)
	    lcd_bias_set_gpio_ctrl(DSV_VNEG_EN, 1);
	else
	    lcd_bias_set_gpio_ctrl(DSV_VNEG_EN, 0);

	LCM_PRINT("[LCD] %s : %s\n",__func__,(enable)? "ON":"OFF");
}

static void reset_lcd_module(unsigned int reset)
{
	if(reset)
	    lcd_bias_set_gpio_ctrl(LCM_RST, 1);
	else
	    lcd_bias_set_gpio_ctrl(LCM_RST, 0);

	LCM_PRINT("[LCD] LCD Reset %s \n",(reset)? "High":"Low");
}

static void touch_reset_pin(unsigned int mode)
{
	if(mode == 1)
		touch_notifier_call_chain(LCD_EVENT_TOUCH_RESET_END, NULL);
	else
		touch_notifier_call_chain(LCD_EVENT_TOUCH_RESET_START, NULL);

	LCM_PRINT("Touch Reset %s \n",(mode)? "High":"Low");
}

static void lcm_init(void)
{
	LCM_PRINT("[LCD] %s : +\n", __func__);
	touch_reset_pin(0);
	reset_lcd_module(0);
	MDELAY(15);

	lcm_dsv_vpos_ctrl(1);
	MDELAY(10);
	lcm_dsv_vneg_ctrl(1);
	MDELAY(5);

	reset_lcd_module(1);
	touch_reset_pin(1);
	MDELAY(2);

	reset_lcd_module(0);
	touch_reset_pin(0);
	MDELAY(2);

	reset_lcd_module(1);
	touch_reset_pin(1);
	MDELAY(12);

	dsi_set_cmdq_V3(lcm_initialization_setting_V3, sizeof(lcm_initialization_setting_V3) / sizeof(struct LCM_setting_table_V3), 1);
	MDELAY(120);

	init_lcm_registers();
	LCM_PRINT("[LCD] %s : -\n", __func__);
}

static void lcm_suspend(void)
{
    init_lcm_registers_sleep();

    LCM_PRINT("[LCD] lcm_suspend \n");
}

static void lcm_resume(void)
{
	lcm_init();
    LCM_PRINT("[LCD] lcm_resume \n");
}

static void lcm_shutdown(void)
{
	reset_lcd_module(0);
	touch_reset_pin(0);

    MDELAY(3);
	lcm_dsv_vneg_ctrl(0);
	MDELAY(10);
    lcm_dsv_vpos_ctrl(0);
    MDELAY(3);

    LCM_PRINT("[LCD] lcm_shutdown \n");
}

static unsigned int lcm_compare_id(void)
{
	if( lge_get_board_revno() < HW_REV_A )
		return 0;

	if( is_lcm_name("TXD-FT8736") == 1 )
		return 1;
	else
		return 0;
}

struct LCM_DRIVER ft8736_hdplus_dsi_vdo_txd_drv = {
    .name = "TXD-FT8736",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params = lcm_get_params,
    .init = lcm_init,
    .suspend = lcm_suspend,
    .resume = lcm_resume,
    .shutdown = lcm_shutdown,
    .compare_id = lcm_compare_id,
};

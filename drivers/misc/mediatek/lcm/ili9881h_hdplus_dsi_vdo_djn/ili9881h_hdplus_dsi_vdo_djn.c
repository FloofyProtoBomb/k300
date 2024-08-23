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
#include <linux/input/lge_touch_notify_oos.h>
#include <soc/mediatek/lge/board_lge.h>
#include "mt6370_pmu_dsv.h"
#include "lcd_bias.h"
#include "disp_recovery.h"

#include "ddp_path.h"
#include "ddp_hal.h"
extern int ddp_dsi_power_on(enum DISP_MODULE_ENUM module, void *cmdq_handle);
extern int ddp_dsi_power_off(enum DISP_MODULE_ENUM module, void *cmdq_handle);

#include <linux/lcd_power_mode.h>

#define FRAME_WIDTH              (720)
#define FRAME_HEIGHT             (1440)
#define LCM_DENSITY              (320)

#define PHYSICAL_WIDTH            (65)
#define PHYSICAL_HEIGHT           (130)

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

static bool flag_deep_sleep_ctrl_available = false;
static bool flag_is_panel_deep_sleep = false;

struct LCM_setting_table {
    unsigned char cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3[] = {
	{0x29, 0xFF, 3,	{0x98, 0x81, 0x01}},
	{0x15, 0x2D, 1,	{0x60}},
	{0x15, 0x00, 1,	{0x48}},
	{0x15, 0x01, 1,	{0x33}},
	{0x15, 0x02, 1,	{0x97}},
	{0x15, 0x03, 1,	{0x05}},
	{0x15, 0x08, 1,	{0x86}},
	{0x15, 0x09, 1,	{0x01}},
	{0x15, 0x0a, 1,	{0x73}},
	{0x15, 0x0b, 1,	{0x00}},
	{0x15, 0x0c, 1,	{0xff}},
	{0x15, 0x0d, 1,	{0xff}},
	{0x15, 0x0e, 1,	{0x05}},
	{0x15, 0x0f, 1,	{0x05}},
	{0x15, 0x12, 1,	{0x06}},
	{0x15, 0x28, 1,	{0x48}},
	{0x15, 0x29, 1,	{0x86}},
	{0x15, 0x31, 1,	{0x07}},
	{0x15, 0x32, 1,	{0x02}},
	{0x15, 0x33, 1,	{0x00}},
	{0x15, 0x34, 1,	{0x15}},
	{0x15, 0x35, 1,	{0x17}},
	{0x15, 0x36, 1,	{0x11}},
	{0x15, 0x37, 1,	{0x13}},
	{0x15, 0x38, 1,	{0x2a}},
	{0x15, 0x39, 1,	{0x06}},
	{0x15, 0x3a, 1,	{0x01}},
	{0x15, 0x3b, 1,	{0x09}},
	{0x15, 0x3c, 1,	{0x0b}},
	{0x15, 0x3d, 1,	{0x07}},
	{0x15, 0x3e, 1,	{0x07}},
	{0x15, 0x3f, 1,	{0x07}},
	{0x15, 0x40, 1,	{0x07}},
	{0x15, 0x41, 1,	{0x07}},
	{0x15, 0x42, 1,	{0x07}},
	{0x15, 0x43, 1,	{0x07}},
	{0x15, 0x44, 1,	{0x07}},
	{0x15, 0x45, 1,	{0x07}},
	{0x15, 0x46, 1,	{0x07}},
	{0x15, 0x47, 1,	{0x07}},
	{0x15, 0x48, 1,	{0x02}},
	{0x15, 0x49, 1,	{0x00}},
	{0x15, 0x4a, 1,	{0x14}},
	{0x15, 0x4b, 1,	{0x16}},
	{0x15, 0x4c, 1,	{0x10}},
	{0x15, 0x4d, 1,	{0x12}},
	{0x15, 0x4e, 1,	{0x2a}},
	{0x15, 0x4f, 1,	{0x06}},
	{0x15, 0x50, 1,	{0x01}},
	{0x15, 0x51, 1,	{0x08}},
	{0x15, 0x52, 1,	{0x0a}},
	{0x15, 0x53, 1,	{0x07}},
	{0x15, 0x54, 1,	{0x07}},
	{0x15, 0x55, 1,	{0x07}},
	{0x15, 0x56, 1,	{0x07}},
	{0x15, 0x57, 1,	{0x07}},
	{0x15, 0x58, 1,	{0x07}},
	{0x15, 0x59, 1,	{0x07}},
	{0x15, 0x5a, 1,	{0x07}},
	{0x15, 0x5b, 1,	{0x07}},
	{0x15, 0x5c, 1,	{0x07}},
	{0x15, 0x61, 1,	{0x07}},
	{0x15, 0x62, 1,	{0x02}},
	{0x15, 0x63, 1,	{0x00}},
	{0x15, 0x64, 1,	{0x12}},
	{0x15, 0x65, 1,	{0x10}},
	{0x15, 0x66, 1,	{0x16}},
	{0x15, 0x67, 1,	{0x14}},
	{0x15, 0x68, 1,	{0x2a}},
	{0x15, 0x69, 1,	{0x06}},
	{0x15, 0x6a, 1,	{0x01}},
	{0x15, 0x6b, 1,	{0x0a}},
	{0x15, 0x6c, 1,	{0x08}},
	{0x15, 0x6d, 1,	{0x07}},
	{0x15, 0x6e, 1,	{0x07}},
	{0x15, 0x6f, 1,	{0x07}},
	{0x15, 0x70, 1,	{0x07}},
	{0x15, 0x71, 1,	{0x07}},
	{0x15, 0x72, 1,	{0x07}},
	{0x15, 0x73, 1,	{0x07}},
	{0x15, 0x74, 1,	{0x07}},
	{0x15, 0x75, 1,	{0x07}},
	{0x15, 0x76, 1,	{0x07}},
	{0x15, 0x77, 1,	{0x07}},
	{0x15, 0x78, 1,	{0x02}},
	{0x15, 0x79, 1,	{0x00}},
	{0x15, 0x7a, 1,	{0x13}},
	{0x15, 0x7b, 1,	{0x11}},
	{0x15, 0x7c, 1,	{0x17}},
	{0x15, 0x7d, 1,	{0x15}},
	{0x15, 0x7e, 1,	{0x2a}},
	{0x15, 0x7f, 1,	{0x06}},
	{0x15, 0x80, 1,	{0x01}},
	{0x15, 0x81, 1,	{0x0b}},
	{0x15, 0x82, 1,	{0x09}},
	{0x15, 0x83, 1,	{0x07}},
	{0x15, 0x84, 1,	{0x07}},
	{0x15, 0x85, 1,	{0x07}},
	{0x15, 0x86, 1,	{0x07}},
	{0x15, 0x87, 1,	{0x07}},
	{0x15, 0x88, 1,	{0x07}},
	{0x15, 0x89, 1,	{0x07}},
	{0x15, 0x8a, 1,	{0x07}},
	{0x15, 0x8b, 1,	{0x07}},
	{0x15, 0x8c, 1,	{0x07}},
	{0x15, 0xa0, 1,	{0x01}},
	{0x15, 0xa1, 1,	{0x82}},
	{0x15, 0xa2, 1,	{0x25}},
	{0x15, 0xa7, 1,	{0x82}},
	{0x15, 0xa8, 1,	{0x82}},
	{0x15, 0xa9, 1,	{0x25}},
	{0x15, 0xaa, 1,	{0x25}},
	{0x15, 0xb0, 1,	{0x34}},
	{0x15, 0xb2, 1,	{0x04}},
	{0x15, 0xb9, 1,	{0x00}},
	{0x15, 0xba, 1,	{0x01}},
	{0x15, 0xc1, 1,	{0x10}},
	{0x15, 0xd0, 1,	{0x01}},
	{0x15, 0xd1, 1,	{0x00}},
	{0x15, 0xe2, 1,	{0x00}},
	{0x15, 0xe6, 1,	{0x22}},
	{0x15, 0xea, 1,	{0x00}},
	{0x15, 0xFC, 1,	{0x09}},
	{0x29, 0xFF, 3,	{0x98, 0x81, 0x02}},
	{0x15, 0x5B, 1,	{0x00}},
	{0x15, 0x0B, 1,	{0x00}},
	{0x15, 0x4D, 1,	{0x4E}},
	{0x15, 0x4E, 1,	{0x00}},

	{0x29, 0xFF, 3,	{0x98, 0x81, 0x05}},
	{0x15, 0x63, 1,	{0x97}},
	{0x15, 0x64, 1,	{0x97}},
	{0x15, 0x68, 1,	{0x65}},
	{0x15, 0x69, 1,	{0x7F}},
	{0x15, 0x6A, 1,	{0xC9}},
	{0x15, 0x6B, 1,	{0xCF}},
	{0x29, 0xFF, 3,	{0x98, 0x81, 0x08}},
	{0x29, 0xE0, 27,{0x00,0x24,0x4D,0x6F,0xA0,0x50,0xCC,0xF1,0x21,0x48,0x95,0x89,0xBF,0xF2,0x22,0xAA,0x54,0x8E,0xB3,0xE0,0xFF,0x06,0x36,0x6F,0x9F,0x03,0xEC}},
	{0x29, 0xE1, 27,{0x00,0x24,0x4D,0x6F,0xA0,0x50,0xCC,0xF1,0x21,0x48,0x95,0x89,0xBF,0xF2,0x22,0xAA,0x54,0x8E,0xB3,0xE0,0xFF,0x06,0x36,0x6F,0x9F,0x03,0xEC}},
	{0x29, 0xFF, 3,	{0x98, 0x81, 0x06}},
	{0x15, 0xC2, 1,	{0x04}},
	{0x15, 0xD6, 1,	{0x85}},
	{0x15, 0xD2, 1,	{0x01}},
	{0x15, 0xD1, 1,	{0x01}},
	{0x15, 0xD0, 1,	{0x4A}},
	{0x15, 0x11, 1,	{0x03}},
	{0x15, 0x13, 1,	{0x45}},
	{0x15, 0x14, 1,	{0x41}},
	{0x15, 0x15, 1,	{0xF1}},
	{0x15, 0x16, 1,	{0x40}},
	{0x15, 0x17, 1,	{0x48}},
	{0x15, 0x18, 1,	{0x3B}},
	{0x29, 0xFF, 3,	{0x98, 0x81, 0x0E}},
	{0x15, 0x11, 1,	{0x90}},//++
	{0x15, 0x13, 1,	{0x0A}},
	{0x15, 0x00, 1,	{0xA0}},

	{0x29, 0xFF, 3,	{0x98, 0x81, 0x04}},
	{0x15, 0x02, 1,	{0x41}},	//CE AXIS EN
	{0x15, 0x0A, 1,	{0x06}},	//SE_RATIO_00
	{0x15, 0x0B, 1,	{0x06}},	//SE_RATIO_01
	{0x15, 0x0C, 1,	{0x07}},	//SE_RATIO_02
	{0x15, 0x0D, 1,	{0x07}},	//SE_RATIO_03
	{0x15, 0x0E, 1,	{0x06}},	//SE_RATIO_04
	{0x15, 0x0F, 1,	{0x06}},	//SE_RATIO_05
	{0x15, 0x10, 1,	{0x07}},	//SE_RATIO_06
	{0x15, 0x11, 1,	{0x07}},	//SE_RATIO_07
	{0x15, 0x12, 1,	{0x07}},	//SE_RATIO_08
	{0x15, 0x13, 1,	{0x07}},	//SE_RATIO_09
	{0x15, 0x14, 1,	{0x07}},	//SE_RATIO_10
	{0x15, 0x15, 1,	{0x07}},	//SE_RATIO_11
	{0x15, 0x16, 1,	{0x07}},	//SE_RATIO_12
	{0x15, 0x17, 1,	{0x07}},	//SE_RATIO_13
	{0x15, 0x18, 1,	{0x07}},	//SE_RATIO_14
	{0x15, 0x19, 1,	{0x07}},	//SE_RATIO_15
	{0x15, 0x1A, 1,	{0x07}},	//SE_RATIO_16
	{0x15, 0x1B, 1,	{0x07}},	//SE_RATIO_17
	{0x15, 0x1C, 1,	{0x07}},	//SE_RATIO_18
	{0x15, 0x1D, 1,	{0x07}},	//SE_RATIO_19
	{0x15, 0x1E, 1,	{0x07}},	//SE_RATIO_20
	{0x15, 0x1F, 1,	{0x07}},	//SE_RATIO_21
	{0x15, 0x20, 1,	{0x07}},	//SE_RATIO_22
	{0x15, 0x21, 1,	{0x07}},	//SE_RATIO_23

    {0x29, 0xFF, 3,	{0x98, 0x81, 0x00}},
    //Open TE
    {0x15, 0x35,1,{0x00}},
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Exit_Sleep[] = {
    {0x05, 0x11, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Disp_On[] = {
    {0x05, 0x29, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Display_Off[] = {
    {0x05, 0x28, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initialization_setting_V3_Enter_Sleep[] = {
    {0x05, 0x10, 1, {0x00}},
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

    //The following defined the fomat for data coming from LCD engine.
    params->dsi.mode                       = SYNC_PULSE_VDO_MODE;   //SYNC_PULSE_VDO_MODE;//BURST_VDO_MODE;
    params->dsi.data_format.format         = LCM_DSI_FORMAT_RGB888;
    params->dsi.PS                         = LCM_PACKED_PS_24BIT_RGB888;
    params->type                           = LCM_TYPE_DSI;
    params->width                          = FRAME_WIDTH;
    params->height                         = FRAME_HEIGHT;

    // physical size
    params->physical_width = PHYSICAL_WIDTH;
    params->physical_height = PHYSICAL_HEIGHT;
    params->density = LCM_DENSITY;

	// DSI
	/* Command mode setting */
	params->dsi.LANE_NUM                    = LCM_FOUR_LANE;
	params->dsi.vertical_sync_active = 4;
	params->dsi.vertical_backporch = 48;
	params->dsi.vertical_frontporch = 216;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active				= 4;
	params->dsi.horizontal_backporch				= 72;
	params->dsi.horizontal_frontporch				= 62;
	params->dsi.horizontal_active_pixel 			= FRAME_WIDTH;

	params->dsi.PLL_CLOCK = 280;

    // Non-continuous clock
	params->dsi.cont_clock = 0;
	params->dsi.ssc_disable	= 1;

	params->dsi.HS_TRAIL = 6;
	params->dsi.HS_PRPR = 6;

#if defined(CONFIG_LGE_DISPLAY_ESD_RECOVERY)
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;

       params->dsi.lcm_esd_check_table[0].cmd = 0x09;
       params->dsi.lcm_esd_check_table[0].count = 3;
       params->dsi.lcm_esd_check_table[0].para_list[0] = 0x80;
       params->dsi.lcm_esd_check_table[0].para_list[1] = 0x03;
       params->dsi.lcm_esd_check_table[0].para_list[2] = 0x06;

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

    dsi_set_cmdq_V3(lcm_initialization_setting_V3_Exit_Sleep, sizeof(lcm_initialization_setting_V3_Exit_Sleep) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(80);

    dsi_set_cmdq_V3(lcm_initialization_setting_V3_Disp_On, sizeof(lcm_initialization_setting_V3_Disp_On) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(20);

    LCM_PRINT("[LCD] %s : -\n", __func__);
}

static void init_lcm_registers_sleep(void)
{
    LCM_PRINT("[LCD] %s : +\n", __func__);

    dsi_set_cmdq_V3(lcm_initialization_setting_V3_Display_Off, sizeof(lcm_initialization_setting_V3_Display_Off) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(20);

    dsi_set_cmdq_V3(lcm_initialization_setting_V3_Enter_Sleep, sizeof(lcm_initialization_setting_V3_Enter_Sleep) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(80);

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

static void notify_touch_driver(unsigned int mode)
{
    if(mode == 1)
      touch_notifier_call_chain(LCD_EVENT_TOUCH_RESET_END, NULL);
    else
      touch_notifier_call_chain(LCD_EVENT_TOUCH_RESET_START, NULL);

    LCM_PRINT("Touch Reset %s \n",(mode)? "High":"Low");
}

static void lcm_touch_reset_pin(unsigned int mode)
{
    if(mode)
      lcd_bias_set_gpio_ctrl(TOUCH_RST, 1);
    else
      lcd_bias_set_gpio_ctrl(TOUCH_RST, 0);

    LCM_PRINT("[LCD] Touch Reset %s \n",(mode)? "High":"Low");
}

static void lcm_init(void)
{
    LCM_PRINT("[LCD] %s : +\n", __func__);

    notify_touch_driver(0);
    reset_lcd_module(0);
    MDELAY(15);

	lcm_dsv_vpos_ctrl(1);
	MDELAY(1);
	lcm_dsv_vneg_ctrl(1);
	lcd_bias_set_vspn(6000);
	MDELAY(1);

    reset_lcd_module(1);
    MDELAY(1);
    notify_touch_driver(1);
    MDELAY(120);

    dsi_set_cmdq_V3(lcm_initialization_setting_V3, sizeof(lcm_initialization_setting_V3) / sizeof(struct LCM_setting_table_V3), 1);

    init_lcm_registers();

    LCM_PRINT("[LCD] %s : -\n", __func__);
}

static void lcm_suspend(void)
{
    init_lcm_registers_sleep();
    flag_deep_sleep_ctrl_available = true;

    LCM_PRINT("[LCD] lcm_suspend \n");
}

static void lcm_resume(void)
{
    flag_deep_sleep_ctrl_available = false;

    if(flag_is_panel_deep_sleep) {
      lcm_init();
      flag_is_panel_deep_sleep = false;

      LCM_PRINT("[LCD] %s : deep sleep mode state. call lcm_init(). \n", __func__);
    } else {
      notify_touch_driver(0);
      reset_lcd_module(0);
      MDELAY(15);

      reset_lcd_module(1);
      MDELAY(1);
      notify_touch_driver(1);
      MDELAY(120);

      dsi_set_cmdq_V3(lcm_initialization_setting_V3, sizeof(lcm_initialization_setting_V3) / sizeof(struct LCM_setting_table_V3), 1);
      init_lcm_registers();

      LCM_PRINT("[LCD] %s\n", __func__);
    }
}

static void lcm_shutdown(void)
{
	UDELAY(10000);
	lcm_touch_reset_pin(0);
	UDELAY(10000);
	reset_lcd_module(0);
	UDELAY(10000);
	lcd_bias_power_off_vspn();
	lcm_dsv_vneg_ctrl(0);
	UDELAY(10000);
	lcm_dsv_vpos_ctrl(0);

    flag_is_panel_deep_sleep = true;

    LCM_PRINT("[LCD] %s\n", __func__);
}

static unsigned int lcm_compare_id(void)
{
    if(is_lcm_name("DJN-ILI9881H") == 1)
      return 1;
    else
      return 0;
}

static void lcm_enter_deep_sleep(void)
{
    if(flag_deep_sleep_ctrl_available) {
      lcd_bias_power_off_vspn();
      lcm_dsv_vneg_ctrl(0);
      MDELAY(2);
      lcm_dsv_vpos_ctrl(0);

      flag_is_panel_deep_sleep = true;

      LCM_PRINT("[LCD] %s\n", __func__);
    }
}

static void lcm_exit_deep_sleep(void)
{
    if(flag_deep_sleep_ctrl_available) {
      ddp_path_top_clock_on();
      ddp_dsi_power_on(DISP_MODULE_DSI0, NULL);
      MDELAY(12);

      lcm_dsv_vpos_ctrl(1);
      MDELAY(5);
      lcm_dsv_vneg_ctrl(1);

      lcd_bias_set_vspn(6000);
      MDELAY(14);

      lcm_touch_reset_pin(0);
      MDELAY(2);
      reset_lcd_module(0);
      MDELAY(2);
      reset_lcd_module(1);
      MDELAY(1);
      lcm_touch_reset_pin(1);
      MDELAY(10);

      dsi_set_cmdq_V3(lcm_initialization_setting_V3, sizeof(lcm_initialization_setting_V3) / sizeof(struct LCM_setting_table_V3), 1);

      ddp_dsi_power_off(DISP_MODULE_DSI0, NULL);
      ddp_path_top_clock_off();

      flag_is_panel_deep_sleep = false;

      LCM_PRINT("[LCD] %s\n", __func__);
    }
}

static void lcm_set_deep_sleep(unsigned int mode)
{
    switch (mode){
      case DEEP_SLEEP_ENTER:
        lcm_enter_deep_sleep();
        break;
      case DEEP_SLEEP_EXIT:
        lcm_exit_deep_sleep();
        break;
      default :
        break;
    }

    LCM_PRINT("[LCD] %s : %d \n", __func__,  mode);
}

struct LCM_DRIVER ili9881h_hdplus_dsi_vdo_djn_drv = {
    .name = "DJN-ILI9881H",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params = lcm_get_params,
    .init = lcm_init,
    .suspend = lcm_suspend,
    .resume = lcm_resume,
    .shutdown = lcm_shutdown,
    .compare_id = lcm_compare_id,
    .set_deep_sleep = lcm_set_deep_sleep,
};

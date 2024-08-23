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

#if defined(BUILD_LK)
#define LCM_PRINT printf
#elif defined(BUILD_UBOOT)
#define LCM_PRINT printf
#else
#define LCM_PRINT printk
#endif

#define FRAME_WIDTH              (720)
#define FRAME_HEIGHT             (1440)

#define PHYSICAL_WIDTH           (65)
#define PHYSICAL_HEIGHT          (130)

static struct LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)        (lcm_util.set_reset_pin((v)))
#define UDELAY(n)               (lcm_util.udelay(n))
#define MDELAY(n)               (lcm_util.mdelay(n))

#define ENABLE  1
#define DISABLE 0

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

static struct LCM_setting_table_V3 lcm_initial_command[] = {
    {0x39, 0xB9, 3,   {0x83, 0x10, 0x2D}},
    {0x39, 0xB1, 11,  {0x20, 0x44, 0x31, 0x31, 0x22, 0x77, 0x2F, 0x57, 0x18, 0x18, 0x18}},
    {0x39, 0xB2, 13,  {0x00, 0x00, 0x05, 0xA0, 0x00, 0x08, 0x24, 0x4B, 0x00, 0x00, 0x00, 0x00, 0x14}},
    {0x39, 0xB4, 14,  {0x01, 0x55, 0x01, 0x55, 0x01, 0x55, 0x01, 0x55, 0x03, 0xFF, 0x01, 0x20, 0x00, 0xFF}},
    {0x15, 0xCC, 1,   {0x02}},
    {0x39, 0xD3, 25,  {0x77, 0x00, 0x3C, 0x03, 0x00, 0x08, 0x00, 0x37, 0x00, 0x33, 0x33, 0x02, 0x02, 0x00, 0x00, 0x32,
                       0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0x39, 0xD5, 44,  {0x18, 0x18, 0x3A, 0x3A, 0x18, 0x18, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x18, 0x18,
                       0x3B, 0x3B, 0x19, 0x19, 0x20, 0x21, 0x22, 0x23, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18}},
    {0x39, 0xD6, 44,  {0x18, 0x18, 0x3A, 0x3A, 0x19, 0x19, 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x18, 0x18,
                       0x3B, 0x3B, 0x18, 0x18, 0x23, 0x22, 0x21, 0x20, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18}},
    {0x39, 0xE7, 23,  {0xFF, 0x0F, 0x00, 0x00, 0x20, 0x00, 0x0E, 0x0E, 0x20, 0x17, 0x1A, 0x93, 0x00, 0x93, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x17, 0x10, 0x68}},
    {0x15, 0xBD, 1,   {0x01}},
    {0x39, 0xE7, 7,   {0x02, 0x3C, 0x01, 0x6E, 0x0D, 0x4A, 0x0E}},
    {0x15, 0xBD, 1,   {0x02}},
    {0x39, 0xE7, 24,  {0xFF, 0x04, 0x00, 0x00, 0x02, 0x00, 0x25, 0x00, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0x15, 0xBD, 1,   {0x00}},
    {0x39, 0xB6, 3,   {0xBF, 0xBF, 0xE0}},
    {0x39, 0xE0, 46,  {0x00, 0x02, 0x07, 0x0B, 0x0E, 0x12, 0x29, 0x31, 0x3B, 0x3C, 0x60, 0x6E, 0x7B, 0x91, 0x95, 0xA3,
                       0xAF, 0xC5, 0xC8, 0x64, 0x6A, 0x78, 0x7F, 0x00, 0x02, 0x07, 0x0B, 0x0E, 0x12, 0x29, 0x31, 0x3B,
                       0x3C, 0x60, 0x6E, 0x7B, 0x91, 0x95, 0xA3, 0xAF, 0xC5, 0xC8, 0x64, 0x6A, 0x78, 0x7F}},
    {0x39, 0xB0, 2,   {0x00, 0x20}},
    {0x39, 0xBA, 19,  {0x70, 0x23, 0xA8, 0x8B, 0xB2, 0x80, 0x80, 0x01, 0x10, 0x00, 0x00, 0x00, 0x08, 0x3D, 0x82, 0x77,
                       0x04, 0x01, 0x00}},
    {0x39, 0xBF, 7,   {0xFC, 0x00, 0x24, 0x9E, 0xF6, 0x00, 0x45}},
    {0x39, 0xCB, 5,   {0x00, 0x53, 0x00, 0x02, 0xDA}},
    {0x15, 0xBD, 1,   {0x01}},
    {0x15, 0xCB, 1,   {0x01}},
    {0x15, 0xBD, 1,   {0x02}},
    {0x39, 0xB4, 8,   {0xC2, 0x00, 0x33, 0x00, 0x33, 0x88, 0xB3, 0x00}},
    {0x39, 0xB1, 3,   {0x7F, 0x07, 0xFF}},
    {0x15, 0xBD, 1,   {0x00}},
};

/* Sleep Out Set */
static struct  LCM_setting_table_V3 lcm_initial_sleep_out[] = {
    {0x05, 0x11, 1, {0x00}},
};

/* Display On Set */
static struct  LCM_setting_table_V3 lcm_initial_disp_on[] = {
    {0x05, 0x29, 1, {0x00}},
};

/* Sleep In Set */
static struct  LCM_setting_table_V3 lcm_initial_sleep_in[] = {
    {0x05, 0x10, 1, {0x00}},
};

/* Display Off Set */
static struct  LCM_setting_table_V3 lcm_initial_display_off[] = {
    {0x05, 0x28, 1, {0x00}},
};

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
    memcpy((void*)&lcm_util, (void*)util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS * params)
{
    memset(params, 0, sizeof(struct LCM_PARAMS));

    params->type = LCM_TYPE_DSI;

    params->width = FRAME_WIDTH;
    params->height = FRAME_HEIGHT;

    /* physical size */
    params->physical_width = PHYSICAL_WIDTH;
    params->physical_height = PHYSICAL_HEIGHT;

    params->dsi.mode = SYNC_PULSE_VDO_MODE;
    /* enable tearing-free */
    params->dbi.te_mode = LCM_DBI_TE_MODE_DISABLED;
    params->dbi.te_edge_polarity = LCM_POLARITY_RISING;

    /* DSI */
    params->dsi.LANE_NUM = LCM_FOUR_LANE;
    /* The following defined the fomat for data coming from LCD engine. */
    params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
    params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
    params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
    params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

    /* Highly depends on LCD driver capability. */
    params->dsi.packet_size = 256;

    params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;
    params->dsi.cont_clock = 0;

    params->dsi.vertical_sync_active = 2;
    params->dsi.vertical_backporch = 6;
    params->dsi.vertical_frontporch = 36;
    params->dsi.vertical_active_line = FRAME_HEIGHT;

    params->dsi.horizontal_sync_active = 12;
    params->dsi.horizontal_backporch = 65;
    params->dsi.horizontal_frontporch = 65;
    params->dsi.horizontal_active_pixel = FRAME_WIDTH;

    params->dsi.PLL_CLOCK = 244;
    params->dsi.ssc_disable = 1;

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

static void init_lcm_registers_sleep_in(void)
{
    dsi_set_cmdq_V3(lcm_initial_display_off, sizeof(lcm_initial_display_off) / sizeof(struct LCM_setting_table_V3), 1);
    dsi_set_cmdq_V3(lcm_initial_sleep_in, sizeof(lcm_initial_sleep_in) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(40);

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void init_lcm_registers_sleep_out(void)
{
    dsi_set_cmdq_V3(lcm_initial_command,sizeof(lcm_initial_command) / sizeof(struct LCM_setting_table_V3), 1);
    dsi_set_cmdq_V3(lcm_initial_sleep_out, sizeof(lcm_initial_sleep_out) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(120);

    dsi_set_cmdq_V3(lcm_initial_disp_on, sizeof(lcm_initial_disp_on) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(10);

    LCM_PRINT("[LCD] %s\n",__func__);
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
    if(mode)
        touch_notifier_call_chain(LCD_EVENT_TOUCH_RESET_END, NULL);
    else
        touch_notifier_call_chain(LCD_EVENT_TOUCH_RESET_START, NULL);

    LCM_PRINT("Touch Reset %s \n",(mode)? "High":"Low");
}

static void lcm_suspend_mfts(void)
{
    LCM_PRINT("[LCD] %s : do nothing \n",__func__);
}

static void lcm_suspend(void)
{
    init_lcm_registers_sleep_in();

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void lcm_resume_mfts(void)
{
    LCM_PRINT("[LCD] %s : do nothing \n",__func__);
}

static void lcm_resume(void)
{
    MDELAY(10);

    touch_reset_pin(DISABLE);
    reset_lcd_module(DISABLE);
    MDELAY(1);

    reset_lcd_module(ENABLE);
    touch_reset_pin(ENABLE);
    MDELAY(50);

    init_lcm_registers_sleep_out();

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void lcm_shutdown(void)
{
    MDELAY(50);

    touch_reset_pin(DISABLE);
    reset_lcd_module(DISABLE);
    MDELAY(1);

    lcm_dsv_vneg_ctrl(DISABLE);
    lcm_dsv_vpos_ctrl(DISABLE);
    MDELAY(1);

    LCM_PRINT("[LCD] %s\n",__func__);
}

static unsigned int lcm_compare_id(void)
{
    if( lge_get_maker_id() == LCE_HX83102D )
        return 1;
    else
        return 0;
}

struct LCM_DRIVER hx83102d_hdplus_dsi_vdo_lce_drv = {
    .name = "LCE-HX83102D",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params = lcm_get_params,
    .suspend = lcm_suspend,
    .resume = lcm_resume,
    .suspend_mfts = lcm_suspend_mfts,
    .resume_mfts = lcm_resume_mfts,
    .shutdown = lcm_shutdown,
    .compare_id = lcm_compare_id,
};

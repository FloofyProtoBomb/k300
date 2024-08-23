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

#include "ddp_hal.h"
#include "ddp_path.h"
#include "lcd_bias.h"
#include "disp_recovery.h"
#include <linux/lcd_power_mode.h>
#include <linux/lge_panel_notify.h>
#include <soc/mediatek/lge/board_lge.h>

/* --------------------------------------------------------------------------- */
/* Local Constants */
/* --------------------------------------------------------------------------- */

/* pixel */
#define FRAME_WIDTH             (720)
#define FRAME_HEIGHT            (1520)
#define LCM_DENSITY             (320)

/* physical dimension */
#define PHYSICAL_WIDTH          (68)
#define PHYSICAL_HEIGHT         (144)

#define ENABLE                  1
#define DISABLE                 0

#define DSV_VOLTAGE             6000
#define USE_DEEP_SLEEP          DISABLE
#define USE_FUNCTION            DISABLE

/* --------------------------------------------------------------------------- */
/* Local Variables */
/* --------------------------------------------------------------------------- */

static struct LCM_UTIL_FUNCS lcm_util = { 0 };

#if USE_DEEP_SLEEP
static bool panel_suspend_state = false;
static bool panel_deep_sleep_state = false;
#endif

#define SET_RESET_PIN(v)        (lcm_util.set_reset_pin((v)))
#define UDELAY(n)               (lcm_util.udelay(n))
#define MDELAY(n)               (lcm_util.mdelay(n))

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

/* --------------------------------------------------------------------------- */
/* External Functions */
/* --------------------------------------------------------------------------- */

extern int ddp_dsi_power_on(enum DISP_MODULE_ENUM module, void *cmdq_handle);
extern int ddp_dsi_power_off(enum DISP_MODULE_ENUM module, void *cmdq_handle);

/* --------------------------------------------------------------------------- */
/* GPIO Set */
/* --------------------------------------------------------------------------- */

//dir out : mode 00 dir in : mode 01

#define LGE_LPWG_SUPPORT

static struct LCM_setting_table_V3 lcm_initial_setting[] = {
    // Page0
    {0x15, 0xE0, 1, {0x00}},

    // PASSWORD
    {0x15, 0xE1, 1, {0x93}},
    {0x15, 0xE2, 1, {0x65}},
    {0x15, 0xE3, 1, {0xF8}},
    {0x15, 0x80, 1, {0x03}},     	//DSI 4 lane

    // Page1
    {0x15, 0xE0, 1, {0x01}},

    // Set VCOM
    {0x15, 0x00, 1, {0x00}},
    {0x15, 0x01, 1, {0x94}},

    // Set VCOM_Reverse
    {0x15, 0x03, 1, {0x00}},
    {0x15, 0x04, 1, {0xA1}},

    // Set Gamma Power, VGMP,VGMN,VGSP,VGSN
    {0x15, 0x17, 1, {0x10}},
    {0x15, 0x18, 1, {0x0F}},     	//VGMP +5.5V
    {0x15, 0x19, 1, {0x00}},
    {0x15, 0x1A, 1, {0x10}},
    {0x15, 0x1B, 1, {0x0F}},     	//VGMN -5.5V
    {0x15, 0x1C, 1, {0x00}},

    // Set Gate Power
    {0x15, 0x24, 1, {0xFE}},

    // Set PowerCtrl2
    {0x15, 0x25, 1, {0x20}},     	//AP=010
    {0x15, 0x27, 1, {0x22}},
    {0x15, 0x35, 1, {0x22}},

    // SetPanel
    {0x15, 0x37, 1, {0x09}},     	//SS=1,BGR=1

    // SET RGBCYC
    {0x15, 0x38, 1, {0x04}},     	//JDT=100 column inversion
    {0x15, 0x39, 1, {0x00}},     	//RGB_N_EQ1, modify 20140806
    {0x15, 0x3A, 1, {0x01}},     	//RGB_N_EQ2, modify 20140806
    {0x15, 0x3C, 1, {0x64}},     	//SET EQ3 for TE_H
    {0x15, 0x3D, 1, {0xFF}},
    {0x15, 0x3E, 1, {0xFF}},
    {0x15, 0x3F, 1, {0xFF}},

    // Set TCON
    {0x15, 0x40, 1, {0x04}},     	//RSO=720RGB
    {0x15, 0x41, 1, {0xBE}},     	//LN=720->1520 line
    {0x15, 0x42, 1, {0x6B}},     	//SLT
    {0x15, 0x43, 1, {0x08}},     	//VFP
    {0x15, 0x44, 1, {0x11}},     	//VBP
    {0x15, 0x45, 1, {0x28}},     	//HBP
    {0x15, 0x4B, 1, {0x04}},

    // power voltage
    {0x15, 0x55, 1, {0x0F}},     	//DCDCM=0001(JD5001)
    {0x15, 0x56, 1, {0x01}},
    {0x15, 0x57, 1, {0x4D}},     	//[7:5]VGH_RT,[4:2]=VGL_RT,[1:0]=VCL_RT
    {0x15, 0x58, 1, {0x05}},     	//AVDD_S AVDD +6V
    {0x15, 0x59, 1, {0x05}},     	//VCL -2.5V AVEE -6V
    {0x15, 0x5A, 1, {0x19}},     	//VGH +17V
    {0x15, 0x5B, 1, {0x24}},     	//VGL -11V
    {0x15, 0x5C, 1, {0x15}},

    // Gamma
    {0x15, 0x5D, 1, {0x78}},
    {0x15, 0x5E, 1, {0x66}},
    {0x15, 0x5F, 1, {0x58}},
    {0x15, 0x60, 1, {0x4D}},
    {0x15, 0x61, 1, {0x4A}},
    {0x15, 0x62, 1, {0x3C}},
    {0x15, 0x63, 1, {0x40}},
    {0x15, 0x64, 1, {0x2A}},
    {0x15, 0x65, 1, {0x43}},
    {0x15, 0x66, 1, {0x41}},
    {0x15, 0x67, 1, {0x3F}},
    {0x15, 0x68, 1, {0x5B}},
    {0x15, 0x69, 1, {0x48}},
    {0x15, 0x6A, 1, {0x4F}},
    {0x15, 0x6B, 1, {0x41}},
    {0x15, 0x6C, 1, {0x3E}},
    {0x15, 0x6D, 1, {0x32}},
    {0x15, 0x6E, 1, {0x21}},
    {0x15, 0x6F, 1, {0x02}},
    {0x15, 0x70, 1, {0x78}},
    {0x15, 0x71, 1, {0x66}},
    {0x15, 0x72, 1, {0x58}},
    {0x15, 0x73, 1, {0x4D}},
    {0x15, 0x74, 1, {0x4A}},
    {0x15, 0x75, 1, {0x3C}},
    {0x15, 0x76, 1, {0x40}},
    {0x15, 0x77, 1, {0x2A}},
    {0x15, 0x78, 1, {0x43}},
    {0x15, 0x79, 1, {0x41}},
    {0x15, 0x7A, 1, {0x3F}},
    {0x15, 0x7B, 1, {0x5B}},
    {0x15, 0x7C, 1, {0x48}},
    {0x15, 0x7D, 1, {0x4F}},
    {0x15, 0x7E, 1, {0x41}},
    {0x15, 0x7F, 1, {0x3E}},
    {0x15, 0x80, 1, {0x32}},
    {0x15, 0x81, 1, {0x21}},
    {0x15, 0x82, 1, {0x02}},

    // Page2, for GIP
    {0x15, 0xE0, 1, {0x02}},

    // GIP_L Pin mapping
    {0x15, 0x00, 1, {0x41}},     	//BWO
    {0x15, 0x01, 1, {0x5F}},     	//RSTO
    {0x15, 0x02, 1, {0x5F}},     	//FWO
    {0x15, 0x03, 1, {0x5F}},    	//CK1O
    {0x15, 0x04, 1, {0x49}},     	//CK2O
    {0x15, 0x05, 1, {0x4B}},     	//CK1BO
    {0x15, 0x06, 1, {0x45}},     	//CK2BO
    {0x15, 0x07, 1, {0x47}},     	//VGL
    {0x15, 0x08, 1, {0x43}},     	//VGL
    {0x15, 0x09, 1, {0x5F}},     	//10 -> VGL
    {0x15, 0x0A, 1, {0x5F}},     	//11 -> VGL
    {0x15, 0x0B, 1, {0x35}},     	//12 -> VGL
    {0x15, 0x0C, 1, {0x5F}},     	//13 -> VGL
    {0x15, 0x0D, 1, {0x5F}},     	//14 -> VGL
    {0x15, 0x0E, 1, {0x5F}},     	//15 -> VGL
    {0x15, 0x0F, 1, {0x5F}},     	//STVO
    {0x15, 0x10, 1, {0x5F}},     	//17 -> VGL
    {0x15, 0x11, 1, {0x5F}},     	//18 -> VGL
    {0x15, 0x12, 1, {0x5F}},     	//19 -> VGL
    {0x15, 0x13, 1, {0x5F}},     	//20 -> VGL
    {0x15, 0x14, 1, {0x15}},     	//21 -> VGL
    {0x15, 0x15, 1, {0x5E}},     	//22 -> VGL

    // GIP_R Pin mapping
    {0x15, 0x16, 1, {0x40}},     	//BWE
    {0x15, 0x17, 1, {0x5F}},     	//RSTE
    {0x15, 0x18, 1, {0x5F}},     	//FWE
    {0x15, 0x19, 1, {0x5F}},    	//CK1E
    {0x15, 0x1A, 1, {0x48}},    	//CK2E
    {0x15, 0x1B, 1, {0x4A}},    	//CK1BE
    {0x15, 0x1C, 1, {0x44}},     	//CK2BE
    {0x15, 0x1D, 1, {0x46}},     	//VGLE
    {0x15, 0x1E, 1, {0x42}},    	//VGLE
    {0x15, 0x1F, 1, {0x5F}},     	//10 -> VGL
    {0x15, 0x20, 1, {0x5F}},     	//11 -> VGL
    {0x15, 0x21, 1, {0x35}},     	//12 -> VGL
    {0x15, 0x22, 1, {0x5F}},     	//13 -> VGL
    {0x15, 0x23, 1, {0x5F}},     	//14 -> VGL
    {0x15, 0x24, 1, {0x5F}},     	//15 -> VGL
    {0x15, 0x25, 1, {0x5F}},     	//STVE
    {0x15, 0x26, 1, {0x5F}},     	//17 -> VGL
    {0x15, 0x27, 1, {0x5F}},     	//18 -> VGL
    {0x15, 0x28, 1, {0x5F}},     	//19 -> VGL
    {0x15, 0x29, 1, {0x5F}},     	//20 -> VGL
    {0x15, 0x2A, 1, {0x15}},     	//21 -> VGL
    {0x15, 0x2B, 1, {0x5E}},     	//22 -> VGL

    // GIP_L_GS Pin mapping
    {0x15, 0x2C, 1, {0x02}},     	//BWO
    {0x15, 0x2D, 1, {0x1F}},     	//RSTO
    {0x15, 0x2E, 1, {0x1F}},     	//FWO
    {0x15, 0x2F, 1, {0x1F}},     	//CK1O
    {0x15, 0x30, 1, {0x06}},     	//CK2O
    {0x15, 0x31, 1, {0x04}},     	//CK1BO
    {0x15, 0x32, 1, {0x0A}},     	//CK2BO
    {0x15, 0x33, 1, {0x08}},     	//VGL
    {0x15, 0x34, 1, {0x00}},     	//VGL
    {0x15, 0x35, 1, {0x1F}},     	//10 -> VGL
    {0x15, 0x36, 1, {0x1E}},     	//11 -> VGL
    {0x15, 0x37, 1, {0x15}},     	//12 -> VGL
    {0x15, 0x38, 1, {0x1F}},     	//13 -> VGL
    {0x15, 0x39, 1, {0x1F}},     	//14 -> VGL
    {0x15, 0x3A, 1, {0x1F}},     	//15 -> VGL
    {0x15, 0x3B, 1, {0x1F}},     	//STVO
    {0x15, 0x3C, 1, {0x1F}},     	//17 -> VGL
    {0x15, 0x3D, 1, {0x1F}},     	//18 -> VGL
    {0x15, 0x3E, 1, {0x1F}},     	//19 -> VGL
    {0x15, 0x3F, 1, {0x1F}},    	//20 -> VGL
    {0x15, 0x40, 1, {0x15}},     	//21 -> VGL
    {0x15, 0x41, 1, {0x1F}},     	//22 -> VGL

    // GIP_R_GS Pin mapping
    {0x15, 0x42, 1, {0x03}},     	//BWE
    {0x15, 0x43, 1, {0x1F}},     	//RSTE
    {0x15, 0x44, 1, {0x1F}},     	//FWE
    {0x15, 0x45, 1, {0x1F}},     	//CK1E
    {0x15, 0x46, 1, {0x07}},     	//CK2E
    {0x15, 0x47, 1, {0x05}},     	//CK1BE
    {0x15, 0x48, 1, {0x0B}},     	//CK2BE
    {0x15, 0x49, 1, {0x09}},     	//VGLE
    {0x15, 0x4A, 1, {0x01}},     	//VGLE
    {0x15, 0x4B, 1, {0x1F}},     	//10 -> VGL
    {0x15, 0x4C, 1, {0x1E}},     	//11 -> VGL
    {0x15, 0x4D, 1, {0x15}},     	//12 -> VGL
    {0x15, 0x4E, 1, {0x1F}},     	//13 -> VGL
    {0x15, 0x4F, 1, {0x1F}},     	//14 -> VGL
    {0x15, 0x50, 1, {0x1F}},     	//15 -> VGL
    {0x15, 0x51, 1, {0x1F}},     	//STVE
    {0x15, 0x52, 1, {0x1F}},     	//17 -> VGL
    {0x15, 0x53, 1, {0x1F}},     	//18 -> VGL
    {0x15, 0x54, 1, {0x1F}},     	//19 -> VGL
    {0x15, 0x55, 1, {0x1F}},     	//20 -> VGL
    {0x15, 0x56, 1, {0x15}},     	//21 -> VGL
    {0x15, 0x57, 1, {0x1F}},     	//22 -> VGL

    // GIP Timing
    {0x15, 0x58, 1, {0x40}},
    {0x15, 0x59, 1, {0x00}},
    {0x15, 0x5A, 1, {0x00}},
    {0x15, 0x5B, 1, {0x30}},
    {0x15, 0x5C, 1, {0x09}},     	//STV_S0
    {0x15, 0x5D, 1, {0x30}},     	//STV_W
    {0x15, 0x5E, 1, {0x01}},
    {0x15, 0x5F, 1, {0x02}},
    {0x15, 0x60, 1, {0x00}},     	//ETV_W
    {0x15, 0x61, 1, {0x01}},
    {0x15, 0x62, 1, {0x02}},
    {0x15, 0x63, 1, {0x0C}},
    {0x15, 0x64, 1, {0x52}},     	//SETV_OFF
    {0x15, 0x65, 1, {0x00}},
    {0x15, 0x66, 1, {0x00}},     	//ETV_S0
    {0x15, 0x67, 1, {0x73}},     	//CKV_W
    {0x15, 0x68, 1, {0x0B}},     	//CKV_S0
    {0x15, 0x69, 1, {0x18}},     	//CKV_ON
    {0x15, 0x6A, 1, {0x52}},     	//CKV_OFF
    {0x15, 0x6B, 1, {0x08}},     	//dummy CLK

    {0x15, 0x6C, 1, {0x00}},
    {0x15, 0x6D, 1, {0x0C}},
    {0x15, 0x6E, 1, {0x04}},
    {0x15, 0x6F, 1, {0x48}},
    {0x15, 0x70, 1, {0x00}},
    {0x15, 0x71, 1, {0x00}},
    {0x15, 0x72, 1, {0x06}},
    {0x15, 0x73, 1, {0x7B}},
    {0x15, 0x74, 1, {0x00}},
    {0x15, 0x75, 1, {0x07}},
    {0x15, 0x76, 1, {0x00}},
    {0x15, 0x77, 1, {0xED}},
    {0x15, 0x78, 1, {0x17}},
    {0x15, 0x79, 1, {0x02}},
    {0x15, 0x7A, 1, {0x06}},
    {0x15, 0x7B, 1, {0x00}},
    {0x15, 0x7C, 1, {0x00}},
    {0x15, 0x7D, 1, {0x03}},
    {0x15, 0x7E, 1, {0x62}},
    {0x15, 0xE0, 1, {0x03}},
    {0x15, 0x9B, 1, {0x00}},

    // Page4
    {0x15, 0xE0, 1, {0x04}},
    {0x15, 0x29, 1, {0x36}},
    {0x15, 0x00, 1, {0x06}},
    {0x15, 0x03, 1, {0x4E}},
    {0x15, 0x04, 1, {0x01}},
    {0x15, 0x09, 1, {0x10}},
    {0x15, 0x0E, 1, {0x2A}},
    {0x15, 0x0F, 1, {0x87}},
    {0x15, 0x96, 1, {0x0A}},     	//CABC 10bits option
    {0x15, 0x97, 1, {0x02}},
    {0x15, 0x98, 1, {0x12}},
    {0x15, 0xA3, 1, {0x06}},
    {0x15, 0xA9, 1, {0x01}},
    {0x15, 0xAA, 1, {0xE0}},
    {0x15, 0xE0, 1, {0x05}},
    {0x15, 0x15, 1, {0x19}},

    // Page0
    {0x15, 0xE0, 1, {0x00}},
    {0x15, 0x51, 1, {0x00}},
    {0x15, 0x53, 1, {0x2C}},

    // TE
    {0x15, 0x35, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initial_sleep_out[] = {
    {0x05, 0x11, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initial_disp_on[] = {
    {0x05, 0x29, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initial_sleep_in[] = {
    {0x05, 0x10, 1, {0x00}},
};

static struct LCM_setting_table_V3 lcm_initial_display_off[] = {
    {0x05, 0x28, 1, {0x00}},
};

static void init_lcm_registers_sleep_in(void)
{
    MDELAY(5);

    dsi_set_cmdq_V3(lcm_initial_display_off, sizeof(lcm_initial_display_off) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(50);

    dsi_set_cmdq_V3(lcm_initial_sleep_in, sizeof(lcm_initial_sleep_in) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(120);

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void init_lcm_registers_sleep_out(void)
{
    dsi_set_cmdq_V3(lcm_initial_setting, sizeof(lcm_initial_setting) / sizeof(struct LCM_setting_table_V3), 1);
    dsi_set_cmdq_V3(lcm_initial_sleep_out, sizeof(lcm_initial_sleep_out) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(120);

    dsi_set_cmdq_V3(lcm_initial_disp_on, sizeof(lcm_initial_disp_on) / sizeof(struct LCM_setting_table_V3), 1);
    MDELAY(5);

    LCM_PRINT("[LCD] %s\n",__func__);
}

/* --------------------------------------------------------------------------- */
/* LCM Driver Implementations */
/* --------------------------------------------------------------------------- */

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
    memcpy((void *)&lcm_util, (void *)util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS *params)
{
    memset(params, 0, sizeof(struct LCM_PARAMS));

    params->type = LCM_TYPE_DSI;

    params->width = FRAME_WIDTH;
    params->height = FRAME_HEIGHT;

    /* physical size */
    params->physical_width = PHYSICAL_WIDTH;
    params->physical_height = PHYSICAL_HEIGHT;
    params->density = LCM_DENSITY;

    params->dsi.mode = SYNC_PULSE_VDO_MODE;
    /* enable tearing-free */
    params->dbi.te_mode = LCM_DBI_TE_MODE_DISABLED;
    params->dbi.te_edge_polarity = LCM_POLARITY_RISING;

    /* DSI */
    /* Command mode setting */
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
    params->dsi.vertical_backporch = 16;
    params->dsi.vertical_frontporch = 23;
    params->dsi.vertical_active_line = FRAME_HEIGHT;

    params->dsi.horizontal_sync_active = 20;
    params->dsi.horizontal_backporch = 92;
    params->dsi.horizontal_frontporch = 92;
    params->dsi.horizontal_active_pixel = FRAME_WIDTH;

    params->dsi.HS_TRAIL = 6;
    params->dsi.HS_PRPR = 6;

    params->dsi.PLL_CLOCK = 280; // mipi clk : 560Mhz
    params->dsi.ssc_disable = 1;

    params->lcm_seq_suspend = LCM_MIPI_VIDEO_FRAME_DONE_SUSPEND;
    params->lcm_seq_shutdown = LCM_SHUTDOWN_AFTER_DSI_OFF;
    params->lcm_seq_resume = LCM_MIPI_READY_VIDEO_FRAME_RESUME;
    params->lcm_seq_power_on = NOT_USE_RESUME;

    params->esd_powerctrl_support = false;

#if defined(CONFIG_LGE_DISPLAY_ESD_RECOVERY)
    params->dsi.esd_check_enable = 1;
    params->dsi.customization_esd_check_enable = 1;

    params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
    params->dsi.lcm_esd_check_table[0].count = 1;
    params->dsi.lcm_esd_check_table[0].para_list[0] = 0x1C;

    set_disp_esd_check_lcm(true);
#endif

    LCM_PRINT("[LCD] %s\n",__func__);
}

#if USE_DEEP_SLEEP
static void set_panel_suspend_state(bool mode)
{
    panel_suspend_state = mode;
}

static void set_panel_deep_sleep_state(bool mode)
{
    panel_deep_sleep_state = mode;
}

static bool get_panel_suspend_state(void)
{
    return panel_suspend_state;
}

static bool get_panel_deep_sleep_state(void)
{
    return panel_deep_sleep_state;
}
#endif

static void lcm_dsv_ctrl(unsigned int enable, unsigned int delay)
{
    if(!enable)
        lcd_bias_power_off_vspn();

    if(enable)
        lcd_bias_set_gpio_ctrl(DSV_VPOS_EN, enable);
    else
        lcd_bias_set_gpio_ctrl(DSV_VNEG_EN, enable);

    if(delay)
        MDELAY(delay);

    if(enable)
        lcd_bias_set_gpio_ctrl(DSV_VNEG_EN, enable);
    else
        lcd_bias_set_gpio_ctrl(DSV_VPOS_EN, enable);

    if(enable)
        lcd_bias_set_vspn(DSV_VOLTAGE);

    LCM_PRINT("[LCD] %s : %s\n",__func__,(enable)? "ON":"OFF");
}

static void lcd_reset_pin(unsigned int mode)
{
    lcd_bias_set_gpio_ctrl(LCM_RST, mode);

    LCM_PRINT("[LCD] LCD Reset %s \n",(mode)? "High":"Low");
}

#if USE_FUNCTION
static void tp_ldo_1v8_ctrl(unsigned int enable)
{
    lcd_bias_set_gpio_ctrl(LCD_LDO_EN, enable);

    LCM_PRINT("[LCD] %s : %s\n",__func__,(enable)? "ON":"OFF");
}

static void touch_reset_pin(unsigned int mode)
{
    lcd_bias_set_gpio_ctrl(TOUCH_RST, mode);

    LCM_PRINT("[LCD] Touch Reset %s \n",(mode)? "High":"Low");
}

static void lcm_reset_ctrl(unsigned int enable, unsigned int delay)
{
    if(enable)
        lcd_reset_pin(enable);
    else
        lge_panel_notifier_call_chain(LGE_PANEL_EVENT_RESET, 0, LGE_PANEL_RESET_LOW);

    if(delay)
        MDELAY(delay);

    if(enable)
        lge_panel_notifier_call_chain(LGE_PANEL_EVENT_RESET, 0, LGE_PANEL_RESET_HIGH);
    else
        lcd_reset_pin(enable);

    LCM_PRINT("[LCD] %s\n",__func__);
}
#endif

static void lcm_suspend(void)
{
    init_lcm_registers_sleep_in();
    MDELAY(10);

    lcm_dsv_ctrl(DISABLE, 1);

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void lcm_suspend_mfts(void)
{
    LCM_PRINT("[LCD] %s : do nothing\n",__func__);
}

static void lcm_resume(void)
{
    lcm_dsv_ctrl(ENABLE, 1);
    MDELAY(5);

    lcd_reset_pin(ENABLE);
    MDELAY(5);

    lcd_reset_pin(DISABLE);
    MDELAY(10);

    lcd_reset_pin(ENABLE);
    MDELAY(20);

    init_lcm_registers_sleep_out();

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void lcm_resume_mfts(void)
{
    LCM_PRINT("[LCD] %s : do nothing\n",__func__);
}

#if USE_DEEP_SLEEP
static void lcm_dsi_ctrl(unsigned int enable)
{
    if(enable) {
        ddp_path_top_clock_on();
        ddp_dsi_power_on(DISP_MODULE_DSI0, NULL);
    } else {
        ddp_dsi_power_off(DISP_MODULE_DSI0, NULL);
        ddp_path_top_clock_off();
    }

    LCM_PRINT("[LCD] %s : %s\n",__func__,(enable)? "on":"off");
}

static void lcm_enter_deep_sleep(void)
{
    if(get_panel_suspend_state()) {
        set_panel_deep_sleep_state(true);

        LCM_PRINT("[LCD] %s\n", __func__);
    }
}

static void lcm_exit_deep_sleep(void)
{
    if(get_panel_deep_sleep_state()) {
        set_panel_deep_sleep_state(false);

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

    LCM_PRINT("[LCD] %s : %d \n", __func__, mode);
}
#endif

static void lcm_shutdown(void)
{
    LCM_PRINT("[LCD] %s : do nothing\n",__func__);
}

/* --------------------------------------------------------------------------- */
/* Get LCM Driver Hooks */
/* --------------------------------------------------------------------------- */
struct LCM_DRIVER jd9365da_hdplus_dsi_vdo_ski_drv = {
    .name = "SKI-JD9365DA",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params = lcm_get_params,
    .suspend = lcm_suspend,
    .resume = lcm_resume,
    .shutdown = lcm_shutdown,
    .suspend_mfts = lcm_suspend_mfts,
    .resume_mfts = lcm_resume_mfts,
#if USE_DEEP_SLEEP
    .set_deep_sleep = lcm_set_deep_sleep,
#endif
};

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
 */

#include <lcm_drv.h>
#include <linux/delay.h>
#include "mt65xx_lcm_list.h"

enum LCM_DSI_MODE_CON lcm_dsi_mode;

struct LCM_DRIVER *lcm_driver_list[] = {
#ifdef CONFIG_JDM_LCM /* Add LCM Driver for JDM */
#if defined(FT8615_HDPLUS_DSI_VDO_AUO)
        &ft8615_hdplus_dsi_vdo_auo_drv,
#endif
#if defined(FT8006P_HDPLUS_DSI_VDO_AUO)
        &ft8006p_hdplus_dsi_vdo_auo_drv,
#endif
#if defined(ILI9881H_HDPLUS_DSI_VDO_LCE)
        &ili9881h_hdplus_dsi_vdo_lce_drv,
#endif
#if defined(ILI9881C_HDPLUS_DSI_VDO_LCE)
    &ili9881c_hdplus_dsi_vdo_lce_lcm_drv,
#endif
#if defined(ST7703_HDPLUS_DSI_VDO_LCE)
    &st7703_hdplus_dsi_vdo_lce_lcm_drv,
#endif
#if defined(JD9365D_HDPLUS_DSI_VDO_HLT)
    &jd9365d_hdplus_dsi_vdo_hlt_lcm_drv,
#endif
#else
#if defined(SW49106_FHDPLUS_DSI_VDO_LGD_CV5)
	&sw49106_fhdplus_dsi_vdo_lgd_cv5_drv,
#endif
#if defined(SW49107_LGD_FHDPLUS_DSI_CMD_CV7A)
  &sw49107_lgd_fhdplus_dsi_cmd_cv7a_lcm_drv,
#endif
#if defined(SW49110_TOVIS_FHDPLUS_DSI_CMD_DH50)
  &sw49110_tovis_fhdplus_dsi_cmd_dh50_lcm_drv,
#endif
#if defined(SW49110_TIANMA_FHDPLUS_DSI_CMD_DH50)
  &sw49110_tianma_fhdplus_dsi_cmd_dh50_lcm_drv,
#endif
#if defined(FT8006M_HD720_DSI_VDO_TCL_CV1)
	&ft8006m_hd720_dsi_vdo_tcl_cv1_lcm_drv,
#endif
#if defined(FT8006P_HDPLUS_DSI_VDO_TXD)
	&ft8006p_hdplus_dsi_vdo_txd_drv,
#endif
#if defined(FT8006P_HDPLUS_DSI_VDO_LCE)
	&ft8006p_hdplus_dsi_vdo_lce_drv,
#endif
#if defined(FT8006P_HDPLUS_DSI_VDO_TIANMA)
	&ft8006p_hdplus_dsi_vdo_tianma_drv,
#endif
#if defined(FT8006P_HDPLUS_DSI_VDO_AUO)
        &ft8006p_hdplus_dsi_vdo_auo_drv,
#endif
#if defined(ILI9881H_HDPLUS_DSI_VDO_DJN)
	&ili9881h_hdplus_dsi_vdo_djn_drv,
#endif
#if defined(ILI9881C_HDPLUS_DSI_VDO_KD)
	&ili9881c_hdplus_dsi_vdo_kd_drv,
#endif
#if defined(ILI9881C_HDPLUS_DSI_VDO_LCE)
	&ili9881c_hdplus_dsi_vdo_lce_lcm_drv,
#endif
#if defined(ILI9881C_HDPLUS_DSI_VDO_TXD)
	&ili9881c_hdplus_dsi_vdo_txd_lcm_drv,
#endif
#if defined(ILI9881D_HDPLUS_DSI_VDO_CTC)
	&ili9881d_hdplus_dsi_vdo_ctc_lcm_drv,
#endif
#if defined(JD9365DA_HDPLUS_DSI_VDO_SKI)
	&jd9365da_hdplus_dsi_vdo_ski_drv,
#endif
#if defined(JD9365D_HDPLUS_DSI_VDO_HLT)
	&jd9365d_hdplus_dsi_vdo_hlt_lcm_drv,
#endif
#if defined(JD9365Z_HDPLUS_DSI_VDO_KD)
	&jd9365z_hdplus_dsi_vdo_kd_lcm_drv,
#endif
#if defined(ST7703_HDPLUS_DSI_VDO_LCE)
	&st7703_hdplus_dsi_vdo_lce_drv,
#endif
#endif

#if defined(ST7703_HDPLUS_DSI_VDO_LCE)
	&st7703_hdplus_dsi_vdo_lce_lcm_drv,
#endif
};

unsigned char lcm_name_list[][128] = {
#if defined(ST7703_HDPLUS_DSI_VDO_LCE)
  "st7703_hdplus_dsi_vdo_lce_drv"
#endif
};

unsigned int lcm_count =
	sizeof(lcm_driver_list) / sizeof(struct LCM_DRIVER *);


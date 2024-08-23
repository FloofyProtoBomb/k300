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

#ifndef __MT65XX_LCM_LIST_H__
#define __MT65XX_LCM_LIST_H__

#include <lcm_drv.h>

#ifdef CONFIG_JDM_LCM
/* Add LCM Driver for JDM */
extern struct LCM_DRIVER ft8615_hdplus_dsi_vdo_auo_drv;
extern struct LCM_DRIVER ft8006p_hdplus_dsi_vdo_auo_drv;
extern struct LCM_DRIVER ili9881h_hdplus_dsi_vdo_lce_drv;
extern struct LCM_DRIVER ili9881c_hdplus_dsi_vdo_lce_lcm_drv;
extern struct LCM_DRIVER st7703_hdplus_dsi_vdo_lce_lcm_drv;
extern struct LCM_DRIVER jd9365d_hdplus_dsi_vdo_hlt_lcm_drv;
#else
extern struct LCM_DRIVER sw49106_fhdplus_dsi_vdo_lgd_cv5_drv;
extern struct LCM_DRIVER sw49107_lgd_fhdplus_dsi_cmd_cv7a_lcm_drv;
extern struct LCM_DRIVER sw49110_tovis_fhdplus_dsi_cmd_dh50_lcm_drv;
extern struct LCM_DRIVER sw49110_tianma_fhdplus_dsi_cmd_dh50_lcm_drv;
extern struct LCM_DRIVER ft8006m_hd720_dsi_vdo_tcl_cv1_lcm_drv;
extern struct LCM_DRIVER ft8006p_hdplus_dsi_vdo_txd_drv;
extern struct LCM_DRIVER ft8006p_hdplus_dsi_vdo_lce_drv;
extern struct LCM_DRIVER ft8006p_hdplus_dsi_vdo_tianma_drv;
extern struct LCM_DRIVER ft8006p_hdplus_dsi_vdo_auo_drv;
extern struct LCM_DRIVER ili9881h_hdplus_dsi_vdo_djn_drv;
extern struct LCM_DRIVER ili9881c_hdplus_dsi_vdo_kd_drv;
extern struct LCM_DRIVER ili9881c_hdplus_dsi_vdo_txd_lcm_drv;
extern struct LCM_DRIVER ili9881d_hdplus_dsi_vdo_ctc_lcm_drv;
extern struct LCM_DRIVER ili9881c_hdplus_dsi_vdo_lce_lcm_drv;
extern struct LCM_DRIVER jd9365z_hdplus_dsi_vdo_kd_lcm_drv;
extern struct LCM_DRIVER jd9365d_hdplus_dsi_vdo_hlt_lcm_drv;
extern struct LCM_DRIVER jd9365da_hdplus_dsi_vdo_ski_drv;
extern struct LCM_DRIVER st7703_hdplus_dsi_vdo_lce_drv;
#endif

#endif

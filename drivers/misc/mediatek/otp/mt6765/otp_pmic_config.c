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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pm_qos.h>
#include <linux/delay.h>
#include <mach/upmu_sw.h>
#include <mach/upmu_hw.h>
#include <mt-plat/mtk_otp.h>
#include <mt-plat/upmu_common.h>
#include "otp_pmic_config.h"

#define kal_uint32 unsigned int

/**************************************************************************
 *EXTERN FUNCTION
 **************************************************************************/
u32 otp_pmic_fsource_set(void)
{
	u32 ret_val = 0;

	/* 1.8V */
	ret_val |= pmic_config_interface(
			(kal_uint32)(PMIC_RG_VEFUSE_VOSEL_ADDR),
			(kal_uint32)(0x4),
			(kal_uint32)(PMIC_RG_VEFUSE_VOSEL_MASK),
			(kal_uint32)(PMIC_RG_VEFUSE_VOSEL_SHIFT)
			);

	/* +40mV */
	ret_val |= pmic_config_interface(
			(kal_uint32)(PMIC_RG_VEFUSE_VOCAL_ADDR),
			(kal_uint32)(0x4),
			(kal_uint32)(PMIC_RG_VEFUSE_VOCAL_MASK),
			(kal_uint32)(PMIC_RG_VEFUSE_VOCAL_SHIFT)
			);

	/* Fsource(VEFUSE or VEFUSE) enabled */
	ret_val |= pmic_config_interface(
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_ADDR),
			(kal_uint32)(1),
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_MASK),
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_SHIFT));

	mdelay(10);

	return ret_val;
}

u32 otp_pmic_fsource_release(void)
{
	u32 ret_val = 0;

	/* Fsource(VEFUSE or VMIPI) disabled */
	ret_val |= pmic_config_interface(
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_ADDR),
			(kal_uint32)(0),
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_MASK),
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_SHIFT));

	mdelay(10);

	return ret_val;
}

u32 otp_pmic_is_fsource_enabled(void)
{
	u32 regVal = 0;

	/*  Check Fsource(VEFUSE or VMIPI) Status */
	pmic_read_interface((kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_ADDR),
			&regVal,
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_MASK),
			(kal_uint32)(PMIC_RG_LDO_VEFUSE_EN_SHIFT)
			);

	/* return 1 : fsource enabled
	 * return 0 : fsource disabled
	 */

	return regVal;
}

static struct pm_qos_request dvfsrc_vcore_opp_req;

u32 otp_pmic_high_vcore_init(void)
{
	pm_qos_add_request(&dvfsrc_vcore_opp_req, PM_QOS_VCORE_OPP,
			PM_QOS_VCORE_OPP_DEFAULT_VALUE);

	return 0;
}

u32 otp_pmic_high_vcore_set(void)
{
	//OPP table :
	//
	//0	0.8 - Min(AP, GPU, MD1 VB)
	//1	0.7 - Min(AP, GPU VB)
	//2	0.7 - MD1 VB
	//3	0.65
	//4	VCORE_OPP_UNREQ

	pm_qos_update_request(&dvfsrc_vcore_opp_req, 0);

	return STATUS_DONE;
}

u32 otp_pmic_high_vcore_release(void)
{
	pm_qos_update_request(&dvfsrc_vcore_opp_req,
		PM_QOS_VCORE_OPP_DEFAULT_VALUE);

	return STATUS_DONE;
}

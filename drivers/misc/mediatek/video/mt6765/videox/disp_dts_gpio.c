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

#include "disp_dts_gpio.h"
#include "disp_helper.h"
#include "disp_drv_log.h"
#include <linux/kernel.h> /* printk */
#include "touch_core.h"
#include "touch_hwif.h"

#ifndef CONFIG_FPGA_EARLY_PORTING
static struct pinctrl *this_pctrl; /* static pinctrl instance */
#endif
static unsigned int touch_rst;

/* DTS state mapping name */
static const char *this_state_name[DTS_GPIO_STATE_MAX] = {
	"mode_te_gpio",
	"mode_te_te",
	"mode_te1_te",
	"lcm_rst_out0_gpio",
	"lcm_rst_out1_gpio",
	"lcm1_rst_out0_gpio",
	"lcm1_rst_out1_gpio",
	"lcm_ldo_out0_gpio",
	"lcm_ldo_out1_gpio",
	"lcd_bias_enp0_gpio",
	"lcd_bias_enp1_gpio",
	"lcd_bias_enn0_gpio",
	"lcd_bias_enn1_gpio"
};

/* pinctrl implementation */
static long _set_state(const char *name)
{
#ifndef CONFIG_FPGA_EARLY_PORTING
	long ret = 0;
	struct pinctrl_state *pState = 0;

	if (!this_pctrl) {
		pr_info("this pctrl is null\n");
		return -1;
	}

	pState = pinctrl_lookup_state(this_pctrl, name);
	if (IS_ERR(pState)) {
		pr_err("lookup state '%s' failed\n", name);
		ret = PTR_ERR(pState);
		goto exit;
	}

	/* select state! */
	pinctrl_select_state(this_pctrl, pState);

exit:
	return ret; /* Good! */
#else
	return 0;
#endif
}

long disp_dts_gpio_init(struct platform_device *pdev)
{
#ifndef CONFIG_FPGA_EARLY_PORTING
	long ret = 0;
	struct pinctrl *pctrl;
	struct device_node *np = NULL;

	/* retrieve */
	pctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pctrl)) {
		pr_err("Cannot find disp pinctrl!");
		ret = PTR_ERR(pctrl);
		goto exit;
	}

	this_pctrl = pctrl;

	np = of_find_compatible_node(NULL, NULL, DISP_DTS_GPIO_NODE);

	if (!np) {
		dev_err(&pdev->dev, "[%s] DT node: Not found\n", __func__);
		ret = -1;
		goto exit;
	} else {
		PROPERTY_GPIO(np, "touch-reset-gpio", touch_rst);
		printk("[DISP][GPIO] touch-reset-gpio = %d\n", touch_rst);
	}

exit:
	return ret;
#else
	return 0;
#endif
}

long disp_dts_gpio_select_state(enum DTS_GPIO_STATE s)
{
	if (!((unsigned int)(s) < (unsigned int)(DTS_GPIO_STATE_MAX))) {
		pr_err("GPIO STATE is invalid,state=%d\n", (unsigned int)s);
		return -1;
	}
	return _set_state(this_state_name[s]);
}

void disp_set_gpio_ctrl(unsigned int ctrl_pin, unsigned int en)
{
  switch(ctrl_pin) {
  case LCM_RST:
    if (en)
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);
    else
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT0);
    break;
  case LCD_LDO_EN:
    if (en)
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_LDO_OUT1);
    else
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_LDO_OUT0);
    break;
  case DSV_VPOS_EN:
    if (en)
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENP1);
    else
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENP0);
    break;
  case DSV_VNEG_EN:
    if (en)
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENN1);
    else
      disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENN0);
    break;
  case TOUCH_RST:
    if (en)
      touch_gpio_direction_output(touch_rst, 1);
    else
      touch_gpio_direction_output(touch_rst, 0);
    break;
  default:
    pr_err("[DISP][GPIO] invalid ctrl_pin!");
    break;
}
}

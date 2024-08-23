#define pr_fmt(fmt) "[LGECHG] %s: " fmt, __func__

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mt-plat/mtk_battery.h>
#include <mt-plat/mtk_boot.h>
#include <mt-plat/mtk_boot_common.h>
#include "mtk_charger_intf.h"
#include "lge_charging.h"

#ifdef CONFIG_LGE_PM_PSEUDO_HVDCP
#include <linux/power/lge_pseudo_hvdcp.h>
#endif

#ifdef CONFIG_LGE_USB
#include <mt6765/usb20.h>
#endif

static int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}

static inline bool apply_limit(int *target, int limit)
{
	if (limit == -1)
		return false;

	if (limit < *target) {
		*target = limit;
		return true;
	}

	return false;
}

static inline bool apply_boost(int *target, int limit)
{
	if (limit == -1)
		return false;

	if (limit > *target) {
		*target = limit;
		return true;
	}

	return false;
}

static int select_current_for_test(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata = &info->chg1_data;

	if (pdata->force_charging_current > 0) {
		data->charging_current_limit = pdata->force_charging_current;
		if (pdata->force_charging_current <= 450000) {
			data->input_current_limit = 500000;
		} else {
			data->input_current_limit =
					info->data.ac_charger_input_current;
			data->charging_current_limit =
					info->data.ac_charger_current;
		}
		return 1;
	}

	if (info->usb_unlimited) {
		data->input_current_limit =
					info->data.ac_charger_input_current;
		data->charging_current_limit =
					info->data.ac_charger_current;
		return 1;
	}

	if ((get_boot_mode() == META_BOOT) ||
	    (get_boot_mode() == ADVMETA_BOOT)) {
		data->input_current_limit = 200000; /* 200mA */
		data->charging_current_limit = 0;
		return 1;
	}

	if (info->atm_enabled == true && (info->chr_type == STANDARD_HOST ||
	    info->chr_type == CHARGING_HOST)) {
		data->input_current_limit = 100000; /* 100mA */
		data->charging_current_limit = 0;
		return 1;
	}

	return 0;
}

static int select_current_by_type(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	int ret;

	/* Based on USB Battery Charging Spec (bc12) */
	switch (info->chr_type) {
	case STANDARD_HOST:
		data->input_current_limit =
				info->data.usb_charger_current;
		data->charging_current_limit =
				info->data.usb_charger_current;
		data->input_voltage = 5000000;
		break;
	case CHARGING_HOST:
		data->input_current_limit =
				info->data.charging_host_charger_current;
		data->charging_current_limit =
				info->data.charging_host_charger_current;
		data->input_voltage = 5000000;
		break;
	case NONSTANDARD_CHARGER:
		data->input_current_limit =
				info->data.non_std_ac_charger_current;
		data->charging_current_limit =
				info->data.non_std_ac_charger_current;
		data->input_voltage = 5000000;
		break;
	case STANDARD_CHARGER:
		data->input_current_limit =
				info->data.ac_charger_input_current;
		data->charging_current_limit =
				info->data.ac_charger_current;
		data->input_voltage = 5000000;

		ret = mtk_pe20_set_charging_current(info,
				&data->charging_current_limit,
				&data->input_current_limit);
		if (!ret)
			data->input_voltage = info->pe2.vbus;

		ret = mtk_pe_set_charging_current(info,
				&data->charging_current_limit,
				&data->input_current_limit);
		if (!ret)
			data->input_voltage = 9000000;
		break;
	case APPLE_2_1A_CHARGER:
		data->input_current_limit =
				info->data.apple_2_1a_charger_current;
		data->charging_current_limit =
				info->data.apple_2_1a_charger_current;
		data->input_voltage = 5000000;
		break;
	case APPLE_1_0A_CHARGER:
		data->input_current_limit =
				info->data.apple_1_0a_charger_current;
		data->charging_current_limit =
				info->data.apple_1_0a_charger_current;
		data->input_voltage = 5000000;
		break;
	case APPLE_0_5A_CHARGER:
		data->input_current_limit =
				info->data.apple_1_0a_charger_current;
		data->charging_current_limit =
				info->data.apple_1_0a_charger_current;
		data->input_voltage = 5000000;
		break;
	default:
		data->input_current_limit = 0;
		data->charging_current_limit = 0;
		data->input_voltage = 5000000;
		break;
	}

	/* Pump Express 4.0 */
	if (mtk_is_TA_support_pd_pps(info)) {
		data->input_current_limit =
			info->data.pe40_single_charger_input_current;
		data->charging_current_limit =
			info->data.pe40_single_charger_current;
		data->input_voltage = info->pe4.avbus * 1000;
	/* USB Type-C */
	} else if (is_typec_adapter(info)) {
		if (adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL) == 3000) {
			data->input_current_limit = 3000000; /* info->data.typec_rp3p0_input_current; */
			data->charging_current_limit = 3000000; /* info->data.typec_rp3p0_current; */
		} else if (adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL) == 1500) {
			data->input_current_limit = 1400000; /* info->data.typec_rp1p5_input_current; */
			data->charging_current_limit = 1400000; /* info->data.typec_rp1p5_current; */
		} else {
			chr_err("type-C: inquire rp error\n");
			data->input_current_limit = 500000;
			data->charging_current_limit = 500000;
		}

		pr_info("type-C:%d current:%d\n",
			info->pd_type,
			adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL));
	/* USB Power Delivery */
	} else if (mtk_pdc_check_charger(info) == true) {
		int vbus = 0, cur = 0, idx = 0;

		ret = mtk_pdc_get_setting(info, &vbus, &cur, &idx);
		if (ret != -1 && idx != -1) {
			data->input_current_limit = cur * 1000;
			data->charging_current_limit = info->data.pd_charger_current;
			data->input_voltage = vbus * 1000;
			ret = mtk_pdc_setup(info, idx);
			if (ret != MTK_ADAPTER_OK) {
				/* TODO : restore previous setting */
			}
		} else {
			data->input_current_limit =
				info->data.usb_charger_current_configured;
			data->charging_current_limit =
				info->data.usb_charger_current_configured;
		}
		pr_info("vbus:%d input_cur:%d idx:%d current:%d\n",
			vbus, cur, idx,
			info->data.pd_charger_current);
	}

	/* Pump Express 4.0 */
	if (mtk_pe40_get_is_connect(info)) {
		apply_limit(&data->input_current_limit,
			info->pe4.pe4_input_current_limit);

		info->pe4.input_current_limit = data->input_current_limit;

		apply_limit(&data->input_current_limit,
			info->pe4.pe4_input_current_limit_setting);
	}

	return 0;
}

static int select_current_by_usb_state(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (get_boot_mode() != NORMAL_BOOT)
		return 0;

	if (is_typec_adapter(info))
		return 0;

	if (mtk_pdc_check_charger(info) == true) {
		/* handle suspend only */
		if (info->usb_state != USB_SUSPEND)
			return 0;
	}

#ifdef CONFIG_LGE_USB
	if (usb_compliance_mode) {
		switch (info->usb_state) {
		case USB_SUSPEND:	/* 0 mA */
			data->input_current_limit = 0;
			break;
		case USB_UNCONFIGURED:	/* 100 mA */
			data->input_current_limit = 100000;
			break;
		case USB_CONFIGURED:	/* 500 mA */
			data->input_current_limit = 500000;
			break;
		default:		/* 1400 mA */
			data->input_current_limit = 1400000;
			break;
		}

		pr_err("usb_state: %d compliance: %d current: %d\n",
				info->usb_state, usb_compliance_mode,
				data->input_current_limit);

		return 0;
	}
#endif

	if (info->chr_type != STANDARD_HOST)
		return 0;

	switch (info->usb_state) {
	case USB_SUSPEND:
		data->input_current_limit =
				info->data.usb_charger_current_suspend;
		break;
	case USB_UNCONFIGURED:
		data->input_current_limit =
				info->data.usb_charger_current_unconfigured;
		break;
	case USB_CONFIGURED:
		data->input_current_limit =
				info->data.usb_charger_current_configured;
		break;
	default:
		data->input_current_limit =
				info->data.usb_charger_current_unconfigured;
		break;
	}

	return 0;
}

static int select_current_by_jeita(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (!info->enable_sw_jeita)
		return 0;

	if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)
			&& info->chr_type == STANDARD_HOST) {
		pr_debug("USBIF & STAND_HOST skip current check\n");
	} else {
		if (info->sw_jeita.sm == TEMP_T0_TO_T1) {
			data->input_current_limit = 500000;
			data->charging_current_limit = 350000;
		}
	}

	return 0;
}

static int select_current_by_tune(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (!is_dual_charger_supported(info))
		return -ENOTSUPP;

	apply_limit(&data->charging_current_limit,
		data->charging_current_tuned);

	return 0;
}

static int select_current_by_thermal(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata = &info->chg1_data;

	apply_limit(&data->charging_current_limit,
		pdata->thermal_charging_current_limit);
	apply_limit(&data->input_current_limit,
		pdata->thermal_input_current_limit);

	return 0;
}

static int select_current_by_water_detected(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (!info->water_detected)
		return 0;

	apply_limit(&data->charging_current_limit,
		info->data.usb_charger_current);
	apply_limit(&data->input_current_limit,
		info->data.usb_charger_current);

	return 0;
}

static int select_current_by_aicl(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata = &info->chg1_data;

	if (mtk_pe20_get_is_connect(info))
		return 0;
	if (mtk_pe_get_is_connect(info))
		return 0;
	if (mtk_is_TA_support_pd_pps(info))
		return 0;

	apply_limit(&data->input_current_limit,
		pdata->input_current_limit_by_aicl);

	return 0;
}

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
static int select_current_by_chgctrl(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	apply_limit(&data->charging_current_limit,
		info->chgctrl.charging_current_limit);
	apply_limit(&data->input_current_limit,
		info->chgctrl.input_current_limit);
	apply_boost(&data->input_current_limit,
		info->chgctrl.input_current_boost);
	if (info->chgctrl.input_suspend)
		data->input_current_limit = 0;

	return 0;
}
#endif

static int select_current_limit(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	u32 charging_current_min;
	u32 input_current_min;
	int ret;

	mutex_lock(&data->lock);

	if (select_current_for_test(info))
		goto done;

	select_current_by_type(info);
	select_current_by_usb_state(info);
	select_current_by_jeita(info);
	select_current_by_tune(info);
	select_current_by_thermal(info);
	select_current_by_water_detected(info);
	select_current_by_aicl(info);
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
	select_current_by_chgctrl(info);
#endif

	ret = charger_dev_get_min_charging_current(info->chg1_dev,
						&charging_current_min);
	if (ret != -ENOTSUPP &&
	    data->charging_current_limit < charging_current_min)
		data->charging_current_limit = 0;
	ret = charger_dev_get_min_input_current(info->chg1_dev,
						&input_current_min);
	if (ret != -ENOTSUPP &&
	    data->input_current_limit < input_current_min)
		data->input_current_limit = 0;

done:
	mutex_unlock(&data->lock);

	return 0;
}

static void select_voltage_limit(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (info->enable_sw_jeita) {
		if (info->sw_jeita.cv != 0) {
			charger_dev_set_constant_voltage(info->chg1_dev,
							info->sw_jeita.cv);
			return;
		}
	}
	/* dynamic cv*/
	data->constant_voltage = info->data.battery_cv;
	mtk_get_dynamic_cv(info, &data->constant_voltage);

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
	apply_limit(&data->constant_voltage,
		info->chgctrl.constant_voltage);
#endif
}

static int enable_slave_charger(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata2 = &info->chg2_data;
	bool chip_enabled = false;
	bool enabled = false;
	int ichg2;
	int ret;

	if (!is_dual_charger_supported(info))
		return -ENOTSUPP;

	mutex_lock(&data->slave);

	charger_dev_is_chip_enabled(info->chg2_dev, &chip_enabled);
	if (!chip_enabled)
		charger_dev_enable_chip(info->chg2_dev, true);

	ichg2 = data->charging_current_limit
			* data->slave_ichg_percent / 100;

	pdata2->input_current_limit = data->input_current_limit;
	charger_dev_set_charging_current(info->chg2_dev, ichg2);
	charger_dev_set_input_current(info->chg2_dev,
			pdata2->input_current_limit);
	charger_dev_set_constant_voltage(info->chg2_dev,
			data->constant_voltage + data->vfloat_offset);

	ret = charger_dev_get_charging_current(info->chg2_dev,
			&pdata2->charging_current_limit);
	if (ret == -ENOTSUPP)
		pdata2->charging_current_limit = ichg2;

	charger_dev_is_enabled(info->chg2_dev, &enabled);
	if (!enabled) {
		charger_dev_enable(info->chg2_dev, true);
		pr_info("enable slave charger done.\n");
	}

	mutex_unlock(&data->slave);

	return 0;
}

static int disable_slave_charger(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata2 = &info->chg2_data;
	bool chip_enabled = true;
	bool enabled = false;

	if (!is_dual_charger_supported(info))
		return -ENOTSUPP;

	mutex_lock(&data->slave);

	data->charging_current_tuned = -1;

	pdata2->charging_current_limit = 0;
	pdata2->input_current_limit = 0;

	charger_dev_is_chip_enabled(info->chg2_dev, &chip_enabled);
	if (!chip_enabled)
		goto out;

	charger_dev_is_enabled(info->chg2_dev, &enabled);
	if (!enabled) {
		charger_dev_enable_chip(info->chg2_dev, false);
		goto out;
	}

	charger_dev_enable(info->chg2_dev, false);
	charger_dev_enable_chip(info->chg2_dev, false);

	pr_info("disable slave charger done.\n");

out:
	mutex_unlock(&data->slave);

	return 0;
}

static bool is_slave_enabled(struct charger_manager *info)
{
	bool chip_enabled = true;
	bool enabled = false;

	if (!is_dual_charger_supported(info))
		return false;

	charger_dev_is_chip_enabled(info->chg2_dev, &chip_enabled);
	if (!chip_enabled)
		return false;

	charger_dev_is_enabled(info->chg2_dev, &enabled);

	return enabled;
}

static bool is_slave_charger_ok(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata2 = &info->chg2_data;
	int ichg1, ichg2_min;
	int ret;

	if (!is_dual_charger_supported(info))
		return false;

	if (data->state != CHR_CC && data->state != CHR_PE40_CC)
		return false;

	if (data->slave_ichg_percent <= 0)
		return false;

	/* estimate main charger charging current */
	ichg1 = data->charging_current_limit
			* (100 - data->slave_ichg_percent) / 100;

	/* get minimum slave charger charging current */
	ret = charger_dev_get_min_charging_current(info->chg2_dev, &ichg2_min);
	if (ret == -ENOTSUPP)
		ichg2_min = 500000;

	/* not enough current to enable slave */
	if (ichg1 < data->step_ieoc)
		return false;
	if (data->charging_current_limit - ichg1 < ichg2_min)
		return false;

	/* battery voltage is too high */
	if (!pdata2->charging_current_limit) {
		int voltage = info->battery_volt * 1000;

		/* close to cv charging */
		if (voltage >= info->data.battery_cv - 50000)
			return false;
	}

	/* TODO : add condition to enable slave */
	if (data->input_voltage > 5000000)
		return true;

	return false;
}

static int adjust_termiation_current(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	int ieoc = data->ieoc;
	bool en_term = true;

	/* if battery not exist, disable te for prevent power-down */
	if (!pmic_is_battery_exist())
		en_term = false;

	/* use ieoc for step charging if slave enabled */
	if (is_slave_enabled(info))
		en_term = false;

	charger_dev_set_eoc_current(info->chg1_dev, ieoc);
	charger_dev_enable_termination(info->chg1_dev, en_term);

	return 0;
}

static void disable_all_charging(struct charger_manager *info)
{
	disable_slave_charger(info);

	if (mtk_pe20_get_is_enable(info)) {
		mtk_pe20_set_is_enable(info, false);
		if (mtk_pe20_get_is_connect(info))
			mtk_pe20_reset_ta_vchr(info);
	}

	if (mtk_pe_get_is_enable(info)) {
		mtk_pe_set_is_enable(info, false);
		if (mtk_pe_get_is_connect(info))
			mtk_pe_reset_ta_vchr(info);
	}

	if (mtk_pe40_get_is_enable(info)) {
		if (mtk_pe40_get_is_connect(info))
			mtk_pe40_end(info, 3, true);
	}

	if (mtk_pdc_check_charger(info))
		mtk_pdc_reset(info);
}

static int enable_charging(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata = &info->chg1_data;
	struct charger_data *pdata2 = &info->chg2_data;
	int charging_enable = true;
	int input_suspend = false;

	mutex_lock(&data->lock);

	/* disable charging */
	if (data->input_current_limit == 0)
		input_suspend = true;

	if (data->disable_charging)
		charging_enable = false;
	if (data->charging_current_limit == 0)
		charging_enable = false;

	if (input_suspend || !charging_enable)
		disable_all_charging(info);

	if (is_slave_charger_ok(info)) {
		enable_slave_charger(info);
	} else {
		disable_slave_charger(info);
	}
	adjust_termiation_current(info);

	pdata->input_current_limit = data->input_current_limit;
	pdata->charging_current_limit = data->charging_current_limit -
			pdata2->charging_current_limit;

	charger_dev_set_input_current(info->chg1_dev,
			pdata->input_current_limit);
	charger_dev_set_charging_current(info->chg1_dev,
			pdata->charging_current_limit);
	charger_dev_set_constant_voltage(info->chg1_dev,
			data->constant_voltage);

	charger_dev_enable(info->chg1_dev, charging_enable);
	charger_manager_enable_hz(info->chg1_consumer, input_suspend);

	pr_info("type:%d type-c:%d pd:%d aicl:%d setting:(%d %d) (%d %d)\n",
		info->chr_type,
		info->pd_adapter ? adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL) : 0,
		info->pd_type,
		_uA_to_mA(pdata->input_current_limit_by_aicl),
		/* setting */
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		_uA_to_mA(pdata2->input_current_limit),
		_uA_to_mA(pdata2->charging_current_limit)
	);

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
	pr_info("chgctrl:%d %d %d thermal:%d %d tune:%d\n",
		_uA_to_mA(info->chgctrl.input_current_limit),
		_uA_to_mA(info->chgctrl.charging_current_limit),
		_uA_to_mA(info->chgctrl.constant_voltage),
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(data->charging_current_tuned)
	);
#endif

	if (input_suspend || !charging_enable)
		goto out;

	if (!mtk_pe20_get_is_enable(info)) {
		mtk_pe20_set_is_enable(info, true);
		mtk_pe20_set_to_check_chr_type(info, true);
	}
	if (!mtk_pe_get_is_enable(info)) {
		mtk_pe_set_is_enable(info, true);
		mtk_pe_set_to_check_chr_type(info, true);
	}

out:
	mutex_unlock(&data->lock);

	return 0;
}


static int run_aicl(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata = &info->chg1_data;
	int input_current_limit_by_aicl =
			pdata->input_current_limit_by_aicl;
	struct timespec time_now;
	struct timespec time_diff;
	int ret = 0;

	if (info->chr_type == CHARGER_UNKNOWN)
		return 0;

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
	/* do not run aicl if icl boosted */
	if (info->chgctrl.input_current_boost != -1)
		return 0;
#endif

	if (mtk_pe20_get_is_connect(info))
		return 0;
	if (mtk_pe_get_is_connect(info))
		return 0;
	if (mtk_is_TA_support_pd_pps(info))
		return 0;

	mutex_lock(&data->lock);

	if (data->input_current_limit == 0)
		goto out_aicl;

	get_monotonic_boottime(&time_now);

	charger_dev_run_aicl(info->chg1_dev,
			&pdata->input_current_limit_by_aicl);

	if (pdata->input_current_limit_by_aicl !=
			input_current_limit_by_aicl) {
		pr_info("aicl done\n");
		data->aicl_done_time = time_now;
		ret = 1;
		goto out_aicl;
	}

	/* not in AICL */
	if (pdata->input_current_limit_by_aicl == -1)
		goto out_aicl;

	if (!data->aicl_interval)
		goto out_aicl;

	time_diff = timespec_sub(time_now, data->aicl_done_time);
	if (time_diff.tv_sec <= data->aicl_interval)
		goto out_aicl;

	pr_info("try increase aicl\n");
	pdata->input_current_limit_by_aicl = -1;
	ret = 1;

out_aicl:
	mutex_unlock(&data->lock);

	return ret;
}

static int tune_current_limit(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct charger_data *pdata = &info->chg1_data;
	struct charger_data *pdata2 = &info->chg2_data;
	int ichg2_tuned;
	u32 ichg2, ichg2_min = 0;
	int ret;

	/* slave charger disabled */
	if (!is_slave_enabled(info))
		return -ENOTSUPP;

	mutex_lock(&data->lock);

	ret = charger_dev_get_min_charging_current(info->chg2_dev, &ichg2_min);
	if (ret == -ENOTSUPP)
		ichg2_min = 500000;

	ret = charger_dev_get_charging_current(info->chg2_dev, &ichg2);
	if (ret == -ENOTSUPP)
		ichg2 = pdata2->charging_current_limit;

	ichg2_tuned = ichg2 - data->step_ichg;
	if (ichg2_tuned <= 0 || ichg2_tuned < ichg2_min) {
		disable_slave_charger(info);

		mutex_unlock(&data->lock);

		return 1;
	}

	charger_dev_set_charging_current(info->chg2_dev, ichg2_tuned);
	ret = charger_dev_get_charging_current(info->chg2_dev,
					&pdata2->charging_current_limit);
	if (ret == -ENOTSUPP)
		pdata2->charging_current_limit = ichg2_tuned;

	data->charging_current_tuned = pdata->charging_current_limit
			+ pdata2->charging_current_limit;

	pr_info("slave current tuned to %dmA\n", _uA_to_mA(ichg2_tuned));

	mutex_unlock(&data->lock);

	return 0;
}

static bool is_fastchg(struct charger_manager *info)
{
	if (info->chr_type == CHARGER_UNKNOWN)
		return false;

#ifdef CONFIG_LGE_PM_PSEUDO_HVDCP
	if (pseudo_hvdcp_is_enabled()
			&& pseudo_hvdcp_is_hvdcp())
		return true;
#endif

	if (mtk_pe20_get_is_enable(info)
			&& mtk_pe20_get_is_connect(info))
		return true;

	if (mtk_pe_get_is_enable(info)
			&& mtk_pe_get_is_connect(info))
		return true;

	if (mtk_pe40_get_is_connect(info))
		return true;

	if (mtk_pdc_check_charger(info)) {
		if (info->pdc.pd_cap_max_mv > 5000)
			return true;
	}

	return false;
}

static struct power_supply *get_charger_psy(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (!data->charger_psy)
		data->charger_psy = power_supply_get_by_name("charger");

	return data->charger_psy;
}

static void notify_charger(struct charger_manager *info,
		enum power_supply_property psp, int value)
{
	struct power_supply *charger = get_charger_psy(info);
	union power_supply_propval val;

	if (!charger)
		return;

	val.intval = value;
	power_supply_set_property(charger, psp, &val);
}

static struct power_supply *get_battery_psy(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (!data->battery_psy)
		data->battery_psy = power_supply_get_by_name("battery");

	return data->battery_psy;
}

static void notify_battery(struct charger_manager *info,
		enum power_supply_property psp, int value)
{
	struct power_supply *battery = get_battery_psy(info);
	union power_supply_propval val;

	if (!battery)
		return;

	val.intval = value;
	power_supply_set_property(battery, psp, &val);
}

static int notify_current_setting(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (info->chr_type == CHARGER_UNKNOWN)
		return 0;

	mutex_lock(&data->lock);

	/* batery */
	notify_battery(info, POWER_SUPPLY_PROP_CURRENT_MAX,
			data->charging_current_limit);
	notify_battery(info, POWER_SUPPLY_PROP_VOLTAGE_MAX,
			data->constant_voltage);

	/* charger */
	notify_charger(info, POWER_SUPPLY_PROP_CURRENT_MAX,
			data->input_current_limit);
	notify_charger(info, POWER_SUPPLY_PROP_VOLTAGE_MAX,
			data->input_voltage);
	notify_charger(info, POWER_SUPPLY_PROP_FASTCHG,
			is_fastchg(info) ? 1 : 0);

	if (info->pd_adapter) {
		bool typec = false;
		int rp;

		rp = adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL);
		if (rp == 3000 || rp == 1500)
			typec = true;

		notify_charger(info, POWER_SUPPLY_PROP_TYPEC, typec ? 1 : 0);
	}

	mutex_unlock(&data->lock);

	return 0;
}

static bool hvdcp_is_connect(struct charger_manager *info)
{
	if (mtk_pe20_get_is_connect(info))
		return true;

	if (mtk_pe_get_is_connect(info))
		return true;

	return false;
}

static int hvdcp_check_charger(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (mtk_pdc_check_charger(info))
		return 0;
	if (mtk_is_TA_support_pd_pps(info))
		return 0;

	if (is_typec_adapter(info)) {
		if (adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL) == 3000)
			return 0;
	}

	/* do not enable hvdcp if once charging done unless actually discharging */
	if (data->recharging && battery_get_uisoc() > 99)
		return 0;

	mutex_lock(&data->lock);

	charger_manager_enable_hz(info->chg1_consumer, false);

	mtk_pe20_check_charger(info);
	mtk_pe_check_charger(info);

	/* notify here to indicate fastchg faster */
	if (hvdcp_is_connect(info))
		notify_charger(info, POWER_SUPPLY_PROP_FASTCHG, 1);

	mutex_unlock(&data->lock);

	return 0;
}

static int hvdcp_start_algorithm(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	mutex_lock(&data->lock);

	mtk_pe20_start_algorithm(info);
	mtk_pe_start_algorithm(info);

	mutex_unlock(&data->lock);

	return 0;
}

static int check_safety(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (!is_slave_enabled(info))
		return 0;

	charger_dev_safety_check(info->chg1_dev, data->step_ieoc);

	return 0;
}

static int check_eoc(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	bool eoc = false;

	charger_dev_is_charging_done(info->chg1_dev, &eoc);
	if (!eoc)
		return 0;

	info->polling_interval = CHARGING_FULL_INTERVAL;
	data->state = CHR_BATFULL;
	charger_dev_do_event(info->chg1_dev, EVENT_EOC, 0);
	pr_info("battery full!\n");

	return 1;
}

static int check_recharging(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	bool chg_done = false;

	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	if (chg_done)
		return 0;

	data->recharging = true;
	info->polling_interval = CHARGING_INTERVAL;
	data->state = CHR_CC;
	charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
	mtk_pe20_set_to_check_chr_type(info, true);
	mtk_pe_set_to_check_chr_type(info, true);
	mtk_pe40_set_is_enable(info, true);
	info->enable_dynamic_cv = true;
	get_monotonic_boottime(&data->charging_begin_time);
	pr_info("battery recharging!\n");

	return 1;
}

static int check_state(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	int state = data->state;
	int notify = CHARGER_NOTIFY_NORMAL;

	if (data->disable_charging) {
		state = CHR_ERROR;
		notify = CHARGER_NOTIFY_ERROR;
	}

	if (data->charging_current_limit == 0) {
		state = CHR_DISABLE;
		notify = CHARGER_NOTIFY_DISABLE;
	}

	if (data->input_current_limit == 0) {
		state = CHR_SUSPEND;
		notify = CHARGER_NOTIFY_SUSPEND;
	}

	/* state changed to error */
	if (state != data->state) {
		data->state = state;
		charger_manager_notifier(info, notify);
		return 1;
	}

	if (notify != CHARGER_NOTIFY_NORMAL)
		return 0;

	/* state changed to normal */
	if (state == CHR_SUSPEND || state == CHR_DISABLE
			|| state == CHR_ERROR) {
		data->state = CHR_CC;
		charger_manager_notifier(info, notify);

		mtk_pe20_set_to_check_chr_type(info, true);
		mtk_pe_set_to_check_chr_type(info, true);
		mtk_pe40_set_is_enable(info, true);
		info->enable_dynamic_cv = true;
		get_monotonic_boottime(&data->charging_begin_time);

		return 1;
	}

	return 0;
}

static bool check_time(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct timespec time_now;
	struct timespec charging_time;
	unsigned int hour, min, sec;

	/* do not check time if battery fully charged */
	if (data->state != CHR_BATFULL) {
		get_monotonic_boottime(&time_now);
		charging_time = timespec_sub(time_now,
				data->charging_begin_time);

		data->total_charging_time = charging_time.tv_sec;
	}

	hour = data->total_charging_time / 3600;
	min = data->total_charging_time % 3600 / 60;
	sec = data->total_charging_time % 60;
	pr_info("time [%dh %02dm %02ds]\n", hour, min, sec);

	if (!info->enable_sw_safety_timer)
		return true;

	/* ignore do_charging if force turn-off state */
	if (data->state == CHR_SUSPEND || data->state == CHR_DISABLE
			|| data->state == CHR_ERROR) {
		return true;
	}

	if (data->total_charging_time >= info->data.max_charging_time) {
		pr_err("SW safety timeout: %d sec > %d sec\n",
			data->total_charging_time,
			info->data.max_charging_time);

		charger_dev_notify(info->chg1_dev,
				CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT);
		return false;
	}

	return true;
}

/* charger manager interface */
static int plug_in(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	data->recharging = false;
	data->state = CHR_CC;
	info->polling_interval = CHARGING_INTERVAL;
	data->disable_charging = false;
	get_monotonic_boottime(&data->charging_begin_time);
	charger_manager_notifier(info, CHARGER_NOTIFY_START_CHARGING);

	return 0;
}

static int plug_out(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	disable_slave_charger(info);

	if (info->chr_type == CHARGER_UNKNOWN) {
#ifdef CONFIG_LGE_PM_CHARGER_HZ
		charger_manager_enable_hz(info->chg1_consumer, true);
#endif
		info->usb_state = USB_UNCONFIGURED;
	}

	data->recharging = false;
	data->total_charging_time = 0;

	mtk_pe20_set_is_cable_out_occur(info, true);
	mtk_pe_set_is_cable_out_occur(info, true);
	mtk_pdc_plugout(info);
	mtk_pe40_plugout_reset(info);
	charger_manager_notifier(info, CHARGER_NOTIFY_STOP_CHARGING);

	return 0;
}

static int do_charging(struct charger_manager *info, bool en)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	pr_info("triggered. en:%d\n", en);
	if (en)
		data->disable_charging = false;
	else
		data->disable_charging = true;

	return 0;
}

static int change_current_setting(struct charger_manager *info)
{
	select_current_limit(info);

	if (info->chr_type == CHARGER_UNKNOWN)
		return 0;

	enable_charging(info);

	return 0;
}

static int charging_algorithm(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	bool hvdcp;

	if (data->state == CHR_CC) {
		if (mtk_pe40_is_ready(info)) {
			pr_info("enter PE4.0!\n");
			data->state = CHR_PE40_INIT;
			info->pe4.is_connect = true;
			return 1;
		}
	}

	if (check_eoc(info))
		return 1;

	hvdcp = hvdcp_is_connect(info);

	hvdcp_check_charger(info);
	hvdcp_start_algorithm(info);

	/* renew charge current for hvdcp */
	if (hvdcp_is_connect(info) != hvdcp)
		select_current_limit(info);

	check_safety(info);

	return 0;
}

static int pe40_charging_algorithm(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (check_eoc(info))
		return 1;

	switch (data->state) {
	case CHR_PE40_INIT:
		mutex_lock(&data->slave);

		mtk_pe40_init_state(info);

		mutex_unlock(&data->slave);

		adjust_termiation_current(info);
		if (data->state != CHR_PE40_INIT)
			return 1;
	case CHR_PE40_CC:
	case CHR_PE40_POSTCC:
		mtk_pe40_cc_state(info);
		break;
	default:
		break;
	}

	check_safety(info);

	return 0;
}

static int full_algorithm(struct charger_manager *info)
{
	return check_recharging(info);
}

static int do_algorithm(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	int ret = 0;

	do {
		select_current_limit(info);
		select_voltage_limit(info);

		check_state(info);
		check_time(info);

		pr_info("state [%d]\n", data->state);
		switch (data->state) {
		case CHR_CC:
		case CHR_POSTCC:
			ret = charging_algorithm(info);
			break;
		case CHR_PE40_INIT:
		case CHR_PE40_CC:
		case CHR_PE40_POSTCC:
			ret = pe40_charging_algorithm(info);
			break;
		case CHR_BATFULL:
			ret = full_algorithm(info);
			break;
		case CHR_ERROR:
		case CHR_DISABLE:
		case CHR_SUSPEND:
			data->recharging = false;
			ret = 0;
			break;
		default:
			break;
		}
		enable_charging(info);
		if (!ret)
			ret = run_aicl(info);
	} while (ret != 0 && info->chr_type);

	notify_current_setting(info);

	charger_dev_dump_registers(info->chg1_dev);
	if (is_slave_enabled(info))
		charger_dev_dump_registers(info->chg2_dev);

	return 0;
}

static int handle_eoc(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;

	if (!is_slave_enabled(info))
		return 0;

	tune_current_limit(info);
	if (!is_slave_enabled(info)) {
		adjust_termiation_current(info);
		if (data->state == CHR_CC)
			data->state = CHR_POSTCC;
		if (data->state == CHR_PE40_CC)
			data->state = CHR_PE40_POSTCC;
	}

	charger_dev_reset_eoc_state(info->chg1_dev);

	return 1;
}

static int do_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, chg1_nb);
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct chgdev_notify *notify = v;

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		if (handle_eoc(info)) {
			_wake_up_charger(info);
			return NOTIFY_DONE;
		}

		/* do not notify if cv is not high enough (cv - 200mA) */
		if (data->constant_voltage <= info->data.battery_cv - 200000)
			break;

		charger_manager_notifier(info, CHARGER_NOTIFY_EOC);
		pr_info("%s: end of charge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		charger_manager_notifier(info, CHARGER_NOTIFY_START_CHARGING);
		pr_info("%s: recharge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT:
		info->safety_timeout = true;
		pr_err("%s: safety timer timeout\n", __func__);

		/* If sw safety timer timeout, do not wake up charger thread */
		if (info->enable_sw_safety_timer)
			return NOTIFY_DONE;
		break;
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		info->vbusov_stat = notify->vbusov_stat;
		pr_err("%s: vbus ovp = %d\n", __func__, info->vbusov_stat);
		break;
	case CHARGER_DEV_NOTIFY_VBUS_MIVR:
		pr_err("%s: vbus mivr\n", __func__);
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
		/* ignore mivr event if input current boosted */
		if (info->chgctrl.input_current_boost != -1)
			return NOTIFY_DONE;
#endif
		_wake_up_charger(info);
		return NOTIFY_DONE;
	default:
		return NOTIFY_DONE;
	}

	if (info->chg1_dev->is_polling_mode == false)
		_wake_up_charger(info);

	return NOTIFY_DONE;
}

static void read_custom_data(struct device_node *np, const char *prop, int *data)
{
	u32 val;

	if (!np || !prop || !data)
		return;

	if (of_property_read_u32(np, prop, &val))
		return;

	*data = (int)val;
}

static void lge_charging_custom_init(struct charger_manager *info)
{
	struct lge_charging_alg_data *data = info->algorithm_data;
	struct device_node *np = info->pdev->dev.of_node;

	np = of_get_child_by_name(np, "lge_charging");
	if (!np)
		return;

	/* primary charger */
	read_custom_data(np, "ieoc", &data->ieoc);
	read_custom_data(np, "aicl-interval", &data->aicl_interval);

	/* secondary charger */
	read_custom_data(np, "step-ieoc", &data->step_ieoc);
	read_custom_data(np, "step-ichg", &data->step_ichg);
	read_custom_data(np, "vfloat-offset", &data->vfloat_offset);
	read_custom_data(np, "slave-ichg-percent", &data->slave_ichg_percent);
}

int lge_charging_init(struct charger_manager *info)
{
	struct lge_charging_alg_data *data;

	data = devm_kzalloc(&info->pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev)
		pr_info("Found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	else
		pr_err("*** Error : can't find primary charger ***\n");

	info->chg2_dev = get_charger_by_name("secondary_chg");
	if (info->chg2_dev)
		pr_info("Found secondary charger [%s]\n",
			info->chg2_dev->props.alias_name);

	data->charging_current_tuned = -1;
	data->recharging = false;

	data->ieoc = 150000;
	data->aicl_interval = 60;
	data->step_ieoc = 450000;
	data->step_ichg = 200000;
	data->vfloat_offset = 100000;
	data->slave_ichg_percent = 50;

	mutex_init(&data->lock);
	mutex_init(&data->slave);

	info->algorithm_data = data;
	info->do_algorithm = do_algorithm;
	info->plug_in = plug_in;
	info->plug_out = plug_out;
	info->do_charging = do_charging;
	info->do_event = do_event;
	info->change_current_setting = change_current_setting;

	lge_charging_custom_init(info);

	return 0;
}

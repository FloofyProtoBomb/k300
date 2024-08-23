#define pr_fmt(fmt) "[CC][GM30] %s: " fmt, __func__

#include <linux/power_supply.h>
#include <linux/power/charger_controller.h>
#include <mt-plat/mtk_charger.h>
#include "mtk_charger_intf.h"

#ifdef CONFIG_LGE_PM_USB_ID
#include <linux/power/lge_cable_id.h>
#endif
#ifdef CONFIG_MTK_BOOT
#include <mt-plat/mtk_boot_common.h>
#endif

#include "charger_controller.h"

struct chgctrl_gm30 {
	struct chgctrl *chip;

	struct delayed_work changed_dwork;
	struct charger_consumer *consumer;
	struct charger_manager *manager;
};

static struct charger_consumer *chgctrl_gm30_get_consumer(struct chgctrl_gm30 *gm30)
{
	struct chgctrl *chip = gm30->chip;

	if (!chip)
		return NULL;

	if (gm30->consumer)
		return gm30->consumer;

	gm30->consumer = charger_manager_get_by_name(chip->dev,
			"charger_controller");

	return gm30->consumer;
}

static struct charger_manager *chgctrl_gm30_get_manager(struct chgctrl_gm30 *gm30)
{
	if (gm30->manager)
		return gm30->manager;

	if (gm30->consumer) {
		gm30->manager = gm30->consumer->cm;
		return gm30->manager;
	}

	/* udpate consumer */
	if (chgctrl_gm30_get_consumer(gm30)) {
		gm30->manager = gm30->consumer->cm;
		return gm30->manager;
	}

	return NULL;
}

static void chgctrl_gm30_changed_work(struct work_struct *work)
{
	struct chgctrl_gm30 *gm30 = container_of(work,
			struct chgctrl_gm30, changed_dwork.work);
	struct chgctrl *chip = gm30->chip;

	if (chip && chip->psy)
		power_supply_changed(chip->psy);
}

static void chgctrl_gm30_changed(struct chgctrl_gm30 *gm30)
{
	schedule_delayed_work(&gm30->changed_dwork,
			round_jiffies(msecs_to_jiffies(100)));
}

const char *chgctrl_get_charger_name(struct chgctrl *chip)
{
	struct chgctrl_gm30 *gm30 = chip->impl;
	struct charger_manager *info;

	info = chgctrl_gm30_get_manager(gm30);
	if (info == NULL)
		return "Unknown";

	if (info->chr_type == CHARGER_UNKNOWN)
		return "Unknown";

	if (mtk_is_TA_support_pd_pps(info))
		return "PE40";

	if (is_typec_adapter(info))
		return "USB_C";

	if (mtk_pdc_check_charger(info))
		return "USB_PD";

	switch (info->chr_type) {
	case STANDARD_HOST:
		return "USB";
	case CHARGING_HOST:
		return "USB_CDP";
	case NONSTANDARD_CHARGER:
		return "NON_STD";
	case STANDARD_CHARGER:
		if (mtk_pe20_get_is_connect(info))
			return "PE20";
		if (mtk_pe_get_is_connect(info))
			return "PE";
		return "USB_DCP";
	case APPLE_2_1A_CHARGER:
		return "APPLE_2_1A";
	case APPLE_1_0A_CHARGER:
		return "APPLE_1_0A";
	case APPLE_0_5A_CHARGER:
		return "APPLE_0_5A";
	default:
		break;
	}

	return "Unknown";
}

int chgctrl_get_usb_voltage_now(struct chgctrl *chip)
{
	return pmic_get_vbus() * 1000;
}

int chgctrl_get_boot_mode(void)
{
#ifdef CONFIG_MTK_BOOT
	int boot_mode = get_boot_mode();

	if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT)
		return CC_BOOT_MODE_CHARGER;
	if (boot_mode == LOW_POWER_OFF_CHARGING_BOOT)
		return CC_BOOT_MODE_CHARGER;
#endif

#ifdef CONFIG_LGE_PM_USB_ID
	if (lge_is_factory_cable_boot())
		return CC_BOOT_MODE_FACTORY;
#endif

	return CC_BOOT_MODE_NORMAL;
}

bool chgctrl_ignore_notify(struct power_supply *psy)
{
	union power_supply_propval val;
	int ret;

	if (psy->desc->type == POWER_SUPPLY_TYPE_BATTERY) {
		/* ignore if battery driver not initialzed */
		ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_TEMP, &val);
		if (ret || val.intval <= -1270)
			return true;
	}

	return false;
}

void chgctrl_icl_changed(void *args)
{
	struct chgctrl *chip = args;
	struct chgctrl_gm30 *gm30 = chip->impl;
	struct charger_manager *info;
	int icl;

	info = chgctrl_gm30_get_manager(gm30);
	if (!info)
		return;

	icl = chgctrl_vote_active_value(&chip->icl);
	if (icl > 0)
		icl *= 1000;

	info->chgctrl.input_current_limit = icl;

	if (info->change_current_setting)
		info->change_current_setting(info);

	chgctrl_gm30_changed(gm30);
}

void chgctrl_fcc_changed(void *args)
{
	struct chgctrl *chip = args;
	struct chgctrl_gm30 *gm30 = chip->impl;
	struct charger_manager *info;
	int fcc;

	info = chgctrl_gm30_get_manager(gm30);
	if (!info)
		return;

	fcc = chgctrl_vote_active_value(&chip->fcc);
	if (fcc > 0)
		fcc *= 1000;

	info->chgctrl.charging_current_limit = fcc;

	if (info->change_current_setting)
		info->change_current_setting(info);

	chgctrl_gm30_changed(gm30);
}

void chgctrl_vfloat_changed(void *args)
{
	struct chgctrl *chip = args;
	struct chgctrl_gm30 *gm30 = chip->impl;
	struct charger_manager *info;
	int vfloat;

	info = chgctrl_gm30_get_manager(gm30);
	if (!info)
		return;

	vfloat = chgctrl_vote_active_value(&chip->vfloat);
	if (vfloat > 0)
		vfloat *= 1000;

	info->chgctrl.constant_voltage = vfloat;

	chgctrl_gm30_changed(gm30);
}

void chgctrl_icl_boost_changed(void *args)
{
	struct chgctrl *chip = args;
	struct chgctrl_gm30 *gm30 = chip->impl;
	struct charger_manager *info;
	int icl_boost;

	info = chgctrl_gm30_get_manager(gm30);
	if (!info)
		return;

	icl_boost = chgctrl_vote_active_value(&chip->icl_boost);
	if (icl_boost > 0)
		icl_boost *= 1000;

	info->chgctrl.input_current_boost = icl_boost;

	if (info->change_current_setting)
		info->change_current_setting(info);

	chgctrl_gm30_changed(gm30);
}

void chgctrl_input_suspend_changed(void *args)
{
	struct chgctrl *chip = args;
	struct chgctrl_gm30 *gm30 = chip->impl;
	struct charger_manager *info;
	bool input_suspend = false;

	info = chgctrl_gm30_get_manager(gm30);
	if (!info)
		return;

	if (chgctrl_vote_active_value(&chip->input_suspend))
		input_suspend = true;

	info->chgctrl.input_suspend = input_suspend;

	if (info->change_current_setting)
		info->change_current_setting(info);

	chgctrl_gm30_changed(gm30);
}

void chgctrl_vbus_changed(void *args)
{
	struct chgctrl *chip = args;
	struct chgctrl_gm30 *gm30 = chip->impl;
	struct charger_consumer *consumer;
	struct charger_manager *info;
	int vbus;
	bool hv_charging_disabled = false;
	int ret;

	vbus = chgctrl_vote_active_value(&chip->vbus);
	if (vbus > 0)
		vbus *= 1000;

	info = chgctrl_gm30_get_manager(gm30);
	if (info)
		info->chgctrl.max_charger_voltage = vbus;

	consumer = chgctrl_gm30_get_consumer(gm30);
	if (!consumer)
		return;

	if (vbus >= 0 && vbus < 5500000)
		hv_charging_disabled = true;

	if (hv_charging_disabled == consumer->hv_charging_disabled) {
		chgctrl_gm30_changed(gm30);
		return;
	}

	ret = charger_manager_enable_high_voltage_charging(consumer,
			hv_charging_disabled ? false : true);
	if (ret)
		consumer->hv_charging_disabled = hv_charging_disabled;
}

int chgctrl_impl_init(struct chgctrl *chip)
{
	struct chgctrl_gm30 *gm30;

	gm30 = devm_kzalloc(chip->dev, sizeof(struct chgctrl_gm30),
			GFP_KERNEL);
	if (!gm30)
		return -ENOMEM;

	gm30->chip = chip;
	INIT_DELAYED_WORK(&gm30->changed_dwork, chgctrl_gm30_changed_work);

	chip->impl = gm30;

	return 0;
}

/* mtk charger driver support */
void chgctrl_get_min_icl(int *uA)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int cur;

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	cur = chgctrl_vote_active_value(&chip->icl_boost);
	if (cur > 0)
		cur *= 1000;
	*uA = cur;
}
EXPORT_SYMBOL(chgctrl_get_min_icl);

void chgctrl_get_max_icl(int *uA)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int cur;

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	cur = chgctrl_vote_active_value(&chip->icl);
	if (cur > 0)
		cur *= 1000;
	*uA = cur;
}
EXPORT_SYMBOL(chgctrl_get_max_icl);

void chgctrl_get_max_fcc(int *uA)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int cur;

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	cur = chgctrl_vote_active_value(&chip->fcc);
	if (cur > 0)
		cur *= 1000;
	*uA = cur;
}
EXPORT_SYMBOL(chgctrl_get_max_fcc);

void chgctrl_get_max_vfloat(int *uV)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int vol;

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	vol = chgctrl_vote_active_value(&chip->vfloat);
	if (vol > 0)
		vol *= 1000;
	*uV = vol;
}
EXPORT_SYMBOL(chgctrl_get_max_vfloat);

void chgctrl_get_input_suspend(bool *suspend)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	bool input_suspend = false;

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	if (chgctrl_vote_active_value(&chip->input_suspend))
		input_suspend = true;
	*suspend = input_suspend;
}
EXPORT_SYMBOL(chgctrl_get_input_suspend);

void chgctrl_get_max_vbus(int *uV)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int vbus;

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	if (chgctrl_vote_active_voter(&chip->vbus) < 0)
		return;

	vbus = chgctrl_vote_active_value(&chip->vbus);
	if (vbus > 0)
		vbus *= 1000;
	*uV = vbus;
}
EXPORT_SYMBOL(chgctrl_get_max_vbus);

/* power delivery support */
void chgctrl_get_max_watt(int *uW)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int vbus, icl;

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	if (chgctrl_vote_active_voter(&chip->vbus) < 0)
		return;

	vbus = chgctrl_vote_active_value(&chip->vbus);
	icl = chgctrl_vote_active_value(&chip->icl);

	*uW = vbus * icl;
}
EXPORT_SYMBOL(chgctrl_get_max_watt);

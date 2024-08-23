#include <linux/power_supply.h>
#include <linux/power/charger_controller.h>

#ifdef CONFIG_LGE_PM_USB_ID
#include <linux/power/lge_cable_id.h>
#endif

#include "charger_controller.h"

__attribute__((weak)) const char *chgctrl_get_charger_name(struct chgctrl *chip)
{
	struct charger_name {
		char *psy_name;
		const char *name;
	} chargers[] = {
		{ .psy_name = "usb", .name = "USB" },
		{ .psy_name = "ac", .name = "USB_DCP" },
	};
	struct power_supply *psy;
	union power_supply_propval val;
	int online = 0;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(chargers); i++) {
		psy = power_supply_get_by_name(chargers[i].psy_name);
		if (!psy)
			continue;
		ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_ONLINE, &val);
		if (!ret)
			online = val.intval;

		power_supply_put(psy);

		if (online)
			break;
	}
	if (online)
		return chargers[i].name;

	return "Unknown";
}

__attribute__((weak)) int chgctrl_get_usb_voltage_now(struct chgctrl *chip)
{
	static struct power_supply *psy = NULL;
	union power_supply_propval val;
	int voltage_now = -1;
	int ret;

	if (!psy)
		psy = power_supply_get_by_name("usb");
	if (!psy)
		return voltage_now;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (!ret)
		voltage_now = val.intval;

	return voltage_now;
}

__attribute__((weak)) int chgctrl_get_usb_voltage_max(struct chgctrl *chip)
{
	static struct power_supply *psy = NULL;
	union power_supply_propval val;
	int voltage_max = -1;
	int ret;

	if (!psy)
		psy = power_supply_get_by_name("usb");
	if (!psy)
		return voltage_max;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
	if (!ret)
		voltage_max = val.intval;

	return voltage_max;
}

__attribute__((weak)) int chgctrl_get_usb_current_max(struct chgctrl *chip)
{
	static struct power_supply *psy = NULL;
	union power_supply_propval val;
	int current_max = -1;
	int ret;

	if (!psy)
		psy = power_supply_get_by_name("usb");
	if (!psy)
		return current_max;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	if (!ret)
		current_max = val.intval;

	return current_max;
}

__attribute__((weak)) int chgctrl_get_bat_current_max(struct chgctrl *chip)
{
	static struct power_supply *psy = NULL;
	union power_supply_propval val;
	int current_now = -1;
	int ret;

	if (!psy)
		psy = power_supply_get_by_name("battery");
	if (!psy)
		return current_now;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	if (!ret)
		current_now = val.intval;

	return current_now;
}

__attribute__((weak)) int chgctrl_get_bat_current_now(struct chgctrl *chip)
{
	static struct power_supply *psy = NULL;
	union power_supply_propval val;
	int current_now = -1;
	int ret;

	if (!psy)
		psy = power_supply_get_by_name("battery");
	if (!psy)
		return current_now;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (!ret)
		current_now = val.intval;

	return current_now;
}

__attribute__((weak)) const char *chgctrl_get_bat_manufacturer(struct chgctrl *chip)
{
	static struct power_supply *psy = NULL;
	union power_supply_propval val;
	int ret;

	if (!psy)
		psy = power_supply_get_by_name("battery_id");
	if (!psy)
		return NULL;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_MANUFACTURER, &val);

	power_supply_changed(psy);

	return val.strval;
}

__attribute__((weak)) int chgctrl_get_boot_mode(void)
{
#ifdef CONFIG_LGE_PM_USB_ID
	if (lge_is_factory_cable_boot())
		return CC_BOOT_MODE_FACTORY;
#endif

	return CC_BOOT_MODE_NORMAL;
}

__attribute__((weak)) bool chgctrl_ignore_notify(struct power_supply *psy)
{
	return false;
}

__attribute__((weak)) void chgctrl_icl_changed(void *args)
{
	return;
}

__attribute__((weak)) void chgctrl_fcc_changed(void *args)
{
	return;
}

__attribute__((weak)) void chgctrl_vfloat_changed(void *args)
{
	return;
}

__attribute__((weak)) void chgctrl_icl_boost_changed(void *args)
{
	return;
}

__attribute__((weak)) void chgctrl_input_suspend_changed(void *args)
{
	return;
}

__attribute__((weak)) void chgctrl_vbus_changed(void *args)
{
	return;
}

__attribute__((weak)) int chgctrl_impl_init(struct chgctrl *chip)
{
	return 0;
}

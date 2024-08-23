#define pr_fmt(fmt) "[CC] %s: " fmt, __func__

#include "charger_controller.h"

void chgctrl_battery_id(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, battery_id_work);

	chgctrl_vote(&chip->fcc, FCC_VOTER_BATTERY_ID, chip->battery_id_fcc);
	chgctrl_vote(&chip->vfloat, VFLOAT_VOTER_BATTERY_ID,
			chip->battery_id_vfloat);
}

void chgctrl_psy_handle_batt_id(struct chgctrl *chip, struct power_supply *psy)
{
	union power_supply_propval val;
	int ret;

	chip->battery_id_fcc = -1;
	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_BATT_ID, &val);
	if (!ret) {
		chip->battery_id_fcc = val.intval ? -1 : 0;
		if (chip->boot_mode == CC_BOOT_MODE_FACTORY)
			chip->battery_id_fcc = -1;

	}

	/* default to use default vfloat */
	chip->battery_id_vfloat = -1;
	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
	if (!ret) {
		chip->battery_id_vfloat = val.intval / 1000;
		if (!val.intval)
			chip->battery_id_vfloat = -1;
	}

	schedule_work(&chip->battery_id_work);
}

void chgctrl_battery_id_init(struct chgctrl *chip)
{
	INIT_WORK(&chip->battery_id_work, chgctrl_battery_id);
}

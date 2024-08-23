#define pr_fmt(fmt) "[CC] %s: " fmt, __func__

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/power_supply.h>

#include "charger_controller.h"

static bool chgctrl_thermal_in_hysterysis(struct chgctrl *chip, int idx, int temp)
{
	int exit_temp = chip->thermal[idx].trigger
			- chip->thermal[idx].offset;

	if (temp > exit_temp)
		return true;

	return false;
}

static int chgctrl_thermal_find_idx(struct chgctrl *chip, int temp)
{
	int i;

	for (i = 0; i < chip->thermal_size; i++) {
		if (temp < chip->thermal[i].trigger)
			break;
	}

	return i - 1;
}

static void chgctrl_thermal(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, thermal_work.work);
	int temp = chgctrl_get_temp() / 10;
	int fcc = -1;
	int idx;

	/* find initial idx */
	idx = chgctrl_thermal_find_idx(chip, temp);

	/* same idx. nothing to do */
	if (idx == chip->thermal_idx)
		return;

	/* idx increased. update */
	if (idx > chip->thermal_idx) {
		fcc = chip->thermal[idx].curr;
		goto update;
	}

	/* idx decreased. check temp is in hysterysis range */
	for (idx = chip->thermal_idx; idx >= 0; idx--) {
		if (chgctrl_thermal_in_hysterysis(chip, idx, temp)) {
			fcc = chip->thermal[idx].curr;
			break;
		}
	}

update:
	chip->thermal_idx = idx;

	chgctrl_vote(&chip->fcc, FCC_VOTER_THERMAL, fcc);
}

int chgctrl_thermal_trigger(struct chgctrl *chip)
{
	if (!chip->thermal_enabled)
		return 0;

	schedule_delayed_work(&chip->thermal_work, 0);

	return 0;
}

int chgctrl_thermal_deinit(struct chgctrl *chip)
{
	return 0;
}

int chgctrl_thermal_init(struct chgctrl *chip)
{
	int i;

	if (!chip->thermal || !chip->thermal_size)
		return 0;

	chip->thermal_idx = -1;
	INIT_DELAYED_WORK(&chip->thermal_work, chgctrl_thermal);

	/* dump thermal */
	for (i = 0; i < chip->thermal_size; i++) {
		pr_info("thermal: %2dd(-%dd) %4dmA",
				chip->thermal[i].trigger,
				chip->thermal[i].offset,
				chip->thermal[i].curr);
	}

	chip->thermal_enabled = true;

	return 0;
}

int chgctrl_thermal_parse_dt(struct chgctrl *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct property *prop = NULL;
	const __be32 *data = NULL;
	int size;
	int trigger = 0;
	int ret = 0;
	int i;

	prop = of_find_property(node, "thermal", &size);
	if (!prop)
		return -ENODATA;

	/* invalid data size */
	if (!size || size % sizeof(struct chgctrl_thermal))
		return -EINVAL;

	chip->thermal_size = size / sizeof(struct chgctrl_thermal);
	chip->thermal = devm_kmalloc(chip->dev, size, GFP_KERNEL);
	if (!chip->thermal)
		return -ENOMEM;

	for (i = 0; i < chip->thermal_size; i++) {
		data = of_prop_next_u32(prop, data, &chip->thermal[i].trigger);
		data = of_prop_next_u32(prop, data, &chip->thermal[i].offset);
		data = of_prop_next_u32(prop, data, &chip->thermal[i].curr);

		/* sanity check */
		if (trigger >= chip->thermal[i].trigger)
			goto sanity_check_failed;
		trigger = chip->thermal[i].trigger;
	}

	return ret;

sanity_check_failed:
	pr_err("thermal: invalid data at %d\n", i);
	pr_err("thermal: %d = <%2d, %d, %4d>\n", i,
			chip->thermal[i].trigger,
			chip->thermal[i].offset,
			chip->thermal[i].curr);

	devm_kfree(chip->dev, chip->thermal);
	chip->thermal = NULL;
	chip->thermal_size = 0;

	return -EINVAL;
}

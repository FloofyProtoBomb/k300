#define pr_fmt(fmt) "[CC] %s: " fmt, __func__

#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/power_supply.h>

#include "charger_controller.h"

/* spec */
static bool chgctrl_spec_is_index_valid(struct chgctrl *chip, int idx)
{
	if (idx < 0)
		return false;
	if (idx >= chip->spec_size)
		return false;

	return true;
}

static bool chgctrl_spec_in_hysterysis(struct chgctrl *chip,
		int next, int volt, int temp)
{
	struct chgctrl_spec *spec = chip->spec;
	int present = chip->spec_idx;

	/* if temperature spec is same, check only voltage */
	if (spec[next].tmin == spec[present].tmin &&
			spec[next].tmax == spec[present].tmax)
		goto check_voltage;

	if (temp > spec[next].tmax - 2)
		return true;
	if (temp < spec[next].tmin + 2)
		return true;

	return false;

check_voltage:
	if (volt > spec[next].volt - 200)
		return true;

	return false;
}

static void chgctrl_spec(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, spec_work.work);
	struct chgctrl_spec *spec = chip->spec;
	int spec_size = chip->spec_size;
	int volt, temp;
	int fcc = 0;
	int i;

	/* spec not exist. do not go further */
	if (!chip->spec)
		return;

	/* update battery data */
	temp = chgctrl_get_temp() / 10;
	volt = chgctrl_get_voltage() / 1000;
	if (volt > chip->spec_vfloat)
		volt = chip->spec_vfloat;

	for (i = 0; i < spec_size; i++) {
		if (temp < spec[i].tmin || temp >= spec[i].tmax)
			continue;
		if (volt > spec[i].volt)
			continue;

		/* found spec */
		fcc = spec[i].curr;
		break;
	}

	/* same spec selected, ignore */
	if (i == chip->spec_idx)
		return;

	/* spec fcc first selected. update immediately */
	if (!chgctrl_spec_is_index_valid(chip, chip->spec_idx))
		goto update;

	/* fcc must be decreased. update immediately */
	if (fcc <= spec[chip->spec_idx].curr)
		goto update;

	/* charger not present. update immediately for next charging */
	if (!chip->charger_online)
		goto update;

	/* check hysterisis range */
	if (chgctrl_spec_in_hysterysis(chip, i, volt, temp))
		return;

update:
	/* update selected spec */
	chip->spec_idx = i;

	chgctrl_vote(&chip->fcc, FCC_VOTER_SPEC, fcc);
}

int chgctrl_spec_trigger(struct chgctrl *chip)
{
	if (!chip->spec_enabled)
		return 0;

	schedule_delayed_work(&chip->spec_work, 0);

	return 0;
}

int chgctrl_spec_deinit(struct chgctrl *chip)
{
	if (!chip->spec_enabled)
		return 0;

	chip->spec_enabled = false;
	cancel_delayed_work(&chip->spec_work);

	return 0;
}

int chgctrl_spec_init(struct chgctrl *chip)
{
	int i;

	if (chip->spec_size <= 0)
		return 0;

	if (!chip->spec)
		return 0;

	chip->spec_idx = -1;
	INIT_DELAYED_WORK(&chip->spec_work, chgctrl_spec);

	/* dump spec */
	for (i = 0; i < chip->spec_size; i++) {
		pr_info("spec: %2dd-%2dd %4dmV %4dmA",
				chip->spec[i].tmin, chip->spec[i].tmax,
				chip->spec[i].volt, chip->spec[i].curr);
	}
	pr_info("spec: vfloat = %4dmV\n", chip->spec_vfloat);

	chip->spec_enabled = true;

	return 0;
}

int chgctrl_spec_parse_dt(struct chgctrl *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct property *prop = NULL;
	const __be32 *data = NULL;
	int size;
	int ret = 0;
	int i;

	prop = of_find_property(node, "spec", &size);
	if (!prop)
		return -ENODATA;

	/* invalid data size */
	if (!size || size % sizeof(struct chgctrl_spec))
		return -EINVAL;

	chip->spec_size = size / sizeof(struct chgctrl_spec);
	chip->spec = devm_kmalloc(chip->dev, size, GFP_KERNEL);
	if (!chip->spec)
		return -ENOMEM;

	chip->spec_vfloat = 0;
	for (i = 0; i < chip->spec_size; i++) {
		data = of_prop_next_u32(prop, data, &chip->spec[i].tmin);
		data = of_prop_next_u32(prop, data, &chip->spec[i].tmax);
		data = of_prop_next_u32(prop, data, &chip->spec[i].volt);
		data = of_prop_next_u32(prop, data, &chip->spec[i].curr);

		if (chip->spec_vfloat < chip->spec[i].volt)
			chip->spec_vfloat = chip->spec[i].volt;
	}

	return ret;
}

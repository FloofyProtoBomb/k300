#define pr_fmt(fmt) "[CC] %s: " fmt, __func__

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/power_supply.h>

#include "charger_controller.h"

/* otp */
enum {
	OTP_VERSION_UNKNOWN,
	OTP_VERSION_1_8,
	OTP_VERSION_1_8_SPR,
	OTP_VERSION_1_9,
	OTP_VERSION_2_0,
	OTP_VERSION_2_1,
	OTP_VERSION_MAX
};

enum {
	OTP_STATE_COLD,
	OTP_STATE_COOL,
	OTP_STATE_NORMAL,
	OTP_STATE_HIGH,
	OTP_STATE_OVERHEAT,
};

enum {
	OTP_STATE_VOLTAGE_LOW,
	OTP_STATE_VOLTAGE_HIGH,
};

static char *otp_versions[] = {
	"unknown",
	"1.8",
	"1.8-sprint",
	"1.9",
	"2.0",
	"2.1",
};

static char *otp_states[] = {
	"cold",
	"cool",
	"normal",
	"high",
	"overheat",
};

struct temp_state {
	int state;
	int start;
	int release;
};

/* OTP Version 1.8 */
static int chgctrl_otp_v18_get_temp_state(struct chgctrl *chip)
{
	const struct temp_state high[] = {
		{OTP_STATE_OVERHEAT,	550,	-30},
		{OTP_STATE_HIGH,	450,	-20},
	};
	const struct temp_state low[] = {
		{OTP_STATE_COLD,	-100,	50},
	};
	int state = chip->otp_temp_state;
	int temp = chgctrl_get_temp();
	int i;

	/* check high temperature */
	for (i = 0; i < ARRAY_SIZE(high); i++) {
		if (temp > high[i].start)
			state = max(state, high[i].state);
		if (state != high[i].state)
			continue;
		if (temp < high[i].start + high[i].release)
			state--;
	}

	/* check low temperature */
	for (i = 0; i < ARRAY_SIZE(low); i++) {
		if (temp < low[i].start)
			state = min(state, low[i].state);
		if (state != low[i].state)
			continue;
		if (temp > low[i].start + low[i].release)
			state++;
	}

	/* otp version 1.8 does not support cool state */
	if (state == OTP_STATE_COOL)
		state = OTP_STATE_NORMAL;

	return state;
}

static int chgctrl_otp_v18_get_volt_state(struct chgctrl *chip)
{
	int voltage = chgctrl_get_voltage();

	voltage /= 1000;
	if (voltage > chip->otp_vfloat)
		return OTP_STATE_VOLTAGE_HIGH;

	return OTP_STATE_VOLTAGE_LOW;
}

static bool chgctrl_otp_v18_get_state(struct chgctrl *chip)
{
	int volt_state, temp_state;
	bool changed = false;

	temp_state = chgctrl_otp_v18_get_temp_state(chip);
	if (temp_state != chip->otp_temp_state) {
		chip->otp_temp_state = temp_state;

		changed = true;
	}

	volt_state = chgctrl_otp_v18_get_volt_state(chip);
	if (volt_state != chip->otp_volt_state) {
		chip->otp_volt_state = volt_state;

		/* need to check only in high state */
		if (temp_state == OTP_STATE_HIGH)
			changed = true;
	}

	return changed;
}

static void chgctrl_otp_v18(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, otp_work.work);
	int fcc = 0;

	if (!chgctrl_otp_v18_get_state(chip))
		return;

	pr_info("otp state changed to %s\n",
			otp_states[chip->otp_temp_state]);

	/* set fcc refer to state */
	switch (chip->otp_temp_state) {
	case OTP_STATE_COLD:
	case OTP_STATE_OVERHEAT:
		fcc = 0;
		break;
	case OTP_STATE_HIGH:
		fcc = 0;
		if (chip->otp_volt_state == OTP_STATE_VOLTAGE_LOW)
			fcc = chip->otp_fcc;
		break;
	case OTP_STATE_NORMAL:
	default:
		break;
	}

	chgctrl_vote(&chip->fcc, FCC_VOTER_OTP, fcc);
}

/* OTP Version 1.8 Sprint */
static int chgctrl_otp_v18_spr_get_temp_state(struct chgctrl *chip)
{
	const struct temp_state high[] = {
		{OTP_STATE_OVERHEAT,	530,	-10},
		{OTP_STATE_HIGH,	450,	-10},
	};
	const struct temp_state low[] = {
		{OTP_STATE_COLD,	-50,	10},
	};
	int state = chip->otp_temp_state;
	int temp = chgctrl_get_temp();
	int i;

	/* check high temperature */
	for (i = 0; i < ARRAY_SIZE(high); i++) {
		if (temp > high[i].start)
			state = max(state, high[i].state);
		if (state != high[i].state)
			continue;
		if (temp < high[i].start + high[i].release)
			state--;
	}

	/* check low temperature */
	for (i = 0; i < ARRAY_SIZE(low); i++) {
		if (temp < low[i].start)
			state = min(state, low[i].state);
		if (state != low[i].state)
			continue;
		if (temp > low[i].start + low[i].release)
			state++;
	}

	/* otp version 1.8 does not support cool state */
	if (state == OTP_STATE_COOL)
		state = OTP_STATE_NORMAL;

	return state;
}

static int chgctrl_otp_v18_spr_get_volt_state(struct chgctrl *chip)
{
	int voltage = chgctrl_get_voltage();

	voltage /= 1000;
	if (voltage > chip->otp_vfloat)
		return OTP_STATE_VOLTAGE_HIGH;

	return OTP_STATE_VOLTAGE_LOW;
}

static bool chgctrl_otp_v18_spr_get_state(struct chgctrl *chip)
{
	int volt_state, temp_state;
	bool changed = false;

	temp_state = chgctrl_otp_v18_spr_get_temp_state(chip);
	if (temp_state != chip->otp_temp_state) {
		chip->otp_temp_state = temp_state;

		changed = true;
	}

	volt_state = chgctrl_otp_v18_spr_get_volt_state(chip);
	if (volt_state != chip->otp_volt_state) {
		chip->otp_volt_state = volt_state;

		/* need to check only in high state */
		if (temp_state == OTP_STATE_HIGH)
			changed = true;
	}

	return changed;
}

static void chgctrl_otp_v18_spr(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, otp_work.work);
	int fcc = -1;

	if (!chgctrl_otp_v18_spr_get_state(chip))
		return;

	pr_info("otp state changed to %s\n",
			otp_states[chip->otp_temp_state]);

	/* set fcc refer to state */
	switch (chip->otp_temp_state) {
	case OTP_STATE_COLD:
	case OTP_STATE_OVERHEAT:
		fcc = 0;
		break;
	case OTP_STATE_HIGH:
		fcc = 0;
		if (chip->otp_volt_state == OTP_STATE_VOLTAGE_LOW)
			fcc = chip->otp_fcc;
		break;
	case OTP_STATE_NORMAL:
	default:
		break;
	}

	chgctrl_vote(&chip->fcc, FCC_VOTER_OTP, fcc);
}

/* OTP Version 1.9 */
static int chgctrl_otp_v19_get_temp_state(struct chgctrl *chip)
{
	const struct temp_state high[] = {
		{OTP_STATE_OVERHEAT,	550,	-30},
		{OTP_STATE_HIGH,	450,	-20},
	};
	const struct temp_state low[] = {
		{OTP_STATE_COLD,	0,	30},
		{OTP_STATE_COOL,	100,	20},
	};
	int state = chip->otp_temp_state;
	int temp = chgctrl_get_temp();
	int i;

	/* check high temperature */
	for (i = 0; i < ARRAY_SIZE(high); i++) {
		if (temp > high[i].start)
			state = max(state, high[i].state);
		if (state != high[i].state)
			continue;
		if (temp < high[i].start + high[i].release)
			state--;
	}

	/* check low temperature */
	for (i = 0; i < ARRAY_SIZE(low); i++) {
		if (temp < low[i].start)
			state = min(state, low[i].state);
		if (state != low[i].state)
			continue;
		if (temp > low[i].start + low[i].release)
			state++;
	}

	return state;
}

static int chgctrl_otp_v19_get_volt_state(struct chgctrl *chip)
{
	int voltage = chgctrl_get_voltage();

	voltage /= 1000;
	if (voltage > chip->otp_vfloat)
		return OTP_STATE_VOLTAGE_HIGH;

	return OTP_STATE_VOLTAGE_LOW;
}

static bool chgctrl_otp_v19_get_state(struct chgctrl *chip)
{
	int volt_state, temp_state;
	bool changed = false;

	temp_state = chgctrl_otp_v19_get_temp_state(chip);
	if (temp_state != chip->otp_temp_state) {
		chip->otp_temp_state = temp_state;

		changed = true;
	}

	volt_state = chgctrl_otp_v19_get_volt_state(chip);
	if (volt_state != chip->otp_volt_state) {
		chip->otp_volt_state = volt_state;

		/* need to check only in high state */
		if (temp_state == OTP_STATE_HIGH)
			changed = true;
	}

	return changed;
}

static void chgctrl_otp_v19(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, otp_work.work);
	int fcc = -1;
	if (!chgctrl_otp_v19_get_state(chip))
		return;

	pr_info("otp state changed to %s\n",
			otp_states[chip->otp_temp_state]);

	/* set fcc refer to state */
	switch (chip->otp_temp_state) {
	case OTP_STATE_COLD:
	case OTP_STATE_OVERHEAT:
		fcc = 0;
		break;
	case OTP_STATE_HIGH:
		fcc = 0;
		if (chip->otp_volt_state == OTP_STATE_VOLTAGE_LOW)
			fcc = chip->otp_fcc;
		break;
	case OTP_STATE_COOL:
		fcc = chip->otp_fcc;
		break;
	case OTP_STATE_NORMAL:
	default:
		break;
	}

	chgctrl_vote(&chip->fcc, FCC_VOTER_OTP, fcc);
}

/* OTP Version 2.0 */
static int chgctrl_otp_v20_get_temp_state(struct chgctrl *chip)
{
	const struct temp_state high[] = {
		{OTP_STATE_OVERHEAT,	550,	-30},
		{OTP_STATE_HIGH,	450,	-20},
	};
	const struct temp_state low[] = {
		{OTP_STATE_COLD,	0,	30},
		{OTP_STATE_COOL,	100,	20},
	};
	int state = chip->otp_temp_state;
	int temp = chgctrl_get_temp();
	int i;

	/* check high temperature */
	for (i = 0; i < ARRAY_SIZE(high); i++) {
		if (temp > high[i].start)
			state = max(state, high[i].state);
		if (state != high[i].state)
			continue;
		if (temp < high[i].start + high[i].release)
			state--;
	}

	/* check low temperature */
	for (i = 0; i < ARRAY_SIZE(low); i++) {
		if (temp < low[i].start)
			state = min(state, low[i].state);
		if (state != low[i].state)
			continue;
		if (temp > low[i].start + low[i].release)
			state++;
	}

	return state;
}

static int chgctrl_otp_v20_get_volt_state(struct chgctrl *chip)
{
	int voltage = chgctrl_get_voltage();

	voltage /= 1000;
	if (voltage > chip->otp_vfloat)
		return OTP_STATE_VOLTAGE_HIGH;

	return OTP_STATE_VOLTAGE_LOW;
}

static bool chgctrl_otp_v20_get_state(struct chgctrl *chip)
{
	int volt_state, temp_state;
	bool changed = false;

	temp_state = chgctrl_otp_v20_get_temp_state(chip);
	if (temp_state != chip->otp_temp_state) {
		chip->otp_temp_state = temp_state;

		changed = true;
	}

	volt_state = chgctrl_otp_v20_get_volt_state(chip);
	if (volt_state != chip->otp_volt_state) {
		chip->otp_volt_state = volt_state;

		/* need to check only in high state */
		if (temp_state == OTP_STATE_HIGH)
			changed = true;
	}

	return changed;
}

static void chgctrl_otp_v20(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, otp_work.work);
	int fcc = -1;
	int vfloat = -1;

	if (!chgctrl_otp_v20_get_state(chip))
		return;

	pr_info("otp state changed to %s\n",
			otp_states[chip->otp_temp_state]);
	if (chip->otp_temp_state == OTP_STATE_OVERHEAT)
		chip->otp_health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (chip->otp_temp_state == OTP_STATE_COLD)
		chip->otp_health = POWER_SUPPLY_HEALTH_COLD;
	else
		chip->otp_health = POWER_SUPPLY_HEALTH_GOOD;

	/* set fcc, vfloat refer to state */
	switch (chip->otp_temp_state) {
	case OTP_STATE_COLD:
	case OTP_STATE_OVERHEAT:
		fcc = 0;
		break;
	case OTP_STATE_HIGH:
		fcc = 0;
		if (chip->otp_volt_state == OTP_STATE_VOLTAGE_LOW) {
			fcc = chip->otp_fcc;
			vfloat = chip->otp_vfloat;
		}
		break;
	case OTP_STATE_COOL:
		fcc = chip->otp_fcc;
		break;
	case OTP_STATE_NORMAL:
	default:
		break;
	}

	chgctrl_vote(&chip->fcc, FCC_VOTER_OTP, fcc);
	chgctrl_vote(&chip->vfloat, VFLOAT_VOTER_OTP, vfloat);
}

/* OTP Version 2.1 */
static int chgctrl_otp_v21_get_temp_state(struct chgctrl *chip)
{
	const struct temp_state high[] = {
		{OTP_STATE_OVERHEAT,	550,	-30},
		{OTP_STATE_HIGH,	450,	-20},
	};
	const struct temp_state low[] = {
		{OTP_STATE_COLD,	0,	30},
		{OTP_STATE_COOL,	100,	20},
	};
	int state = chip->otp_temp_state;
	int temp = chgctrl_get_temp();
	int i;

	/* check high temperature */
	for (i = 0; i < ARRAY_SIZE(high); i++) {
		if (temp > high[i].start)
			state = max(state, high[i].state);
		if (state != high[i].state)
			continue;
		if (temp < high[i].start + high[i].release)
			state--;
	}

	/* check low temperature */
	for (i = 0; i < ARRAY_SIZE(low); i++) {
		if (temp < low[i].start)
			state = min(state, low[i].state);
		if (state != low[i].state)
			continue;
		if (temp > low[i].start + low[i].release)
			state++;
	}

	return state;
}

static int chgctrl_otp_v21_get_volt_state(struct chgctrl *chip)
{
	int voltage = chgctrl_get_voltage();

	voltage /= 1000;
	if (voltage > chip->otp_vfloat)
		return OTP_STATE_VOLTAGE_HIGH;

	return OTP_STATE_VOLTAGE_LOW;
}

static bool chgctrl_otp_v21_get_state(struct chgctrl *chip)
{
	int volt_state, temp_state;
	bool changed = false;

	temp_state = chgctrl_otp_v21_get_temp_state(chip);
	if (temp_state != chip->otp_temp_state) {
		chip->otp_temp_state = temp_state;

		changed = true;
	}

	volt_state = chgctrl_otp_v21_get_volt_state(chip);
	if (volt_state != chip->otp_volt_state) {
		chip->otp_volt_state = volt_state;

		/* need to check only in high state */
		if (temp_state == OTP_STATE_HIGH)
			changed = true;
		if (temp_state == OTP_STATE_COOL)
			changed = true;
	}

	return changed;
}

static void chgctrl_otp_v21(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, otp_work.work);
	int fcc = -1;
	int vfloat = -1;

	if (!chgctrl_otp_v21_get_state(chip))
		return;

	pr_info("otp state changed to %s\n",
			otp_states[chip->otp_temp_state]);
	if (chip->otp_temp_state == OTP_STATE_OVERHEAT)
		chip->otp_health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (chip->otp_temp_state == OTP_STATE_COLD)
		chip->otp_health = POWER_SUPPLY_HEALTH_COLD;
	else
		chip->otp_health = POWER_SUPPLY_HEALTH_GOOD;

	/* set fcc, vfloat refer to state */
	switch (chip->otp_temp_state) {
	case OTP_STATE_COLD:
	case OTP_STATE_OVERHEAT:
		fcc = 0;
		break;
	case OTP_STATE_HIGH:
		fcc = 0;
		if (chip->otp_volt_state == OTP_STATE_VOLTAGE_LOW) {
			fcc = chip->otp_fcc;
			vfloat = chip->otp_vfloat;
		}
		break;
	case OTP_STATE_COOL:
		fcc = chip->otp_fcc;
		if (chip->otp_volt_state == OTP_STATE_VOLTAGE_HIGH)
			fcc = 500;
		break;
	case OTP_STATE_NORMAL:
	default:
		break;
	}

	chgctrl_vote(&chip->fcc, FCC_VOTER_OTP, fcc);
	chgctrl_vote(&chip->vfloat, VFLOAT_VOTER_OTP, vfloat);
}

int chgctrl_otp_trigger(struct chgctrl *chip)
{
	if (!chip->otp_enabled)
		return 0;

	schedule_delayed_work(&chip->otp_work, 0);

	return 0;
}

int chgctrl_otp_deinit(struct chgctrl *chip)
{
	if (!chip->otp_enabled)
		return 0;

	chip->otp_enabled = false;
	cancel_delayed_work(&chip->otp_work);

	return 0;
}

int chgctrl_otp_init(struct chgctrl *chip)
{
	chip->otp_health = POWER_SUPPLY_HEALTH_GOOD;
	chip->otp_temp_state = OTP_STATE_NORMAL;
	chip->otp_volt_state = OTP_STATE_VOLTAGE_LOW;

	switch(chip->otp_version) {
	case OTP_VERSION_1_8:
		INIT_DELAYED_WORK(&chip->otp_work, chgctrl_otp_v18);
		break;
	case OTP_VERSION_1_8_SPR:
		INIT_DELAYED_WORK(&chip->otp_work, chgctrl_otp_v18_spr);
		break;
	case OTP_VERSION_1_9:
		INIT_DELAYED_WORK(&chip->otp_work, chgctrl_otp_v19);
		break;
	case OTP_VERSION_2_0:
		INIT_DELAYED_WORK(&chip->otp_work, chgctrl_otp_v20);
		break;
	case OTP_VERSION_2_1:
		INIT_DELAYED_WORK(&chip->otp_work, chgctrl_otp_v21);
		break;
	default:
		break;
	}

	if (chip->otp_version == OTP_VERSION_UNKNOWN)
		return 0;

	pr_info("otp: version %s\n", otp_versions[chip->otp_version]);
	pr_info("otp: fcc=%dmA, vfloat=%dmV\n", chip->otp_fcc, chip->otp_vfloat);

	chip->otp_enabled = true;

	return 0;
}

int chgctrl_otp_parse_dt(struct chgctrl *chip)
{
	struct device_node *node = chip->dev->of_node;
	const char *version;
	int i;
	int ret = 0;

	ret = of_property_read_u32(node, "otp-fcc", &chip->otp_fcc);
	if (ret)
		chip->otp_fcc = 1000;
	ret = of_property_read_u32(node, "otp-vfloat", &chip->otp_vfloat);
	if (ret)
		chip->otp_vfloat = 4000;

	ret = of_property_read_string(node, "otp-version", &version);
	if (!ret) {
		/* find otp version */
		for (i = 0; i < ARRAY_SIZE(otp_versions); i++) {
			if (!strcmp(version, otp_versions[i])) {
				chip->otp_version = i;
				break;
			}
		}
	}


	return 0;
}

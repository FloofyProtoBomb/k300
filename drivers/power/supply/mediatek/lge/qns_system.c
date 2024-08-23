/*
* qns_system.c version 1.2
* Qnovo QNS wrapper implementation
* Copyright (C) 2014 Qnovo Corp
* Miro Zmrzli ,miro@qnovocorp.com>
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#define pr_fmt(fmt)    "%s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/alarmtimer.h>

#define READ_CURRENT_SIGN	(-1)
#define CHARGE_CURRENT_PROP	POWER_SUPPLY_PROP_QNS_FCC
#define CHARGE_VOLTAGE_PROP	POWER_SUPPLY_PROP_QNS_VFLOAT

#define QNS_OK			0
#define QNS_ERROR		-1

static struct power_supply* battery_psy = NULL;
static struct power_supply* charger_psy = NULL;
static struct power_supply* battery_id_psy = NULL;

static struct alarm alarm;
static bool alarm_inited = false;
static int alarm_value = 0;

static struct wakeup_source wakelock;
static bool wakelock_inited = false;
static bool wakelock_held = false;

static struct wakeup_source charge_wakelock;
static bool charge_wakelock_inited = false;
static bool charge_wakelock_held = false;

static int options = -1;

static struct power_supply* qns_get_battery_psy(const char *err_msg)
{
	if (battery_psy)
		return battery_psy;

	battery_psy = power_supply_get_by_name("battery");
	if (battery_psy == NULL)
		pr_err("QNS: ERROR: unable to get battery. %s\n", err_msg);

	return battery_psy;
}

static struct power_supply* qns_get_charger_psy(const char *err_msg)
{
	if (charger_psy)
		return charger_psy;

	charger_psy = power_supply_get_by_name("charger_controller");
	if (charger_psy == NULL)
		pr_err("QNS: ERROR: unable to get charger_controller. %s\n", err_msg);

	return charger_psy;
}

static struct power_supply* qns_get_battery_id_psy(const char *err_msg)
{
	if (battery_id_psy)
		return battery_id_psy;

	battery_id_psy = power_supply_get_by_name("battery_id");
	if (battery_id_psy == NULL)
		pr_err("QNS: ERROR: unable to get battery_id. %s\n", err_msg);

	return battery_id_psy;
}

static int qns_set_ibat(int mA)
{
	static int prev_mA = -1;
	struct power_supply *psy;
	union power_supply_propval propVal;
	int ret;

	if (prev_mA == mA)
		return QNS_OK;

	pr_info("QNS: new charge current: %dmA\n", mA);

	psy = qns_get_charger_psy("Can't set the current!");
	if (!psy)
		return QNS_ERROR;

	propVal.intval = mA;
	ret = power_supply_set_property(psy, CHARGE_CURRENT_PROP, &propVal);
	if (ret) {
		pr_err("QNS: ERROR: unable to set charging current! Does %s have "
				"POWER_SUPPLY_PROP_QNS_FCC property?\n", psy->desc->name);
		return QNS_ERROR;
	}

	prev_mA = mA;

	return QNS_OK;
}

static int qns_set_vbat(int mV)
{
	static int prev_mV = -1;
	struct power_supply *psy;
	union power_supply_propval propVal;
	int ret;

	if (prev_mV == mV)
		return QNS_OK;

	pr_info("QNS: new charge voltage: %dmV", mV);

	psy = qns_get_charger_psy("Can't set the voltage!");
	if (!psy)
		return QNS_ERROR;

	propVal.intval = mV;
	ret = power_supply_set_property(psy, CHARGE_VOLTAGE_PROP, &propVal);
	if (ret) {
		pr_err("QNS: ERROR: unable to set charging voltage! Does %s have "
				"POWER_SUPPLY_PROP_QNS_VFLOAT property?\n", psy->desc->name);
		return QNS_ERROR;
	}

	prev_mV = mV;

	return QNS_OK;
}

static bool qns_is_charging(void)
{
	struct power_supply *psy;
	union power_supply_propval propVal = {0, };

	psy = qns_get_charger_psy("Can't read charging state!");
	if (!psy)
		return false;

	if (power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS, &propVal) != 0) {
		pr_err("QNS: ERROR: unable to read charger properties! Dose %s have "
				"POWER_SUPPLY_PROP_STATUS property?\n", psy->desc->name);
		return false;
	}

	return propVal.intval == POWER_SUPPLY_STATUS_CHARGING;
}

static int qns_get_scvt(int *soc, int *c, int *v, int *tx10)
{
	/*
	 * soc : battery capacity in %
	 * c : current in mA
	 * v : voltage in mV
	 * t : temperature in 0.1 dec c
	 */
	struct power_supply *psy;
	union power_supply_propval ret = {0, };
	int retVal = QNS_OK;

	psy = qns_get_battery_psy("Can't read soc/c/v/t!");
	if (!psy) {
		pr_err("QNS: battery power supply is not registered yet.\n");
		if (c != NULL) *c = 0;
		if (v != NULL) *v = 4000;
		if (tx10 != NULL) *tx10 = 250;
		if (soc != NULL) *soc = 50;
		return QNS_ERROR;
	}

	if (c != NULL) {
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &ret) != 0) {
			pr_err("QNS: ERROR: unable to read battery property POWER_SUPPLY_PROP_CURRENT_NOW\n");
			*c = 0;
			retVal = QNS_ERROR;
		} else {
			*c = READ_CURRENT_SIGN * ret.intval / 1000;
		}
	}

	if (v != NULL) {
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &ret) != 0) {
			pr_err("QNS: ERROR: unable to read battery property POWER_SUPPLY_PROP_VOLTAGE_NOW\n");
			*v = 0;
			retVal = QNS_ERROR;
		} else {
			*v = ret.intval / 1000;
		}
	}

	if (tx10 != NULL) {
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_TEMP, &ret) != 0) {
			pr_err("QNS: ERROR: unable to read battery property POWER_SUPPLY_PROP_TEMP\n");
			*tx10 = 0;
			retVal = QNS_ERROR;
		} else {
			*tx10 = ret.intval;
		}
	}

	if (soc != NULL) {
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &ret) != 0) {
			pr_err("QNS: ERROR: unable to read battery property POWER_SUPPLY_PROP_CAPACITY\n");
			*soc = 0;
			retVal = QNS_ERROR;
		} else {
			*soc = ret.intval;
		}
	}

	return retVal;
}

static int qns_get_fcc(int *fcc, int *design)
{
	/*
	 * fcc : POWER_SUPPLY_PROP_CHARGE_FULL
	 * design : POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN
	 */
	struct power_supply *psy;
	union power_supply_propval ret = {0, };
	int retVal = QNS_OK;

	psy = qns_get_battery_psy("Can't read fcc/design!");
	if (!psy)
		return QNS_ERROR;

	if (fcc != NULL) {
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL, &ret) != 0) {
			pr_err("QNS: ERROR: unable to read battery POWER_SUPPLY_PROP_CHARGE_FULL property.\n");
			*fcc = 0;
			retVal = QNS_ERROR;
		} else {
			*fcc = ret.intval / 1000;
		}
	}

	if (design != NULL) {
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &ret) != 0) {
			pr_err("QNS: ERROR: unable to read battery POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN property.\n");
			*design = 0;
			retVal = QNS_ERROR;
		} else {
			*design = ret.intval / 1000;
		}
	}

	return retVal;
}

#define QNS_BATTERY_TYPE_LEN 50
static char qns_battery_type[QNS_BATTERY_TYPE_LEN];

static int qns_get_battery_type(const char **battery_type)
{
	struct power_supply *psy;
	union power_supply_propval model_name;
	union power_supply_propval charge_full_design;
	union power_supply_propval manufacturer;
	int ret;

	if (!battery_type)
		return QNS_ERROR;

	psy = qns_get_battery_id_psy("Can't read battery_type!");
	if (!psy)
		return QNS_ERROR;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_MODEL_NAME,
			&model_name);
	if (ret) {
		pr_err("QNS: ERROR: unable to read battery "
			"POWER_SUPPLY_PROP_MODEL_NAME property.\n");

		return QNS_ERROR;
	}
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_MANUFACTURER,
			&manufacturer);
	if (ret) {
		pr_err("QNS: ERROR: unable to read battery "
			"POWER_SUPPLY_PROP_MANUFACTURER property.\n");

		return QNS_ERROR;
	}
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
			&charge_full_design);
	if (ret) {
		pr_err("QNS: ERROR: unable to read battery "
			"POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN property.\n");

		return QNS_ERROR;
	}

	/* make battery type string as LGE_BLT39_Lishen_3000mA */
	snprintf(qns_battery_type, QNS_BATTERY_TYPE_LEN, "LGE_%s_%s_%dmAh",
			model_name.strval, manufacturer.strval, charge_full_design.intval / 1000);
	pr_info("QNS: QNS_BATTERY_TYPE = %s\n", qns_battery_type);

	*battery_type = qns_battery_type;

	return QNS_OK;
}

static ssize_t qns_param_show(struct class *dev, struct class_attribute *attr,
			      char *buf);
static ssize_t qns_param_store(struct class *dev, struct class_attribute *attr,
			       const char *buf, size_t count);

#define QNS_ATTR_READ (S_IRUGO)
#define QNS_ATTR_WRITE (S_IWUSR|S_IWGRP)
#define QNS_ATTR_RW (S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP|S_IROTH)

static struct class_attribute qns_attrs[] = {
	__ATTR(charging_state, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(current_now, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(voltage, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(temp, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(fcc, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(design, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(soc, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(battery_type, QNS_ATTR_READ, qns_param_show, NULL),
	__ATTR(charge_current, QNS_ATTR_WRITE, NULL, qns_param_store),
	__ATTR(charge_voltage, QNS_ATTR_WRITE, NULL, qns_param_store),
	__ATTR(alarm, QNS_ATTR_RW, qns_param_show, qns_param_store),
	__ATTR(options, QNS_ATTR_RW, qns_param_show, qns_param_store),
	__ATTR_NULL,
};

enum {
	IS_CHARGING,
	CURRENT,
	VOLTAGE,
	TEMPERATURE,
	FCC,
	DESIGN,
	SOC,
	BATTERY_TYPE,
	CHARGE_CURRENT,
	CHARGE_VOLTAGE,
	ALARM,
	OPTIONS,
};

static enum alarmtimer_restart qns_alarm_handler(struct alarm * alarm, ktime_t now)
{
	pr_info("QNS: ALARM! System wakeup!\n");
	__pm_stay_awake(&wakelock);
	wakelock_held = true;
	alarm_value = 1;
	return ALARMTIMER_NORESTART;
}

static ssize_t qns_param_show(struct class *dev, struct class_attribute *attr,
			      char *buf)
{
	ssize_t size = 0;
	const ptrdiff_t off = attr - qns_attrs;
	static int t, c, v;
	const char *battery_type = "Unknown";

	switch (off) {
	case IS_CHARGING:
		size = scnprintf(buf, PAGE_SIZE, "%d\n", qns_is_charging() ? 1 : 0);
		break;
	case CURRENT:
		qns_get_scvt(NULL, &c, &v, NULL);
		size = scnprintf(buf, PAGE_SIZE, "%d\n", c);
		break;
	case VOLTAGE:
		size = scnprintf(buf, PAGE_SIZE, "%d\n", v);
		break;
	case TEMPERATURE:
		qns_get_scvt(NULL, NULL, NULL, &t);
		size = scnprintf(buf, PAGE_SIZE, "%d\n", t);
		break;
	case FCC:
		qns_get_fcc(&t, NULL);
		size = scnprintf(buf, PAGE_SIZE, "%d\n", t);
		break;
	case DESIGN:
		qns_get_fcc(NULL, &t);
		size = scnprintf(buf, PAGE_SIZE, "%d\n", t);
		break;
	case SOC:
		qns_get_scvt(&t, NULL, NULL, NULL);
		size = scnprintf(buf, PAGE_SIZE, "%d\n", t);
		break;
	case ALARM:
		size = scnprintf(buf, PAGE_SIZE, "%d\n", alarm_value);
		break;
	case OPTIONS:
		size = scnprintf(buf, PAGE_SIZE, "%d\n", options);
		break;
	case BATTERY_TYPE:
		qns_get_battery_type(&battery_type);
		size = scnprintf(buf, PAGE_SIZE, "%s\n", battery_type);
		break;
	}

	return size;
}

enum alarm_values {
	CHARGE_WAKELOCK = -4,
	CHARGE_WAKELOCK_RELEASE = -3,
	HANDLED = -2,
	CANCEL = -1,
	IMMEDIATE = 0,
};

static ssize_t qns_param_store(struct class *dev, struct class_attribute *attr,
			       const char *buf, size_t count)
{
	int val, ret = -EINVAL;
	ktime_t next_alarm;
	const ptrdiff_t off = attr - qns_attrs;

	switch(off) {
	case CHARGE_CURRENT:
		ret = kstrtoint(buf, 10, &val);
		if (!ret && (val > 0)) {
			qns_set_ibat(val);
			return count;
		} else {
			return -EINVAL;
		}
		break;
	case CHARGE_VOLTAGE:
		ret = kstrtoint(buf, 10, &val);
		if (!ret && (val > 0)) {
			qns_set_vbat(val);
			return count;
		} else {
			return -EINVAL;
		}
		break;
	case ALARM:
		ret = kstrtoint(buf, 10, &val);

		if (!wakelock_inited) {
			wakeup_source_init(&wakelock, "QnovoQNS");
			wakelock_inited = true;
		}

		if (!charge_wakelock_inited) {
			wakeup_source_init(&charge_wakelock, "QnovoQNS_charge");
			charge_wakelock_inited = true;
		}

		if (ret)
			break;

		if (val == CHARGE_WAKELOCK) {
			if (!charge_wakelock_held) {
				pr_info("QNS: ALARM: acquiring charge_wakelock via CHARGE_WAKELOCK\n");
				__pm_stay_awake(&charge_wakelock);
				charge_wakelock_held = true;
			}
		} else if (val == CHARGE_WAKELOCK_RELEASE) {
			if (charge_wakelock_held) {
				pr_info("QNS: Alarm: releasing charge_wakelock via CHARGE_WAKELOCK_RELEASE\n");
				__pm_relax(&charge_wakelock);
				charge_wakelock_held = false;
			}
		} else if (val == HANDLED) {
			if (wakelock_held) {
					pr_info("QNS: Alarm: releasing wakelock via HANDLED\n");
					__pm_relax(&wakelock);
			}
			alarm_value = 0;
			wakelock_held = false;
		} else if (val == CANCEL) {
			if (alarm_inited) {
				alarm_cancel(&alarm);
			}
			alarm_value = 0;
			if(wakelock_held) {
				pr_info("QNS: Alarm: releasing wakelock via CANCEL\n");
				__pm_relax(&wakelock);
			}
			wakelock_held = false;
		} else if (val == IMMEDIATE) {
			if (!wakelock_held) {
				pr_info("QNS: Alarm: acquiring wakelock via IMMEDIATE\n");

				__pm_stay_awake(&wakelock);
				wakelock_held = true;
			}
		} else if(val > 0) {
			if (!alarm_inited) {
				alarm_init(&alarm, ALARM_REALTIME, qns_alarm_handler);
				alarm_inited = true;
			}

			next_alarm = ktime_set(val, 0);
			alarm_start_relative(&alarm, next_alarm);

			if (wakelock_held) {
				pr_info("QNS: Alarm: releasing wakelock via alarm>0\n");

				__pm_relax(&wakelock);
			}
			alarm_value = 0;
			wakelock_held = false;
		}
		break;
	case OPTIONS:
		ret = kstrtoint(buf, 10, &val);
		pr_info("----------------- QNS: Options: %d\n", val);
		if (!ret && (val >= 0))
			options = val;
		else
			return -EINVAL;
		break;
	}

	return count;
}

static struct class qns_class = {
	.name = "qns",
	.owner = THIS_MODULE,
	.class_attrs = qns_attrs,
};

MODULE_AUTHOR("Miro Zmrzli <miro@qnovocorp.com>");
MODULE_DESCRIPTION("QNS System Driver v2");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("QNS");

static int qnovo_qns_init(void)
{
	class_register(&qns_class);

	return 0;
}
static void qnovo_qns_exit(void)
{
	class_unregister(&qns_class);
}

module_init(qnovo_qns_init);
module_exit(qnovo_qns_exit);

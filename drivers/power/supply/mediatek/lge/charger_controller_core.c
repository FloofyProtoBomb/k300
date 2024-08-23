/* Copyright (C) 2017 LG Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt) "[CC] %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/async.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include "charger_controller.h"

#ifdef CONFIG_TCPC_CLASS
#include <tcpm.h>
#endif

#define CC_RW_PERM (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)

#define TIME_TO_FULL_MODE

/* display */
static void chgctrl_fb(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work, struct chgctrl, fb_work);
	int fcc = chip->display_on ? chip->fb_fcc : -1;

	chgctrl_vote(&chip->fcc, FCC_VOTER_DISPLAY, fcc);
}

static void chgctrl_fb_trigger(struct chgctrl *chip)
{
	if (!chip->fb_enabled)
		return;

	if (chip->boot_mode != CC_BOOT_MODE_NORMAL)
		return;

	schedule_work(&chip->fb_work);
}

static int chgctrl_fb_parse_dt(struct chgctrl *chip)
{
	struct device_node *node = chip->dev->of_node;
	int ret = 0;

	ret = of_property_read_u32(node, "fb-fcc", &chip->fb_fcc);
	if (ret)
		return ret;

	pr_info("fb: fcc=%dmA\n", chip->fb_fcc);

	chip->fb_enabled = true;

	return 0;
}


#ifdef TIME_TO_FULL_MODE
/* time to full(TTF) */
#define DISCHARGE			-1
#define FULL				-1
#define EMPTY				0
#define NOT_SUPPORT_STEP_CHARGING	1

#define TTF_START_MS	(7000)	 /* 7 sec */
#define TTF_MONITOR_MS	(10000)

static int ibat_set_max = 0;
static int time_to_full_monitor_time = TTF_START_MS;
static int pre_soc = 0;
static int batt_comp_value = 0;
static int ibat_now_for_profile = 0;

static int time_to_full_time_get_current(struct chgctrl *chip)
{
	const char *chgtype = chgctrl_get_charger_name(chip);
	int iusb_aicl = 0;
	int i;

	if (ibat_set_max)
		return ibat_set_max;

	if (!strncmp(chgtype,"PE",2)) {
		ibat_set_max = chip->pep_current;
		goto update;
	}
	else if (!strcmp(chgtype,"USB_DCP")) {
		iusb_aicl = chgctrl_get_usb_current_max(chip);
		iusb_aicl /= 1000;
		iusb_aicl += 100; /* aicl error compensation */
		ibat_set_max = iusb_aicl;

		if (iusb_aicl > chip->dcp_current)
			ibat_set_max = chip->dcp_current;
		else
			ibat_set_max -= 100;
		if (chip->dcp_comp)
			ibat_set_max += (ibat_set_max * chip->dcp_comp /100);
		pr_info("[TTF] Set dcp_current (%d)\n", ibat_set_max);
		return ibat_set_max;
	}
	else if (!strcmp(chgtype,"USB")) {
		ibat_set_max = chip->sdp_current;
		if (chip->sdp_comp)
			ibat_set_max += (ibat_set_max * chip->sdp_comp /100);
		pr_info("[TTF] Set sdp_current (%d)\n", ibat_set_max);
		return ibat_set_max;
	}
	else if (!strcmp(chgtype,"NON_STD")) {
		iusb_aicl = chgctrl_get_usb_current_max(chip);
		iusb_aicl /= 1000;
		ibat_set_max = iusb_aicl;

		if (iusb_aicl > chip->non_std_current)
			ibat_set_max = chip->non_std_current;
		if (chip->non_std_comp)
			ibat_set_max += (ibat_set_max * chip->non_std_comp /100);

		pr_info("[TTF] Set non_std_current (%d)\n", ibat_set_max);
		return ibat_set_max;
	}
	else if (!strcmp(chgtype,"USB_PD")) {
		iusb_aicl = chgctrl_get_usb_current_max(chip);
		iusb_aicl /= 1000;
		ibat_set_max = iusb_aicl;

		if (chip->pd_comp)
			ibat_set_max += (ibat_set_max * chip->pd_comp /100);

		pr_info("[TTF] Set pd_current (%d)\n", ibat_set_max);
		return ibat_set_max;
	}
	else {
		iusb_aicl = chgctrl_get_usb_current_max(chip);
		iusb_aicl /= 1000;
		ibat_set_max = iusb_aicl;

		pr_info("[TTF] Set No Defined dcp_current (%d)\n", ibat_set_max);
		return ibat_set_max;
	}

update:
	for (i = 0; i < chip->cc_data_length ;i++) {
		if (ibat_set_max >= chip->cc_data[i].cur)
			break;
	}
	i = i >= chip->cc_data_length ? chip->cc_data_length - 1 : i;

	ibat_set_max = chip->cc_data[i].cur;

	if (chip->pep_comp)
		ibat_set_max += (ibat_set_max * chip->pep_comp /100);

	pr_info("[TTF] Set pep_current (%d)\n", ibat_set_max);
	return ibat_set_max;
}

#define SOC_TO_MHA(x) ((x) * chip->full_capacity)
//#define MHA_TO_TIME(x, y) (((x) * 60 * 60) / y)
static int time_to_full_time_in_cc
		(struct chgctrl *chip, int start, int end, int cur)
{
	unsigned int time_in_cc = 0;
	unsigned int mha_to_charge;

	/* calculate time to cc charging */
	mha_to_charge = SOC_TO_MHA(end - start);
	mha_to_charge /= 1000;  /* three figures SOC */
	//time_in_cc = MHA_TO_TIME(mha_to_charge, cur);
	time_in_cc = (mha_to_charge * 60 * 60) / cur;
	pr_info("[TTF] time_in_cc = %d, mha_to_charge = %d\n",
			time_in_cc, mha_to_charge);

	return time_in_cc;
}

static void time_to_full_calc_cc_step_time
		(struct chgctrl *chip, int end)
{
	unsigned int mha_to_charge = 0;
	unsigned int cc_length = chip->cc_data_length;
	int i = 0;

	/* cc time update to new cc step table */
	for (i = 1; i < cc_length; i++) {
		mha_to_charge = SOC_TO_MHA(chip->cc_data[i].soc - chip->cc_data[i-1].soc);
		mha_to_charge /= 1000;
		chip->time_in_step_cc[i-1] = (mha_to_charge * 60 * 60) / chip->cc_data[i-1].cur;
		pr_info("[TTF][Step] time_in_step_cc = %d, mha_to_charge = %d\n",
				chip->time_in_step_cc[i-1], mha_to_charge);
	}

	mha_to_charge = SOC_TO_MHA(end - chip->cc_data[cc_length-1].soc);
	mha_to_charge /= 1000;
	chip->time_in_step_cc[cc_length-1] = (mha_to_charge * 60 * 60) / chip->cc_data[cc_length-1].cur;
	pr_info("[TTF][Step] time_in_step_cc = %d, mha_to_charge = %d\n",
			chip->time_in_step_cc[cc_length-1], mha_to_charge);

	for (i = 0; i < cc_length; i++)
		pr_info("[TTF][Step] CC_Step_Time [%d] : [%d]\n", i, chip->time_in_step_cc[i]);
}

static int time_to_full_time_in_step_cc
		(struct chgctrl *chip, int start, int end, int cur)
{
	unsigned int time_in_cc = 0;
	unsigned int mha_to_charge = 0;
	unsigned int cc_length = chip->cc_data_length;
	int i = 0;;

	/* Calculate all of cc step time from Table when charging start once*/
	if (chip->time_in_step_cc[0] == EMPTY)
		time_to_full_calc_cc_step_time(chip, end);

	/* cc step section check for current soc */
	for (i = 1; i < cc_length ;i++) {
		if (start < (chip->cc_data[i].soc))
			break;
	}
	pr_info("[TTF][Step] cc_soc_index = %d\n", i);

	/* Calculate cc_time_soc */
	if (i < cc_length) {
		mha_to_charge = SOC_TO_MHA(chip->cc_data[i].soc - start);
		mha_to_charge /= 1000;
		time_in_cc = (mha_to_charge * 60 * 60) / chip->cc_data[i-1].cur;
		pr_info("[TTF][Step][1] time_in_cc = %d, mha_to_charge = %d, ibat = %d\n",
				time_in_cc, mha_to_charge, chip->cc_data[i-1].cur);
	}
	else {
		mha_to_charge = SOC_TO_MHA(end - start);
		mha_to_charge /= 1000;
		time_in_cc = (mha_to_charge * 60 * 60) / chip->cc_data[cc_length-1].cur;
		pr_info("[TTF][Step][2] time_in_cc = %d, mha_to_charge = %d, ibat = %d\n",
				time_in_cc, mha_to_charge, chip->cc_data[cc_length-1].cur);
	}
	pr_info("[TTF][Step] time_in_cc = %d\n", time_in_cc);
	/* Calculate total cc time */
	for ( ; i < cc_length; i++)
		time_in_cc = time_in_cc + chip->time_in_step_cc[i];

	pr_info("[TTF][Step] Total time_in_step_cc = %d\n", time_in_cc);

	return time_in_cc;
}

static int time_to_full_time_in_cv
		(struct chgctrl *chip, int start, int end, int i)
{
	unsigned int time_in_cv = 0;

	/* will not enter cv */
	if (end <= start)
		return chip->cv_data[i].time;

	for (i = 0; i < chip->cv_data_length ;i++) {
		if (end <= chip->cv_data[i].soc)
			break;
	}

	if (i >= chip->cv_data_length)
		return 0;

	pr_info("[TTF] cv_data_index: %d\n", i);

	/* calculate remain time in cv (linearity) */
	time_in_cv = chip->cv_data[i-1].time - chip->cv_data[i].time;
	time_in_cv /= chip->cv_data[i].soc - chip->cv_data[i-1].soc;
	time_in_cv *= chip->cv_data[i].soc - end;
	time_in_cv += chip->cv_data[i].time;

	return time_in_cv;
}

static int time_to_full_evaluate_work(struct chgctrl *chip, int soc);
static int chgctrl_time_to_full_clear(struct chgctrl *chip);
static void chgctrl_time_to_full(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, time_to_full_work.work);
	unsigned int time_in_cc = 0;
	unsigned int time_in_cv = 0;
	int soc;
	int soc_cv;
	int ibat;
	int i;

	if (chip->battery_status == POWER_SUPPLY_STATUS_DISCHARGING) {
		pr_info("[TTF] POWER_SUPPLY_STATUS_DISCHARGING\n");
		chgctrl_time_to_full_clear(chip);
		return;
	}

	if (chip->battery_status == POWER_SUPPLY_STATUS_FULL) {
		pr_info("[TTF] POWER_SUPPLY_STATUS_FULL\n");
		chgctrl_time_to_full_clear(chip);
		return;
	}

	/* get soc */
	soc = chgctrl_get_ttf_capacity();

	if (soc >= 1000) {
		pr_info("[TTF] TTF capacity 1000\n");
		chgctrl_time_to_full_clear(chip);
		return;
	}

	/* get charging current in normal setting */
	ibat = time_to_full_time_get_current(chip);
	if (ibat <= 0)
		return;

	ibat_now_for_profile = chgctrl_get_bat_current_now(chip);
	ibat_now_for_profile /= 1000;
	ibat_now_for_profile *= -1;

	if ((chip->cc_data_length <= 0) || (chip->cc_data_length > 5))
		return;

	/* Determine cv soc */
	for (i = 0; i < chip->cv_data_length ;i++) {
		if (abs(ibat) >= chip->cv_data[i].cur)
			break;
	}
	i = i >= chip->cv_data_length ? chip->cv_data_length - 1 : i;
	soc_cv = chip->cv_data[i].soc;
	pr_info("[TTF] before calculate = soc: %d, cv_soc: %d, ibat: %d, index: %d\n",
					soc, soc_cv, ibat, i);

	if (soc >= soc_cv)
		goto cv_time;

	if ((chip->cc_data_length) < 2)
		goto cc_time;

	if (i != 0) /*Now need to cc_step_time*/
		goto cc_time;

/* cc_step_time: */
	time_in_cc = time_to_full_time_in_step_cc(chip, soc, soc_cv, ibat);
	goto cv_time;

cc_time:
	time_in_cc = time_to_full_time_in_cc(chip, soc, soc_cv, ibat);

cv_time:
	time_in_cv = time_to_full_time_in_cv(chip, soc_cv, soc, i);

	chip->time_to_full_now = time_in_cc + time_in_cv;

	if (batt_comp_value) /* batt id compensation */
		chip->time_to_full_now += (chip->time_to_full_now * batt_comp_value / 100);

	if (chip->report_ttf_comp) /* report ttf compensation */
		chip->time_to_full_now += (chip->time_to_full_now * chip->report_ttf_comp / 100);

	if (chip->min_comp) /* minite compensation */
		chip->time_to_full_now += (chip->min_comp * 60);

	pr_info("[TTF] BATTERY(%d%%, %d(%d)mA) TIME(%d%%)(%dsec, %dsec till %d%% + %dsec)\n",
			soc, ibat, ibat_now_for_profile,
			batt_comp_value, chip->time_to_full_now, time_in_cc, soc_cv/10, time_in_cv);

	if (soc >= 996)
		chip->time_to_full_now = FULL;

	time_to_full_evaluate_work(chip, soc);

	/* TTF update */
	if ((chip->time_to_full_now != DISCHARGE) && time_to_full_monitor_time == TTF_START_MS) {
		pr_info("[TTF] Update\n");

		if (chip->psy)
			power_supply_changed(chip->psy);

		time_to_full_monitor_time = TTF_MONITOR_MS;
	}

	schedule_delayed_work(to_delayed_work(work),
			msecs_to_jiffies(time_to_full_monitor_time));
}

static int time_to_full_update_work_clear(struct chgctrl *chip)
{
	unsigned int cc_length = chip->cc_data_length;
	int i = 0;

	for (i = 0; i < cc_length ; i++) {
		chip->time_in_step_cc[i] = EMPTY;
	}
	chip->time_to_full_now = DISCHARGE;

	return 0;
}

/* for evaluate */
static int time_to_full_evaluate_work_clear(struct chgctrl *chip)
{
	int i;

	/* Unnecessary clear routine skip */
	if (chip->soc_now == EMPTY)
		return 0;

	for (i = 0; i <= 100 ; i++) {
		chip->runtime_consumed[i] = EMPTY;
		chip->ttf_remained[i] = EMPTY;
	}
	chip->starttime_of_charging = EMPTY;
	chip->starttime_of_soc = EMPTY;
	chip->soc_begin = EMPTY;
	chip->soc_now = EMPTY;

	return 0;
}

static int remains_by_ttf(struct chgctrl *chip, int soc) {
	int begin_soc = chip->soc_begin;

	if (chip->ttf_remained[soc] == EMPTY) {
		if (begin_soc != EMPTY && begin_soc <= soc)
			chip->ttf_remained[soc] = chip->time_to_full_now;
	}
	return chip->ttf_remained[soc];
}

static int time_to_full_evaluate_report(struct chgctrl *chip, long eoc) {
	int i, begin_soc = chip->soc_begin;
	int really_remained[100+1] = { 0 };

	/* Do not need evaluation */
	if (begin_soc >= 100)
		return 0;

	really_remained[100] = 0;
	for (i = 99; begin_soc <= i; --i)
		really_remained[i] = chip->runtime_consumed[i] + really_remained[i+1];

//	pr_info("[TTF][Evaluate] Evaluating... charging from %d(%ld) to 100(%ld), (duration %ld)\n",
//		 begin_soc, chip->starttime_of_charging, eoc, eoc-chip->starttime_of_charging);
	pr_info("[TTF][Evaluate] soc, consumed"	/* really measured */
				", real, ttf"		/* for comparison */
				", diff, IBAT\n");			/* ttf really diff to min */
	for (i = begin_soc; i <= 100; ++i) {
		pr_info("[TTF][Evaluate] %d, %d, %d, %d, %d, %d\n"
			, i, chip->runtime_consumed[i], really_remained[i]
			, chip->ttf_remained[i]
			, (chip->ttf_remained[i] - really_remained[i]) / 60
			, chip->soc_now_ibat[i]);
	}
	return 0;
}

static int time_to_full_evaluate_work(struct chgctrl *chip, int soc)
{
	int remains_ttf = EMPTY;
	int soc_now = (soc + 5) / 10;	/* round-up radix */
	int ibat;
	long now;
	struct timespec	tspec;

	if ((soc >= 1000) && (chip->starttime_of_charging == EMPTY))
		return 0;

	if (soc <= 0)
		return 0;

	if (soc_now >= 100)
		soc_now = 100;
	else if (soc_now <= 0)
		return 0;

	if (chip->soc_now == soc_now)
		return 0;

	get_monotonic_boottime(&tspec);
	now = tspec.tv_sec;

	if (chip->starttime_of_charging == EMPTY) {
		// New insertion
		chip->soc_begin = soc_now;
		chip->starttime_of_charging = now;
	}

	/* Soc rasing up */
	chip->runtime_consumed[soc_now-1] = now - chip->starttime_of_soc;

	/* Update time me */
	chip->soc_now = soc_now;
	chip->starttime_of_soc = now;
	ibat = chgctrl_get_bat_current_now(chip);
	chip->soc_now_ibat[soc_now] = (ibat / 1000) * -1;

	remains_ttf = remains_by_ttf(chip, soc_now);

	if (soc >= 995) {
		/* Evaluate NOW! (at the 100% soc) */
		time_to_full_evaluate_report(chip, now);
		time_to_full_evaluate_work_clear(chip);
	}

	return 0;
}
/* for evaluate */

static int chgctrl_time_to_full_clear(struct chgctrl *chip)
{
	if (!chip->time_to_full_mode)
		return 0;

	pr_info("[TTF] chgctrl_time_to_full_clear\n");

	ibat_set_max = 0;
	time_to_full_monitor_time = TTF_START_MS;
	pre_soc = 0;

	time_to_full_update_work_clear(chip);
	time_to_full_evaluate_work_clear(chip);
	cancel_delayed_work(&chip->time_to_full_work);

	return 0;
}

static int chgctrl_time_to_full_trigger(struct chgctrl *chip)
{
	unsigned long delay = 0;
	int soc;

	if (!chip->time_to_full_mode)
		return 0;

	if (!chip->charger_online)
		return 0;

	if (chip->battery_status == POWER_SUPPLY_STATUS_DISCHARGING)
		return 0;

	/* Prevent duplication trigger by psy_handle_battery */
	soc = chgctrl_get_ttf_capacity();
	if (soc < 0 || pre_soc == soc) {
		return 0;
	}
	pre_soc = soc;

	/* wait for settle input current by aicl */
	if (time_to_full_monitor_time == TTF_START_MS) {
		pr_info("[TTF] wait %dms to start\n",
				time_to_full_monitor_time);
		delay = msecs_to_jiffies(time_to_full_monitor_time);
	}

	schedule_delayed_work(&chip->time_to_full_work, delay);

	return 0;
}

static int chgctrl_time_to_full_time_update_batt_comp(struct chgctrl *chip,
						      struct power_supply *psy)
{
	struct device_node *node = chip->dev->of_node;
	char batt_comp_prop[20];
	union power_supply_propval val;
	int ret;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_MANUFACTURER, &val);
	if (ret) {
		pr_info("[TTF] Get Battery ID failed\n");
		return ret;
	}
	if (!val.strval) {
		pr_info("[TTF] No Battery ID\n");
		return -ENODEV;
	}

	snprintf(batt_comp_prop, 20, "batt_comp-%s", val.strval);
	ret = of_property_read_u32(node, batt_comp_prop, &batt_comp_value);
	if (ret) {
		pr_info("[TTF] using legacy for %s\n", batt_comp_prop);
		batt_comp_value = 0;

		if (!strncmp(val.strval, "LGC", 3) || !strncmp(val.strval,"Lgc", 3)) {
			batt_comp_value = chip->batt_comp[0];
		} else if (!strncmp(val.strval,"TOCAD", 5) || !strncmp(val.strval,"Tocad", 5)) {
			batt_comp_value = chip->batt_comp[1];
		} else if (!strncmp(val.strval, "ATL", 3) || !strncmp(val.strval,"Atl", 3)) {
			batt_comp_value = chip->batt_comp[2];
		} else if (!strncmp(val.strval, "BYD", 3) || !strncmp(val.strval,"Byd", 3)) {
			batt_comp_value = chip->batt_comp[3];
		} else if (!strncmp(val.strval, "LISHEN", 6) || !strncmp(val.strval, "Lishen", 6)) {
			batt_comp_value = chip->batt_comp[4];
		} else {
			pr_info("[TTF] batt_comp not match\n");
			return -ENXIO;
		}
	}

	pr_info("[TTF] %s, batt_comp : %d\n", val.strval, batt_comp_value);

	return 0;
}

static int chgctrl_time_to_full_init(struct chgctrl *chip)
{
	time_to_full_update_work_clear(chip);
	time_to_full_evaluate_work_clear(chip);

	INIT_DELAYED_WORK(&chip->time_to_full_work, chgctrl_time_to_full);

	return 0;
}

static int chgctrl_time_to_full_parse_dt(struct chgctrl *chip, bool remake, struct device_node *node)
{
	struct property *prop;
	int len = 0;
	int rc = 0;
	int i;

	if (!remake) {
		chip->time_to_full_mode = of_property_read_bool(node, "time_to_full_mode");
		if (!chip->time_to_full_mode)
			return 0;
	}

	if (!chip->time_to_full_mode)
		return 0;

	of_property_read_u32(node, "battery_full_capacity", &chip->full_capacity);
	of_property_read_u32(node, "sdp_current", &chip->sdp_current);
	of_property_read_u32(node, "dcp_current", &chip->dcp_current);
	of_property_read_u32(node, "pep_current", &chip->pep_current);
	of_property_read_u32(node, "non_std_current", &chip->non_std_current);
	of_property_read_u32(node, "report_ttf_comp", &chip->report_ttf_comp);
	of_property_read_u32(node, "sdp_comp", &chip->sdp_comp);
	of_property_read_u32(node, "dcp_comp", &chip->dcp_comp);
	of_property_read_u32(node, "pep_comp", &chip->pep_comp);
	of_property_read_u32(node, "pd_comp", &chip->pd_comp);
	of_property_read_u32(node, "cdp_comp", &chip->cdp_comp);
	of_property_read_u32(node, "non_std_comp", &chip->non_std_comp);
	of_property_read_u32(node, "min_comp", &chip->min_comp);

	if (remake)
		goto debug_dump;

	/* Batt_id comp Table */
	prop = of_find_property(node, "batt_comp", NULL);
	if (prop) {
		rc = of_property_read_u32_array(node, "batt_comp",
				chip->batt_comp, 5);
		if (rc < 0) {
			pr_info("[TTF] failed to read batt_comp : %d\n", rc);
		}
	} else {
		pr_info("[TTF] there is not batt_comp\n");
	}

	/* CC Table */
	chip->cc_data = NULL;
	if (!of_get_property(node, "cc_data", &len)) {
		pr_info("[TTF] there is not cc_data\n");
		goto err;
	}
	chip->cc_data = kzalloc(len, GFP_KERNEL);
	if (!chip->cc_data) {
		pr_info("[TTF] failed to alloc cc_data\n");
		goto err;
	}

	chip->cc_data_length = len / sizeof(struct cc_step);
	pr_info("[TTF] cc_data_length : %d / %d \n", chip->cc_data_length, len);
	rc = of_property_read_u32_array(node, "cc_data",
				(u32 *)chip->cc_data, len/sizeof(u32));
	if (rc) {
		pr_info("[TTF] failed to read cc_data : %d\n", rc);
		goto err;
	}

	/* CV Table */
	chip->cv_data = NULL;
	if (!of_get_property(node, "cv_data", &len)) {
		pr_info("[TTF] there is not cv_data\n");
		goto err;
	}
	chip->cv_data = kzalloc(len, GFP_KERNEL);
	if (!chip->cv_data) {
		pr_info("[TTF] failed to alloc cv_data\n");
		goto err;
	}

	chip->cv_data_length = len / sizeof(struct cv_slope);
	pr_info("[TTF] cv_data_length : %d / %d \n", chip->cv_data_length, len);
	rc = of_property_read_u32_array(node, "cv_data",
				(u32 *)chip->cv_data, len/sizeof(u32));
	if (rc) {
		pr_info("[TTF] failed to read cv_data : %d\n", rc);
		goto err;
	}

debug_dump:
	/* Debug */
	if (chip->cc_data) {
		pr_info("[TTF] CC Table\n");
		pr_info("[TTF] current, soc\n");
		for(i = 0; i < chip->cc_data_length; i++)
			pr_info("[TTF] %d, %d\n", chip->cc_data[i].cur, chip->cc_data[i].soc);
	} else {
		pr_info("[TTF] CC Table Read Fail\n");
		goto err;
	}

	if (chip->cv_data) {
		pr_info("[TTF] CV Table\n");
		pr_info("[TTF] current, soc, time\n");
		for(i = 0; i < chip->cv_data_length; i++)
			pr_info("[TTF] %d, %d, %d\n", chip->cv_data[i].cur, chip->cv_data[i].soc, chip->cv_data[i].time);
	} else {
		pr_info("[TTF] CV Table Read Fail\n");
		goto err;
	}

	if (chip->batt_comp != NULL) {
		pr_info("[TTF] batt_comp Table\n");
		pr_info("[TTF] LGC, TOCAD, ATL, BYD, LISHEN\n");
		pr_info("[TTF] %d %d %d %d %d\n",
				chip->batt_comp[0], chip->batt_comp[1], chip->batt_comp[2], chip->batt_comp[3], chip->batt_comp[4]);
	} else {
		pr_info("[TTF] batt_comp Table Read Fail\n");
	}

	pr_info("[TTF] full_capacity: %d, report_ttf_comp: %d, sdp_comp: %d, "\
			"cdp_comp: %d, dcp_comp: %d, pep_comp:%d, pd_comp:%d, min_comp: %d\n",
			chip->full_capacity, chip->report_ttf_comp, chip->sdp_comp,
			chip->cdp_comp, chip->dcp_comp, chip->pep_comp, chip->pd_comp, chip->min_comp);

	/* Debug */

	return 0;

err:
	if (chip->cc_data) {
		devm_kfree(chip->dev, chip->cc_data);
		chip->cc_data = NULL;
	}

	if (chip->cv_data) {
		devm_kfree(chip->dev, chip->cv_data);
		chip->cv_data = NULL;
	}

	return 1;
}
#endif

#ifdef CONFIG_TCPC_CLASS
#define WEAK_CHARGER_TRRIGER_MS	(500)	 /* 500 msec */
#define WEAK_DETECT_COUNT	2
#define WEAK_CHARGER_ICL_MA	900
static unsigned int weak_charger_count = 0;
static unsigned int weak_charger_count_pre = 0;

static int chgctrl_weak_charger_clear(struct chgctrl *chip);
static void chgctrl_weak_charger_work(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, weak_charger_work.work);

	if (weak_charger_count >= WEAK_DETECT_COUNT) {
		pr_info("[WEAK] weak_charger detection\n");
		chgctrl_vote(&chip->icl, ICL_VOTER_WEAK, WEAK_CHARGER_ICL_MA);
		chgctrl_weak_charger_clear(chip);
	}

	weak_charger_count_pre = weak_charger_count;

	if (chip->tcpc_plug_in) {
		weak_charger_count++;
		cancel_delayed_work(&chip->weak_charger_work);
	}
	pr_info("[WEAK] weak_charger_count pre: %d, now: %d\n", weak_charger_count_pre, weak_charger_count);

	if (!chip->tcpc_plug_in) {
		chgctrl_vote(&chip->icl, ICL_VOTER_WEAK, -1);
		chgctrl_weak_charger_clear(chip);
	}
}

static int chgctrl_weak_charger_init(struct chgctrl *chip)
{
	chgctrl_weak_charger_clear(chip);

	INIT_DELAYED_WORK(&chip->weak_charger_work, chgctrl_weak_charger_work);

	return 0;
}

static int chgctrl_weak_charger_clear(struct chgctrl *chip)
{
	weak_charger_count = 0;
	weak_charger_count_pre = 0;
	cancel_delayed_work(&chip->weak_charger_work);

	return 0;
}

static int chgctrl_weak_charger_trigger(struct chgctrl *chip)
{
	unsigned long delay = 0;

	if (!chip->tcpc_plug_in) {
		pr_info("[WEAK] wait %dms to start\n", WEAK_CHARGER_TRRIGER_MS);
		delay = msecs_to_jiffies(WEAK_CHARGER_TRRIGER_MS);
		schedule_delayed_work(&chip->weak_charger_work, delay);
	}

	return 0;
}
#endif

/* restrict charging */
enum {
	RESTRICTED_VOTER_LCD,
	RESTRICTED_VOTER_CALL,
	RESTRICTED_VOTER_TDMB,
	RESTRICTED_VOTER_UHDREC,
	RESTRICTED_VOTER_WFD,
	RESTRICTED_VOTER_MAX
};

static char *restricted_voters[] = {
	[RESTRICTED_VOTER_LCD] = "LCD",
	[RESTRICTED_VOTER_CALL] = "CALL",
	[RESTRICTED_VOTER_TDMB] = "TDMB",
	[RESTRICTED_VOTER_UHDREC] = "UHDREC",
	[RESTRICTED_VOTER_WFD] = "WFD",
};

static void chgctrl_restricted_changed(void *args)
{
	struct chgctrl *chip = args;
	int value;

	if (!chip)
		return;

	value = chgctrl_vote_active_value(&chip->restricted);
	chgctrl_vote(&chip->fcc, FCC_VOTER_RESTRICTED, value);
}

static int chgctrl_restricted_get_voter(struct chgctrl *chip,
					const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(restricted_voters); i++) {
		if (!strcmp(name, restricted_voters[i]))
			break;
	}
	if (i >= ARRAY_SIZE(restricted_voters))
		return -ENODEV;

	return i;
}

static int chgctrl_restricted_get_value(struct chgctrl *chip,
					const char *name, const char *mode)
{
	struct device_node *np = chip->dev->of_node;
	char propname[30];
	int limit;
	int ret;

	if (!strcmp(mode, "OFF"))
		return -1;

	snprintf(propname, sizeof(propname), "restricted-%s-%s",
			name, mode);

	ret = of_property_read_u32(np, propname, &limit);
	if (ret)
		return -ENOTSUPP;

	return limit;
}

static int param_set_restricted(const char *val,
				const struct kernel_param *kp)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	char name[10], mode[10];
	int voter, value;
	int ret;

	if (!chip) {
		pr_info("not ready yet. ignore\n");
		return -ENODEV;
	}

	ret = sscanf(val, "%s %s", name, mode);
	if (ret != 2)
		return -EINVAL;

	voter = chgctrl_restricted_get_voter(chip, name);
	if (voter == -ENODEV)
		return -ENODEV;

	value = chgctrl_restricted_get_value(chip, name, mode);
	if (value == -ENOTSUPP)
		return -ENOTSUPP;

	chgctrl_vote(&chip->restricted, voter, value);

	return 0;
}

static int param_get_restricted(char *buffer,
				const struct kernel_param *kp)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int voter;

	if (!chip)
		return scnprintf(buffer, PAGE_SIZE, "not ready");

	voter = chgctrl_vote_active_voter(&chip->restricted);
	if (voter < 0)
		return scnprintf(buffer, PAGE_SIZE, "none");

	return scnprintf(buffer, PAGE_SIZE, "%s", restricted_voters[voter]);
}

static struct kernel_param_ops restricted_ops = {
	.set = param_set_restricted,
	.get = param_get_restricted,
};
module_param_cb(restricted_charging, &restricted_ops, NULL, CC_RW_PERM);

/* factory */
static void chgctrl_factory(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, factory_work);

	if (chip->boot_mode != CC_BOOT_MODE_FACTORY)
		return;

	chgctrl_vote(&chip->icl_boost, ICL_BOOST_VOTER_FACTORY,
			chip->factory_icl);
	chgctrl_vote(&chip->fcc, FCC_VOTER_FACTORY,
			chip->factory_fcc);
	chgctrl_vote(&chip->vbus, VBUS_VOTER_FACTORY,
			chip->factory_vbus);
}

static int chgctrl_factory_parse_dt(struct chgctrl *chip)
{
	struct device_node *node = chip->dev->of_node;
	int ret;

	ret = of_property_read_u32(node, "factory-icl",
			&chip->factory_icl);
	if (ret)
		chip->factory_icl = 1500;
	ret = of_property_read_u32(node, "factory-fcc",
			&chip->factory_fcc);
	if (ret)
		chip->factory_fcc = 500;
	ret = of_property_read_u32(node, "factory-vbus",
			&chip->factory_vbus);
	if (ret)
		chip->factory_vbus = 5000;

	return 0;
}

/* chargerlogo */
static int chgctrl_chargerlogo_parse_dt(struct chgctrl *chip, struct device_node *node)
{
	int ret = 0;

	if (chip->boot_mode != CC_BOOT_MODE_CHARGER)
		return 0;

	ret = of_property_read_u32(node, "chargerlogo-icl", &chip->chargerlogo_icl);
	if (ret)
		chip->chargerlogo_icl = chip->default_icl;
	ret = of_property_read_u32(node, "chargerlogo-fcc", &chip->chargerlogo_fcc);
	if (ret)
		chip->chargerlogo_fcc = chip->default_fcc;

	chip->default_icl = chip->chargerlogo_icl;
	chip->default_fcc = chip->chargerlogo_fcc;

	pr_info("[chargerlogo] icl: %d, fcc: %d\n",
			chip->default_icl, chip->default_fcc);

	return 0;
}

static bool vzw_slow_chg_mode = false;

static int chgctrl_vzw_chg_parse_dt(struct chgctrl *chip, struct device_node *node)
{
	chip->vzw_chg_mode = of_property_read_bool(node, "vzw_chg_mode");
	vzw_slow_chg_mode = chip->vzw_chg_mode;
	if (chip->vzw_chg_mode)
		return 0;
	chip->vzw_chg_mode = of_property_read_bool(node, "slow_chg_mode");
	vzw_slow_chg_mode = chip->vzw_chg_mode;

	return 0;
}

bool get_vzw_slow_chg_mode(void)
{
	return vzw_slow_chg_mode;
}

/* override battery properties */
static int chgctrl_override_status(struct chgctrl *chip, int status)
{
	int capacity;

	if (status != POWER_SUPPLY_STATUS_CHARGING &&
			status != POWER_SUPPLY_STATUS_FULL)
		return status;

	if (chip->ccd_status != POWER_SUPPLY_STATUS_UNKNOWN)
		return chip->ccd_status;

	capacity = chgctrl_get_capacity();
	if (capacity >= 100)
		status = POWER_SUPPLY_STATUS_FULL;
	else
		status = POWER_SUPPLY_STATUS_CHARGING;

	return status;
}

static int chgctrl_override_health(struct chgctrl *chip, int health)
{
	if (health != POWER_SUPPLY_HEALTH_GOOD)
		return health;

	if (chip->ccd_health != POWER_SUPPLY_HEALTH_UNKNOWN)
		return chip->ccd_health;

	if (chip->otp_health == POWER_SUPPLY_HEALTH_OVERHEAT)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	if (chip->otp_health == POWER_SUPPLY_HEALTH_COLD)
		return POWER_SUPPLY_HEALTH_COLD;

	return health;
}

static int chgctrl_override_batt_therm(struct chgctrl *chip, int origin_batt_therm)
{
	unsigned int temp_comp = 0;
	int batt_therm = 0;

	if (!chip->charger_online)
		return origin_batt_therm;

	if (chip->display_on)
		return origin_batt_therm;

	temp_comp = ibat_now_for_profile * ibat_now_for_profile * chip->batt_therm_comp / 1000000;
	batt_therm = origin_batt_therm - temp_comp;

//	pr_info("[JJB] report_therm: %d batt_therm ori: %d, temp_comp: %d (%d), ibat: %d\n",
//			batt_therm, origin_batt_therm, temp_comp, chip->batt_therm_comp, ibat_now_for_profile);

	return batt_therm;
}

void chgctrl_battery_property_override(enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct chgctrl *chip = chgctrl_get_drvdata();

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	switch(psp) {
	case POWER_SUPPLY_PROP_STATUS:
		chip->battery_status = val->intval;
		val->intval = chgctrl_override_status(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		chip->battery_present = val->intval;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		chip->battery_health = val->intval;
		val->intval = chgctrl_override_health(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		if (chip->technology == POWER_SUPPLY_TECHNOLOGY_UNKNOWN)
			break;
		val->intval = chip->technology;
		break;
	/* save original battery data */
	case POWER_SUPPLY_PROP_CAPACITY:
		chip->battery_capacity = val->intval;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		chip->battery_voltage = val->intval;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		chip->battery_temperature = val->intval;
		val->intval = chgctrl_override_batt_therm(chip, val->intval);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(chgctrl_battery_property_override);

void chgctrl_charger_property_override(enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct chgctrl *chip = chgctrl_get_drvdata();

	/* charger-contoller not ready. return */
	if (!chip)
		return;

	switch(psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (chgctrl_vote_active_value(&chip->input_suspend))
			val->intval = 0;
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(chgctrl_charger_property_override);

/* notifier block */
static int chgctrl_fb_notifier_call(struct notifier_block *nb,
				    unsigned long event, void *v)
{
	struct chgctrl *chip = container_of(nb, struct chgctrl, fb_nb);
	struct fb_event *ev = (struct fb_event *)v;
	bool display_on = false;

	if (event != FB_EVENT_BLANK)
		return NOTIFY_DONE;

	if (!ev || !ev->data)
		return NOTIFY_DONE;

	if (*(int*)ev->data == FB_BLANK_UNBLANK)
		display_on = true;

	if (chip->display_on == display_on)
		return NOTIFY_DONE;

	chip->display_on = display_on;
	pr_info("fb %s\n", (display_on ? "on" : "off"));

	chgctrl_fb_trigger(chip);

	return NOTIFY_DONE;
}

/* verizon carrier */
enum {
	VZW_NO_CHARGER,
	VZW_NORMAL_CHARGING,
	VZW_INCOMPATIBLE_CHARGING,
	VZW_UNDER_CURRENT_CHARGING,
	VZW_USB_DRIVER_UNINSTALLED,
	VZW_CHARGER_STATUS_MAX,
};
#define SLOW_CHARGING_CURRENT 450
static void chgctrl_psy_handle_vzw(struct chgctrl *chip,
				       struct power_supply *psy)
{
	const char *chgtype = chgctrl_get_charger_name(chip);
	int iusb_aicl;
	
	if (!chip->charger_online) {
		chip->vzw_chg_state = VZW_NO_CHARGER;
		return;
	}
	chip->vzw_chg_state = VZW_NORMAL_CHARGING;
	/* check incompatible charger */
	if(!strcmp(chgtype,"NON_STD")) {
		chip->vzw_chg_state = VZW_INCOMPATIBLE_CHARGING;
		return;
	}
	/* check slow charger */
	if(!strcmp(chgtype,"USB_DCP")) {
		iusb_aicl = chgctrl_get_usb_current_max(chip);
		iusb_aicl /= 1000;
		if (iusb_aicl < SLOW_CHARGING_CURRENT)
			chip->vzw_chg_state = VZW_UNDER_CURRENT_CHARGING;
	}
}

static void chgctrl_psy_handle_battery(struct chgctrl *chip,
				       struct power_supply *psy)
{
	union power_supply_propval val;
	int ret;

	if (chgctrl_ignore_notify(psy))
		return;

	/* ignore if battery not exist */
	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_PRESENT, &val);
	if (ret || val.intval == 0)
		return;

	/* trigger otp */
	chgctrl_otp_trigger(chip);

	/* trigger spec */
	chgctrl_spec_trigger(chip);

	/* trigger thermal */
	chgctrl_thermal_trigger(chip);

	/* trigger info */
	chgctrl_info_trigger(chip);

#ifdef TIME_TO_FULL_MODE
	/* trigger time to full */
	chgctrl_time_to_full_trigger(chip);
#endif
}

static void chgctrl_psy_handle_charger(struct chgctrl *chip)
{
	static struct power_supply *charger = NULL;
	union power_supply_propval val;
	int ret;

	if (!charger) {
		charger = power_supply_get_by_name("charger");
		if (!charger)
			return;
	}

	ret = power_supply_get_property(charger,
			POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret)
		return;
	if (val.intval == chip->charger_online)
		return;

	chip->charger_online = val.intval;
	pr_info("charger %s\n", val.intval ? "online" : "offline");

#ifdef TIME_TO_FULL_MODE
	if (chip->charger_online)
		chgctrl_time_to_full_trigger(chip);
	else
		chgctrl_time_to_full_clear(chip);
#endif
}

static int chgctrl_psy_notifier_call(struct notifier_block *nb,
				     unsigned long event, void *v)
{
	struct chgctrl *chip = container_of(nb, struct chgctrl, psy_nb);
	struct power_supply *psy = (struct power_supply *)v;

	pr_debug("notified by %s\n", psy->desc->name);

	switch (psy->desc->type) {
	case POWER_SUPPLY_TYPE_BATTERY:
		chgctrl_psy_handle_battery(chip, psy);
		break;
	case POWER_SUPPLY_TYPE_MAINS:
	case POWER_SUPPLY_TYPE_USB:
	case POWER_SUPPLY_TYPE_USB_DCP:
	case POWER_SUPPLY_TYPE_USB_CDP:
		chgctrl_psy_handle_charger(chip);
		if (chip->vzw_chg_mode)
			chgctrl_psy_handle_vzw(chip, psy);
		break;
	case POWER_SUPPLY_TYPE_UNKNOWN:
		if (!strcmp(psy->desc->name, "battery_id")) {
			chgctrl_psy_handle_batt_id(chip, psy);
#ifdef TIME_TO_FULL_MODE
			chgctrl_time_to_full_time_update_batt_comp(chip, psy);
#endif
		}


		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_DONE;
}

#ifdef CONFIG_TCPC_CLASS
static int chgctrl_pd_tcp_notifier_call(struct notifier_block *pnb,
				unsigned long event, void *data)
{
	struct chgctrl *chip = container_of(pnb, struct chgctrl, pd_nb);
	struct tcp_notify *noti = data;

	switch (event) {
	case TCP_NOTIFY_TYPEC_STATE:
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
		    (noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_DEBUG)) {
			pr_info("%s USB Plug in\n", __func__);
			chip->tcpc_plug_in = true;
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_DEBUG)
			&& noti->typec_state.new_state == TYPEC_UNATTACHED) {
			pr_info("%s USB Plug out\n", __func__);
			chip->tcpc_plug_in = false;
		}
		break;
	}
	chgctrl_weak_charger_trigger(chip);
	return NOTIFY_DONE;
}
#endif


static int chgctrl_init_notifier(struct chgctrl *chip)
{
	/* power supply notifier */
	chip->psy_nb.notifier_call = chgctrl_psy_notifier_call;
	power_supply_reg_notifier(&chip->psy_nb);

	/* frame buffer notifier */
	chip->fb_nb.notifier_call = chgctrl_fb_notifier_call;
	fb_register_client(&chip->fb_nb);

	return 0;
}

/* power supply */
static enum power_supply_property chgctrl_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_USB_CURRENT_MAX,
	POWER_SUPPLY_PROP_STORE_DEMO_ENABLED,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CCD_ICL,
	POWER_SUPPLY_PROP_CCD_FCC,
	POWER_SUPPLY_PROP_CCD_VFLOAT,
};

static enum power_supply_property chgctrl_properties_vzw[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_USB_CURRENT_MAX,
	POWER_SUPPLY_PROP_STORE_DEMO_ENABLED,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CCD_ICL,
	POWER_SUPPLY_PROP_CCD_FCC,
	POWER_SUPPLY_PROP_CCD_VFLOAT,
	POWER_SUPPLY_PROP_VZW_CHG,
};

static int chgctrl_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct chgctrl *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->battery_status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = chip->battery_present;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = chgctrl_vote_active_value(&chip->fcc);
		val->intval *= 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = chgctrl_vote_get_value(&chip->fcc,
				FCC_VOTER_DEFAULT);
		val->intval *= 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = chgctrl_vote_active_value(&chip->vfloat);
		val->intval *= 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = chgctrl_vote_get_value(&chip->vfloat,
				VFLOAT_VOTER_DEFAULT);
		val->intval *= 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = 1;
		if (chgctrl_vote_get_value(&chip->input_suspend,
				INPUT_SUSPEND_VOTER_USER))
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		val->intval = 1;
		if (!chgctrl_vote_get_value(&chip->fcc, FCC_VOTER_USER))
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
		val->intval = 0;
		if (!chgctrl_vote_get_value(&chip->icl_boost,
				ICL_BOOST_VOTER_USB_CURRENT_MAX))
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
		val->intval = chip->store_demo_mode;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = chip->time_to_full_now;
		break;
	case POWER_SUPPLY_PROP_CCD_ICL:
		val->intval = chgctrl_vote_get_value(&chip->icl,
				ICL_VOTER_CCD);
		break;
	case POWER_SUPPLY_PROP_CCD_FCC:
		val->intval = chgctrl_vote_get_value(&chip->fcc,
				FCC_VOTER_CCD);
		break;
	case POWER_SUPPLY_PROP_CCD_VFLOAT:
		val->intval = chgctrl_vote_get_value(&chip->vfloat,
				VFLOAT_VOTER_CCD);
		break;
	case POWER_SUPPLY_PROP_VZW_CHG:
		val->intval = chip->vzw_chg_state;
		break;
#ifdef CONFIG_LGE_PM_QNOVO_QNS
	case POWER_SUPPLY_PROP_QNS_FCC:
		val->intval = chgctrl_vote_get_value(&chip->fcc,
				FCC_VOTER_QNOVO);
		break;
	case POWER_SUPPLY_PROP_QNS_VFLOAT:
		val->intval = chgctrl_vote_get_value(&chip->vfloat,
				VFLOAT_VOTER_QNOVO);
		break;
#endif
	default:
		break;
	}

	return 0;
}

static int chgctrl_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct chgctrl *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		chgctrl_vote(&chip->input_suspend, INPUT_SUSPEND_VOTER_USER,
				val->intval ? 0 : 1);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		chgctrl_vote(&chip->fcc, FCC_VOTER_USER, val->intval ? -1 : 0);
		break;
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
		chgctrl_vote(&chip->icl_boost, ICL_BOOST_VOTER_USB_CURRENT_MAX,
				(val->intval ? 900 : -1));
		break;
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
		if (chip->store_demo_mode == val->intval)
			return 0;
		chip->store_demo_mode = val->intval;
		break;
	case POWER_SUPPLY_PROP_CCD_ICL:
		chgctrl_vote(&chip->icl, ICL_VOTER_CCD, val->intval);
		break;
	case POWER_SUPPLY_PROP_CCD_FCC:
		chgctrl_vote(&chip->fcc, FCC_VOTER_CCD, val->intval);
		break;
	case POWER_SUPPLY_PROP_CCD_VFLOAT:
		chgctrl_vote(&chip->vfloat, VFLOAT_VOTER_CCD, val->intval);
		break;
#ifdef CONFIG_LGE_PM_QNOVO_QNS
	case POWER_SUPPLY_PROP_QNS_FCC:
		chgctrl_vote(&chip->fcc, FCC_VOTER_QNOVO, val->intval);
		break;
	case POWER_SUPPLY_PROP_QNS_VFLOAT:
		chgctrl_vote(&chip->vfloat, VFLOAT_VOTER_QNOVO, val->intval);
		break;
#endif
	default:
		break;
	}

	return 0;
}

static int chgctrl_property_is_writeable(struct power_supply *psy,
					 enum power_supply_property psp)
{
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
	case POWER_SUPPLY_PROP_CCD_ICL:
	case POWER_SUPPLY_PROP_CCD_FCC:
	case POWER_SUPPLY_PROP_CCD_VFLOAT:
		ret = 1;
		break;
	default:
		break;
	}

	return ret;
}

static int chgctrl_init_power_supply(struct chgctrl *chip)
{
	chip->psy_desc.name = "charger_controller";
	chip->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->psy_desc.properties = chgctrl_properties;
	chip->psy_desc.num_properties =
			ARRAY_SIZE(chgctrl_properties);
	if (chip->vzw_chg_mode) {
		chip->psy_desc.properties = chgctrl_properties_vzw;
		chip->psy_desc.num_properties =
			ARRAY_SIZE(chgctrl_properties_vzw);
	}
	chip->psy_desc.get_property = chgctrl_get_property;
	chip->psy_desc.set_property = chgctrl_set_property;
	chip->psy_desc.property_is_writeable = chgctrl_property_is_writeable;

	chip->psy_cfg.drv_data = chip;

	chip->psy = power_supply_register(chip->dev, &chip->psy_desc,
			&chip->psy_cfg);
	if (!chip->psy)
		return -1;

	return 0;
}

/* vote */
static char *icl_voters[] = {
	[ICL_VOTER_DEFAULT] = "default",
	[ICL_VOTER_USER] = "user",
	[ICL_VOTER_CCD] = "ccd",
	[ICL_VOTER_RESTRICTED] = "restricted",
	[ICL_VOTER_GAME] = "game",
	[ICL_VOTER_WEAK] = "weak_charger",
	[ICL_VOTER_PSEUDO_HVDCP] = "pseudo_hvdcp",
	[ICL_VOTER_CHARGERLOGO] = "chargerlogo",
};

static char *fcc_voters[] = {
	[FCC_VOTER_DEFAULT] = "default",
	[FCC_VOTER_USER] = "user",
	[FCC_VOTER_CCD] = "ccd",
	[FCC_VOTER_QNOVO] = "qnovo",
	[FCC_VOTER_OTP] = "otp",
	[FCC_VOTER_SPEC] = "spec",
	[FCC_VOTER_THERMAL] = "thermal",
	[FCC_VOTER_DISPLAY] = "display",
	[FCC_VOTER_RESTRICTED] = "restricted",
	[FCC_VOTER_GAME] = "game",
	[FCC_VOTER_BATTERY_ID] = "battery_id",
	[FCC_VOTER_ATCMD] = "atcmd",
	[FCC_VOTER_FACTORY] = "factory",
	[FCC_VOTER_CHARERLOGO] = "chargerlogo",
};

static char *vfloat_voters[] = {
	[VFLOAT_VOTER_DEFAULT] = "default",
	[VFLOAT_VOTER_USER] = "user",
	[VFLOAT_VOTER_CCD] = "ccd",
	[VFLOAT_VOTER_QNOVO] = "qnovo",
	[VFLOAT_VOTER_OTP] = "otp",
	[VFLOAT_VOTER_BATTERY_ID] = "battery_id",
};

static char *icl_boost_voters[] = {
	[ICL_BOOST_VOTER_USER] = "user",
	[ICL_BOOST_VOTER_PSEUDO_BATTERY] = "pseudo_battery",
	[ICL_BOOST_VOTER_USB_CURRENT_MAX] = "usb_current_max",
	[ICL_BOOST_VOTER_ATCMD] = "atcmd",
	[ICL_BOOST_VOTER_FACTORY] = "factory",
};

static char *input_suspend_voters[] = {
	[INPUT_SUSPEND_VOTER_USER] = "user",
	[INPUT_SUSPEND_VOTER_WATER_DETECT] = "water_detect",
};

static char *vbus_voters[] = {
	[VBUS_VOTER_DEFAULT] = "default",
	[VBUS_VOTER_USER] = "user",
	[VBUS_VOTER_CCD] = "ccd",
	[VBUS_VOTER_CAMERA] = "camera",
	[VBUS_VOTER_NETWORK] = "network",
	[VBUS_VOTER_FACTORY] = "factory",
};

static int chgctrl_init_vote(struct chgctrl *chip)
{
	int ret;

	/* Input Current Limit */
	chip->icl.name = "icl";
	chip->icl.type = CC_VOTER_TYPE_MIN;
	chip->icl.voters = icl_voters;
	chip->icl.size = ICL_VOTER_MAX;
	chip->icl.function = chgctrl_icl_changed;
	chip->icl.args = chip;
	ret = chgctrl_vote_init(&chip->icl, chip->dev, chip->default_icl);
	if (ret)
		return ret;

	/* Fast Charge Current */
	chip->fcc.name = "fcc";
	chip->fcc.type = CC_VOTER_TYPE_MIN;
	chip->fcc.voters = fcc_voters;
	chip->fcc.size = FCC_VOTER_MAX;
	chip->fcc.function = chgctrl_fcc_changed;
	chip->fcc.args = chip;
	ret = chgctrl_vote_init(&chip->fcc, chip->dev, chip->default_fcc);
	if (ret)
		return ret;

	/* Floating Voltage */
	chip->vfloat.name = "vfloat";
	chip->vfloat.type = CC_VOTER_TYPE_MIN;
	chip->vfloat.voters = vfloat_voters;
	chip->vfloat.size = VFLOAT_VOTER_MAX;
	chip->vfloat.function = chgctrl_vfloat_changed;
	chip->vfloat.args = chip;
	ret = chgctrl_vote_init(&chip->vfloat, chip->dev, chip->default_vfloat);
	if (ret)
		return ret;

	/* Input Current Limit Boost */
	chip->icl_boost.name = "icl_boost";
	chip->icl_boost.type = CC_VOTER_TYPE_MAX;
	chip->icl_boost.voters = icl_boost_voters;
	chip->icl_boost.size = ICL_BOOST_VOTER_MAX;
	chip->icl_boost.function = chgctrl_icl_boost_changed;
	chip->icl_boost.args = chip;
	ret = chgctrl_vote_init(&chip->icl_boost, chip->dev, -1);
	if (ret)
		return ret;

	/* Input Suspend */
	chip->input_suspend.name = "input_suspend";
	chip->input_suspend.type = CC_VOTER_TYPE_TRIGGER;
	chip->input_suspend.voters = input_suspend_voters;
	chip->input_suspend.size = INPUT_SUSPEND_VOTER_MAX;
	chip->input_suspend.function = chgctrl_input_suspend_changed;
	chip->input_suspend.args = chip;
	ret = chgctrl_vote_init(&chip->input_suspend, chip->dev, -1);
	if (ret)
		return ret;

	/* VBUS (for high voltage input) */
	chip->vbus.name = "vbus";
	chip->vbus.type = CC_VOTER_TYPE_MIN;
	chip->vbus.voters = vbus_voters;
	chip->vbus.size = VBUS_VOTER_MAX;
	chip->vbus.function = chgctrl_vbus_changed;
	chip->vbus.args = chip;
	ret = chgctrl_vote_init(&chip->vbus, chip->dev, chip->default_vbus);
	if (ret)
		return ret;

	/* Restricted Charging */
	chip->restricted.name = "restricted";
	chip->restricted.type = CC_VOTER_TYPE_MIN;
	chip->restricted.voters = restricted_voters;
	chip->restricted.size = RESTRICTED_VOTER_MAX;
	chip->restricted.function = chgctrl_restricted_changed;
	chip->restricted.args = chip;
	ret = chgctrl_vote_init(&chip->restricted, chip->dev, -1);
	if (ret)
		return ret;

	return 0;
}

static void chgctrl_init_setting_info(struct chgctrl *chip)
{
	chip->default_icl = 3250;
	chip->default_fcc = 1200;
	chip->default_vfloat = 4400;
	chip->default_vbus = 5000;
	chip->technology = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
	chip->batt_therm_comp = 0;
	chip->chargerlogo_icl = chip->default_icl;
	chip->chargerlogo_fcc = chip->default_fcc;
	chip->vzw_chg_mode = false;
}

static int chgctrl_read_info(struct chgctrl *chip, struct device_node *node)
{
	int ret = 0;
	bool remake = false;

	if (!node) {
		chgctrl_init_setting_info(chip);
		node =  chip->dev->of_node;
		pr_info("chgctrl_read_info init setting\n");
	} else {
		remake = true;
		pr_info("chgctrl_read_info remake setting\n");
	}

	of_property_read_u32(node, "icl", &chip->default_icl);
	of_property_read_u32(node, "fcc", &chip->default_fcc);
	of_property_read_u32(node, "vfloat", &chip->default_vfloat);
	of_property_read_u32(node, "vbus", &chip->default_vbus);
	of_property_read_u32(node, "technology", &chip->technology);
	of_property_read_u32(node, "batt_therm_comp", &chip->batt_therm_comp);

	ret = chgctrl_fb_parse_dt(chip);
#ifdef TIME_TO_FULL_MODE
	ret = chgctrl_time_to_full_parse_dt(chip, remake, node);
#endif
	ret = chgctrl_chargerlogo_parse_dt(chip, node);
	ret = chgctrl_vzw_chg_parse_dt(chip, node);
	if (!remake) {
		ret = chgctrl_otp_parse_dt(chip);
		ret = chgctrl_thermal_parse_dt(chip);
		ret = chgctrl_spec_parse_dt(chip);
		ret = chgctrl_game_parse_dt(chip);
		ret = chgctrl_factory_parse_dt(chip);
	}

	return 0;
}

static void chgctrl_read_info_dump(struct chgctrl *chip)
{
	pr_info("[MODEL] icl: %d, fcc: %d, vbus: %d, chargerlogo(%d:%d)\n",
			chip->default_icl, chip->default_fcc, chip->default_vbus, chip->chargerlogo_icl, chip->chargerlogo_fcc);
}

__attribute__((weak)) char *lge_get_model_name(void) { return NULL; }
static const char *chgctrl_model_find_setting(struct device_node *node)
{
	char *name;
	const char *index;
	int arr_cnt, arr_num;

	/* parse from group */
	arr_cnt = of_property_count_strings(node, "group");
	if (arr_cnt > 0) {
		pr_info("[MODEL] Total model is %d\n", arr_cnt/2);
	} else {
		pr_info("[MODEL] ERROR group isn't exist\n");
		return NULL;
	}

	/* compare model name */
	name = lge_get_model_name();
	if (!name)
		return NULL;

	for (arr_num = 0; arr_num < arr_cnt; arr_num++) {
		of_property_read_string_index(node, "group", arr_num++, &index);
		if (!strcmp(name, index)) {
			of_property_read_string_index(node, "group", arr_num, &index);
			pr_info("[MODEL] %s %s \n", name, index);
			return index;
		}
	}

	pr_info("[MODEL] %s Default Setting \n", name);

	return NULL;
}

static int chgctrl_parse_dt(struct chgctrl *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *setting = NULL;
	const char *index;

	chgctrl_read_info(chip, NULL);

	/* find model setting */
	node = of_get_child_by_name(node, "model");
	if (!node)
		goto dump;

	index = chgctrl_model_find_setting(node);
	if (!index)
		goto dump;

	setting = of_get_child_by_name(node, index);
	if (!setting)
		goto dump;

	chgctrl_read_info(chip, setting);

dump:
	chgctrl_read_info_dump(chip);
	return 0;
}

static int chgctrl_probe(struct platform_device *pdev)
{
	struct chgctrl *chip = NULL;
	int ret = 0;

	chip = devm_kzalloc(&pdev->dev,
			sizeof(struct chgctrl), GFP_KERNEL);
	if (!chip) {
		pr_err("failed to alloc memory\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	chip->boot_mode = chgctrl_get_boot_mode();
	platform_set_drvdata(pdev, chip);

#ifdef CONFIG_TCPC_CLASS
	chip->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!chip->tcpc)
		return -EPROBE_DEFER;

	chip->pd_nb.notifier_call = chgctrl_pd_tcp_notifier_call;
	ret = register_tcp_dev_notifier(chip->tcpc, &chip->pd_nb,
					TCP_NOTIFY_TYPE_USB);
#endif

	chip->ccd_status = POWER_SUPPLY_STATUS_UNKNOWN;
	chip->ccd_health = POWER_SUPPLY_HEALTH_UNKNOWN;
	chip->ccd_ttf = -1;

	ret = chgctrl_parse_dt(chip);
	if (ret) {
		pr_err("failed to parse dt\n");
		return ret;
	}

	ret = chgctrl_impl_init(chip);
	if (ret) {
		pr_err("failed to init impl\n");
		return ret;
	}

	ret = chgctrl_init_vote(chip);
	if (ret) {
		pr_err("failed to init vote\n");
		return ret;
	}

	chgctrl_otp_init(chip);
	INIT_WORK(&chip->fb_work, chgctrl_fb);
	chgctrl_spec_init(chip);
	chgctrl_thermal_init(chip);
	chgctrl_game_init(chip);
#ifdef TIME_TO_FULL_MODE
	chgctrl_time_to_full_init(chip);
#endif
	chgctrl_battery_id_init(chip);

#ifdef CONFIG_TCPC_CLASS
	chgctrl_weak_charger_init(chip);
#endif

	INIT_WORK(&chip->factory_work, chgctrl_factory);
	chgctrl_info_init(chip);

	ret = chgctrl_init_power_supply(chip);
	if (ret) {
		pr_err("failed to init power supply\n");
		return ret;
	}

	ret = chgctrl_init_notifier(chip);
	if (ret) {
		pr_err("failed to init notifier\n");
		return ret;
	}

#ifdef CONFIG_LGE_PM_BATTERY_ID
	chgctrl_get_bat_manufacturer(chip);
#endif

	if (chip->boot_mode == CC_BOOT_MODE_FACTORY)
		schedule_work(&chip->factory_work);

	return 0;
}

static int chgctrl_remove(struct platform_device *pdev)
{
	struct chgctrl *chip = platform_get_drvdata(pdev);

	chgctrl_info_deinit(chip);
	chgctrl_game_deinit(chip);
	chgctrl_thermal_deinit(chip);
	chgctrl_spec_deinit(chip);
	chgctrl_otp_deinit(chip);

#ifdef TIME_TO_FULL_MODE
	chgctrl_time_to_full_clear(chip);
#endif

#ifdef CONFIG_TCPC_CLASS
	chgctrl_weak_charger_clear(chip);
#endif

	power_supply_unregister(chip->psy);

	return 0;
}

static struct of_device_id chgctrl_match_table[] = {
	{
		.compatible = "lge,charger-controller",
	},
	{},
};

static struct platform_driver chgctrl_driver = {
	.probe = chgctrl_probe,
	.remove = chgctrl_remove,
	.driver = {
		.name = "charger-controller",
		.owner = THIS_MODULE,
		.of_match_table = chgctrl_match_table,
	},
};

static void chgctrl_init_async(void *data, async_cookie_t cookie)
{
	platform_driver_register(&chgctrl_driver);
}

static int __init chgctrl_init(void)
{
	async_schedule(chgctrl_init_async, NULL);

	return 0;
}

static void __exit chgctrl_exit(void)
{
	platform_driver_unregister(&chgctrl_driver);
}

module_init(chgctrl_init);
module_exit(chgctrl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Charger IC Current Controller");
MODULE_VERSION("1.2");

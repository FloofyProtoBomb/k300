#ifndef _LGE_CHARGING_H
#define _LGE_CHARGING_H

#include <linux/power_supply.h>

/*****************************************************************************
 *  LGE Charging State
 ****************************************************************************/

struct lge_charging_alg_data {
	/* charging state. this must be a first item */
	int state;

	bool disable_charging;
	struct mutex lock;
	struct mutex slave;

	/* time */
	unsigned int total_charging_time;
	struct timespec charging_begin_time;
	struct timespec aicl_done_time;

	/* charger setting */
	int input_current_limit;
	int charging_current_limit;
	int constant_voltage;
	int input_voltage;

	/* status */
	int charging_current_tuned;
	bool recharging;

	/* power supply */
	struct power_supply *charger_psy;
	struct power_supply *battery_psy;

	/* policy */
	int ieoc;
	int aicl_interval;
	int step_ieoc;
	int step_ichg;
	int vfloat_offset;
	int slave_ichg_percent;
};

#endif /* End of _LGE_CHARGING_H */

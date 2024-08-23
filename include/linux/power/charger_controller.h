#ifndef __CHARGER_CONTROLLER_H__
#define __CHARGER_CONTROLLER_H__

#include <linux/power_supply.h>

void chgctrl_battery_property_override(enum power_supply_property psp,
				       union power_supply_propval *val);
void chgctrl_charger_property_override(enum power_supply_property psp,
				       union power_supply_propval *val);

/* pseudo power-supply support */
enum {
    PSEUDO_BATTERY,
    PSEUDO_HVDCP,
};
void chgctrl_set_pseudo_mode(int mode, int en);

/* water detect support */
void chgctrl_set_water_detect(bool detected);

#if CONFIG_MTK_GAUGE_VERSION == 30
/* mtk gm 3.0 (2.5 included) driver support */
void chgctrl_get_min_icl(int *uA);
void chgctrl_get_max_icl(int *uA);
void chgctrl_get_max_fcc(int *uA);
void chgctrl_get_max_vfloat(int *uV);
void chgctrl_get_input_suspend(bool *suspend);
void chgctrl_get_max_vbus(int *uV);

/* power delivery support */
void chgctrl_get_max_watt(int *uW);
#endif

#endif

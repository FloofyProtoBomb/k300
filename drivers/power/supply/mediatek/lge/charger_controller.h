#ifndef _CHARGER_CONTROLLER_H
#define _CHARGER_CONTROLLER_H

#include <linux/mutex.h>
#include <linux/power_supply.h>

#define TIME_TO_FULL_MODE

/* boot mode */
enum {
	CC_BOOT_MODE_NORMAL,
	CC_BOOT_MODE_CHARGER,
	CC_BOOT_MODE_FACTORY,
};

/* voter */
enum voter_type {
	CC_VOTER_TYPE_MIN,
	CC_VOTER_TYPE_MAX,
	CC_VOTER_TYPE_TRIGGER,
};

struct chgctrl_vote {
	char *name;
	int type;
	struct mutex lock;

	/* vote information */
	char **voters;
	int *values;
	int size;

	/* active */
	int active_voter;
	int active_value;

	/* handler */
	struct work_struct handler;
	void (*function)(void *);
	void *args;
};

int chgctrl_vote(struct chgctrl_vote* vote, int voter, int value);
void chgctrl_vote_dump(struct chgctrl_vote* vote);
int chgctrl_vote_active_value(struct chgctrl_vote* vote);
int chgctrl_vote_active_voter(struct chgctrl_vote* vote);
int chgctrl_vote_get_value(struct chgctrl_vote* vote, int voter);
int chgctrl_vote_init(struct chgctrl_vote* vote, struct device *dev, int value);

#ifdef TIME_TO_FULL_MODE
struct cc_step {
	s32 cur;
	s32 soc;
};
struct cv_slope {
	s32 cur;
	s32 soc;
	s32 time;
};
#endif

/* core */
enum {
	ICL_VOTER_DEFAULT,
	ICL_VOTER_USER,
	ICL_VOTER_CCD,
	ICL_VOTER_RESTRICTED,
	ICL_VOTER_GAME,
	ICL_VOTER_WEAK,
	ICL_VOTER_PSEUDO_HVDCP,
	ICL_VOTER_CHARGERLOGO,
	ICL_VOTER_MAX
};

enum {
	FCC_VOTER_DEFAULT,
	FCC_VOTER_USER,
	FCC_VOTER_CCD,
	FCC_VOTER_QNOVO,
	FCC_VOTER_OTP,
	FCC_VOTER_SPEC,
	FCC_VOTER_THERMAL,
	FCC_VOTER_DISPLAY,
	FCC_VOTER_RESTRICTED,
	FCC_VOTER_GAME,
	FCC_VOTER_BATTERY_ID,
	FCC_VOTER_ATCMD,
	FCC_VOTER_FACTORY,
	FCC_VOTER_CHARERLOGO,
	FCC_VOTER_MAX
};

enum {
	VFLOAT_VOTER_DEFAULT,
	VFLOAT_VOTER_USER,
	VFLOAT_VOTER_CCD,
	VFLOAT_VOTER_QNOVO,
	VFLOAT_VOTER_OTP,
	VFLOAT_VOTER_BATTERY_ID,
	VFLOAT_VOTER_MAX
};

enum {
	ICL_BOOST_VOTER_USER,
	ICL_BOOST_VOTER_PSEUDO_BATTERY,
	ICL_BOOST_VOTER_USB_CURRENT_MAX,
	ICL_BOOST_VOTER_ATCMD,
	ICL_BOOST_VOTER_FACTORY,
	ICL_BOOST_VOTER_MAX
};

enum {
	INPUT_SUSPEND_VOTER_USER,
	INPUT_SUSPEND_VOTER_WATER_DETECT,
	INPUT_SUSPEND_VOTER_MAX
};

enum {
	VBUS_VOTER_DEFAULT,
	VBUS_VOTER_USER,
	VBUS_VOTER_CCD,
	VBUS_VOTER_CAMERA,
	VBUS_VOTER_NETWORK,
	VBUS_VOTER_FACTORY,
	VBUS_VOTER_MAX
};

struct chgctrl_spec {
	/* temperature range */
	int tmin;
	int tmax;

	/* charge limit */
	int volt;
	int curr;
};

struct chgctrl_thermal {
	/* trigger temperature */
	int trigger;
	int offset;

	/* limit */
	int curr;
};

struct chgctrl {
	struct device *dev;
	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *psy;

	struct notifier_block psy_nb;
	struct notifier_block fb_nb;

	/* votes */
	struct chgctrl_vote icl;
	struct chgctrl_vote fcc;
	struct chgctrl_vote vfloat;
	struct chgctrl_vote icl_boost;
	struct chgctrl_vote input_suspend;
	struct chgctrl_vote vbus;

	/* internal data */
	int charger_online;

	/* weak charger */
#ifdef CONFIG_TCPC_CLASS
	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
	bool tcpc_plug_in;
	struct delayed_work weak_charger_work;
#endif

	int battery_present;
	int battery_status;
	int battery_health;
	int battery_capacity;
	int battery_voltage;
	int battery_current;
	int battery_temperature;

	bool display_on;

	int boot_mode;

	/* device-tree data*/
	int default_icl;
	int default_fcc;
	int default_vfloat;
	int default_vbus;
	int technology;

	/* batt_therm compensation */
	int batt_therm_comp;

	/* information */
	struct delayed_work info_work;
	struct wakeup_source info_ws;

	/* implementation */
	void *impl;

	/* otp */
	bool otp_enabled;
	int otp_version;
	struct delayed_work otp_work;
	int otp_temp_state;
	int otp_volt_state;
	int otp_health;
	int otp_fcc;
	int otp_vfloat;

	/* frame buffer */
	bool fb_enabled;
	struct work_struct fb_work;
	int fb_fcc;

	/* store demo */
	int store_demo_mode;

	/* spec */
	bool spec_enabled;
	struct delayed_work spec_work;
	struct chgctrl_spec *spec;
	int spec_size;
	int spec_idx;
	int spec_vfloat;

	/* restrict charging */
	struct chgctrl_vote restricted;

	/* thermal */
	bool thermal_enabled;
	struct delayed_work thermal_work;
	struct chgctrl_thermal *thermal;
	int thermal_size;
	int thermal_idx;

	/* game */
	bool game_enabled;
	struct delayed_work game_work;
	int game_fcc;
	int game_icl;
	int game_light_fcc;
	int game_light_icl;
	int game_light_load;
	int game_light_sec;
	int game_lowbatt_fcc;
	int game_lowbatt_icl;
	int game_lowbatt_soc;

#ifdef TIME_TO_FULL_MODE
	/* Time to full report */
	bool time_to_full_mode;
	struct delayed_work time_to_full_work;
	struct cc_step *cc_data;
	struct cc_step *dynamic_cc_data;
	struct cv_slope *cv_data;
	unsigned int cc_data_length;
	unsigned int cv_data_length;

	unsigned int time_in_step_cc[5];

	int full_capacity;
	int sdp_current;
	int dcp_current;
	int pep_current;
	int non_std_current;
	int report_ttf_comp;

	int sdp_comp;
	int dcp_comp;
	int pep_comp;
	int pd_comp;
	int cdp_comp;
	int non_std_comp;
	int min_comp;
	int batt_comp[5];

	int time_to_full_now; //ttf_now

	/* for evaluation */
	int runtime_consumed[101];
	int ttf_remained[101];
	int soc_now_ibat[101];

	long starttime_of_charging;
	long starttime_of_soc;
	int soc_begin;
	int soc_now;
#endif

#ifdef CONFIG_LGE_PM_BATTERY_ID
	/* battery id */
	struct work_struct battery_id_work;
	int battery_id_fcc;
	int battery_id_vfloat;
#endif

	/* factory */
	struct work_struct factory_work;
	int factory_icl;
	int factory_fcc;
	int factory_vbus;

	/* chargerlogo */
	int chargerlogo_icl;
	int chargerlogo_fcc;

	/* ccd */
	int ccd_health;
	int ccd_status;
	int ccd_ttf;

	/* verizon carrier */
	int vzw_chg_state;
	bool vzw_chg_mode;
};

/* common api */
struct chgctrl* chgctrl_get_drvdata(void);

int chgctrl_get_boot_mode(void);

int chgctrl_get_capacity(void);
int chgctrl_get_ttf_capacity(void);
int chgctrl_get_voltage(void);
int chgctrl_get_temp(void);

/* implementation api */
void chgctrl_icl_changed(void *args);
void chgctrl_fcc_changed(void *args);
void chgctrl_vfloat_changed(void *args);
void chgctrl_icl_boost_changed(void *args);
void chgctrl_input_suspend_changed(void *args);
void chgctrl_vbus_changed(void *args);
int chgctrl_impl_init(struct chgctrl *chip);

const char *chgctrl_get_charger_name(struct chgctrl *chip);
int chgctrl_get_usb_voltage_now(struct chgctrl *chip);
int chgctrl_get_usb_voltage_max(struct chgctrl *chip);
int chgctrl_get_usb_current_max(struct chgctrl *chip);
int chgctrl_get_bat_current_max(struct chgctrl *chip);
int chgctrl_get_bat_current_now(struct chgctrl *chip);

const char *chgctrl_get_bat_manufacturer(struct chgctrl *chip);
bool chgctrl_ignore_notify(struct power_supply *psy);

/* info */
void chgctrl_info_trigger(struct chgctrl *chip);
void chgctrl_info_deinit(struct chgctrl *chip);
void chgctrl_info_init(struct chgctrl *chip);

/* otp */
int chgctrl_otp_trigger(struct chgctrl *chip);
int chgctrl_otp_deinit(struct chgctrl *chip);
int chgctrl_otp_init(struct chgctrl *chip);
int chgctrl_otp_parse_dt(struct chgctrl *chip);

/* spec */
int chgctrl_spec_trigger(struct chgctrl *chip);
int chgctrl_spec_deinit(struct chgctrl *chip);
int chgctrl_spec_init(struct chgctrl *chip);
int chgctrl_spec_parse_dt(struct chgctrl *chip);

/* thermal */
int chgctrl_thermal_trigger(struct chgctrl *chip);
int chgctrl_thermal_deinit(struct chgctrl *chip);
int chgctrl_thermal_init(struct chgctrl *chip);
int chgctrl_thermal_parse_dt(struct chgctrl *chip);

/* game */
int chgctrl_game_deinit(struct chgctrl *chip);
int chgctrl_game_init(struct chgctrl *chip);
int chgctrl_game_parse_dt(struct chgctrl *chip);

/* battery_id */
#ifdef CONFIG_LGE_PM_BATTERY_ID
void chgctrl_battery_id(struct work_struct *work);
void chgctrl_psy_handle_batt_id(struct chgctrl *chip,
				struct power_supply *psy);
void chgctrl_battery_id_init(struct chgctrl *chip);
#else
static inline void chgctrl_battery_id(struct work_struct *work)
{ return; }
static inline void chgctrl_psy_handle_batt_id(struct chgctrl *chip,
					      struct power_supply *psy)
{ return; }
static inline void chgctrl_battery_id_init(struct chgctrl *chip)
{ return; }
#endif

/* model */
bool get_vzw_slow_chg_mode(void);

#endif

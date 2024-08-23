#define pr_fmt(fmt) "[CC] %s: " fmt, __func__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/power_supply.h>

#include "charger_controller.h"

#define CC_GAME_RW_PERM (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)

enum {
	CC_GAME_LOAD_NONE,
	CC_GAME_LOAD_LIGHT,
	CC_GAME_LOAD_HEAVY,
};

static int game_mode = 0;
static int game_load = CC_GAME_LOAD_NONE;
static struct timespec start;

static int set_game_mode(const char *val, const struct kernel_param *kp)
{
	struct chgctrl *chip = chgctrl_get_drvdata();
	int ret;

	if (!chip)
		return -ENODEV;

	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("failed to set game mode (%d)\n", ret);
		return ret;
	}

	if (!chip->game_enabled)
		return 0;

	if (!game_mode) {
		if (game_load == CC_GAME_LOAD_NONE)
			return 0;

		/* to deactive immediatly, cancel scheduled work */
		cancel_delayed_work(&chip->game_work);

		goto game_start;
	}

	if (game_load != CC_GAME_LOAD_NONE)
		return 0;

game_start:
	schedule_delayed_work(&chip->game_work, 0);

	return 0;
}
module_param_call(game_mode, set_game_mode,
		  param_get_int, &game_mode, CC_GAME_RW_PERM);

extern bool mtk_get_gpu_loading(unsigned int *pLoading);
static void chgctrl_game(struct work_struct *work)
{
	struct chgctrl *chip = container_of(work,
			struct chgctrl, game_work.work);
	unsigned int load = chip->game_light_load;
	int fcc = chip->game_fcc;
	int icl = chip->game_icl;
	struct timespec now, diff;

	if (!game_mode) {
		chgctrl_vote(&chip->fcc, FCC_VOTER_GAME, -1);
		chgctrl_vote(&chip->icl, ICL_VOTER_GAME, -1);
		game_load = CC_GAME_LOAD_NONE;
		return;
	}

	get_monotonic_boottime(&now);

	/* assume high loading in start */
	if (game_load == CC_GAME_LOAD_NONE) {
		start = now;
		game_load = CC_GAME_LOAD_HEAVY;
		goto out_vote;
	}

	mtk_get_gpu_loading(&load);
	if (load < chip->game_light_load) {
		/* already in low. ignore */
		if (game_load == CC_GAME_LOAD_LIGHT)
			goto out_reschedule;

		/* not enough time passed to judge */
		diff = timespec_sub(now, start);
		if (diff.tv_sec <= chip->game_light_sec)
			goto out_reschedule;

		game_load = CC_GAME_LOAD_LIGHT;
		fcc = chip->game_light_fcc;
		icl = chip->game_light_icl;
		goto out_vote;
	}

	/* mark current time as start */
	start = now;
	if (game_load != CC_GAME_LOAD_HEAVY)
		game_load = CC_GAME_LOAD_HEAVY;

out_vote:
	if (chgctrl_get_capacity() < chip->game_lowbatt_soc) {
		fcc = chip->game_lowbatt_fcc;
		icl = chip->game_lowbatt_icl;
	}

	chgctrl_vote(&chip->fcc, FCC_VOTER_GAME, fcc);
	chgctrl_vote(&chip->icl, ICL_VOTER_GAME, icl);

out_reschedule:
	schedule_delayed_work(to_delayed_work(work),
			msecs_to_jiffies(10000));
}

int chgctrl_game_deinit(struct chgctrl *chip)
{
	return 0;
}

int chgctrl_game_init(struct chgctrl *chip)
{
	INIT_DELAYED_WORK(&chip->game_work, chgctrl_game);

	return 0;
}

int chgctrl_game_parse_dt(struct chgctrl *chip)
{
	struct device_node *node = chip->dev->of_node;
	int ret = 0;

	ret = of_property_read_u32(node, "game-fcc",
			&chip->game_fcc);
	if (ret)
		chip->game_fcc = -1;

	ret = of_property_read_u32(node, "game-light-fcc",
			&chip->game_light_fcc);
	if (ret)
		chip->game_light_fcc = chip->game_fcc;

	ret = of_property_read_u32(node, "game-icl",
			&chip->game_icl);

	if (ret)
		chip->game_icl = -1;

	ret = of_property_read_u32(node, "game-light-icl",
			&chip->game_light_icl);

	if (ret)
		chip->game_light_icl = chip->game_icl;

	ret = of_property_read_u32(node, "game-light-load",
			&chip->game_light_load);
	if (ret)
		chip->game_light_load = 80;

	ret = of_property_read_u32(node, "game-light-sec",
			&chip->game_light_sec);
	if (ret)
		chip->game_light_sec = 100;

	ret = of_property_read_u32(node, "game-lowbatt-fcc",
			&chip->game_lowbatt_fcc);
	if (ret)
		chip->game_lowbatt_fcc = chip->game_fcc;

	ret = of_property_read_u32(node, "game-lowbatt_icl",
			&chip->game_lowbatt_icl);

	if (ret)
		chip->game_lowbatt_icl = chip->game_icl;

	ret = of_property_read_u32(node, "game-lowbatt-soc",
			&chip->game_lowbatt_soc);
	if (ret)
		chip->game_lowbatt_soc = 15;

	if ((chip->game_fcc == -1) && (chip->game_icl == -1))
		return -1;

	chip->game_enabled = true;

	return 0;
}

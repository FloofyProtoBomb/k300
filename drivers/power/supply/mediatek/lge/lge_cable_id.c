#define pr_fmt(fmt) "[CABLE_ID] %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/reboot.h>

/* PD */
#include <tcpm.h>
#ifdef CONFIG_MTK_BOOT
#include <mt-plat/mtk_boot_common.h>
#endif

#ifdef CONFIG_MACH_LGE
#include <soc/mediatek/lge/board_lge.h>
#endif
#ifdef CONFIG_LGE_HANDLE_PANIC
#include <soc/mediatek/lge/lge_handle_panic.h>
#endif
#include <linux/power/lge_cable_id.h>

extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);

struct cable_id_table {
	int adc_min;
	int adc_max;
	usb_cable_type type;
};

struct lge_cable_id {
	struct device *dev;

	struct work_struct update_work;
	struct work_struct reboot_work;
	struct mutex lock;

	int type;
	int voltage;

	struct notifier_block psy_nb;
	int online;

	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
	bool typec_debug;

	struct timespec time_disconnect;
	bool usb_configured;

	/* device configuration */
	struct pinctrl *sel;
	unsigned int transition_delay;

	int channel;
	unsigned int delay;

	/* options */
	bool embedded_battery;
	int debounce;

	struct cable_id_table *table;
	int table_size;

	int *otg_table;
};

static struct lge_cable_id *g_chip = NULL;

/* CAUTION: These strings are come from LK. */
static char *cable_str[] = {
	" "," "," "," "," "," ",
	"LT_56K",
	"LT_130K",
	"400MA",
	"DTC_500MA",
	"Abnormal_400MA",
	"LT_910K",
	"NO_INIT",
};

/* boot cable id inforamtion */
static usb_cable_type lge_boot_cable = NO_INIT_CABLE;

usb_cable_type lge_get_board_cable(void)
{
	return	lge_boot_cable;
}
EXPORT_SYMBOL(lge_get_board_cable);

bool lge_is_factory_cable_boot(void)
{
	switch (lge_boot_cable) {
	case LT_CABLE_56K:
	case LT_CABLE_130K:
	case LT_CABLE_910K:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(lge_is_factory_cable_boot);

/* runtime cable id inforamtion */
int lge_get_cable_voltage(void)
{
	if (!g_chip) {
		pr_err("not ready\n");
		return 0;
	}

	return g_chip->voltage;
}
EXPORT_SYMBOL(lge_get_cable_voltage);

int lge_get_cable_value(void)
{
	if (!g_chip) {
		pr_err("not ready\n");
		return 0;
	}

	switch (g_chip->type) {
	case LT_CABLE_56K:
		return 56;
	case LT_CABLE_130K:
		return 130;
	case LT_CABLE_910K:
		return 910;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(lge_get_cable_value);

bool lge_is_factory_cable(void)
{
	if (!g_chip) {
		pr_err("not ready\n");
		return 0;
	}

	switch (g_chip->type) {
	case LT_CABLE_56K:
	case LT_CABLE_130K:
	case LT_CABLE_910K:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(lge_is_factory_cable);

usb_cable_type lge_get_cable_type(void)
{
	if (!g_chip) {
		pr_err("not ready\n");
		return 0;
	}

	/* Remain type log temp.*/
	pr_err("%d\n", g_chip->type);
	return g_chip->type;
}
EXPORT_SYMBOL(lge_get_cable_type);

void lge_cable_id_set_usb_configured(bool configured)
{
	struct lge_cable_id *chip = g_chip;

	if (!chip) {
		pr_err("not ready\n");
		return;
	}

	if (!configured)
		return;

	if (chip->usb_configured)
		return;
	chip->usb_configured = true;

	if (!chip->embedded_battery)
		return;

	switch (chip->type) {
	case LT_CABLE_56K:
	case LT_CABLE_910K:
		schedule_work(&chip->reboot_work);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(lge_cable_id_set_usb_configured);

static int cable_id_get_voltage(struct lge_cable_id *chip, int *voltage)
{
	int data[4] = {0, 0, 0, 0};
	int rawvalue = 0;
	int ret;

	ret = IMM_GetOneChannelValue(chip->channel, data, &rawvalue);
	if (ret < 0) {
		pr_err("failed to read cable id\n");
		*voltage = 0;
		return ret;
	}

	*voltage = data[0] * 1000 + data[2];

	return 0;
}

#define MAX_USB_VOLTAGE_COUNT 4
#define CABLE_VOLTAGE_DIFFERENCE 100
#define NORMAL_CABLE_VOLTAGE 1500
static int lge_read_factory_cable_voltage(struct lge_cable_id *chip, int *voltage)
{
	bool normal_case = false;
	int i = 0, cable_voltage = 0;
	int cable_voltage_data[2] = {0};

	do {
		if (i != 0) msleep(10);

		if (cable_id_get_voltage(chip, &cable_voltage) < 0)
			return -1;
		cable_voltage_data[0] = cable_voltage;

		msleep(20);

		if (cable_id_get_voltage(chip, &cable_voltage) < 0)
			return -1;

		cable_voltage_data[1] = cable_voltage;

		if (abs(cable_voltage_data[1] - cable_voltage_data[0])
				< CABLE_VOLTAGE_DIFFERENCE) {
			normal_case = true;
			break;
		}
	} while (!normal_case && (++i < MAX_USB_VOLTAGE_COUNT));

	*voltage = cable_voltage;

	return 0;
}

static int lge_read_check_cable_voltage(struct lge_cable_id *chip, int *voltage)
{
	bool abnormal_cable = false;
	int i = 0, j = 0, cable_voltage = 0;
	int cable_voltage_data[MAX_USB_VOLTAGE_COUNT] = {0};

	do {
		if (i != 0) msleep(10);

		if (cable_id_get_voltage(chip, &cable_voltage) < 0)
			return -1;

		cable_voltage_data[i] = cable_voltage;

		for (j = 1; j < i + 1; j++) {
			/* Assume that the cable is normal when the differences are over 100 mV */
			if (abs(cable_voltage_data[i] - cable_voltage_data[i-j])
					> CABLE_VOLTAGE_DIFFERENCE) {
				abnormal_cable = true;
				cable_voltage = NORMAL_CABLE_VOLTAGE;
				break;
			}
		}
	} while (!abnormal_cable && (++i < MAX_USB_VOLTAGE_COUNT));

	*voltage = cable_voltage;

	return 0;
}

static int cable_id_read_voltage(struct lge_cable_id *chip, int *voltage)
{
	if (!chip->embedded_battery)
		return cable_id_get_voltage(chip, voltage);

	/* embedded battery */
	if (lge_is_factory_cable_boot())
		return lge_read_factory_cable_voltage(chip, voltage);

	return lge_read_check_cable_voltage(chip, voltage);
}

static void cable_id_read_pre(struct lge_cable_id *chip)
{
	struct pinctrl_state *state;

	if (IS_ERR(chip->sel))
		return;

	state = pinctrl_lookup_state(chip->sel, "transition");
	if (!IS_ERR(state)) {
		pinctrl_select_state(chip->sel, state);
		if (chip->transition_delay)
			msleep(chip->transition_delay);
	}

	state = pinctrl_lookup_state(chip->sel, "adc");
	if (IS_ERR(state))
		return;

	pinctrl_select_state(chip->sel, state);
}

static void cable_id_read_post(struct lge_cable_id *chip)
{
	struct pinctrl_state *state;

	if (IS_ERR(chip->sel))
		return;

	state = pinctrl_lookup_state(chip->sel, "transition");
	if (!IS_ERR(state)) {
		pinctrl_select_state(chip->sel, state);
		if (chip->transition_delay)
			msleep(chip->transition_delay);
	}

	state = pinctrl_lookup_state(chip->sel, "default");
	if (IS_ERR(state))
		return;

	pinctrl_select_state(chip->sel, state);
}

static int cable_id_find_type(struct lge_cable_id *chip, int adc)
{
	int i;

	/* if valid table not exist, just return as normal */
	if (!chip->table || !chip->table_size)
		return USB_CABLE_400MA;

	for (i = 0; i < chip->table_size; i++) {
		/* found matched cable id */
		if (adc >= chip->table[i].adc_min
				&& adc <= chip->table[i].adc_max) {
			return chip->table[i].type;
		}
	}

	return -EINVAL;
}

static void cable_id_update(struct work_struct *work)
{
	struct lge_cable_id *chip = container_of(work,
			struct lge_cable_id, update_work);
	int retry_cnt = 3;
	int cnt;
	int voltage;
	int type;
	int ret;

	mutex_lock(&chip->lock);

	if (!chip->online) {
		chip->type = NO_INIT_CABLE;
		chip->voltage = 0;
		goto out_update;
	}

	/* do not read adc if cable is not for debug */
	if (chip->tcpc && !chip->typec_debug) {
		chip->type = USB_CABLE_400MA;
		chip->voltage = 0;
		goto out_update;
	}

	cable_id_read_pre(chip);

	/* wait for adc voltage stabilized */
	if (chip->delay)
		msleep(chip->delay);

	for (cnt = 0; cnt < retry_cnt; cnt++) {
		/* wait before retry */
		if (cnt)
			msleep(50);

		ret = cable_id_read_voltage(chip, &voltage);
		if (ret)
			continue;

		pr_info("cable id voltage = %dmV\n", voltage);

		type = cable_id_find_type(chip, voltage);
		if (type < 0)
			continue;

		/* found type. exit loop */
		pr_info("cable id = %s\n", cable_str[type]);
		chip->type = type;
		chip->voltage = voltage;

		break;
	}

	cable_id_read_post(chip);

out_update:
	mutex_unlock(&chip->lock);

	if (chip->embedded_battery)
		schedule_work(&chip->reboot_work);

	return;
}

static void cable_id_reboot(struct work_struct *work)
{
	struct lge_cable_id *chip = container_of(work,
			struct lge_cable_id, reboot_work);
	struct timespec time_now;
	struct timespec time_diff;

	get_monotonic_boottime(&time_now);

	/* mark disconnect time */
	if (!chip->online) {
		chip->time_disconnect = time_now;
		return;
	}

	switch (chip->type) {
	case LT_CABLE_56K:
#ifdef CONFIG_MTK_BOOT
		/* do not reboot except normal boot */
		if (get_boot_mode() != NORMAL_BOOT)
			break;
#endif
		if (lge_is_factory_cable_boot())
			break;

		pr_info("[FACTORY] PIF_56K detected in NORMAL BOOT, reboot!!\n");
		/* wait for usb configuration */
		msleep(500);
		kernel_restart("LGE Reboot by PIF 56k");
		break;
	case LT_CABLE_910K:
#ifdef CONFIG_MTK_BOOT
		/* do not reboot in recovery boot */
		if (get_boot_mode() == RECOVERY_BOOT)
			break;
#endif
#ifdef CONFIG_MACH_LGE
		/* do not reboot in laf mode */
		if (lge_get_laf_mode())
			break;
#endif

		if (lge_boot_cable == LT_CABLE_910K) {
			/* do not reboot before plug-out */
			if (!chip->time_disconnect.tv_sec)
				break;
			/* do not reboot if usb not configured yet */
			if (!chip->usb_configured)
				break;
		}

		/* do not reboot if cable re-plugged too fast */
		time_diff = timespec_sub(time_now, chip->time_disconnect);
		if (time_diff.tv_sec <= chip->debounce)
			break;

		pr_info("[FACTORY] PIF_910K detected, reboot!!\n");
		/* wait for usb configuration */
		msleep(500);
#ifdef CONFIG_LGE_HANDLE_PANIC
		lge_set_reboot_reason(LGE_REBOOT_REASON_DLOAD);
#endif
		kernel_restart("LGE Reboot by PIF 910k");
		break;
	default:
		break;
	}
}

static int cable_id_psy_notifier_call(struct notifier_block *nb,
				      unsigned long event, void *v)
{
	struct lge_cable_id *chip = container_of(nb,
			struct lge_cable_id, psy_nb);
	struct power_supply *psy = (struct power_supply*)v;
	union power_supply_propval val;
	int ret;

	/* handle only usb */
	switch (psy->desc->type) {
	case POWER_SUPPLY_TYPE_USB:
	case POWER_SUPPLY_TYPE_USB_CDP:
		break;
	default:
		return NOTIFY_DONE;
	}

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret)
		return NOTIFY_DONE;

	if (val.intval == chip->online)
		return NOTIFY_DONE;

	chip->online = val.intval;
	schedule_work(&chip->update_work);

	return NOTIFY_DONE;
}

#ifdef CONFIG_TCPC_CLASS
static int cable_id_pd_tcp_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct lge_cable_id *chip = container_of(nb,
			struct lge_cable_id, pd_nb);
	struct tcp_notify *noti = data;

	if (event != TCP_NOTIFY_TYPEC_STATE)
		return NOTIFY_OK;

	switch (noti->typec_state.new_state) {
	case TYPEC_ATTACHED_DEBUG:
	case TYPEC_ATTACHED_DBGACC_SNK:
	case TYPEC_ATTACHED_CUSTOM_SRC:
		pr_info("debug accessory attached\n");
		chip->typec_debug = true;
		break;
	default:
		if (chip->typec_debug)
			pr_info("debug unattached\n");
		chip->typec_debug = false;
		break;
	}

	return NOTIFY_OK;
}
#endif

int lge_cable_id_is_otg(void)
{
	struct lge_cable_id *chip = g_chip;
	int retry_cnt = 3;
	int cnt;
	int voltage = 0;
	int is_otg = 0;

	if (!chip) {
		pr_err("not ready\n");
		return 1;
	}

	/* if charger exist, assume not otg cable */
	if (chip->online)
		return 0;

	/* otg table not exist, assume otg connected */
	if (!chip->otg_table)
		return 1;

	mutex_lock(&chip->lock);

	cable_id_read_pre(chip);

	/* wait for adc voltage stabilized */
	if (chip->delay)
		msleep(chip->delay);

	for (cnt = 0; cnt < retry_cnt; cnt++) {
		if (cable_id_read_voltage(chip, &voltage))
			continue;

		pr_info("otg adc voltage = %dmV\n", voltage);
		if (voltage < chip->otg_table[0] ||
				 voltage > chip->otg_table[1])
			continue;

		/* otg adc in in range */
		pr_info("otg adc valid\n");
		is_otg = 1;
		break;
	}

	cable_id_read_post(chip);

	mutex_unlock(&chip->lock);

	return is_otg;
}
EXPORT_SYMBOL(lge_cable_id_is_otg);

/*
 * AT%USBIDADC
 * - output : adc,id
 * - /proc/lge_power/testmode/usb_id
 */
#define USB_ID_ATCMD_RO_PERM (S_IRUSR | S_IRGRP | S_IROTH)
static int param_get_atcmd_usb_id(char *buffer, const struct kernel_param *kp)
{
	int adc, id;

	if (!g_chip)
		return -ENODEV;

	adc = lge_get_cable_voltage();
	id = lge_get_cable_value();

	return scnprintf(buffer, PAGE_SIZE, "%d,%d", adc, id);
}
static struct kernel_param_ops atcmd_usb_id = {
	.get = param_get_atcmd_usb_id,
};
module_param_cb(atcmd_usb_id, &atcmd_usb_id, NULL, USB_ID_ATCMD_RO_PERM);

static int param_get_usb_id(char *buffer, const struct kernel_param *kp)
{
	int voltage, ret;

	if (!g_chip)
		return -ENODEV;

	mutex_lock(&g_chip->lock);

	cable_id_read_pre(g_chip);

	/* wait for adc voltage stabilized */
	if (g_chip->delay)
		msleep(g_chip->delay);

	ret = cable_id_read_voltage(g_chip, &voltage);
	if (ret)
		voltage = 0;

	cable_id_read_post(g_chip);

	mutex_unlock(&g_chip->lock);

	return scnprintf(buffer, PAGE_SIZE, "%d", voltage * 1000);
}
static struct kernel_param_ops usb_id = {
	.get = param_get_usb_id,
};
module_param_cb(usb_id, &usb_id, NULL, USB_ID_ATCMD_RO_PERM);

static void cable_id_dump_info(struct lge_cable_id *chip)
{
	int i;

	pr_info("channel = %d, delay = %d ms\n", chip->channel, chip->delay);
	if (chip->embedded_battery) {
		pr_info("embedded battery mode with %d sec debounce\n",
				chip->debounce);
	}

	pr_info("%s mode\n", chip->tcpc ? "type-c" : "type-b");
	for (i = 0; i < chip->table_size; i++) {
		pr_info("%s = %dmV to %dmV\n", cable_str[chip->table[i].type],
			chip->table[i].adc_min, chip->table[i].adc_max);
	}

	if (chip->otg_table) {
		pr_info("OTG = %dmV to %dmV\n",
				chip->otg_table[0], chip->otg_table[1]);
	}
}

static int cable_id_parse_dt(struct lge_cable_id *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct property *prop = NULL;
	const __be32 *data = NULL;
	int size;
	int ret;
	int i;

	ret = of_property_read_u32(node, "channel", &chip->channel);
	if (ret)
		return ret;

	ret = of_property_read_u32(node, "delay", &chip->delay);
	if (ret)
		chip->delay = 0;

	ret = of_property_read_u32(node, "transition-delay",
			&chip->transition_delay);
	if (ret)
		chip->transition_delay = 0;

	chip->embedded_battery = of_property_read_bool(node, "embedded-battery");
	ret = of_property_read_u32(node, "debounce", &chip->debounce);
	if (ret)
		chip->debounce = 5;	/* default : 5sec */

	prop = of_find_property(node, "range", &size);
	if (!prop)
		return -ENODATA;

	/* invalid data size */
	if (!size || size % sizeof(struct cable_id_table))
		return -EINVAL;

	chip->table_size = size / sizeof(struct cable_id_table);
	chip->table = (struct cable_id_table *)devm_kzalloc(chip->dev, size,
			GFP_KERNEL);
	if (!chip->table)
		return -ENOMEM;

	for (i = 0; i < chip->table_size; i++) {
		data = of_prop_next_u32(prop, data, &chip->table[i].adc_min);
		data = of_prop_next_u32(prop, data, &chip->table[i].adc_max);
		data = of_prop_next_u32(prop, data, &chip->table[i].type);
		if (chip->table[i].type < LT_CABLE_56K
				|| chip->table[i].type > NO_INIT_CABLE)
			return -EINVAL;
	}

	chip->otg_table = (int *)devm_kzalloc(chip->dev, 2, GFP_KERNEL);
	if (!chip->otg_table)
		return 0;
	if (of_property_read_u32_array(node, "otg-range", chip->otg_table, 2)) {
		devm_kfree(chip->dev, chip->otg_table);
		chip->otg_table = NULL;
	}

	return 0;
}

static int cable_id_probe(struct platform_device *pdev)
{
	struct lge_cable_id *chip = NULL;
	int ret = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct lge_cable_id),
			GFP_KERNEL);
	if (!chip) {
		pr_err("failed to alloc memory\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	mutex_init(&chip->lock);
	INIT_WORK(&chip->update_work, cable_id_update);
	INIT_WORK(&chip->reboot_work, cable_id_reboot);

	chip->sel = devm_pinctrl_get(&pdev->dev);

	ret = cable_id_parse_dt(chip);
	if (ret) {
		pr_err("failed to parse device-tree\n");
		return ret;
	}

#ifdef CONFIG_TCPC_CLASS
	chip->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!chip->tcpc)
		return -EPROBE_DEFER;

	chip->pd_nb.notifier_call = cable_id_pd_tcp_notifier_call;
	ret = register_tcp_dev_notifier(chip->tcpc, &chip->pd_nb,
					TCP_NOTIFY_TYPE_USB);
#endif

	/* power supply notifier */
	chip->psy_nb.notifier_call = cable_id_psy_notifier_call;
	ret = power_supply_reg_notifier(&chip->psy_nb);
	if (ret) {
		pr_err("failed to register notifier for power_supply\n");
		return ret;
	}

	cable_id_dump_info(chip);

	g_chip = chip;

	return 0;
}

static int cable_id_remove(struct platform_device *pdev)
{
	struct lge_cable_id *chip = platform_get_drvdata(pdev);

	power_supply_unreg_notifier(&chip->psy_nb);

	return 0;
}

static struct of_device_id cable_id_match_table[] = {
	{
		.compatible = "lge,cable-id",
	},
	{},
};

static struct platform_driver cable_id_driver = {
	.probe = cable_id_probe,
	.remove = cable_id_remove,
	.driver = {
		.name = "cable-id",
		.owner = THIS_MODULE,
		.of_match_table = cable_id_match_table,
	},
};

static int __init boot_cable_setup(char *cable_info)
{
	int len;
	int i;

	lge_boot_cable = NO_INIT_CABLE;

	for (i = LT_CABLE_56K; i <= NO_INIT_CABLE; i++)	{
		len = max(strlen(cable_info), strlen(cable_str[i]));
		if(strncmp(cable_info, cable_str[i], len))
			continue;

		/* cable type matched */
		lge_boot_cable = (usb_cable_type) i;
		break;
	}

	pr_info("boot cable = %s\n", cable_str[lge_boot_cable]);

	return 1;
}
__setup("lge.cable=", boot_cable_setup);

static int __init fdt_find_boot_cable(unsigned long node, const char *uname,
	int depth, void *data)
{
	char *id;

	if (depth != 1)
		return 0;

	if (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0)
		return 0;

	id = (char*)of_get_flat_dt_prop(node, "lge,boot-cable", NULL);
	if (!id)
		return 0;

	boot_cable_setup(id);

	return 1;
}

static int __init lge_cable_id_pure_init(void)
{
	int rc;

	rc = of_scan_flat_dt(fdt_find_boot_cable, NULL);
	if (!rc)
		pr_err("boot cable not found\n");

	return 0;
}

static int __init lge_cable_id_init(void)
{
	return platform_driver_register(&cable_id_driver);
}

static void __exit lge_cable_id_exit(void)
{
	platform_driver_unregister(&cable_id_driver);
}

pure_initcall(lge_cable_id_pure_init);
module_init(lge_cable_id_init);
module_exit(lge_cable_id_exit);

MODULE_DESCRIPTION("LG Electronics USB Cable ID Module");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");

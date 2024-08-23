/* Copyright (c) 2013-2014, LG Eletronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
#include "charger_controller.h"
#endif


#define PWR_SYSFS_MANDATORY_MAX_NUM 5

struct power_sysfs_array {
	const char *group;
	const char *user_node;
	const char *kernel_node;
};

static const char *group_names[] = {
	"adc",
	"battery",
	"charger",
	"lcd",
	"key_led",
	"cpu",
	"gpu",
	"platform",
	"psfeature",
	"testmode",
};

static struct power_sysfs_array *arr;
static int arr_cnt;

static int power_sysfs_parse_dt(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int arr_num, ret, i;

	arr_cnt = of_property_count_strings(node, "sysfs,node") / 3;
	if (arr_cnt > 0)
		pr_info("%s : Total sysfs node is %d\n", __func__, arr_cnt);
	else {
		pr_err("%s : ERROR sysfs node isn't exist\n", __func__);
		return 0;
	}

	arr = devm_kzalloc(&pdev->dev, arr_cnt *
			sizeof(struct power_sysfs_array), GFP_KERNEL);
	if (!arr) {
		pr_err("%s : ERROR get sysfs array\n", __func__);
		return -1;
	}

	for (arr_num = 0, i = 0; arr_num < arr_cnt; arr_num++) {
		ret = of_property_read_string_index(node, "sysfs,node",
				i++, &arr[arr_num].group);
		if (ret) {
			pr_err("%s : ERROR get %ith group\n", __func__, arr_num);
			goto err_get_array;
		}
		ret = of_property_read_string_index(node, "sysfs,node",
				i++, &arr[arr_num].user_node);
		if (ret) {
			pr_err("%s : ERROR get %ith user_node\n", __func__, arr_num);
			goto err_get_array;
		}
		ret = of_property_read_string_index(node, "sysfs,node",
				i++, &arr[arr_num].kernel_node);
		if (ret) {
			pr_err("%s : ERROR get %ith kernel_node\n", __func__, arr_num);
			goto err_get_array;
		}
	}

	return 0;

err_get_array:
	return -1;
}

static int power_sysfs_make_path(struct platform_device *pdev)
{
	int arr_num, group_num;
	struct proc_dir_entry *p, *temp_p;
	struct proc_dir_entry **groups_p;

	groups_p = devm_kzalloc(&pdev->dev, ARRAY_SIZE(group_names) *
			sizeof(struct proc_dir_entry *), GFP_KERNEL);
	if (!groups_p) {
		pr_err("%s : ERROR make groups pointer \n", __func__);
		return -ENOMEM;
	}

	/* Set Power Sysfs root directory */
	p = proc_mkdir("lge_power", NULL);
	if (p == NULL) {
		pr_err("%s : ERROR make root sysfs \n", __func__);
		return -ENOMEM;
	}

	/* Set Power Sysfs group directory */
	for (group_num = 0; group_num < ARRAY_SIZE(group_names); group_num++) {
		groups_p[group_num] = proc_mkdir(group_names[group_num], p);
		if (groups_p[group_num] == NULL) {
			pr_err("%s : ERROR make %s group \n", __func__,
					group_names[group_num]);
			return -ENOMEM;
		}
	}

	/* Set Power Sysfs Path */
	for (arr_num = 0; arr_num < arr_cnt; arr_num++) {
		for (group_num = 0; group_num < ARRAY_SIZE(group_names); group_num++) {
			if (!strcmp(arr[arr_num].group, group_names[group_num]))
				break;
		}

		if (group_num == ARRAY_SIZE(group_names)) {
			pr_err("%s : invalid group %s", __func__, arr[arr_num].group);
			continue;
		}

		if (!strcmp(arr[arr_num].kernel_node, "NULL")) {
			pr_warn("%s : %s user node didn't have kernel node \n",
					__func__, arr[arr_num].user_node);
			continue;
		}

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
		if (!get_vzw_slow_chg_mode()) {
			if (!strcmp(arr[arr_num].user_node, "vzw_chg")) {
				pr_info("Skip vzw_chg sysnode %s %s %s\n",
					arr[arr_num].group, arr[arr_num].user_node, arr[arr_num].kernel_node);
				continue;
			}
		}
#endif

		temp_p = proc_symlink(arr[arr_num].user_node, groups_p[group_num],
				arr[arr_num].kernel_node);
		if (temp_p == NULL) {
			pr_err("%s : ERROR make %ith sysfs path(%s, %s, %s)\n",
					__func__, arr_num, arr[arr_num].group,
					arr[arr_num].kernel_node, arr[arr_num].user_node);
			return -ENOMEM;
		}
	}

	return 0;
}

static int power_sysfs_probe(struct platform_device *pdev)
{
	int ret;

	if (!pdev->dev.of_node)
		return -EINVAL;

	ret = power_sysfs_parse_dt(pdev);
	if (ret < 0) {
		pr_err("%s : ERROR Parsing DT\n", __func__);
		return ret;
	}

	ret = power_sysfs_make_path(pdev);
	if (ret != 0) {
		pr_err("%s : ERROR make sysfs path\n", __func__);
		return ret;
	}

	pr_info("%s : Success Power sysfs Init\n", __func__);

	return ret;
}

static int power_sysfs_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id power_sysfs_match_table[] = {
	{ .compatible = "lge,power-sysfs" },
	{},
};

static struct platform_driver power_sysfs_driver = {
	.probe = power_sysfs_probe,
	.remove = power_sysfs_remove,
	.driver = {
		.name = "power-sysfs",
		.owner = THIS_MODULE,
		.of_match_table = power_sysfs_match_table,
	},
};

static int __init power_sysfs_init(void)
{
	return platform_driver_register(&power_sysfs_driver);
}

static void power_sysfs_exit(void)
{
	platform_driver_unregister(&power_sysfs_driver);
}

late_initcall(power_sysfs_init);
module_exit(power_sysfs_exit);
MODULE_DESCRIPTION("LGE Power sysfs driver");
MODULE_LICENSE("GPL v2");

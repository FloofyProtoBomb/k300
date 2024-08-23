#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

#
# Makefile for misc devices that really don't fit anywhere else.
#

subdir-ccflags-$(CONFIG_MTK_LCM) += -I$(srctree)/drivers/misc/mediatek/lcm/inc \
                                    -I$(srctree)/drivers/misc/mediatek/pmic/mt6370/inc
subdir-ccflags-$(CONFIG_LGE_SET_LCD_BIAS) += -I$(srctree)/drivers/misc/mediatek/lcd_bias
subdir-ccflags-$(CONFIG_MTK_FB) += -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/dispsys \
                                   -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/dispsys/mt6735 \
                                   -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/videox \
                                   -I$(srctree)/drivers/misc/mediatek/video/include

JDM_LCM_LISTS := $(subst ",,$(CONFIG_CUSTOM_KERNEL_LCM))
obj-$(CONFIG_MTK_LCM) += $(foreach LCM,$(JDM_LCM_LISTS),$(LCM)/)

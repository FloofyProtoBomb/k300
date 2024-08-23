dtb-$(CONFIG_ARCH_MEDIATEK) += mt6755-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6795-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += auto2712m1v1-ivi-boot.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += auto2712m1v1-ivi_agl-vp1-boot.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += auto2712m1v1-initramfs.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += auto2712m1v1-ivi-nand-boot.dtb

ifeq ($(strip $(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE)), y)
dtb-y += $(addsuffix .dtb, $(notdir $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))))
endif

ifeq ($(findstring lge,$(CONFIG_BUILD_ARM64_DTB_OVERLAY_IMAGE_NAMES)),lge)
$(info use lge dts from $(CONFIG_BUILD_ARM64_DTB_OVERLAY_IMAGE_NAMES))
dts-dirs += lge
else
ifeq ($(strip $(CONFIG_BUILD_ARM64_DTB_OVERLAY_IMAGE)), y)
dtb-y += $(addsuffix .dtb, $(notdir $(subst $\",,$(CONFIG_BUILD_ARM64_DTB_OVERLAY_IMAGE_NAMES))))
endif
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb

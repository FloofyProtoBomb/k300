
# In case the platform does NOT support this type of sensors
ifeq ($(CONFIG_JDM_LCM),y)
obj-$(CONFIG_CUSTOM_KERNEL_CHARGEPUMP) += jdm/
else
obj-$(CONFIG_DSV_DW8768L) += dw8768l/
endif
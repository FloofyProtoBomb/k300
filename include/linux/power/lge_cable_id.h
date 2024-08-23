#ifndef __LGE_CABLE_ID_H__
#define __LGE_CABLE_ID_H__

typedef enum {
	LT_CABLE_56K		= 6,
	LT_CABLE_130K		= 7,
	USB_CABLE_400MA		= 8,
	USB_CABLE_DTC_500MA	= 9,
	ABNORMAL_USB_CABLE_400MA	= 10,
	LT_CABLE_910K		= 11,
	NO_INIT_CABLE		= 12,
} usb_cable_type;

extern usb_cable_type lge_get_board_cable(void);
extern bool lge_is_factory_cable_boot(void);

extern int lge_get_cable_voltage(void);
extern int lge_get_cable_value(void);
extern bool lge_is_factory_cable(void);
extern usb_cable_type lge_get_cable_type(void);
extern void lge_cable_id_set_usb_configured(bool configured);

extern int lge_cable_id_is_otg(void);

#endif

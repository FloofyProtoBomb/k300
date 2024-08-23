#ifndef __CUSTOM_BATTERY_TABLE__
#define __CUSTOM_BATTERY_TABLE__

#define BAT_NTC_68 1
#define RBAT_PULL_UP_R             62000
#define RBAT_PULL_UP_VOLT          1800
#define BIF_NTC_R 16000

/*
 * NTC data sheet value has low accuracy in low voltage
 * This value is tuning value
 */
struct FUELGAUGE_TEMPERATURE Fg_Temperature_Table[21] = {
		{-20, 801075 },
		{-15, 579371},
		{-10, 398175},
		{-5, 300908},
		{0, 235614},
		{5, 176403},
		{10, 141524},
		{15, 108303},
		{20, 81520},
		{25, 63492},
		{30, 50675},
		{35, 40698},
		{40, 32880},
		{45, 26718},
		{50, 22909},
		{55, 18774},
		{60, 15423},
		{65, 12967},
		{70, 10791},
		{75, 9021},
		{80, 7574},
};
#endif

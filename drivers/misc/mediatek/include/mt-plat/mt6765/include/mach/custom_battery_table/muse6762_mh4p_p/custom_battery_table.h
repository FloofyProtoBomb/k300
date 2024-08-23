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
		{-20, 601129},
		{-15, 459581},
		{-10, 362915},
		{-5, 280636},
		{0, 213810},
		{5, 168665},
		{10, 131929},
		{15, 103032},
		{20, 81520},
		{25, 65681},
		{30, 52685},
		{35, 42660},
		{40, 34465},
		{45, 27890},
		{50, 22909},
		{55, 18774},
		{60, 15423},
		{65, 12967},
		{70, 10791},
		{75, 9021},
		{80, 7574},
};
#endif

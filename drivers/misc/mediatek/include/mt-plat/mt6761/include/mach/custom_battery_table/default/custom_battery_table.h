#ifndef __CUSTOM_BATTERY_TABLE__
#define __CUSTOM_BATTERY_TABLE__

#define BAT_NTC_68 1
#define RBAT_PULL_UP_R             62000
#define RBAT_PULL_UP_VOLT          1800
#define BIF_NTC_R 16000

/* 68K NTC values from data sheet */
struct FUELGAUGE_TEMPERATURE Fg_Temperature_Table[21] = {
		{-20, 738978},
		{-15, 485782},
		{-10, 409600},
		{-5, 309217},
		{0, 235606},
		{5, 180980},
		{10, 140139},
		{15, 109344},
		{20, 85929},
		{25, 68000},
		{30, 54167},
		{35, 43421},
		{40, 35016},
		{45, 28406},
		{50, 23166},
		{55, 18997},
		{60, 15657},
		{65, 12967},
		{70, 10794},
		{75, 9021},
		{80, 7575},
};
#endif

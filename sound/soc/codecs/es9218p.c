/*
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See http://www.gnu.org/licenses/gpl-2.0.html for more details.
*/

/*
 *  ES9218P Device Driver.
 *
 *  This program is device driver for ES9218P chip set.
 *  The ES9218P is a high-performance 32-bit, 2-channel audio SABRE HiFi D/A converter
 *  with headphone amplifier, analog volume control and output switch designed for
 *  audiophile-grade portable application such as mobile phones and digital music player,
 *  consumer applications such as USB DACs and A/V receivers, as well as professional
 *  such as mixer consoles and digital audio workstations.
 *
 *  Copyright (C) 2016, ESS Technology International Ltd.
 *
 */

#include    <linux/module.h>
#include    <linux/moduleparam.h>
#include    <linux/init.h>
#include    <linux/slab.h>
#include    <linux/delay.h>
#include    <linux/pm.h>
#include    <linux/platform_device.h>
#include    <sound/core.h>
#include    <sound/pcm.h>
#include    <sound/pcm_params.h>
#include    <sound/soc.h>
#include    <sound/initval.h>
#include    <sound/tlv.h>
#include    <trace/events/asoc.h>
#include    <linux/of_gpio.h>
#include    <linux/gpio.h>
#include    <linux/i2c.h>
#include    <linux/fs.h>
#include    <linux/string.h>
#include    <linux/regulator/consumer.h>
#include    "es9218p.h"

#if defined(CONFIG_LGE_HIFI_HWINFO)
#include <soc/mediatek/lge/board_lge.h>
#endif

#define     ES9218P_SYSFS               // use this feature only for user debug, not release
#define     SHOW_LOGS                   // show debug logs only for debug mode, not release
#ifdef CONFIG_MACH_MT6762_MH4
#define     LGE_HW_GAIN_TUNING 12
#elif defined CONFIG_MACH_MT6762_MH4P
#define     LGE_HW_GAIN_TUNING 10
#endif
static struct regulator *hifi_vcn33_ldo;

enum hifi_gpio_type {
        PINCTRL_HIFI_DEFAULT = 0,
        PINCTRL_HIFI_RESET_OFF,
        PINCTRL_HIFI_RESET_ON,
        PINCTRL_MODE_LOW,
        PINCTRL_MODE_HIGH,
        PINCTRL_POWER_EXT_LDO_OFF,
        PINCTRL_POWER_EXT_LDO_ON,
        PINCTRL_HIFI_NUM
};

static struct pinctrl *pinctrl_hifi;

struct hifi_gpio_attr {
        const char *name;
        struct pinctrl_state *gpioctrl;
};

struct hifi_gpio_attr hifi_gpios[PINCTRL_HIFI_NUM] = {
        [PINCTRL_HIFI_DEFAULT] = {"hifi_default", NULL},
        [PINCTRL_HIFI_RESET_OFF] = {"hifi_reset_off", NULL},
        [PINCTRL_HIFI_RESET_ON] = {"hifi_reset_on", NULL},
        [PINCTRL_MODE_LOW] = {"hifi_mode_low", NULL},
        [PINCTRL_MODE_HIGH] = {"hifi_mode_high", NULL},
        [PINCTRL_POWER_EXT_LDO_OFF] = {"hifi_ext_ldo_off", NULL},
        [PINCTRL_POWER_EXT_LDO_ON] = {"hifi_ext_ldo_on", NULL},
};

struct es9218_reg {
    unsigned char   num;
    unsigned char   value;
};

/*
 *  We only include the analogue supplies here; the digital supplies
 *  need to be available well before this driver can be probed.
 */

#ifdef CONFIG_MACH_MT6762_MH4
struct es9218_reg es9218_init_register[] = {
{ ES9218P_REG_00,        0x00 },    // System Register
{ ES9218P_REG_01,        0xC0 },    // Input selection
{ ES9218P_REG_02,        0x34 },    // Mixing, Serial Data and Automute Configuration
{ ES9218P_REG_03,        0x40 },    // Analog Volume Control
{ ES9218P_REG_04,        0xFF },    // Automute Time
{ ES9218P_REG_05,        0x7A },    // Automute Level
{ ES9218P_REG_06,        0x02 },    // DoP and Volmue Ramp Rate
{ ES9218P_REG_07,        0x40 },    // Filter Bandwidth and System Mute
{ ES9218P_REG_08,        0xB0 },    // GPIO1-2 Confgiguratioin
{ ES9218P_REG_09,        0x22 },    // GPIO1-2 Confgiguratioin
{ ES9218P_REG_10,        0x02 },    // Master Mode and Sync Configuration
{ ES9218P_REG_11,        0x00 },    // Overcureent Protection
{ ES9218P_REG_12,        0xFA },    // ASRC/DPLL Bandwidth
{ ES9218P_REG_13,        0x00 },    // THD Compensation Bypass & Mono Mode
{ ES9218P_REG_14,        0x07 },    // Soft Start Configuration
{ ES9218P_REG_15,        0x01 },    // Volume Control
{ ES9218P_REG_16,        0x01 },    // Volume Control
{ ES9218P_REG_17,        0xFF },    // Master Trim
{ ES9218P_REG_18,        0xFF },    // Master Trim
{ ES9218P_REG_19,        0xFF },    // Master Trim
{ ES9218P_REG_20,        0x7F },    // Master Trim
{ ES9218P_REG_21,        0x0D },    // GPIO Input Selection
{ ES9218P_REG_22,        0x46 },    // THD Compensation C2 (left)
{ ES9218P_REG_23,        0x02 },    // THD Compensation C2 (left)
{ ES9218P_REG_24,        0x28 },    // THD Compensation C3 (left)
{ ES9218P_REG_25,        0x00 },    // THD Compensation C3 (left)
{ ES9218P_REG_26,        0x62 },    // Charge Pump Soft Start Delay
{ ES9218P_REG_27,        0xD4 },    // Charge Pump Soft Start Delay
{ ES9218P_REG_28,        0xF0 },
{ ES9218P_REG_29,        0x0D },    // General Confguration
{ ES9218P_REG_30,        0x00 },    // GPIO Inversion & Automatic Clock Gearing
{ ES9218P_REG_31,        0x00 },    // GPIO Inversion & Automatic Clock Gearing
{ ES9218P_REG_32,        0x00 },    // Amplifier Configuration
//default           { ES9218P_REG_34,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_35,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_36,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_37,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_40,        0x00 },    // Programmable FIR RAM Address
//default           { ES9218P_REG_41,        0x00 },    // Programmable FIR RAM Data
//default           { ES9218P_REG_42,        0x00 },    // Programmable FIR RAM Data
//default           { ES9218P_REG_43,        0x00 },    // Programmable FIR RAM Data
//will be upadated  { ES9218P_REG_44,        0x00 },    // Programmable FIR Configuration
//will be upadated  { ES9218P_REG_45,        0x00 },    // Analog Control Override
//will be upadated  { ES9218P_REG_46,        0x00 },    // dig_over_en/reserved/apdb/cp_clk_sel/reserved
//will be upadated  { ES9218P_REG_47,        0x00 },    // enfcb/encp_oe/enaux_oe/cpl_ens/cpl_enw/sel3v3_ps/ensm_ps/sel3v3_cph
//will be upadated  { ES9218P_REG_48,        0x02 },    // reserved/enhpa_out/reverved
//default           { ES9218P_REG_49,        0x62 },    // Automatic Clock Gearing Thresholds
//default           { ES9218P_REG_50,        0xc0 },    // Automatic Clock Gearing Thresholds
//default           { ES9218P_REG_51,        0x0d },    // Automatic Clock Gearing Thresholds
//will be upadated  { ES9218P_REG_53,        0x00 },    // THD Compensation C2 (Right)
//will be upadated  { ES9218P_REG_54,        0x00 },    // THD Compensation C2 (Right)
//will be upadated  { ES9218P_REG_55,        0x00 },    // THD Compensation C3 (Right)
//will be upadated  { ES9218P_REG_56,        0x00 },    // THD Compensation C3 (Right)
//default           { ES9218P_REG_60,        0x00 },    // DAC Analog Trim Control
};
#elif defined CONFIG_MACH_MT6762_MH4P
struct es9218_reg es9218_init_register[] = {
{ ES9218P_REG_00,        0x00 },    // System Register
{ ES9218P_REG_01,        0xC0 },    // Input selection
{ ES9218P_REG_02,        0xB4 },    // Mixing, Serial Data and Automute Configuration
{ ES9218P_REG_03,        0x40 },    // Analog Volume Control
{ ES9218P_REG_04,        0xFF },    // Automute Time
{ ES9218P_REG_05,        0x7A },    // Automute Level
{ ES9218P_REG_06,        0x02 },    // DoP and Volmue Ramp Rate
{ ES9218P_REG_07,        0x40 },    // Filter Bandwidth and System Mute
{ ES9218P_REG_08,        0xB0 },    // GPIO1-2 Confgiguratioin
{ ES9218P_REG_09,        0x22 },    // GPIO1-2 Confgiguratioin
{ ES9218P_REG_10,        0x02 },    // Master Mode and Sync Configuration
{ ES9218P_REG_11,        0x90 },    // Overcureent Protection
{ ES9218P_REG_12,        0xFA },    // ASRC/DPLL Bandwidth
{ ES9218P_REG_13,        0x00 },    // THD Compensation Bypass & Mono Mode
{ ES9218P_REG_14,        0x07 },    // Soft Start Configuration
{ ES9218P_REG_15,        0x01 },    // Volume Control
{ ES9218P_REG_16,        0x01 },    // Volume Control
{ ES9218P_REG_17,        0xFF },    // Master Trim
{ ES9218P_REG_18,        0xFF },    // Master Trim
{ ES9218P_REG_19,        0xFF },    // Master Trim
{ ES9218P_REG_20,        0x7F },    // Master Trim
{ ES9218P_REG_21,        0x0D },    // GPIO Input Selection
{ ES9218P_REG_22,        0x46 },    // THD Compensation C2 (left)
{ ES9218P_REG_23,        0x02 },    // THD Compensation C2 (left)
{ ES9218P_REG_24,        0x28 },    // THD Compensation C3 (left)
{ ES9218P_REG_25,        0x00 },    // THD Compensation C3 (left)
{ ES9218P_REG_26,        0x62 },    // Charge Pump Soft Start Delay
{ ES9218P_REG_27,        0xD4 },    // Charge Pump Soft Start Delay
{ ES9218P_REG_28,        0xF0 },
{ ES9218P_REG_29,        0x0D },    // General Confguration
{ ES9218P_REG_30,        0x00 },    // GPIO Inversion & Automatic Clock Gearing
{ ES9218P_REG_31,        0x00 },    // GPIO Inversion & Automatic Clock Gearing
{ ES9218P_REG_32,        0x00 },    // Amplifier Configuration
//default           { ES9218P_REG_34,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_35,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_36,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_37,        0x00 },    // Programmable NCO
//default           { ES9218P_REG_40,        0x00 },    // Programmable FIR RAM Address
//default           { ES9218P_REG_41,        0x00 },    // Programmable FIR RAM Data
//default           { ES9218P_REG_42,        0x00 },    // Programmable FIR RAM Data
//default           { ES9218P_REG_43,        0x00 },    // Programmable FIR RAM Data
//will be upadated  { ES9218P_REG_44,        0x00 },    // Programmable FIR Configuration
//will be upadated  { ES9218P_REG_45,        0x00 },    // Analog Control Override
//will be upadated  { ES9218P_REG_46,        0x00 },    // dig_over_en/reserved/apdb/cp_clk_sel/reserved
//will be upadated  { ES9218P_REG_47,        0x00 },    // enfcb/encp_oe/enaux_oe/cpl_ens/cpl_enw/sel3v3_ps/ensm_ps/sel3v3_cph
//will be upadated  { ES9218P_REG_48,        0x02 },    // reserved/enhpa_out/reverved
//default           { ES9218P_REG_49,        0x62 },    // Automatic Clock Gearing Thresholds
//default           { ES9218P_REG_50,        0xc0 },    // Automatic Clock Gearing Thresholds
//default           { ES9218P_REG_51,        0x0d },    // Automatic Clock Gearing Thresholds
//will be upadated  { ES9218P_REG_53,        0x00 },    // THD Compensation C2 (Right)
//will be upadated  { ES9218P_REG_54,        0x00 },    // THD Compensation C2 (Right)
//will be upadated  { ES9218P_REG_55,        0x00 },    // THD Compensation C3 (Right)
//will be upadated  { ES9218P_REG_56,        0x00 },    // THD Compensation C3 (Right)
//default           { ES9218P_REG_60,        0x00 },    // DAC Analog Trim Control
};
#endif

static const u32 master_trim_tbl[] = {
    /*  0   db */   0x7FFFFFFF,
    /*- 0.5 db */   0x78D6FC9D,
    /*- 1   db */   0x721482BF,
    /*- 1.5 db */   0x6BB2D603,
    /*- 2   db */   0x65AC8C2E,
    /*- 2.5 db */   0x5FFC888F,
    /*- 3   db */   0x5A9DF7AA,
    /*- 3.5 db */   0x558C4B21,
    /*- 4   db */   0x50C335D3,
    /*- 4.5 db */   0x4C3EA838,
    /*- 5   db */   0x47FACCEF,
    /*- 5.5 db */   0x43F4057E,
    /*- 6   db */   0x4026E73C,
    /*- 6.5 db */   0x3C90386F,
    /*- 7   db */   0x392CED8D,
    /*- 7.5 db */   0x35FA26A9,
    /*- 8   db */   0x32F52CFE,
    /*- 8.5 db */   0x301B70A7,
    /*- 9   db */   0x2D6A866F,
    /*- 9.5 db */   0x2AE025C2,
    /*- 10  db */   0x287A26C4,
    /*- 10.5db */   0x26368073,
    /*- 11  db */   0x241346F5,
    /*- 11.5db */   0x220EA9F3,
    /*- 12  db */   0x2026F30F,
    /*- 12.5db */   0x1E5A8471,
    /*- 13  db */   0x1CA7D767,
    /*- 13.5db */   0x1B0D7B1B,
    /*- 14  db */   0x198A1357,
    /*- 14.5db */   0x181C5761,
    /*- 15  db */   0x16C310E3,
    /*- 15.5db */   0x157D1AE1,
    /*- 16  db */   0x144960C5,
    /*- 16.5db */   0x1326DD70,
    /*- 17  db */   0x12149A5F,
    /*- 17.5db */   0x1111AEDA,
    /*- 18  db */   0x101D3F2D,
    /*- 18.5db */   0xF367BED,
    /*- 19  db */   0xE5CA14C,
    /*- 19.5db */   0xD8EF66D,
    /*- 20  db */   0xCCCCCCC,
    /*- 20.5db */   0xC157FA9,
    /*- 21  db */   0xB687379,
    /*- 21.5db */   0xAC51566,
    /*- 22  db */   0xA2ADAD1,
    /*- 22.5db */   0x99940DB,
    /*- 23  db */   0x90FCBF7,
    /*- 23.5db */   0x88E0783,
    /*- 24  db */   0x8138561,
    /*- 24.5db */   0x79FDD9F,
    /*- 25  db */   0x732AE17,
    /*- 25.5db */   0x6CB9A26,
    /*- 26  db */   0x66A4A52,
    /*- 26.5db */   0x60E6C0B,
    /*- 27  db */   0x5B7B15A,
    /*- 27.5db */   0x565D0AA,
    /*- 28  db */   0x518847F,
    /*- 28.5db */   0x4CF8B43,
    /*- 29  db */   0x48AA70B,
    /*- 29.5db */   0x4499D60,
    /*- 30  db */   0x40C3713,
    /*- 30.5db */   0x3D2400B,
    /*- 31  db */   0x39B8718,
    /*- 31.5db */   0x367DDCB,
    /*- 32  db */   0x337184E,
    /*- 32.5db */   0x3090D3E,
    /*- 33  db */   0x2DD958A,
    /*- 33.5db */   0x2B48C4F,
    /*- 34  db */   0x28DCEBB,
    /*- 34.5db */   0x2693BF0,
    /*- 35  db */   0x246B4E3,
    /*- 35.5db */   0x2261C49,
    /*- 36  db */   0x207567A,
    /*- 36.5db */   0x1EA4958,
    /*- 37  db */   0x1CEDC3C,
    /*- 37.5db */   0x1B4F7E2,
    /*- 38  db */   0x19C8651,
    /*- 38.5db */   0x18572CA,
    /*- 39  db */   0x16FA9BA,
    /*- 39.5db */   0x15B18A4,
    /*- 40  db */   0x147AE14,
};

static const u8 avc_vol_tbl[] = {
    /*  0   db */   0x40,
    /*- 1   db */   0x41,
    /*- 2   db */   0x42,
    /*- 3   db */   0x43,
    /*- 4   db */   0x44,
    /*- 5   db */   0x45,
    /*- 6   db */   0x46,
    /*- 7   db */   0x47,
    /*- 8   db */   0x48,
    /*- 9   db */   0x49,
    /*- 10  db */   0x4A,
    /*- 11  db */   0x4B,
    /*- 12  db */   0x4C,
    /*- 13  db */   0x4D,
    /*- 14  db */   0x4E,
    /*- 15  db */   0x4F,
    /*- 16  db */   0X50,
    /*- 17  db */   0X51,
    /*- 18  db */   0X52,
    /*- 19  db */   0X53,
    /*- 20  db */   0X54,
    /*- 21  db */   0X55,
    /*- 22  db */   0X56,
    /*- 23  db */   0X57,
    /*- 24  db */   0X58,
    /*- 25  db */   0X59,
};

static struct es9218_priv *g_es9218_priv = NULL;
static int es9218_reg_no = 0;
static unsigned int es9218_bps = 16;
static unsigned int es9218_rate = 48000;
static int es9218_write_reg(struct i2c_client *client, int reg, u8 value);
static int es9218_read_reg(struct i2c_client *client, int reg);


#define ES9218_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |  \
        SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |   \
        SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |   \
        SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_176400) //|SNDRV_PCM_RATE_352800  TODO for dop128

#define ES9218_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE | \
        SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
        SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE | \
        SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE)

#ifdef ES9218P_SYSFS
struct es9218_regmap {
    const char *name;
    uint8_t reg;
    int writeable;
} es9218_regs[] = {
    { "00_SYSTEM_REGISTERS",                       ES9218P_REG_00, 1 },
    { "01_INPUT_SELECTION",                        ES9218P_REG_01, 1 },
    { "02_MIXING_&_AUTOMUTE_CONFIGURATION",        ES9218P_REG_02, 1 },
    { "03_ANALOG_VOLUME_CONTROL",                  ES9218P_REG_03, 1 },
    { "04_AUTOMUTE_TIME",                          ES9218P_REG_04, 1 },
    { "05_AUTOMUTE_LEVEL",                         ES9218P_REG_05, 1 },
    { "06_DoP_&_VOLUME_RAMP_RATE",                 ES9218P_REG_06, 1 },
    { "07_FILTER_BANDWIDTH_&_SYSTEM_MUTE",         ES9218P_REG_07, 1 },
    { "08_GPIO1-2_CONFIGURATION",                  ES9218P_REG_08, 1 },
    { "09_RESERVED_09",                            ES9218P_REG_09, 1 },
    { "10_MASTER_MODE_&_SYNC_CONFIGURATION",       ES9218P_REG_10, 1 },
    { "11_OVERCURRENT_PROTECTION",                 ES9218P_REG_11, 1 },
    { "12_ASRC/DPLL_BANDWIDTH",                    ES9218P_REG_12, 1 },
    { "13_THD_COMPENSATION_BYPASS",                ES9218P_REG_13, 1 },
    { "14_SOFT_START_CONFIGURATION",               ES9218P_REG_14, 1 },
    { "15_VOLUME_CONTROL_1",                       ES9218P_REG_15, 1 },
    { "16_VOLUME_CONTROL_2",                       ES9218P_REG_16, 1 },
    { "17_MASTER_TRIM_3",                          ES9218P_REG_17, 1 },
    { "18_MASTER_TRIM_2",                          ES9218P_REG_18, 1 },
    { "19_MASTER_TRIM_1",                          ES9218P_REG_19, 1 },
    { "20_MASTER_TRIM_0",                          ES9218P_REG_20, 1 },
    { "21_GPIO_INPUT_SELECTION",                   ES9218P_REG_21, 1 },
    { "22_THD_COMPENSATION_C2_2",                  ES9218P_REG_22, 1 },
    { "23_THD_COMPENSATION_C2_1",                  ES9218P_REG_23, 1 },
    { "24_THD_COMPENSATION_C3_2",                  ES9218P_REG_24, 1 },
    { "25_THD_COMPENSATION_C3_1",                  ES9218P_REG_25, 1 },
    { "26_CHARGE_PUMP_SOFT_START_DELAY",           ES9218P_REG_26, 1 },
    { "27_GENERAL_CONFIGURATION",                  ES9218P_REG_27, 1 },
    { "28_RESERVED",                               ES9218P_REG_28, 1 },
    { "29_GIO_INVERSION_&_AUTO_CLOCK_GEAR",        ES9218P_REG_29, 1 },
    { "30_CHARGE_PUMP_CLOCK_2",                    ES9218P_REG_30, 1 },
    { "31_CHARGE_PUMP_CLOCK_1",                    ES9218P_REG_31, 1 },
    { "32_AMPLIFIER_CONFIGURATION",                ES9218P_REG_32, 1 },
    { "33_RESERVED",                               ES9218P_REG_33, 1 },
    { "34_PROGRAMMABLE_NCO_4",                     ES9218P_REG_34, 1 },
    { "35_PROGRAMMABLE_NCO_3",                     ES9218P_REG_35, 1 },
    { "36_PROGRAMMABLE_NCO_2",                     ES9218P_REG_36, 1 },
    { "37_PROGRAMMABLE_NCO_1",                     ES9218P_REG_37, 1 },
    { "38_RESERVED_38",                            ES9218P_REG_38, 1 },
    { "39_RESERVED_39",                            ES9218P_REG_39, 1 },
    { "40_PROGRAMMABLE_FIR_RAM_ADDRESS",           ES9218P_REG_40, 1 },
    { "41_PROGRAMMABLE_FIR_RAM_DATA_3",            ES9218P_REG_41, 1 },
    { "42_PROGRAMMABLE_FIR_RAM_DATA_2",            ES9218P_REG_42, 1 },
    { "43_PROGRAMMABLE_FIR_RAM_DATA_1",            ES9218P_REG_43, 1 },
    { "44_PROGRAMMABLE_FIR_CONFIGURATION",         ES9218P_REG_44, 1 },
    { "45_ANALOG_CONTROL_OVERRIDE",                ES9218P_REG_45, 1 },
    { "46_DIGITAL_OVERRIDE",                       ES9218P_REG_46, 1 },
    { "47_RESERVED",                               ES9218P_REG_47, 1 },
    { "48_SEPERATE_CH_THD",                        ES9218P_REG_48, 1 },
    { "49_AUTOMATIC_CLOCK_GEARING_THRESHOLDS_3",   ES9218P_REG_49, 1 },
    { "50_AUTOMATIC_CLOCK_GEARING_THRESHOLDS_2",   ES9218P_REG_50, 1 },
    { "51_AUTOMATIC_CLOCK_GEARING_THRESHOLDS_1",   ES9218P_REG_51, 1 },
    { "52_RESERVED",                               ES9218P_REG_52, 1 },
    { "53_THD_COMPENSATION_C2_2",                  ES9218P_REG_53, 1 },
    { "54_THD_COMPENSATION_C2_1",                  ES9218P_REG_54, 1 },
    { "55_THD_COMPENSATION_C3_2",                  ES9218P_REG_55, 1 },
    { "56_THD_COMPENSATION_C3_1",                  ES9218P_REG_56, 1 },
    { "57_RESERVED",                               ES9218P_REG_57, 1 },
    { "58_RESERVED",                               ES9218P_REG_58, 1 },
    { "59_RESERVED",                               ES9218P_REG_59, 1 },
    { "60_DAC_ANALOG_TRIM_CONTROL",                ES9218P_REG_60, 1 },
    { "64_CHIP_STATUS",                            ES9218P_REG_64, 0 },
    { "65_GPIO_READBACK",                          ES9218P_REG_65, 0 },
    { "66_DPLL_NUMBER_4",                          ES9218P_REG_66, 0 },
    { "67_DPLL_NUMBER_3",                          ES9218P_REG_67, 0 },
    { "68_DPLL_NUMBER_2",                          ES9218P_REG_68, 0 },
    { "69_DPLL_NUMBER_1",                          ES9218P_REG_69, 0 },
    { "70_RESERVED",                               ES9218P_REG_70, 0 },
    { "71_RESERVED",                               ES9218P_REG_71, 0 },
    { "72_INPUT_SELECTION_AND_AUTOMUTE_STATUS",    ES9218P_REG_72, 0 },
    { "73_RAM_COEFFEICIENT_READBACK_3",            ES9218P_REG_73, 0 },
    { "74_RAM_COEFFEICIENT_READBACK_2",            ES9218P_REG_74, 0 },
    { "75_RAM_COEFFEICIENT_READBACK_1",            ES9218P_REG_75, 0 },
};

static ssize_t es9218_registers_show(struct device *dev,
                  struct device_attribute *attr, char *buf)
{
    unsigned i, n, reg_count;
    u8 read_buf;

    reg_count = sizeof(es9218_regs) / sizeof(es9218_regs[0]);
    for (i = 0, n = 0; i < reg_count; i++) {
        read_buf = es9218_read_reg(g_es9218_priv->i2c_client, es9218_regs[i].reg);
        n += scnprintf(buf + n, PAGE_SIZE - n,
                   "%-40s <#%02d>= 0x%02X\n",
                   es9218_regs[i].name, es9218_regs[i].reg,
                   read_buf);
    }

    return n;
}

static ssize_t es9218_registers_store(struct device *dev,
                   struct device_attribute *attr,
                   const char *buf, size_t count)
{
    unsigned i, reg_count, value;
    int error = 0;
    char name[45];

    if (count >= 45) {
        pr_err("%s:input too long\n", __func__);
        return -1;
    }

    if (sscanf(buf, "%40s %x", name, &value) != 2) {
        pr_err("%s:unable to parse input\n", __func__);
        return -1;
    }

    pr_info("%s: %s %0xx",__func__,name,value);
    reg_count = sizeof(es9218_regs) / sizeof(es9218_regs[0]);
    for (i = 0; i < reg_count; i++) {
        if (!strcmp(name, es9218_regs[i].name)) {
            if (es9218_regs[i].writeable) {
                error = es9218_write_reg(g_es9218_priv->i2c_client,
                                            es9218_regs[i].reg, value);
                if (error) {
                    pr_err("%s:Failed to write %s\n", __func__, name);
                    return -1;
                }
            }
            else {
                pr_err("%s:Register %s is not writeable\n", __func__, name);
                return -1;
            }

            return count;
        }
    }

    pr_err("%s:no such register %s\n", __func__, name);
    return -1;
}

static DEVICE_ATTR(registers, S_IWUSR | S_IRUGO,
        es9218_registers_show, es9218_registers_store);

static struct attribute *es9218_attrs[] = {
    &dev_attr_registers.attr,
    NULL
};

static const struct attribute_group es9218_attr_group = {
    .attrs = es9218_attrs,
};

#endif  //  End of  #ifdef  ES9218P_SYSFS

static int es9218_master_trim(struct i2c_client *client, int vol)
{
    int ret = 0;
    u32 value;

    if (vol >= sizeof(master_trim_tbl)/sizeof(master_trim_tbl[0])) {
        pr_err("%s() : Invalid vol = %d return \n", __func__, vol);
        return 0;
    }

    value = master_trim_tbl[vol];
    pr_info("%s(): MasterTrim = %08X \n", __func__, value);
/*
    if  (es9218_power_state == ESS_PS_IDLE) {
        pr_err("%s() : Invalid vol = %d return \n", __func__, vol);
        return 0;
    }
*/
    ret |= es9218_write_reg(g_es9218_priv->i2c_client , ES9218P_REG_17,
                        value&0xFF);

    ret |= es9218_write_reg(g_es9218_priv->i2c_client,  ES9218P_REG_18,
                        (value&0xFF00)>>8);

    ret |= es9218_write_reg(g_es9218_priv->i2c_client,  ES9218P_REG_19,
                        (value&0xFF0000)>>16);

    ret |= es9218_write_reg(g_es9218_priv->i2c_client,  ES9218P_REG_20,
                        (value&0xFF000000)>>24);
    return ret;
}

/*
 *  Program stage1 and stage2 filter coefficients
 */
static int es9218_sabre_cfg_custom_filter(struct sabre_custom_filter *sabre_filter)
{
    int rc, i, *coeff;
    int count_stage1;
    u8  rv, reg;

    reg = es9218_read_reg(g_es9218_priv->i2c_client, ES9218P_REG_07);
    pr_info("%s(): filter = %d, ES9218P_REG_07:%x \n", __func__, g_es9218_priv->es9218_data->custom_filter_mode, reg);
    reg &= ~0xE0;

    if (g_es9218_priv->es9218_data->custom_filter_mode > 3) {
        if(es9218_rate == 384000)
           rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_44, 0x01);
        else
           rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_44, 0x00);

        switch (g_es9218_priv->es9218_data->custom_filter_mode) {
            case 4:
            // 3b000: linear phase fast roll-off filter
                rv = reg|0x00;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            case 5:
            // 3b001: linear phase slow roll-off filter
                rv = reg|0x20;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            case 6:
            // 3b010: minimum phase fast roll-off filter #1
                rv = reg|0x40;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            case 7:
            // 3b011: minimum phase slow roll-off filter
                rv = reg|0x60;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            case 8:
            // 3b100: apodizing fast roll-off filter type 1 (default)
                rv = reg|0x80;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            case 9:
            // 3b101: apodizing fast roll-off filter type 2
                rv = reg|0xA0;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            case 10:
            // 3b110: corrected minimum phase fast roll-off filter
                rv = reg|0xC0;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            case 11:
            // 3b111: brick wall filter
                rv = reg|0xE0;
                rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
                break;
            default:
                pr_info("%s(): default = %d \n", __func__, g_es9218_priv->es9218_data->custom_filter_mode);
                break;
        }
        return rc;
    }

    count_stage1 = sizeof(sabre_filter->stage1_coeff)/sizeof(sabre_filter->stage1_coeff[0]);

    pr_info("%s: count_stage1 : %d",__func__,count_stage1);

    rv = (sabre_filter->symmetry << 2) | 0x02;        // set the write enable bit
    rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_44, rv);
    if (rc < 0) {
        pr_err("%s: rc = %d return ",__func__, rc);
        return rc;
    }

    rv = es9218_read_reg(g_es9218_priv->i2c_client, ES9218P_REG_40 - 1);
    coeff = sabre_filter->stage1_coeff;
    for (i = 0; i < count_stage1 ; i++) { // Program Stage 1 filter coefficients
        u8 value[4];
        value[0] =  i;
        value[1] = (*coeff & 0xff);
        value[2] = ((*coeff>>8) & 0xff);
        value[3] = ((*coeff>>16) & 0xff);
        i2c_smbus_write_block_data(g_es9218_priv->i2c_client, ES9218P_REG_40 - 1, 4, value);
        coeff++;
    }
    coeff = sabre_filter->stage2_coeff;
    for (i = 0; i < 16; i++) { // Program Stage 2 filter coefficients
        u8 value[4];
        value[0] =  128 + i;
        value[1] = (*coeff & 0xff);
        value[2] = ((*coeff>>8) & 0xff);
        value[3] = ((*coeff>>16) & 0xff);
        i2c_smbus_write_block_data(g_es9218_priv->i2c_client, ES9218P_REG_40 - 1, 4, value);
        coeff++;
    }
    es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_40 - 1, rv);

    rv = (sabre_filter->shape << 5); // select the custom filter roll-off shape
    rv |= 0x80;
    rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, rv);
    if (rc < 0) {
        pr_err("%s: rc = %d return ",__func__, rc);
        return rc;
    }
    rv = (sabre_filter->symmetry << 2); // disable the write enable bit
    rv |= 0x1; // Use the custom oversampling filter.
    rc = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_44, rv);
    if (rc < 0) {
        pr_err("%s: rc = %d return ",__func__, rc);
        return rc;
    }
    return 0;
}

/* es9218 volume control fuctions 
es9218_left_volume_get()
es9218_left_volume_put()
es9218_right_volume_get()
es9218_right_volume_put()
*/

static int es9218_left_volume_get(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = es9218_read_reg(g_es9218_priv->i2c_client, ES9218P_REG_15);
    pr_debug("%s(): Left Volume = -%d * 0.5 dB\n", __func__, (int)ucontrol->value.integer.value[0]);

    return 0;
}

static int es9218_left_volume_put(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
    int ret = 0;

    g_es9218_priv->es9218_data->g_left_volume = (int)ucontrol->value.integer.value[0] +
                                                (int)(es9218_init_register[ES9218P_REG_15].value);
    printk("%s(): Left Volume = -%d * 0.5 dB\n", __func__, g_es9218_priv->es9218_data->g_left_volume);

    if (g_es9218_priv->es9218_data->status == E_es9218_STATUS_AUX) {
        pr_err("%s() : mode is Bypass(%d), save volume value but don't write register\n", __func__, g_es9218_priv->es9218_data->status);
        return 0;
    }

    if (g_es9218_priv->es9218_data->impedance == IMPEDANCE_NORMAL)
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_15, g_es9218_priv->es9218_data->g_left_volume + LGE_HW_GAIN_TUNING);
    else
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_15, g_es9218_priv->es9218_data->g_left_volume);

    return ret;
}

static int es9218_right_volume_get(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = es9218_read_reg(g_es9218_priv->i2c_client, ES9218P_REG_16);
    pr_debug("%s(): Right Volume = -%d * 0.5 dB\n", __func__, (int)ucontrol->value.integer.value[0]);

    return 0;
}

static int es9218_right_volume_put(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
    int ret = 0;

    g_es9218_priv->es9218_data->g_right_volume = (int)ucontrol->value.integer.value[0] +
                                                 (int)(es9218_init_register[ES9218P_REG_16].value);
    printk("%s(): Right Volume = -%d * 0.5 dB\n", __func__, g_es9218_priv->es9218_data->g_right_volume);

    if (g_es9218_priv->es9218_data->status == E_es9218_STATUS_AUX) {
        pr_err("%s() : mode is Bypass(%d), save volume value but don't write register\n", __func__, g_es9218_priv->es9218_data->status);
        return 0;
    }

    if (g_es9218_priv->es9218_data->impedance == IMPEDANCE_NORMAL)
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_16, g_es9218_priv->es9218_data->g_right_volume + LGE_HW_GAIN_TUNING);
    else
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_16, g_es9218_priv->es9218_data->g_right_volume);

    return ret;
}

/*
 *      ES9812P's Power state / mode control signals
 *      reset_gpio;         //HIFI_RESET_N
 *      power_gpio;         //HIFI_LDO_SW
 *      hph_switch_gpio;    //HIFI_MODE2
 *      reset_gpio=H && hph_switch_gpio=L   --> HiFi mode
 *      reset_gpio=L && hph_switch_gpio=H   --> Low Power Bypass mode
 *      reset_gpio=L && hph_switch_gpio=L   --> Standby mode(Shutdown mode)
 *      reset_gpio=H && hph_switch_gpio=H   --> LowFi mode
 */

static int es9218_hifi(void)
{
	int i = 0;
	int reg_num;
	int ret = -1;
	u8  i2c_len_reg = 0;
	u8  in_cfg_reg = 0;

	printk("enter %s, line %d\n", __func__, __LINE__);
	mutex_lock(&g_es9218_priv->power_lock);
	if (E_es9218_STATUS_HIFI == g_es9218_priv->es9218_data->status) {
		printk("%s es9218 hifi has been opened\n", __func__);
		mutex_unlock(&g_es9218_priv->power_lock);
		return 0;
	}

	/*if (E_es9218_STATUS_AUX == g_es9218_priv->es9218_data->status) {
		pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_MODE_LOW].gpioctrl);
        msleep(50);
	}*/
	pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_MODE_LOW].gpioctrl);
	pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_HIFI_RESET_ON].gpioctrl);
	msleep(10);

	reg_num = sizeof(es9218_init_register)/sizeof(struct es9218_reg);
	for (i=0; i<reg_num; i++) {
		es9218_write_reg(g_es9218_priv->i2c_client,
				es9218_init_register[i].num,
				es9218_init_register[i].value);
	}

    pr_warn("%s es9218_bps=%d es9218_rate=%d\n", __func__, es9218_bps, es9218_rate);
	switch (es9218_bps) {
        case 16 :
            i2c_len_reg = 0x0;
            in_cfg_reg |= i2c_len_reg;
            ret = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_01, in_cfg_reg);
            break;
        case 24 :
            i2c_len_reg = 0x40;
            in_cfg_reg |= i2c_len_reg;
            ret = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_01, in_cfg_reg);
            break;
        case 32 :
        default :
            i2c_len_reg = 0x80;
            in_cfg_reg |= i2c_len_reg;
            ret = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_01, in_cfg_reg);

    }
    // set sample rate to ESS
    switch(es9218_rate)
    {
        case 48000:
            ret = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_06, 0x46);
            break;

        default:
            ret = es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_06, 0x43);
            break;
    }
    //improvement routing delay, it need to check amp operation is stable or not
    //msleep(260);

    //restore L/R volume
    if (g_es9218_priv->es9218_data->impedance == IMPEDANCE_NORMAL) {
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_15, g_es9218_priv->es9218_data->g_left_volume + LGE_HW_GAIN_TUNING);
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_16, g_es9218_priv->es9218_data->g_right_volume + LGE_HW_GAIN_TUNING);
    } else {
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_15, g_es9218_priv->es9218_data->g_left_volume);
        es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_16, g_es9218_priv->es9218_data->g_right_volume);
    }

    //restore digital filter mode
    es9218_master_trim(g_es9218_priv->i2c_client, g_es9218_priv->es9218_data->custom_filter_mode_volume);
    es9218_sabre_cfg_custom_filter(&es9218_sabre_custom_ft[g_es9218_priv->es9218_data->custom_filter_mode]);

    // es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_32, 0x0F); original
    switch(g_es9218_priv->es9218_data->impedance)
    {
        case IMPEDANCE_NORMAL:
            es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_32, 0x02);
            break;
        case IMPEDANCE_HIGH:
            es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_32, 0x03);
            break;
        case AUX:
            es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_32, 0x03);
            break;
        default:
            es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_32, 0x02);
            break;
    }

    g_es9218_priv->es9218_data->status = E_es9218_STATUS_HIFI;
    mutex_unlock(&g_es9218_priv->power_lock);
    printk("exit %s, line %d\n", __func__, __LINE__);
    return 0;
}

static int es9218_aux(void)
{
	int reg = 0;
	mutex_lock(&g_es9218_priv->power_lock);

	if (E_es9218_STATUS_AUX == g_es9218_priv->es9218_data->status) {
		printk("%s es9218 aux has been opened\n", __func__);
		mutex_unlock(&g_es9218_priv->power_lock);
		return 0;
	}

	reg = es9218_read_reg(g_es9218_priv->i2c_client, ES9218P_REG_07);
	reg |= 0x01;
	es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, reg);
	// HUAQIN : add system mute when turnoff hifi
	msleep(50);
	es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_32, 0x00);
	/*if (E_es9218_STATUS_HIFI != g_es9218_priv->es9218_data->status) {
	    pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_MODE_LOW].gpioctrl);
	} else {
		es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_32, 0x00);
        msleep(10);
	}*/
	pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_HIFI_RESET_OFF].gpioctrl);
	pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_MODE_HIGH].gpioctrl);
	msleep(10);

	g_es9218_priv->es9218_data->status = E_es9218_STATUS_AUX;
	mutex_unlock(&g_es9218_priv->power_lock);

	printk("enter %s, line %d\n", __func__, __LINE__);
	return 0;
}

static int es9218_standby(void)
{
	mutex_lock(&g_es9218_priv->power_lock);
	if (E_es9218_STATUS_STANDBY == g_es9218_priv->es9218_data->status) {
		printk("%s es9218 standby has been opened\n", __func__);
		mutex_unlock(&g_es9218_priv->power_lock);
		return 0;
	}

	if (E_es9218_STATUS_HIFI == g_es9218_priv->es9218_data->status) {
		es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, 0x41);
		msleep(10);
        // HUAQIN : add system mute when turnoff hifi
		es9218_write_reg(g_es9218_priv->i2c_client,	ES9218P_REG_32, 0x00);
		msleep(50);
	}
	pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_MODE_LOW].gpioctrl);
	pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_HIFI_RESET_OFF].gpioctrl);
	msleep(50);
	g_es9218_priv->es9218_data->status = E_es9218_STATUS_STANDBY;
	mutex_unlock(&g_es9218_priv->power_lock);

	printk("enter %s, line %d\n", __func__, __LINE__);

	return 0;
}

static int es9218_custom_filter_get(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = g_es9218_priv->es9218_data->custom_filter_mode;
    printk("%s(): current custom filter : %d\n", __func__, g_es9218_priv->es9218_data->custom_filter_mode);
    return 0;
}

static int es9218_custom_filter_put(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
    int reg = 0;

    switch (ucontrol->value.integer.value[0])
    {
        case SHORT:
            g_es9218_priv->es9218_data->custom_filter_mode_volume = 0;
            g_es9218_priv->es9218_data->custom_filter_mode = 6;
            // Huaqin use corrected minimum phase fast roll-off filter as default filter (3b110, 7:5)
            break;
        case SHARP:
            g_es9218_priv->es9218_data->custom_filter_mode_volume = 4;
            g_es9218_priv->es9218_data->custom_filter_mode = 4;
            break;
        case SLOW:
            g_es9218_priv->es9218_data->custom_filter_mode_volume = 2;
            g_es9218_priv->es9218_data->custom_filter_mode = 5;
            break;
        default:
            g_es9218_priv->es9218_data->custom_filter_mode_volume = 0;
            g_es9218_priv->es9218_data->custom_filter_mode = 6;
            break;
    }

    printk("%s(): select custom filter : %d -> %d\n", __func__, (int)ucontrol->value.integer.value[0], g_es9218_priv->es9218_data->custom_filter_mode);

    if (g_es9218_priv->es9218_data->status == E_es9218_STATUS_AUX) {
        pr_err("%s() : mode is Bypass(%d), save filter value but don't write register\n", __func__, g_es9218_priv->es9218_data->status);
        return 0;
    }

    reg = es9218_read_reg(g_es9218_priv->i2c_client, ES9218P_REG_07);
    reg = reg|0x01;
    es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, reg);
    mdelay(30);
    es9218_master_trim(g_es9218_priv->i2c_client, g_es9218_priv->es9218_data->custom_filter_mode_volume);
    es9218_sabre_cfg_custom_filter(&es9218_sabre_custom_ft[g_es9218_priv->es9218_data->custom_filter_mode]);
    reg = es9218_read_reg(g_es9218_priv->i2c_client, ES9218P_REG_07);
    reg &= ~0x01;
    es9218_write_reg(g_es9218_priv->i2c_client, ES9218P_REG_07, reg);
    mdelay(30);
    return 0;
}

static int es9218_get_amp_mode(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = g_es9218_priv->es9218_data->impedance;
    return 0;
}

static int es9218_set_amp_mode(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
{
    int ret = 0;
    printk("%s: ucontrol->value.integer.value[0]  = %ld\n", __func__, ucontrol->value.integer.value[0]);
    g_es9218_priv->es9218_data->impedance = ucontrol->value.integer.value[0];
    return ret;
}

int es9218_get_status(void)
{
	return g_es9218_priv->es9218_data->status;
}
EXPORT_SYMBOL(es9218_get_status);

int es9218_set_status(int mode)
{
	switch (mode)
	{
		case E_es9218_STATUS_HIFI:
		    es9218_hifi();
		    break;
		case E_es9218_STATUS_AUX:
		    es9218_aux();
		    break;
		case E_es9218_STATUS_STANDBY:
		default:
		    es9218_standby();
		    break;
	}
	return 0;
}
EXPORT_SYMBOL(es9218_set_status);

static int es9218_get_work_mode(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_es9218_priv->es9218_data->status;

	return 0;
}

static int es9218_set_work_mode(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;

	printk("%s: ucontrol->value.integer.value[0]  = %ld\n",
		__func__, ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0])
	{
		case E_es9218_STATUS_HIFI:
		    es9218_hifi();
		    break;
		case E_es9218_STATUS_AUX:
		    es9218_aux();
		    break;
		case E_es9218_STATUS_STANDBY:
		default:
		    es9218_standby();
		    break;
	}
		
	return ret;
}

static int es9218_get_i2s_length(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;

	reg_val = es9218_read_reg(g_es9218_priv->i2c_client,
				INPUT_CONFIG_SOURCE);
	reg_val = reg_val >> 6;
	ucontrol->value.integer.value[0] = reg_val;

	printk("%s: i2s_length = 0x%x\n", __func__, reg_val);

	return 0;
}

static int es9218_set_i2s_length(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;

	printk("%s: ucontrol->value.integer.value[0]  = %ld\n",
		__func__, ucontrol->value.integer.value[0]);

	reg_val = es9218_read_reg(g_es9218_priv->i2c_client,
				INPUT_CONFIG_SOURCE);

	reg_val &= ~(I2S_BIT_FORMAT_MASK);
	reg_val |=  ucontrol->value.integer.value[0] << 6;

	es9218_write_reg(g_es9218_priv->i2c_client,
				INPUT_CONFIG_SOURCE, reg_val);
	return 0;
}

static int es9218_get_clk_divider(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;

	reg_val = es9218_read_reg(g_es9218_priv->i2c_client,
				MASTER_MODE_CONTROL);
	reg_val = reg_val >> 5;
	ucontrol->value.integer.value[0] = reg_val;

	printk("%s: i2s_length = 0x%x\n", __func__, reg_val);

	return 0;
}

static int es9218_set_clk_divider(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;

	printk("%s: ucontrol->value.integer.value[0]  = %ld\n",
		__func__, ucontrol->value.integer.value[0]);

	reg_val = es9218_read_reg(g_es9218_priv->i2c_client,
				MASTER_MODE_CONTROL);

	reg_val &= ~(I2S_CLK_DIVID_MASK);
	reg_val |=  ucontrol->value.integer.value[0] << 5;

	es9218_write_reg(g_es9218_priv->i2c_client,
				MASTER_MODE_CONTROL, reg_val);
	return 0;
}

static int es9218_reg_no_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = es9218_reg_no;

	return 0;
}

static int es9218_reg_no_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es9218_reg_no = ucontrol->value.integer.value[0];

	return 0;
}

static int es9218_reg_val_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;
	
	reg_val = es9218_read_reg(g_es9218_priv->i2c_client,
				es9218_reg_no);
	ucontrol->value.integer.value[0] = reg_val;

	return 0;
}

static int es9218_reg_val_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;
	reg_val = ucontrol->value.integer.value[0];
    es9218_write_reg(g_es9218_priv->i2c_client,
				es9218_reg_no, reg_val);
	return 0;
}

static const char * const es9218_amp_mode_texts[] = {
    "IMPEDANCE_NORMAL", "IMPEDANCE_HIGH", "AUX"
};

static const char * const es9218_work_mode_texts[] = {
	"Standby", "Aux", "Hifi"
};

static const char * const es9218_i2s_length_texts[] = {
	"16bit", "24bit", "32bit", "32bit"
};

static const char * const es9218_clk_divider_texts[] = {
	"DIV4", "DIV8", "DIV16", "DIV16"
};

static const struct soc_enum es9218_amp_mode_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(es9218_amp_mode_texts),
        es9218_amp_mode_texts);

static const struct soc_enum es9218_work_mode_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(es9218_work_mode_texts),
		es9218_work_mode_texts);

static const struct soc_enum es9218_i2s_length_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(es9218_i2s_length_texts),
		es9218_i2s_length_texts);

static const struct soc_enum es9218_clk_divider_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(es9218_clk_divider_texts),
		es9218_clk_divider_texts);

static struct snd_kcontrol_new es9218_digital_ext_snd_controls[] = {
	/* commit controls */
    SOC_SINGLE_EXT("es9218 Custom Filter", SND_SOC_NOPM, 0, 12, 0,
                    es9218_custom_filter_get,
                    es9218_custom_filter_put),
    SOC_SINGLE_EXT("es9218 Left Volume", SND_SOC_NOPM, 0, 256, 0,
                    es9218_left_volume_get,
                    es9218_left_volume_put),
    SOC_SINGLE_EXT("es9218 Right Volume", SND_SOC_NOPM, 0, 256, 0,
                    es9218_right_volume_get,
                    es9218_right_volume_put),
    SOC_ENUM_EXT("es9218 Amp Mode", es9218_amp_mode_enum,
            es9218_get_amp_mode, es9218_set_amp_mode),
	SOC_ENUM_EXT("es9218 Work Mode", es9218_work_mode_enum,
			es9218_get_work_mode, es9218_set_work_mode),
	SOC_ENUM_EXT("es9218 I2s Length", es9218_i2s_length_enum,
			es9218_get_i2s_length, es9218_set_i2s_length),
	SOC_ENUM_EXT("es9218 CLK Divider", es9218_clk_divider_enum,
			es9218_get_clk_divider, es9218_set_clk_divider),
	SOC_SINGLE_EXT("es9218 Register No", SND_SOC_NOPM, 0, 80, 0,
			es9218_reg_no_get, es9218_reg_no_put),
	SOC_SINGLE_EXT("es9218 Register Value", SND_SOC_NOPM, 0, 255, 0,
			es9218_reg_val_get, es9218_reg_val_put)
};


static int es9218_read_reg(struct i2c_client *client, int reg)
{
    int ret;

    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        pr_err("%s: err %d\n", __func__, ret);
    }

    return ret;
}

static int es9218_write_reg(struct i2c_client *client, int reg, u8 value)
{
    int ret, i;

    //pr_notice("%s(): %03d=0x%x\n", __func__, reg, value);
    for (i = 0; i < 3; i++) {
        ret = i2c_smbus_write_byte_data(client, reg, value);
        if (ret < 0) {
            pr_err("%s: err %d,and try again\n", __func__, ret);
            mdelay(50);
        }
        else {
            break;
        }
    }

    if (ret < 0) {
        pr_err("%s: err %d\n", __func__, ret);
    }

    return ret;
}
static int es9218_populate_get_pdata(struct device *dev,
        struct es9218_data *pdata)
{
    int i = 0;
    pinctrl_hifi = devm_pinctrl_get(dev);

    if (IS_ERR(pinctrl_hifi)) {
        pr_err("%s: Cannot find pinctrl_hifi_reset!\n", __func__);
    }

    for(i = 0; i < ARRAY_SIZE(hifi_gpios); i++) {
        hifi_gpios[i].gpioctrl = pinctrl_lookup_state(pinctrl_hifi, hifi_gpios[i].name);
        if(IS_ERR(hifi_gpios[i].gpioctrl)) {
            hifi_gpios[i].gpioctrl = NULL;
            pr_err("%s: pinctrl_lookup_state %s fail\n", __func__, hifi_gpios[i].name);
        }
    }

    return 0;
}

int es9218_pcm_hw_params_dl1(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
    int ret = -1;
	pr_warn("%s(): enter\n", __func__);

    es9218_bps  = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min;
    es9218_rate = params_rate(params);

    // set bit width to ESS
    /*  Bit width(depth) Per Sec. Setting     */
    pr_warn("%s es9218_bps=%d es9218_rate=%d\n", __func__, es9218_bps, es9218_rate);
    pr_warn("%s(): exit, ret=%d\n", __func__, ret);
    return ret;
}
EXPORT_SYMBOL(es9218_pcm_hw_params_dl1);


static int es9218_pcm_hw_params(struct snd_pcm_substream *substream,
        struct snd_pcm_hw_params *params,
        struct snd_soc_dai *codec_dai)
{
    return 0;
}

static int es9218_mute(struct snd_soc_dai *dai, int mute)
{
    return 0;
}

static int es9218_set_dai_sysclk(struct snd_soc_dai *codec_dai,
        int clk_id, unsigned int freq, int dir)
{
    return 0;
}


static int es9218_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
    return 0;
}

static int es9218_startup(struct snd_pcm_substream *substream,
               struct snd_soc_dai *dai)
{
    return 0;
}

static void es9218_shutdown(struct snd_pcm_substream *substream,
               struct snd_soc_dai *dai)
{
	return;
}

static int es9218_hw_free(struct snd_pcm_substream *substream,
               struct snd_soc_dai *dai)
{
    return 0;
}


static const struct snd_soc_dai_ops es9218_dai_ops = {
    .hw_params      = es9218_pcm_hw_params,  //soc_dai_hw_params
    .digital_mute   = es9218_mute,
    .set_fmt        = es9218_set_dai_fmt,
    .set_sysclk     = es9218_set_dai_sysclk,
    .startup        = es9218_startup,
    .shutdown       = es9218_shutdown,
    .hw_free        = es9218_hw_free,
};

static struct snd_soc_dai_driver es9218_dai[] = {
    {
        .name   = "es9218-hifi",
        .playback = {
            .stream_name    = "Playback",
            .channels_min   = 2,
            .channels_max   = 2,
            .rates          = ES9218_RATES,
            .formats        = ES9218_FORMATS,
        },
        .capture = {
            .stream_name    = "Capture",
            .channels_min   = 2,
            .channels_max   = 2,
            .rates          = ES9218_RATES,
            .formats        = ES9218_FORMATS,
        },
        .ops = &es9218_dai_ops,
    },
};

static  int es9218_codec_probe(struct snd_soc_codec *codec)
{
    struct es9218_priv *priv = snd_soc_codec_get_drvdata(codec);

    pr_warn("%s(): entry\n", __func__);

    if (priv)
        priv->codec = codec;
    else
        pr_err("%s(): fail !!!!!!!!!!\n", __func__);

    codec->control_data = snd_soc_codec_get_drvdata(codec);

    pr_warn("%s(): exit \n", __func__);
    return 0;
}

static int  es9218_codec_remove(struct snd_soc_codec *codec)
{
    return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_es9218 = {
    .probe          = es9218_codec_probe,
    .remove         = es9218_codec_remove,
#ifdef KERNEL_4_9_XX
    .component_driver = {
#endif
    .controls       = es9218_digital_ext_snd_controls,
    .num_controls   = ARRAY_SIZE(es9218_digital_ext_snd_controls),
#ifdef KERNEL_4_9_XX
    },
#endif

};


static int es9218_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    struct es9218_priv  *priv;
    struct es9218_data  *pdata;
    int ret = 0;

    pr_warn("%s: enter.\n", __func__);

#if defined(CONFIG_LGE_HIFI_HWINFO)
    if (!lge_get_hifi_hwinfo()) {
        pr_err("%s: hifi not support for this pcb\n", __func__);
        return -ENODEV;
    }
#endif

    if (!i2c_check_functionality(client->adapter,
                I2C_FUNC_SMBUS_BYTE_DATA)) {
        pr_err("%s: no support for i2c read/write byte data\n", __func__);
        return -EIO;
    }

    if (client->dev.of_node) {
        pdata = devm_kzalloc(&client->dev,
                sizeof(struct es9218_data), GFP_KERNEL);
        if (!pdata) {
            pr_err("Failed to allocate memory\n");
            return -ENOMEM;
        }

        ret = es9218_populate_get_pdata(&client->dev, pdata);
        if (ret) {
            pr_err("Parsing DT failed(%d)", ret);
            return ret;
        }
    } else {
        pdata = client->dev.platform_data;
    }

    if (!pdata) {
        pr_err("%s: no platform data\n", __func__);
        return -EINVAL;
    }

    priv = devm_kzalloc(&client->dev, sizeof(struct es9218_priv),
            GFP_KERNEL);
    if (priv == NULL) {
        return -ENOMEM;
    }

	//set 3.3V PMIC for HiFi DAC
	hifi_vcn33_ldo = regulator_get(NULL, "vcn33_bt");
	if (IS_ERR(hifi_vcn33_ldo)){
		pr_err("Regulator_get hifi_vcn33_ldo fail\n");
	}else{
		ret = regulator_set_voltage(hifi_vcn33_ldo, 3300 * 1000, 3300 * 1000);
		if (ret) {
			pr_err("%s: Failed to set hifi vcn33 voltage,ret=%d\n", __func__, ret);
		}
		ret = regulator_enable(hifi_vcn33_ldo);
		if (ret) {
			pr_err("%s: Failed to enable hifi vcn33 voltage,ret=%d\n", __func__, ret);
		}
	}
    priv->i2c_client = client;
    priv->es9218_data = pdata;
    i2c_set_clientdata(client, priv);

/*  default mode : standby
    pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_MODE_LOW].gpioctrl);
    pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_HIFI_RESET_OFF].gpioctrl);
    priv->es9218_data->status = E_es9218_STATUS_STANDBY;
*/
// default mode : bypass
    pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_HIFI_RESET_OFF].gpioctrl);
    pinctrl_select_state(pinctrl_hifi, hifi_gpios[PINCTRL_MODE_HIGH].gpioctrl);
    priv->es9218_data->status = E_es9218_STATUS_AUX;

// default impedance status from init: normal impedance
    priv->es9218_data->impedance = IMPEDANCE_NORMAL;

    g_es9218_priv = priv;

    mutex_init(&g_es9218_priv->power_lock);
    ret = snd_soc_register_codec(&client->dev,
                      &soc_codec_dev_es9218,
                      es9218_dai, ARRAY_SIZE(es9218_dai));

#ifdef ES9218P_SYSFS
    ret = sysfs_create_group(&client->dev.kobj, &es9218_attr_group);
#endif

    pr_notice("%s: snd_soc_register_codec ret = %d\n",__func__, ret);
    return ret;
}

static int es9218_remove(struct i2c_client *client)
{
#ifdef USE_CONTROL_EXTERNAL_LDO_FOR_DVDD
    struct es9218_data  *pdata;
    pdata = (struct es9218_data*)i2c_get_clientdata(client);    //pdata = (struct es9218_data*)client->dev.driver_data;
    if( pdata->vreg_dvdd != NULL )
        regulator_put(pdata->vreg_dvdd);
#endif
    snd_soc_unregister_codec(&client->dev);
    mutex_destroy(&g_es9218_priv->power_lock);
    return 0;
}

static struct of_device_id es9218_match_table[] = {
    { .compatible = "dac,es9218-codec", },
    {}
};
MODULE_DEVICE_TABLE(of, es9218_match_table);

static const struct i2c_device_id es9218_id[] = {
    { "es9218-codec", 0 },
    { },
};

MODULE_DEVICE_TABLE(i2c, es9218_id);

static struct i2c_driver es9218_i2c_driver = {
    .driver = {
        .name           = "es9218-codec",
        .owner          = THIS_MODULE,
        .of_match_table = es9218_match_table,
    },
    .probe      = es9218_probe,
    .remove     = es9218_remove,
    .id_table   = es9218_id,
};



static int __init es9218_init(void)
{
    pr_warn("%s()\n", __func__);
    if (i2c_add_driver(&es9218_i2c_driver) != 0) {
        pr_err("[es9218_init] failed to register es9218_i2c_driver.\n");
    }
    else {
        pr_warn("[es9218_init] success to register es9218_i2c_driver.\n");
    }
    return 0;
}

static void __exit es9218_exit(void)
{
    i2c_del_driver(&es9218_i2c_driver);
}

module_init(es9218_init);
module_exit(es9218_exit);

MODULE_DESCRIPTION("ASoC ES9218 driver");
MODULE_AUTHOR("ESS-LINshaodong");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:es9218-codec");

#ifdef BUILD_LK
#include <sys/types.h>
#else
#include <linux/string.h>
#endif
#include "lcm_drv.h"
#include "disp_dts_gpio.h"
#include "disp_recovery.h"
#include "sm5109.h"

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------
#ifdef BUILD_LK
    #define LCM_PRINT printf
#else
    #define LCM_PRINT printk
#endif

#ifndef FALSE
  #define FALSE   0
#endif

#ifndef TRUE
  #define TRUE    1
#endif

#define REGFLAG_DELAY       0xFFFC
#define REGFLAG_UDELAY  0xFFFB
#define REGFLAG_END_OF_TABLE    0xFFFD
#define REGFLAG_RESET_LOW   0xFFFE
#define REGFLAG_RESET_HIGH  0xFFFF

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------
#define FRAME_WIDTH               (720)
#define FRAME_HEIGHT              (1560)
#define LCM_DENSITY               (320)

#define PHYSICAL_WIDTH            (65)
#define PHYSICAL_HEIGHT           (130)

#define LCM_MODULE_NAME    "ILI9881C_LCE"

#ifdef BUILD_LK
#define LCM_ID                    (0x9881)
#endif
#define DSV_VOLTAGE             5800

// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------
#ifdef BUILD_LK
static LCM_UTIL_FUNCS lcm_util;
#else
static struct LCM_UTIL_FUNCS lcm_util;
#endif

#define SET_RESET_PIN(v)                                          lcm_util.set_reset_pin((v))
#define UDELAY(n)                                                 lcm_util.udelay(n)
#define MDELAY(n)                                                 lcm_util.mdelay(n)
#define dsi_set_cmdq(pdata, queue_size, force_update)             lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)          lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq_V3(para_tbl,size,force_update)               lcm_util.dsi_set_cmdq_V3(para_tbl,size,force_update)
#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)   lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define read_reg_v2(cmd, buffer, buffer_size)                     lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

#ifdef CONFIG_HQ_SET_LCD_BIAS
#define SET_LCD_BIAS_EN(en, seq, value)                           lcm_util.set_lcd_bias_en(en, seq, value)
#endif

extern void disp_set_gpio_ctrl(unsigned int ctrl_pin, unsigned int en);
extern int sm5109_set_vspn_value(unsigned int value);

struct LCM_setting_table {
    unsigned int cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static void push_table(void *cmdq, struct LCM_setting_table *table,
    unsigned int count, unsigned char force_update)
{
    unsigned int i;
    unsigned int cmd;

    for (i = 0; i < count; i++) {
        cmd = table[i].cmd;

        switch (cmd) {

        case REGFLAG_DELAY:
            if (table[i].count <= 10)
                UDELAY(1000 * table[i].count);
            else
                MDELAY(table[i].count);
            break;

        case REGFLAG_UDELAY:
            UDELAY(table[i].count);
            break;

        case REGFLAG_END_OF_TABLE:
            break;

        default:
            dsi_set_cmdq_V22(cmdq, cmd,
                table[i].count,
                table[i].para_list,
                force_update);
        }
    }
}

static struct LCM_setting_table lcm_suspend_setting[] = {
	{REGFLAG_DELAY, 1, {} },
	{0xFF, 0x03, {0x98,0x81,0x00}},
    {0x28, 0, {} },
    {REGFLAG_DELAY, 20, {} },
    {0x10, 0, {} },
    {REGFLAG_DELAY, 120, {} },
};

static struct LCM_setting_table init_setting_vdo[] = {
    {0xFF,0x03,{0x98,0x81,0x03}},
    {0x01,0x01,{0x00}},
    {0x02,0x01,{0x00}},
    {0x03,0x01,{0x73}},
    {0x04,0x01,{0x00}},
    {0x05,0x01,{0x00}},
    {0x06,0x01,{0x08}},
    {0x07,0x01,{0x00}},
    {0x08,0x01,{0x00}},
    {0x09,0x01,{0x00}},
    {0x0a,0x01,{0x00}},
    {0x0b,0x01,{0x00}},
    {0x0c,0x01,{0x15}},
    {0x0d,0x01,{0x00}},
    {0x0e,0x01,{0x00}},
    {0x0f,0x01,{0x00}},
    {0x10,0x01,{0x00}},
    {0x11,0x01,{0x00}},
    {0x12,0x01,{0x00}},
    {0x13,0x01,{0x15}},
    {0x14,0x01,{0x15}},
    {0x15,0x01,{0x00}},
    {0x16,0x01,{0x00}},
    {0x17,0x01,{0x00}},
    {0x18,0x01,{0x00}},
    {0x19,0x01,{0x00}},
    {0x1a,0x01,{0x00}},
    {0x1b,0x01,{0x00}},
    {0x1c,0x01,{0x00}},
    {0x1d,0x01,{0x00}},
    {0x1e,0x01,{0x48}},
    {0x1f,0x01,{0x40}},
    {0x20,0x01,{0x06}},
    {0x21,0x01,{0x01}},
    {0x22,0x01,{0x06}},
    {0x23,0x01,{0x01}},
    {0x24,0x01,{0x00}},
    {0x25,0x01,{0x00}},
    {0x26,0x01,{0x00}},
    {0x27,0x01,{0x00}},
    {0x28,0x01,{0x73}},
    {0x29,0x01,{0x33}},
    {0x2a,0x01,{0x00}},
    {0x2b,0x01,{0x00}},
    {0x2c,0x01,{0x0B}},
    {0x2d,0x01,{0x00}},
    {0x2e,0x01,{0x02}},
    {0x2f,0x01,{0x00}},
    {0x30,0x01,{0x00}},
    {0x31,0x01,{0x00}},
    {0x32,0x01,{0x12}},
    {0x33,0x01,{0x00}},
    {0x34,0x01,{0x04}},
    {0x35,0x01,{0x0a}},
    {0x36,0x01,{0x03}},
    {0x37,0x01,{0x08}},
    {0x38,0x01,{0x00}},
    {0x39,0x01,{0x00}},
    {0x3a,0x01,{0x00}},
    {0x3b,0x01,{0x00}},
    {0x3c,0x01,{0x00}},
    {0x3d,0x01,{0x00}},
    {0x3e,0x01,{0x00}},
    {0x3f,0x01,{0x00}},
    {0x40,0x01,{0x00}},
    {0x41,0x01,{0x00}},
    {0x42,0x01,{0x00}},
    {0x43,0x01,{0x08}},
    {0x44,0x01,{0x00}},
    {0x50,0x01,{0x01}},
    {0x51,0x01,{0x23}},
    {0x52,0x01,{0x45}},
    {0x53,0x01,{0x67}},
    {0x54,0x01,{0x89}},
    {0x55,0x01,{0xab}},
    {0x56,0x01,{0x01}},
    {0x57,0x01,{0x23}},
    {0x58,0x01,{0x45}},
    {0x59,0x01,{0x67}},
    {0x5a,0x01,{0x89}},
    {0x5b,0x01,{0xab}},
    {0x5c,0x01,{0xcd}},
    {0x5d,0x01,{0xef}},
    {0x5e,0x01,{0x00}},
    {0x5f,0x01,{0x06}},
    {0x60,0x01,{0x02}},
    {0x61,0x01,{0x02}},
    {0x62,0x01,{0x02}},
    {0x63,0x01,{0x0c}},
    {0x64,0x01,{0x0d}},
    {0x65,0x01,{0x0e}},
    {0x66,0x01,{0x0f}},
    {0x67,0x01,{0x00}},
    {0x68,0x01,{0x17}},
    {0x69,0x01,{0x07}},
    {0x6a,0x01,{0x02}},
    {0x6b,0x01,{0x02}},
    {0x6c,0x01,{0x02}},
    {0x6d,0x01,{0x02}},
    {0x6e,0x01,{0x02}},
    {0x6f,0x01,{0x02}},
    {0x70,0x01,{0x02}},
    {0x71,0x01,{0x02}},
    {0x72,0x01,{0x02}},
    {0x73,0x01,{0x16}},
    {0x74,0x01,{0x01}},
    {0x75,0x01,{0x06}},
    {0x76,0x01,{0x02}},
    {0x77,0x01,{0x02}},
    {0x78,0x01,{0x02}},
    {0x79,0x01,{0x0c}},
    {0x7a,0x01,{0x0d}},
    {0x7b,0x01,{0x0e}},
    {0x7c,0x01,{0x0f}},
    {0x7d,0x01,{0x00}},
    {0x7e,0x01,{0x17}},
    {0x7f,0x01,{0x07}},
    {0x80,0x01,{0x02}},
    {0x81,0x01,{0x02}},
    {0x82,0x01,{0x02}},
    {0x83,0x01,{0x02}},
    {0x84,0x01,{0x02}},
    {0x85,0x01,{0x02}},
    {0x86,0x01,{0x02}},
    {0x87,0x01,{0x02}},
    {0x88,0x01,{0x02}},
    {0x89,0x01,{0x16}},
    {0x8A,0x01,{0x01}},

    {0xFF,0x03,{0x98,0x81,0x04}},
    {0x6C,0x01,{0x15}},
    {0x6E,0x01,{0x1A}},
    {0x6F,0x01,{0xA5}},
    {0x8D,0x01,{0x2A}},
    {0x87,0x01,{0xBA}},
    {0xB2,0x01,{0xD1}},

    {0x3B,0x01,{0x98}},
    {0x35,0x01,{0x17}},
    {0x3A,0x01,{0x92}},
    {0xB5,0x01,{0x27}},
    {0x31,0x01,{0x75}},
    {0x30,0x01,{0x03}},
    {0x33,0x01,{0x14}},
    {0x38,0x01,{0x01}},
    {0x39,0x01,{0x00}},


    {0xFF,0x03,{0x98,0x81,0x01}},
    {0x22,0x01,{0x0A}},
    {0x2E,0x01,{0x0E}},
    {0x2F,0x01,{0x80}},
    {0x31,0x01,{0x00}},

    {0x50,0x01,{0xD9}},
    {0x51,0x01,{0xD3}},
    {0x60,0x01,{0x0A}},
    {0x61,0x01,{0x00}},
    {0x62,0x01,{0x20}},
    {0x63,0x01,{0x10}},

    {0xA0,0x01,{0x08}},
    {0xA1,0x01,{0x15}},
    {0xA2,0x01,{0x20}},
    {0xA3,0x01,{0x11}},
    {0xA4,0x01,{0x13}},
    {0xA5,0x01,{0x26}},
    {0xA6,0x01,{0x1A}},
    {0xA7,0x01,{0x1C}},
    {0xA8,0x01,{0x76}},
    {0xA9,0x01,{0x1B}},
    {0xAA,0x01,{0x27}},
    {0xAB,0x01,{0x70}},
    {0xAC,0x01,{0x1F}},
    {0xAD,0x01,{0x1E}},
    {0xAE,0x01,{0x53}},
    {0xAF,0x01,{0x26}},
    {0xB0,0x01,{0x2B}},
    {0xB1,0x01,{0x51}},
    {0xB2,0x01,{0x61}},
    {0xB3,0x01,{0x39}},

    {0xC0,0x01,{0x08}},
    {0xC1,0x01,{0x15}},
    {0xC2,0x01,{0x21}},
    {0xC3,0x01,{0x11}},
    {0xC4,0x01,{0x13}},
    {0xC5,0x01,{0x25}},
    {0xC6,0x01,{0x1A}},
    {0xC7,0x01,{0x1D}},
    {0xC8,0x01,{0x77}},
    {0xC9,0x01,{0x1B}},
    {0xCA,0x01,{0x27}},
    {0xCB,0x01,{0x70}},
    {0xCC,0x01,{0x1E}},
    {0xCD,0x01,{0x1E}},
    {0xCE,0x01,{0x52}},
    {0xCF,0x01,{0x26}},
    {0xD0,0x01,{0x2B}},
    {0xD1,0x01,{0x52}},
    {0xD2,0x01,{0x61}},
    {0xD3,0x01,{0x39}},

    {0xFF,0x03,{0x98,0x81,0x00}},
    //Open TE
    {0x35,0x01,{0x00}},
    //Sleep Out
    {0x11,0x01,{0x00}},
    {REGFLAG_DELAY,120,{}},
    //Display On
    {0x29,0x01,{0x00}},
    {REGFLAG_DELAY,20,{}},

    {0xFF, 0x03, {0x98, 0x81, 0x09}}, //Add for solving ESD soft failure.[panzhibin@huaqin.com]

    //End of Table
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------
#ifdef BUILD_LK
static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}
#else
static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}
#endif

#ifdef BUILD_LK
static void lcm_get_params(LCM_PARAMS *params)
{
    memset(params, 0, sizeof(LCM_PARAMS));
#else
static void lcm_get_params(struct LCM_PARAMS *params)
{
    memset(params, 0, sizeof(struct LCM_PARAMS));
#endif

    //The following defined the fomat for data coming from LCD engine.
    params->dsi.mode                       = SYNC_PULSE_VDO_MODE;   //SYNC_PULSE_VDO_MODE;//BURST_VDO_MODE;
    params->dsi.data_format.format         = LCM_DSI_FORMAT_RGB888;
    params->dsi.PS                         = LCM_PACKED_PS_24BIT_RGB888;
    params->type                           = LCM_TYPE_DSI;
    params->width                          = FRAME_WIDTH;
    params->height                         = FRAME_HEIGHT;

    // DSI
    params->dsi.LANE_NUM                   = LCM_FOUR_LANE;
    params->dsi.vertical_sync_active                       = 8;
    params->dsi.vertical_backporch                         = 20;
    params->dsi.vertical_frontporch                        = 20;
    params->dsi.vertical_active_line       = FRAME_HEIGHT;
    params->dsi.horizontal_sync_active                     = 16;
    params->dsi.horizontal_backporch                       = 92;
    params->dsi.horizontal_frontporch                      = 92;
    params->dsi.horizontal_active_pixel    = FRAME_WIDTH;
    params->dsi.PLL_CLOCK                                  = 284;

    params->physical_width                 = PHYSICAL_WIDTH;
    params->physical_height                = PHYSICAL_HEIGHT;
#ifndef BUILD_LK
    params->density                        = LCM_DENSITY;
#endif

    // Non-continuous clock
    params->dsi.noncont_clock = TRUE;
    //params->dsi.noncont_clock_period = 2; // Unit : frames
    params->dsi.clk_lp_per_line_enable              = 1;

    params->dsi.ssc_disable = 1;//default enable SSC
    //params->dsi.ssc_range = 5;

    params->dsi.esd_check_enable                    = 1;
    params->dsi.customization_esd_check_enable      = 0;

    params->dsi.lcm_esd_check_table[0].cmd          = 0x09;
    params->dsi.lcm_esd_check_table[0].count        = 3;
    params->dsi.lcm_esd_check_table[0].para_list[0] = 0x80;
    params->dsi.lcm_esd_check_table[0].para_list[1] = 0x03;
    params->dsi.lcm_esd_check_table[0].para_list[2] = 0x06;
    params->lcm_seq_suspend = LCM_MIPI_VIDEO_FRAME_DONE_SUSPEND;
    params->lcm_seq_shutdown = LCM_SHUTDOWN_AFTER_DSI_OFF;
    params->lcm_seq_resume = LCM_MIPI_READY_VIDEO_FRAME_RESUME;
    params->lcm_seq_power_on = NOT_USE_RESUME;
}


static void lcm_dsv_ctrl(unsigned int enable, unsigned int delay, unsigned int value)
{
    if(enable==1){
        disp_set_gpio_ctrl(DSV_VPOS_EN, 1);
        sm5109_set_vspn_value(value);

        if(delay)
            MDELAY(delay);

        disp_set_gpio_ctrl(DSV_VNEG_EN, 1);
    } else {
        disp_set_gpio_ctrl(DSV_VNEG_EN, 0);

        if(delay)
            MDELAY(delay);

        disp_set_gpio_ctrl(DSV_VPOS_EN, 0);
    }

    LCM_PRINT("[LCD] %s : %s\n",__func__,(enable)? "ON":"OFF");
}

static void lcd_reset_pin(unsigned int mode)
{
    disp_set_gpio_ctrl(LCM_RST, mode);

    LCM_PRINT("[LCD] LCD Reset %s \n",(mode)? "High":"Low");
}

static void lcm_reset(unsigned int ms1, unsigned int ms2, unsigned int ms3)
{
    if (ms1) {
        //pinctrl_select_state(lcd_bias_pctrl, lcd_rst_pins1);
        //SET_RESET_PIN(1);
	lcd_reset_pin(1);
        MDELAY(ms1);
    }

    if (ms2) {
        //pinctrl_select_state(lcd_bias_pctrl, lcd_rst_pins0);
        //SET_RESET_PIN(0);
		lcd_reset_pin(0);
        MDELAY(ms2);
    }

    if (ms3) {
        //pinctrl_select_state(lcd_bias_pctrl, lcd_rst_pins1);
        //SET_RESET_PIN(1);
		lcd_reset_pin(1);
        MDELAY(ms3);
    }
    LCM_PRINT("[LCD] %s\n",__func__);
}

static void lcm_init(void)
{
#ifdef CONFIG_HQ_SET_LCD_BIAS
    SET_LCD_BIAS_EN(ON, VSP_FIRST_VSN_AFTER, 5800);  //open lcd bias
    MDELAY(5);
#endif
    lcm_dsv_ctrl(1, 1, 5800);
    MDELAY(5);
    lcm_reset(5, 10, 11);

    push_table(NULL, init_setting_vdo, ARRAY_SIZE(init_setting_vdo), 1);

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void lcm_suspend(void)
{
    push_table(NULL, lcm_suspend_setting, ARRAY_SIZE(lcm_suspend_setting), 1);

    lcd_reset_pin(0);
    MDELAY(5);
	lcm_dsv_ctrl(0, 1, 5800);
	MDELAY(5);
	LCM_PRINT("[LCD] %s\n",__func__);
#ifdef CONFIG_HQ_SET_LCD_BIAS
    LCM_PRINT("%s---- lk lcm_suspend\n", LCM_MODULE_NAME);
    SET_LCD_BIAS_EN(OFF, VSN_FIRST_VSP_AFTER, 5800);  //close lcd bias
#endif
}

static void lcm_resume(void)
{
    LCM_PRINT("%s Init Start!\n", LCM_MODULE_NAME);

    lcm_init();

    LCM_PRINT("%s Init End!\n", LCM_MODULE_NAME);
}

#ifdef BUILD_LK
static unsigned int lcm_compare_id(void)
{
    unsigned int id = 0;
    unsigned char buffer[4] = {0};
    unsigned int array[16] = {0};

#ifdef CONFIG_HQ_SET_LCD_BIAS
     SET_LCD_BIAS_EN(ON, VSP_FIRST_VSN_AFTER, 5800);  //open lcd bias
     MDELAY(15);
#endif

    lcm_reset(10, 10, 120);

    array[0] = 0x00023700;  /* read id return two byte,version and id */
    dsi_set_cmdq(array, 1, 1);
    MDELAY(5);

    read_reg_v2(0x04, buffer, 2);
    id = (buffer[0]<<8) | buffer[1] ;

    LCM_PRINT("[%s]:%s, id:0x%x\n", LCM_MODULE_NAME, __func__, id);

    return (LCM_ID == id) ? TRUE : FALSE;
}
#endif

#ifdef BUILD_LK
LCM_DRIVER ili9881c_hdplus_dsi_vdo_lce_lcm_drv =
#else
struct LCM_DRIVER ili9881c_hdplus_dsi_vdo_lce_lcm_drv =
#endif
{
    .name           = "LCE-ILI9881C",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params     = lcm_get_params,
    .init           = lcm_init,
    .suspend        = lcm_suspend,
    .resume         = lcm_resume,
#ifdef BUILD_LK
    .compare_id     = lcm_compare_id,
#endif
};

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

#define LCM_MODULE_NAME    "ST7703_LCE"

#ifdef BUILD_LK
#define LCM_ID                    (0x7703)
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
extern int sm5109_set_vspn(void);

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
    {REGFLAG_DELAY, 5, {} },
    {0x28, 0, {} },
    {REGFLAG_DELAY, 50, {} },
    {0x10, 0, {} },
    {REGFLAG_DELAY, 120, {} },
};

static struct LCM_setting_table init_setting_vdo[] = {
    {0xB9,3,{0xF1,0x12,0x83}},
    {0xBA,27,{0x33,0x81,0x05,0xF9,0x0E,0x0E,0x20,0x00,0x00,0x00,
		      0x00,0x00,0x00,0x00,0x44,0x25,0x00,0x91,0x0A,0x00,
		      0x00,0x00,0x4F,0x01,0x03,0x02,0x37}},
    {0xB8,4,{0x76,0x22,0x20,0x03}},
    {0xB3,10,{0x0C,0x10,0x28,0x28,0x03,0xFF,0x00,0x00,0x00,0x00}},
    {0xC0,9,{0x73,0x73,0x50,0x50,0xA0,0x00,0x08,0x70,0x00}},
    {0xBC,1,{0x4F}},
    {0xCC,1,{0x0B}},
    {0xB4,1,{0x80}},
    {0xB1,1,{0x86}},
    {0xB2,3,{0x0E,0x12,0xF0}},
    {0xE3,14,{0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,
              0x28,0x80,0xC0,0x17}},
    {0xC1,12,{0x27,0x00,0x35,0x35,0x99,0xE1,0xFF,0xFF,0xEE,0xEE,0x77,0x77}},
	{0xC6,6,{0x01,0x00,0xFF,0xDF,0x00,0x20}},
    {0xB5,2,{0x0F,0x0F}},
    {0xB6,2,{0x9D,0x9D}},
    {0xBF,3,{0x02,0x11,0x00}},
	{0xE9,63,{0xC2,0x10,0x08,0x00,0x00,0xA0,0x51,0x12,0x31,0x23,
			  0x47,0x86,0xA0,0x51,0x47,0x08,0x00,0x04,0x01,0x00,
			  0x00,0x00,0x00,0x04,0x01,0x00,0x00,0x00,0x18,0x88,
			  0x13,0x57,0x88,0x38,0x88,0x88,0x88,0x88,0xFF,0x08,
			  0x88,0x02,0x46,0x88,0x28,0x88,0x88,0x88,0x88,0xFF,
			  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			  0x00,0x00,0x00}},
	{0xEA,61,{0x00,0x1A,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
			  0x00,0x00,0x28,0x88,0x64,0x20,0xF8,0x08,0x88,0x88,
			  0x88,0x88,0xF8,0x38,0x88,0x75,0x31,0xF8,0x18,0x88,
			  0x88,0x88,0x88,0xF8,0x23,0x00,0x00,0x00,0x87,0x00,
			  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			  0x00}},
	{0xE0,34,{0x00,0x04,0x06,0x27,0x2B,0x3F,0x35,0x34,0x06,0x0B,
	          0x0C,0x10,0x11,0x10,0x12,0x12,0x18,
			  0x00,0x04,0x06,0x27,0x2B,0x3F,0x35,0x34,0x06,0x0B,
			  0x0C,0x10,0x11,0x10,0x12,0x12,0x18}},

    {0xC7,1,{0x10}},
    {0xEF,3,{0xFF,0xFF,0x01}},

    //Sleep Out
    {0x11,1,{0x00}},
    {REGFLAG_DELAY,120,{}},

    //Display On
    {0x29,1,{0x00}},
    {REGFLAG_DELAY,30,{}},

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




    params->type = LCM_TYPE_DSI;




    params->width = FRAME_WIDTH;

    params->height = FRAME_HEIGHT;




    /* physical size */

    params->physical_width = PHYSICAL_WIDTH;

    params->physical_height = PHYSICAL_HEIGHT;




    params->dsi.mode = SYNC_PULSE_VDO_MODE;

    /* enable tearing-free */

    params->dbi.te_mode = LCM_DBI_TE_MODE_DISABLED;

    params->dbi.te_edge_polarity = LCM_POLARITY_RISING;




    /* DSI */

    /* Command mode setting */

    params->dsi.LANE_NUM = LCM_FOUR_LANE;

    /* The following defined the fomat for data coming from LCD engine. */

    params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;

    params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;

    params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;

    params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;




    /* Highly depends on LCD driver capability. */

    params->dsi.packet_size = 256;




    params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

    params->dsi.cont_clock = 0;




    params->dsi.vertical_sync_active = 3;

    params->dsi.vertical_backporch = 10;

    params->dsi.vertical_frontporch = 16;

    params->dsi.vertical_active_line = FRAME_HEIGHT;




    params->dsi.horizontal_sync_active = 10;

    params->dsi.horizontal_backporch = 40;

    params->dsi.horizontal_frontporch = 40;

    params->dsi.horizontal_active_pixel = FRAME_WIDTH;




    params->dsi.HS_TRAIL = 6;

    params->dsi.HS_PRPR = 6;




    params->dsi.PLL_CLOCK = 241;

    params->dsi.ssc_disable = 1;


    params->dsi.esd_check_enable                    = 0;
    params->dsi.customization_esd_check_enable      = 0;

    params->dsi.lcm_esd_check_table[0].cmd          = 0x68;
    params->dsi.lcm_esd_check_table[0].count        = 1;
    params->dsi.lcm_esd_check_table[0].para_list[0] = 0xC0;

    params->dsi.lcm_esd_check_table[1].cmd          = 0x09;
    params->dsi.lcm_esd_check_table[1].count        = 3;
    params->dsi.lcm_esd_check_table[1].para_list[0] = 0x80;
    params->dsi.lcm_esd_check_table[1].para_list[1] = 0x73;
    params->dsi.lcm_esd_check_table[1].para_list[2] = 0x04;

    params->dsi.lcm_esd_check_table[2].cmd          = 0xAF;
    params->dsi.lcm_esd_check_table[2].count        = 1;
    params->dsi.lcm_esd_check_table[2].para_list[0] = 0xFD;

    params->lcm_seq_suspend = LCM_MIPI_VIDEO_FRAME_DONE_SUSPEND;
    params->lcm_seq_shutdown = LCM_SHUTDOWN_AFTER_DSI_OFF;
    params->lcm_seq_resume = LCM_MIPI_READY_VIDEO_FRAME_RESUME;
    params->lcm_seq_power_on = NOT_USE_RESUME;
}


static void lcm_dsv_ctrl(unsigned int enable, unsigned int delay)
{
    if(enable==1){
        disp_set_gpio_ctrl(DSV_VPOS_EN, 1);
        sm5109_set_vspn();

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

    lcm_reset(10, 10, 120);
    lcm_dsv_ctrl(1, 1);
    MDELAY(15);




    push_table(NULL, init_setting_vdo, ARRAY_SIZE(init_setting_vdo), 1);

    LCM_PRINT("[LCD] %s\n",__func__);
}



static void lcm_suspend(void)
{

    push_table(NULL, lcm_suspend_setting, ARRAY_SIZE(lcm_suspend_setting), 1);


    lcm_dsv_ctrl(0, 1);
    MDELAY(5);

    lcd_reset_pin(0);
    MDELAY(10);

    LCM_PRINT("[LCD] %s\n",__func__);
}

static void lcm_resume(void)
{

    lcm_init();

    LCM_PRINT("[LCD] %s\n",__func__);
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
LCM_DRIVER st7703_hdplus_dsi_vdo_lce_lcm_drv =
#else
struct LCM_DRIVER st7703_hdplus_dsi_vdo_lce_lcm_drv =
#endif
{
    .name           = "LCE-ST7703",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params     = lcm_get_params,
    .init           = lcm_init,
    .suspend        = lcm_suspend,
    .resume         = lcm_resume,
#ifdef BUILD_LK
    .compare_id     = lcm_compare_id,
#endif
};

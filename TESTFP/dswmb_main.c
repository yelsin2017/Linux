#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <asm/gpio.h>
#include <mach/ftpmu010.h>
#include <mach/fmem.h>
#include <linux/synclink.h>
#include <linux/miscdevice.h>

#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <mach/ftpmu010.h>

#include "platform_mb.h"

static int plat_pmu_fd = -1;

/*
 * GM8136 PMU registers for chio/i2c/spi...
 */
static pmuReg_t plat_pmu_reg_8136[] = {
	/*
	 * IP Main Clock Select Setting Register [offset=0x28]
	 * ----------------------------------------------------
	 * [2 : 1] ext_clkout_src  ==> 0: PLL3(540/594MHz), 1: OSCH(30MHz) 2: PLL2 CNTP3(600MHz)
	 * [12:11] ssp0_clk        ==> 0: PLL3(540/594Mhz), 1: PLL1 cntp3(810MHz), 2: PLL2(600MHz)
	 * [14:13] ssp1_clk        ==> 0: PLL3(540/594Mhz), 1: PLL1 cntp3(810MHz), 2: PLL2(600MHz)
	 * [18]    ext_clkout_sscg ==> 0: bypass, 1: SSCG
	 */
	{
	 .reg_off   = 0x28,
	 .bits_mask = 0x07047806,
	 .lock_bits = 0x00000000,   ///< default don't lock these bit
	 .init_val  = (0x1 << 11) | (0x1 << 13) | (0x0 << 24),  //pll1 cntp3(810/3=270, 270/11=24.5454)
	 .init_mask = (0x3 << 11) | (0x3 << 13) | (0x7 << 24),            ///< don't init
	},

	/*
	 * Driving Capacity and Slew Rate and Hold time Control 0 [offset=0x40]
	 * --------------------------------------------------------------------
	 * [21:20] ext_clkout_driving ==> 0:2mA 1:4mA 2:6mA 3:8mA
	 * [22]    ext_clkout_schmitt ==> 0:normal 1: schmitt-triggre
	 * [23]    ext_clkout_slew    ==> 0:fast 1:slow
	 */
	{
	 .reg_off   = 0x40,
	 .bits_mask = 0x00f00000,
	 .lock_bits = 0x00000000,   ///< default don't lock these bit
	 .init_val  = 0x00100000,   ///< default driving to 4mA
	 .init_mask = 0x00300000,
	},

	/*
	 * Driving Capacity and Slew Rate and Hold time Control 1 [offset=0x44]
	 * --------------------------------------------------------------------
	 * [28] ext_clkout_slew_enb ==> Slew Rate control, 1:enable 0:disable
	 */
	{
	 .reg_off   = 0x44,
	 .bits_mask = 0x10000000,
	 .lock_bits = 0x00000000,   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = 0,            ///< don't init
	},

	/*
	 * Multi-Function Port Setting Register 0 [offset=0x50]
	 * -------------------------------------------------------
	 * [13:12] ext_clkout_pin ==> 0: GPIO_0[6],  1: EXT_CLKOUT
	 */
	{
	 .reg_off   = 0x50,
	 .bits_mask = 0x00003000,
	 .lock_bits = 0x00000000,   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = 0,            ///< don't init
	},

	/*
	 * SSCG Register                          [offset=0x6c]
	 * ----------------------------------------------------
	 * [17:16] ext_clkout_sscg_mr ==> 0~3
	 */
	{
	 .reg_off   = 0x6c,
	 .bits_mask = 0x00030000,
	 .lock_bits = 0x00000000,   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = 0,            ///< don't init
	},

	/*
	 * EXT/panel pixel/LCDC scalar -- x-bit counter [offset=0x78]
	 * ----------------------------------------------------------
	 * [21:16] ext_clkout_div
	 */
	{
	 .reg_off   = 0x78,
	 .bits_mask = 0x003f0000,
	 .lock_bits = 0x00000000,   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = 0,            ///< don't init
	},

	/*
	 * hardcore clk off : 1-gating clock [offset=0xac]
	 * ----------------------------------------------------
	 * [4]  ext_clkout_gate      => 0: not gating 1: gating
	 * [16] ext_clkout_sscg_gate => 0: not gating 1: gating
	 */
	{
	 .reg_off   = 0xac,
	 .bits_mask = 0x00010010,
	 .lock_bits = 0x00000000,   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = 0,            ///< don't init
	},

	/*
	 * SSP/ADC/ADDA x-bit counter [offset=0x74]
	 * -------------------------------------------------
	 * [5 :0] SSP0 clock divided value ==> 10 (810/3/(10+1) = 24.5454Mhz)
	 * [13:8] SSP1 clock divided value ==> 10 (810/3/(10+1) = 24.5454Mhz)
	 */
	{
	 .reg_off   = 0x74,
	 .bits_mask = (0x3f << 8) | (0x3f),
	 .lock_bits = 0x0,
	 .init_val  = (0x0a << 8) | (0x0a),
	 .init_mask = (0x3f << 8) | (0x3f),
	},

	/*
	 * system control register [offset=0x7c]
	 * -------------------------------------------------
	 * [29] SSP1 source select ==> 0: ADDA(DA) 1: external pin
	 */
	{
	 .reg_off   = 0x7c,
	 .bits_mask = 0x1 << 29,
	 .lock_bits = 0x0,
	 .init_val  = 0x0,
	 .init_mask = 0x1 << 29,
	},

	/*
	 * SSP0~1 pclk & sclk [offset=0xb8]
	 * ---------------------------------------------------------
	 * [4]  ssp0 pclk & sclk
	 * [5]  ssp1 pclk & sclk
	 */
	 {
	 .reg_off   = 0xb8,
	 .bits_mask = 0x3 << 4,
	 .lock_bits = 0x0,
	 .init_val  = 0x0,
	 .init_mask = 0x3 << 4,
	},
	/*
	 * 面板健盘按键
	 * ----------------------------------------------------
	 * [23:22]	LEFT	==> 0: GPIO_1[2]
	 * [21:20]	RIGHT	==> 0: GPIO_1[1]
	 * [9:8]		DOWN	==> 0: GPIO_0[27]
	 * [7:6]		UP		==> 0: GPIO_0[26]
	 * [5:4]		ESC		==> 0: GPIO_0[25]
	 * [3:2]		ENTER	==> 0: GPIO_0[24]
	 * ----------------------------------------------------
	 * LED指示灯
	 * ----------------------------------------------------
	 * [19:18]	ALARM_LED	==> 0: GPIO_1[0]
	 * ----------------------------------------------------
	 */
	{
	 .reg_off   = 0x58,
	 .bits_mask = ((0x0f << 20) | (0x0f << 6) | (0x0f << 2) | (0x03 << 18)),
	 .lock_bits = ((0x0f << 20) | (0x0f << 6) | (0x0f << 2) | (0x03 << 18)),   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = ((0x0f << 20) | (0x0f << 6) | (0x0f << 2) | (0x03 << 18)),            ///< don't init
	},
	/*
	 * 串口通道选择
	 * ----------------------------------------------------
	 * [15:14]		153D_S1	==> 0: GPIO_1[29]
	 * [13:12]		153D_S0	==> 0: GPIO_1[28]
	 * ----------------------------------------------------
	 */
	{
	 .reg_off   = 0x64,
	 .bits_mask = (0x0f << 12),
	 .lock_bits = (0x0f << 12),   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = (0x0f << 12),            ///< don't init
	},
	/*
	 * LED指示灯
	 * ----------------------------------------------------
	 * [9:8]	RUN_LED	==> 0: GPIO_0[4]
	 * ----------------------------------------------------
	 */
	{
	 .reg_off   = 0x50,
	 .bits_mask = (0x03 << 8),
	 .lock_bits = (0x03 << 8),   ///< default don't lock these bit
	 .init_val  = 0,
	 .init_mask = (0x03 << 8),            ///< don't init
	},
	/*
	 * SPI接口
	 * ----------------------------------------------------
	 * [17:16] SSP1_SCLK	==> 1: SSP1_SCLK
	 * [15:14] SSP1_RXD		==> 1: SSP1_RXD
	 * [13:12] SSP1_TXD		==> 1: SSP1_TXD
	 * [11:10] SSP1_FS		==> 1: SSP1_FS
	 * ----------------------------------------------------
	 */
	{
	 .reg_off   = 0x58,
	 .bits_mask = (0xff << 10),
	 .lock_bits = (0xff << 10),   ///< default don't lock these bit
	 .init_val  = (0x55 << 10),
	 .init_mask = (0xff << 10),            ///< don't init
	},
};

static pmuRegInfo_t plat_pmu_reg_info_8136 = {
	"MB_8136",
	ARRAY_SIZE(plat_pmu_reg_8136),
	ATTR_TYPE_NONE,
	&plat_pmu_reg_8136[0]
};

static int __init plat_init(void)
{
	int ret = 0;
	int fd;

	printk("dswmb version %d\n", PLAT_COMMON_VERSION);

	/* register pmu register for register access */
	fd = ftpmu010_register_reg(&plat_pmu_reg_info_8136);
	if(fd < 0) {
		printk("register pmu register failed!!\n");
		ret = -1;
		goto exit;
	}

	plat_pmu_fd = fd;

	dswmb_comch_init();
	dswmb_key_init();

exit:
	return ret;
}

static void __exit plat_exit(void)
{
	dswmb_key_uninit();
	dswmb_comch_uninit();

	/* deregister pmu register */
	if(plat_pmu_fd >= 0)
		ftpmu010_deregister_reg(plat_pmu_fd);
}

module_init(plat_init);
module_exit(plat_exit);

MODULE_DESCRIPTION(" YELSINDB Plaftorm Common Driver");
MODULE_AUTHOR("YELSINDB Technology Corp.");
MODULE_LICENSE("GPL");

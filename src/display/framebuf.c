/* $Id$ */
//
// Проект HF Dream Receiver (КВ приёмник мечты)
// автор Гена Завидовский mgs2001@mail.ru
// UA1ARN
//
// Доработки для LS020 Василий Линывый, livas60@mail.ru
//
// Функции построения изображений в буфере - вне зависимости от того, есть ли в процессоре LTDC.'
// Например при offscreen composition растровых изображений для SPI дисплеев
//

#include "hardware.h"

#include "board.h"
#include "display.h"
#include "formats.h"
#include "spi.h"	// hardware_spi_master_send_frame
#include "display2.h"
#include "fontmaps.h"
#include <string.h>

/*
	Dead time value in the AXI clock cycle inserted between two consecutive accesses on
	the AXI master port. These bits represent the minimum guaranteed number of cycles
	between two consecutive AXI accesses
 */
#define DMA2D_AMTCR_DT_VALUE 255uL	/* 0..255 */
#define DMA2D_AMTCR_DT_ENABLE 1uL	/* 0..1 */

#define MDMA_CH		MDMA_Channel0
#define MDMA_DATA	(MDMA_Channel1->CSAR)	// регистр выделенного канала MDMA используется для хранения значения цвета. Переиферия не кэшируется.

#ifndef DMA2D_CR_LOM
	#define DMA2D_CR_LOM	(1u << 6)	/* documented but missing in headers. */
#endif

#define MDMA_CCR_PL_VALUE 0uL	// PL: priority 0..3: min..max

#if LCDMODE_LTDC_L24
	#define DMA2D_FGPFCCR_CM_VALUE_MAIN		(1 * DMA2D_FGPFCCR_CM_0)	/* 0001: RGB888 */
	#define MDMA_CTCR_xSIZE_MAIN			0x00	// 1 byte
	//#define DMA2D_OPFCCR_CM_VALUE_MAIN	(1 * DMA2D_OPFCCR_CM_0)	/* 001: RGB888 */

#elif LCDMODE_MAIN_L8
	#define DMA2D_FGPFCCR_CM_VALUE_MAIN		(5 * DMA2D_FGPFCCR_CM_0)	/* 0101: L8 */
	#define MDMA_CTCR_xSIZE_MAIN			0x00	// 1 byte
	////#define DMA2D_OPFCCR_CM_VALUE	(x * DMA2D_OPFCCR_CM_0)	/* not supported */

#elif LCDMODE_MAIN_ARGB888
	#define DMA2D_FGPFCCR_CM_VALUE_MAIN		(0 * DMA2D_FGPFCCR_CM_0)	/* 0000: ARGB888 */
	#define MDMA_CTCR_xSIZE_MAIN			0x02	// 10: Word (32-bit)
	#define DMA2D_OPFCCR_CM_VALUE_MAIN		(0 * DMA2D_OPFCCR_CM_0)	/* 0: 000: ARGB8888 */

#elif LCDMODE_MAIN_RGB565
	#define DMA2D_FGPFCCR_CM_VALUE_MAIN		(2 * DMA2D_FGPFCCR_CM_0)	/* 0010: RGB565 */
	#define MDMA_CTCR_xSIZE_MAIN			0x01	// 2 byte
	#define DMA2D_OPFCCR_CM_VALUE_MAIN		(2 * DMA2D_OPFCCR_CM_0)	/* 010: RGB565 */

#endif /* LCDMODE_MAIN_L8 */

#if LCDMODE_PIP_L8
	#define DMA2D_FGPFCCR_CM_VALUE_PIP	(5 * DMA2D_FGPFCCR_CM_0)	/* 0101: L8 */
	#define MDMA_CTCR_xSIZE_PIP			0x00	// 1 byte
	////#define DMA2D_OPFCCR_CM_VALUE_MAIN	(x * DMA2D_OPFCCR_CM_0)	/* not supported */

#elif LCDMODE_PIP_RGB565
	#define DMA2D_FGPFCCR_CM_VALUE_PIP	(2 * DMA2D_FGPFCCR_CM_0)	/* 0010: RGB565 */
	#define DMA2D_OPFCCR_CM_VALUE_PIP	(2 * DMA2D_OPFCCR_CM_0)	/* 010: RGB565 */
	#define MDMA_CTCR_xSIZE_PIP			0x01	// 2 byte

#else /* LCDMODE_MAIN_L8 */
	#define DMA2D_FGPFCCR_CM_VALUE_PIP	DMA2D_FGPFCCR_CM_VALUE_MAIN
	#define DMA2D_OPFCCR_CM_VALUE_PIP	DMA2D_OPFCCR_CM_VALUE_MAIN
	#define MDMA_CTCR_xSIZE_PIP			MDMA_CTCR_xSIZE_MAIN

#endif /* LCDMODE_MAIN_L8 */

//#define DMA2D_FGPFCCR_CM_VALUE_L24	(1 * DMA2D_FGPFCCR_CM_0)	/* 0001: RGB888 */
//#define DMA2D_FGPFCCR_CM_VALUE_L16	(2 * DMA2D_FGPFCCR_CM_0)	/* 0010: RGB565 */
//#define DMA2D_FGPFCCR_CM_VALUE_L8	(5 * DMA2D_FGPFCCR_CM_0)	/* 0101: L8 */

//#define MDMA_CTCR_xSIZE_U32			0x02	// 4 byte
//#define MDMA_CTCR_xSIZE_U16			0x01	// 2 byte
//#define MDMA_CTCR_xSIZE_U8			0x00	// 1 byte
//#define MDMA_CTCR_xSIZE_RGB565		0x01	// 2 byte

#if WITHMDMAHW && (CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1)

static uint_fast8_t
mdma_getbus(uintptr_t addr)
{
#if CPUSTYLE_STM32H7XX
	addr &= 0xFF000000uL;
	return (addr == 0x00000000uL || addr == 0x20000000uL);

#elif CPUSTYLE_STM32MP1 && CORE_CA7
	// SYSMEM
	// DDRCTRL
	/*
	 * 0: The system/AXI bus is used on channel x.
	 * 1: The AHB bus/TCM is used on channel x.
	 */
	addr &= 0xFF000000uL;
	return (addr == 0x00000000uL || addr == 0x20000000uL);

#elif CPUSTYLE_STM32MP1 && CORE_CM4
	#error M4 core not supported
	/*
	 * 0: The system/AXI bus is used on channel x.
	 * 1: The AHB bus/TCM is used on channel x.
	 */
	addr &= 0xFF000000uL;
	return (addr == 0x00000000uL || addr == 0x20000000uL);

#else
	return 0;

#endif
}


static uint_fast8_t
mdma_tlen(uint_fast32_t nb, uint_fast8_t ds)
{
	return (nb < 128 ? nb : 128) / ds * ds;
}

//	For the TCM memory accesses, the burst access is only allowed when the increment
//	and data size are identical and lower than or equal to 32-bit.

// DBURST value must be programmed as to ensure that the burst size is lower than the
// Transfer Length. If this is not ensured, the result is unpredictable.

// И еще несколько условий..

static uint_fast8_t
mdma_getburst(uint_fast16_t tlen, uint_fast8_t bus, uint_fast8_t xinc)
{
	if (bus == 0 && xinc == 0)
		return 4;
	if (xinc == 0)
		return 0;

	if (bus != 0)
		return 0;

	if (tlen >= 128)
		return 7;
	if (tlen >= 64)
		return 6;
	if (tlen >= 32)
		return 5;
	if (tlen >= 16)
		return 4;
	if (tlen >= 8)
		return 3;
	if (tlen >= 4)
		return 2;
	if (tlen >= 2)
		return 1;
	return 0;
}

static void
mdma_stop_unused(void)
{
	MDMA_CH->CCR &= ~ MDMA_CCR_EN_Msk;
	while ((MDMA_CH->CCR & MDMA_CCR_EN_Msk) != 0)
		;
}

/* запустить пересылку и дождаться завершения. */
static void
mdma_startandwait(void)
{
	// MDMA implementation
	MDMA_CH->CIFCR =
		MDMA_CIFCR_CLTCIF_Msk |
		MDMA_CIFCR_CBTIF_Msk |
		MDMA_CIFCR_CBRTIF_Msk |
		MDMA_CIFCR_CCTCIF_Msk |
		MDMA_CIFCR_CTEIF_Msk |
		0;
	(void) MDMA_CH->CIFCR;

	// Set priority
	MDMA_CH->CCR = (MDMA_CH->CCR & ~ (MDMA_CCR_PL_Msk)) |
			(MDMA_CCR_PL_VALUE << MDMA_CCR_PL_Pos) |
			0;
	(void) MDMA_CH->CCR;

	MDMA_CH->CCR |= MDMA_CCR_EN_Msk;
	(void) MDMA_CH->CCR;
	/* start transfer */
	MDMA_CH->CCR |= MDMA_CCR_SWRQ_Msk;
	(void) MDMA_CH->CCR;

	/* wait for complete */
	while ((MDMA_CH->CISR & MDMA_CISR_CTCIF_Msk) == 0)	// Channel x Channel Transfer Complete interrupt flag
	{
		ASSERT((MDMA_CH->CISR & MDMA_CISR_TEIF_Msk) == 0);	/* Channel x transfer error interrupt flag */
		hardware_nonguiyield();
	}
	//__DMB();	//ensure the ordering of data cache maintenance operations and their effects
	ASSERT((MDMA_CH->CISR & MDMA_CISR_TEIF_Msk) == 0);	/* Channel x transfer error interrupt flag */

}

void arm_hardware_mdma_initialize(void)
{
#if CPUSTYLE_STM32MP1
	/* Enable the MDMA Clock */
	RCC->MP_AHB6ENSETR = RCC_MP_AHB6ENSETR_MDMAEN;	/* MDMA clock enable */
	(void) RCC->MP_AHB6ENSETR;
	RCC->MP_AHB6LPENSETR = RCC_MP_AHB6LPENSETR_MDMALPEN;	/* MDMA clock enable */
	(void) RCC->MP_AHB6LPENSETR;
	//RCC->MP_TZAHB6ENSETR = RCC_MP_TZAHB6ENSETR_MDMAEN;
	//(void) RCC->MP_TZAHB6ENSETR;

	/* SYSCFG clock enable */
	RCC->MP_APB3ENSETR = RCC_MP_APB3ENSETR_SYSCFGEN;
	(void) RCC->MP_APB3ENSETR;
	RCC->MP_APB3LPENSETR = RCC_MP_APB3LPENSETR_SYSCFGLPEN;
	(void) RCC->MP_APB3LPENSETR;
	/*
	 * Interconnect update : select master using the port 1.
	 * LTDC = AXI_M9.
	 * MDMA = AXI_M7.
	 */
	SYSCFG->ICNR |= SYSCFG_ICNR_AXI_M7;
	(void) SYSCFG->ICNR;

#elif CPUSTYLE_STM32H7XX
	/* Enable the MDMA Clock */
	RCC->AHB3ENR |= RCC_AHB3ENR_MDMAEN_Msk;	/* MDMA clock enable */
	(void) RCC->AHB3ENR;
	RCC->AHB3LPENR |= RCC_AHB3LPENR_MDMALPEN_Msk;
	(void) RCC->AHB3LPENR;

#else /* CPUSTYLE_STM32H7XX */
	/* Enable the MDMA Clock */
	RCC->AHB1ENR |= RCC_AHB1ENR_MDMAEN;	/* MDMA clock enable */
	(void) RCC->AHB1ENR;
	RCC->AHB3LPENR |= RCC_AHB3LPENR_MDMALPEN_Msk;
	(void) RCC->AHB3LPENR;

#endif /* CPUSTYLE_STM32H7XX */
}

#elif WITHMDMAHW & CPUSTYPE_T113

/* Использование G2D для формирования изображений */
// https://github.com/tinalinux/linux-3.10/blob/46f73ef4efcb4014b25e5ad1eca750ad62a1d0ff/drivers/char/sunxi_g2d/g2d_driver.c
// https://github.com/tinalinux/linux-3.10/blob/46f73ef4efcb4014b25e5ad1eca750ad62a1d0ff/drivers/char/sunxi_g2d/g2d_regs.h
// https://github.com/tinalinux/linux-3.10/blob/46f73ef4efcb4014b25e5ad1eca750ad62a1d0ff/drivers/char/sunxi_g2d/g2d_bsp_sun8iw11.c
/* Input DMA setting */
#define G2D_FILL_ENABLE		(1<<16)
#define G2D_FILL_DISABLE	(0<<16)

/* Work Mode Select */
#define G2D_IDMA_ENABLE		(1<<0)
#define G2D_IDMA_DISABLE	(0<<0)

/* Scaler Control Select */
#define G2D_SCALER_DISABLE	(0<<0)
#define G2D_SCALER_ENABLE	(1<<0)
#define G2D_SCALER_4TAP4	(0<<4)

#define G2D_FINISH_IRQ		(1<<8)
#define G2D_ERROR_IRQ			(1<<9)

/* mixer data format */
typedef enum {
	/* share data format */
	G2D_FMT_ARGB_AYUV8888	= (0x0),
	G2D_FMT_BGRA_VUYA8888	= (0x1),
	G2D_FMT_ABGR_AVUY8888	= (0x2),
	G2D_FMT_RGBA_YUVA8888	= (0x3),

	G2D_FMT_XRGB8888		= (0x4),
	G2D_FMT_BGRX8888		= (0x5),
	G2D_FMT_XBGR8888		= (0x6),
	G2D_FMT_RGBX8888		= (0x7),

	G2D_FMT_ARGB4444		= (0x8),
	G2D_FMT_ABGR4444		= (0x9),
	G2D_FMT_RGBA4444		= (0xA),
	G2D_FMT_BGRA4444		= (0xB),

	G2D_FMT_ARGB1555		= (0xC),
	G2D_FMT_ABGR1555		= (0xD),
	G2D_FMT_RGBA5551		= (0xE),
	G2D_FMT_BGRA5551		= (0xF),

	G2D_FMT_RGB565			= (0x10),
	G2D_FMT_BGR565			= (0x11),

	G2D_FMT_IYUV422			= (0x12),

	G2D_FMT_8BPP_MONO		= (0x13),
	G2D_FMT_4BPP_MONO		= (0x14),
	G2D_FMT_2BPP_MONO		= (0x15),
	G2D_FMT_1BPP_MONO		= (0x16),

	G2D_FMT_PYUV422UVC		= (0x17),
	G2D_FMT_PYUV420UVC		= (0x18),
	G2D_FMT_PYUV411UVC		= (0x19),

	/* just for output format */
	G2D_FMT_PYUV422			= (0x1A),
	G2D_FMT_PYUV420			= (0x1B),
	G2D_FMT_PYUV411			= (0x1C),

	/* just for input format */
	G2D_FMT_8BPP_PALETTE	= (0x1D),
	G2D_FMT_4BPP_PALETTE	= (0x1E),
	G2D_FMT_2BPP_PALETTE	= (0x1F),
	G2D_FMT_1BPP_PALETTE	= (0x20),

	G2D_FMT_PYUV422UVC_MB16	= (0x21),
	G2D_FMT_PYUV420UVC_MB16	= (0x22),
	G2D_FMT_PYUV411UVC_MB16	= (0x23),
	G2D_FMT_PYUV422UVC_MB32	= (0x24),
	G2D_FMT_PYUV420UVC_MB32	= (0x25),
	G2D_FMT_PYUV411UVC_MB32	= (0x26),
	G2D_FMT_PYUV422UVC_MB64	= (0x27),
	G2D_FMT_PYUV420UVC_MB64	= (0x28),
	G2D_FMT_PYUV411UVC_MB64	= (0x29),
	G2D_FMT_PYUV422UVC_MB128= (0x2A),
	G2D_FMT_PYUV420UVC_MB128= (0x2B),
	G2D_FMT_PYUV411UVC_MB128= (0x2C),

}g2d_data_fmt;

typedef enum {
	G2D_SEQ_NORMAL = 0x0,

	/* for interleaved yuv422 */
	G2D_SEQ_VYUY   = 0x1,				/* pixel 0�ڵ�16λ */
	G2D_SEQ_YVYU   = 0x2,				/* pixel 1�ڵ�16λ */

	/* for uv_combined yuv420 */
	G2D_SEQ_VUVU   = 0x3,

	/* for 16bpp rgb */
	G2D_SEQ_P10	= 0x4,				/* pixel 0�ڵ�16λ */
	G2D_SEQ_P01	= 0x5,				/* pixel 1�ڵ�16λ */

	/* planar format or 8bpp rgb */
	G2D_SEQ_P3210  = 0x6,				/* pixel 0�ڵ�8λ */
	G2D_SEQ_P0123  = 0x7,				/* pixel 3�ڵ�8λ */

	/* for 4bpp rgb */
	G2D_SEQ_P76543210  = 0x8,			/* 7,6,5,4,3,2,1,0 */
	G2D_SEQ_P67452301  = 0x9,			/* 6,7,4,5,2,3,0,1 */
	G2D_SEQ_P10325476  = 0xA,			/* 1,0,3,2,5,4,7,6 */
	G2D_SEQ_P01234567  = 0xB,			/* 0,1,2,3,4,5,6,7 */

	/* for 2bpp rgb */
	G2D_SEQ_2BPP_BIG_BIG	   = 0xC,	/* 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0 */
	G2D_SEQ_2BPP_BIG_LITTER	= 0xD,	/* 12,13,14,15,8,9,10,11,4,5,6,7,0,1,2,3 */
	G2D_SEQ_2BPP_LITTER_BIG	= 0xE,	/* 3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12 */
	G2D_SEQ_2BPP_LITTER_LITTER = 0xF,	/* 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 */

	/* for 1bpp rgb */
	G2D_SEQ_1BPP_BIG_BIG	   = 0x10,	/* 31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0 */
	G2D_SEQ_1BPP_BIG_LITTER	= 0x11,	/* 24,25,26,27,28,29,30,31,16,17,18,19,20,21,22,23,8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,7 */
	G2D_SEQ_1BPP_LITTER_BIG	= 0x12,	/* 7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8,23,22,21,20,19,18,17,16,31,30,29,28,27,26,25,24 */
	G2D_SEQ_1BPP_LITTER_LITTER = 0x13,	/* 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31 */
}g2d_pixel_seq;


typedef enum {
	G2D_FIL_NONE			= 0x00000000,
	G2D_FIL_PIXEL_ALPHA		= 0x00000001,
	G2D_FIL_PLANE_ALPHA		= 0x00000002,
	G2D_FIL_MULTI_ALPHA		= 0x00000004,
}g2d_fillrect_flags;

typedef enum {
	G2D_BLT_NONE			= 0x00000000,
	G2D_BLT_PIXEL_ALPHA		= 0x00000001,
	G2D_BLT_PLANE_ALPHA		= 0x00000002,
	G2D_BLT_MULTI_ALPHA		= 0x00000004,
	G2D_BLT_SRC_COLORKEY	= 0x00000008,
	G2D_BLT_DST_COLORKEY	= 0x00000010,
	G2D_BLT_FLIP_HORIZONTAL	= 0x00000020,
	G2D_BLT_FLIP_VERTICAL	= 0x00000040,
	G2D_BLT_ROTATE90		= 0x00000080,
	G2D_BLT_ROTATE180		= 0x00000100,
	G2D_BLT_ROTATE270		= 0x00000200,
	G2D_BLT_MIRROR45		= 0x00000400,
	G2D_BLT_MIRROR135		= 0x00000800,
	G2D_BLT_SRC_PREMULTIPLY	= 0x00001000,
	G2D_BLT_DST_PREMULTIPLY	= 0x00002000,
} g2d_blt_flags;

/* g2d color gamut */
typedef enum {
	G2D_BT601,
	G2D_BT709,
	G2D_BT2020,
} g2d_color_gmt;
/*
 * 0:Top to down, Left to right
 * 1:Top to down, Right to left
 * 2:Down to top, Left to right
 * 3:Down to top, Right to left
 */
enum g2d_scan_order{
	G2D_SM_TDLR = 0x00000000,
	G2D_SM_TDRL = 0x00000001,
	G2D_SM_DTLR = 0x00000002,
	G2D_SM_DTRL = 0x00000003,
};


// https://github.com/lianghuixin/licee4.4/blob/bfee1d63fa355a54630244307296a00a973b70b0/linux-4.4/drivers/char/sunxi_g2d/g2d_bsp_v2.c

/* module base addr */
//#define G2D_TOP_BASE        (0x00000 + G2D_BASE)
//#define G2D_MIXER_BASE      (0x00100 + G2D_BASE)
//#define G2D_BLD_BASE        (0x00400 + G2D_BASE)
//#define G2D_V0_BASE         (0x00800 + G2D_BASE)
//#define G2D_UI0_BASE        (0x01000 + G2D_BASE)
//#define G2D_UI1_BASE        (0x01800 + G2D_BASE)
//#define G2D_UI2_BASE        (0x02000 + G2D_BASE)
//#define G2D_WB_BASE         (0x03000 + G2D_BASE)
//#define G2D_VSU_BASE        (0x08000 + G2D_BASE)
//#define G2D_ROT_BASE        (0x28000 + G2D_BASE)
//#define G2D_GSU_BASE        (0x30000 + G2D_BASE)

//#define G2D_V0 		((G2D_TypeDef *) G2D_V0_BASE)				/*!< \brief G2D Interface register set access pointer */

/* register offset */
/* TOP register */
//#define G2D_SCLK_GATE  (0x00 + G2D_TOP)
//#define G2D_HCLK_GATE  (0x04 + G2D_TOP)
//#define G2D_AHB_RESET  (0x08 + G2D_TOP)
//#define G2D_SCLK_DIV   (0x0C + G2D_TOP)

/* MIXER GLB register */
//#define G2D_MIXER_CTL  (0x00 + G2D_MIXER)
//#define G2D_MIXER_INT  (0x04 + G2D_MIXER)
//#define G2D_MIXER_CLK  (0x08 + G2D_MIXER)

/* LAY VIDEO register */
//#define V0_ATTCTL      (0x00 + G2D_V0)
//#define V0_MBSIZE      (0x04 + G2D_V0)
//#define V0_COOR        (0x08 + G2D_V0)
//#define V0_PITCH0      (0x0C + G2D_V0)
//#define V0_PITCH1      (0x10 + G2D_V0)
//#define V0_PITCH2      (0x14 + G2D_V0)
//#define V0_LADD0       (0x18 + G2D_V0)
//#define V0_LADD1       (0x1C + G2D_V0)
//#define V0_LADD2       (0x20 + G2D_V0)
//#define V0_FILLC       (0x24 + G2D_V0)
//#define V0_HADD        (0x28 + G2D_V0)
//#define V0_SIZE        (0x2C + G2D_V0)
//#define V0_HDS_CTL0    (0x30 + G2D_V0)
//#define V0_HDS_CTL1    (0x34 + G2D_V0)
//#define V0_VDS_CTL0    (0x38 + G2D_V0)
//#define V0_VDS_CTL1    (0x3C + G2D_V0)

/* LAY0 UI register */
//#define UI0_ATTR       (0x00 + G2D_UI0)
//#define UI0_MBSIZE     (0x04 + G2D_UI0)
//#define UI0_COOR       (0x08 + G2D_UI0)
//#define UI0_PITCH      (0x0C + G2D_UI0)
//#define UI0_LADD       (0x10 + G2D_UI0)
//#define UI0_FILLC      (0x14 + G2D_UI0)
//#define UI0_HADD       (0x18 + G2D_UI0)
//#define UI0_SIZE       (0x1C + G2D_UI0)

/* LAY1 UI register */
//#define UI1_ATTR       (0x00 + G2D_UI1)
//#define UI1_MBSIZE     (0x04 + G2D_UI1)
//#define UI1_COOR       (0x08 + G2D_UI1)
//#define UI1_PITCH      (0x0C + G2D_UI1)
//#define UI1_LADD       (0x10 + G2D_UI1)
//#define UI1_FILLC      (0x14 + G2D_UI1)
//#define UI1_HADD       (0x18 + G2D_UI1)
//#define UI1_SIZE       (0x1C + G2D_UI1)
//
///* LAY2 UI register */
//#define UI2_ATTR       (0x00 + G2D_UI2)
//#define UI2_MBSIZE     (0x04 + G2D_UI2)
//#define UI2_COOR       (0x08 + G2D_UI2)
//#define UI2_PITCH      (0x0C + G2D_UI2)
//#define UI2_LADD       (0x10 + G2D_UI2)
//#define UI2_FILLC      (0x14 + G2D_UI2)
//#define UI2_HADD       (0x18 + G2D_UI2)
//#define UI2_SIZE       (0x1C + G2D_UI2)
//
///* VSU register */
//#define VS_CTRL           (0x000 + G2D_VSU)
//#define VS_OUT_SIZE       (0x040 + G2D_VSU)
//#define VS_GLB_ALPHA      (0x044 + G2D_VSU)
//#define VS_Y_SIZE         (0x080 + G2D_VSU)
//#define VS_Y_HSTEP        (0x088 + G2D_VSU)
//#define VS_Y_VSTEP        (0x08C + G2D_VSU)
//#define VS_Y_HPHASE       (0x090 + G2D_VSU)
//#define VS_Y_VPHASE0      (0x098 + G2D_VSU)
//#define VS_C_SIZE         (0x0C0 + G2D_VSU)
//#define VS_C_HSTEP        (0x0C8 + G2D_VSU)
//#define VS_C_VSTEP        (0x0CC + G2D_VSU)
//#define VS_C_HPHASE       (0x0D0 + G2D_VSU)
//#define VS_C_VPHASE0      (0x0D8 + G2D_VSU)
//#define VS_Y_HCOEF0       (0x200 + G2D_VSU)
//#define VS_Y_VCOEF0       (0x300 + G2D_VSU)
//#define VS_C_HCOEF0       (0x400 + G2D_VSU)
//
///* BLD register */
//#define BLD_EN_CTL         (0x000 + G2D_BLD)
//#define BLD_FILLC0         (0x010 + G2D_BLD)
//#define BLD_FILLC1         (0x014 + G2D_BLD)
//#define BLD_CH_ISIZE0      (0x020 + G2D_BLD)
//#define BLD_CH_ISIZE1      (0x024 + G2D_BLD)
//#define BLD_CH_OFFSET0     (0x030 + G2D_BLD)
//#define BLD_CH_OFFSET1     (0x034 + G2D_BLD)
//#define BLD_PREMUL_CTL     (0x040 + G2D_BLD)
//#define BLD_BK_COLOR       (0x044 + G2D_BLD)
//#define BLD_SIZE           (0x048 + G2D_BLD)
//#define BLD_CTL            (0x04C + G2D_BLD)
//#define BLD_KEY_CTL        (0x050 + G2D_BLD)
//#define BLD_KEY_CON        (0x054 + G2D_BLD)
//#define BLD_KEY_MAX        (0x058 + G2D_BLD)
//#define BLD_KEY_MIN        (0x05C + G2D_BLD)
//#define BLD_OUT_COLOR      (0x060 + G2D_BLD)
//#define ROP_CTL            (0x080 + G2D_BLD)
//#define ROP_INDEX0         (0x084 + G2D_BLD)
//#define ROP_INDEX1         (0x088 + G2D_BLD)
//#define BLD_CSC_CTL        (0x100 + G2D_BLD)
//#define BLD_CSC0_COEF00    (0x110 + G2D_BLD)
//#define BLD_CSC0_COEF01    (0x114 + G2D_BLD)
//#define BLD_CSC0_COEF02    (0x118 + G2D_BLD)
//#define BLD_CSC0_CONST0    (0x11C + G2D_BLD)
//#define BLD_CSC0_COEF10    (0x120 + G2D_BLD)
//#define BLD_CSC0_COEF11    (0x124 + G2D_BLD)
//#define BLD_CSC0_COEF12    (0x128 + G2D_BLD)
//#define BLD_CSC0_CONST1    (0x12C + G2D_BLD)
//#define BLD_CSC0_COEF20    (0x130 + G2D_BLD)
//#define BLD_CSC0_COEF21    (0x134 + G2D_BLD)
//#define BLD_CSC0_COEF22    (0x138 + G2D_BLD)
//#define BLD_CSC0_CONST2    (0x13C + G2D_BLD)
//#define BLD_CSC1_COEF00    (0x140 + G2D_BLD)
//#define BLD_CSC1_COEF01    (0x144 + G2D_BLD)
//#define BLD_CSC1_COEF02    (0x148 + G2D_BLD)
//#define BLD_CSC1_CONST0    (0x14C + G2D_BLD)
//#define BLD_CSC1_COEF10    (0x150 + G2D_BLD)
//#define BLD_CSC1_COEF11    (0x154 + G2D_BLD)
//#define BLD_CSC1_COEF12    (0x158 + G2D_BLD)
//#define BLD_CSC1_CONST1    (0x15C + G2D_BLD)
//#define BLD_CSC1_COEF20    (0x160 + G2D_BLD)
//#define BLD_CSC1_COEF21    (0x164 + G2D_BLD)
//#define BLD_CSC1_COEF22    (0x168 + G2D_BLD)
//#define BLD_CSC1_CONST2    (0x16C + G2D_BLD)
//#define BLD_CSC2_COEF00    (0x170 + G2D_BLD)
//#define BLD_CSC2_COEF01    (0x174 + G2D_BLD)
//#define BLD_CSC2_COEF02    (0x178 + G2D_BLD)
//#define BLD_CSC2_CONST0    (0x17C + G2D_BLD)
//#define BLD_CSC2_COEF10    (0x180 + G2D_BLD)
//#define BLD_CSC2_COEF11    (0x184 + G2D_BLD)
//#define BLD_CSC2_COEF12    (0x188 + G2D_BLD)
//#define BLD_CSC2_CONST1    (0x18C + G2D_BLD)
//#define BLD_CSC2_COEF20    (0x190 + G2D_BLD)
//#define BLD_CSC2_COEF21    (0x194 + G2D_BLD)
//#define BLD_CSC2_COEF22    (0x198 + G2D_BLD)
//#define BLD_CSC2_CONST2    (0x19C + G2D_BLD)
//
///* WB register */
//#define WB_ATT             (0x00 + G2D_WB)
//#define WB_SIZE            (0x04 + G2D_WB)
//#define WB_PITCH0          (0x08 + G2D_WB)
//#define WB_PITCH1          (0x0C + G2D_WB)
//#define WB_PITCH2          (0x10 + G2D_WB)
//#define WB_LADD0           (0x14 + G2D_WB)
//#define WB_HADD0           (0x18 + G2D_WB)
//#define WB_LADD1           (0x1C + G2D_WB)
//#define WB_HADD1           (0x20 + G2D_WB)
//#define WB_LADD2           (0x24 + G2D_WB)
//#define WB_HADD2           (0x28 + G2D_WB)
//
///* Rotate register */
//#define ROT_CTL            (0x00 + G2D_ROT)
//#define ROT_INT            (0x04 + G2D_ROT)
//#define ROT_TIMEOUT        (0x08 + G2D_ROT)
//#define ROT_IFMT           (0x20 + G2D_ROT)
//#define ROT_ISIZE          (0x24 + G2D_ROT)
//#define ROT_IPITCH0        (0x30 + G2D_ROT)
//#define ROT_IPITCH1        (0x34 + G2D_ROT)
//#define ROT_IPITCH2        (0x38 + G2D_ROT)
//#define ROT_ILADD0         (0x40 + G2D_ROT)
//#define ROT_IHADD0         (0x44 + G2D_ROT)
//#define ROT_ILADD1         (0x48 + G2D_ROT)
//#define ROT_IHADD1         (0x4C + G2D_ROT)
//#define ROT_ILADD2         (0x50 + G2D_ROT)
//#define ROT_IHADD2         (0x54 + G2D_ROT)
//#define ROT_OSIZE          (0x84 + G2D_ROT)
//#define ROT_OPITCH0        (0x90 + G2D_ROT)
//#define ROT_OPITCH1        (0x94 + G2D_ROT)
//#define ROT_OPITCH2        (0x98 + G2D_ROT)
//#define ROT_OLADD0         (0xA0 + G2D_ROT)
//#define ROT_OHADD0         (0xA4 + G2D_ROT)
//#define ROT_OLADD1         (0xA8 + G2D_ROT)
//#define ROT_OHADD1         (0xAC + G2D_ROT)
//#define ROT_OLADD2         (0xB0 + G2D_ROT)
//#define ROT_OHADD2         (0xB4 + G2D_ROT)

/* clear most of the registers value to default */
static uint32_t mixer_reg_init(void){
	//uint32_t i;

//	for(i=0;i<=0x148;i+=4)
//		write_wvalue(i, 0);
	//G2D_V0->G2D_SCAN_ORDER_REG = 0x15FF0000;//DMA MBUS Length

//	/* initial the color space converter parameter */
//	csc_coeff_set();
//
//	/* initial the scaler coefficient parameter */
//	scaler_coeff_set();

	return 0;
}

void arm_hardware_mdma_initialize(void)
{
	//PRINTF("arm_hardware_mdma_initialize (G2D)\n");
	CCU->MBUS_CLK_REG |= (1uL << 30);				// MBUS Reset 1: De-assert reset
	CCU->MBUS_MAT_CLK_GATING_REG |= (1uL << 10);	// Gating MBUS Clock For G2D

	CCU->G2D_CLK_REG = (CCU->G2D_CLK_REG & ~ (0x07uL << 24)) |
		0x01 * (1uL << 24) |	// 000: PLL_PERI(2X), 001: PLL_VIDEO0(4X), 010: PLL_VIDEO1(4X), 011: PLL_AUDIO1(DIV2)
		0;
	CCU->G2D_CLK_REG |= (1uL << 31);	// G2D_CLK_GATING

	//CCU->G2D_BGR_REG = 0;
	CCU->G2D_BGR_REG |= (1uL << 0);		/* Enable gating clock for G2D 1: Pass */
	//CCU->G2D_BGR_REG &= ~ (1uL << 16);	/* G2D reset 0: Assert */
	CCU->G2D_BGR_REG |= (1uL << 16);	/* G2D reset 1: De-assert */
	//memset(G2D, 0xFF, sizeof * G2D);
	//printhex(G2D_V0, G2D_V0, sizeof * G2D_V0);
	//PRINTF("arm_hardware_mdma_initialize (G2D) done.\n");

	G2D_TOP->G2D_SCLK_DIV = (G2D_TOP->G2D_SCLK_DIV & ~ 0xFFuL) |
		4 * (1uL << 4) |	// ROT divider (looks like power of 2)
		4 * (1uL << 0) |	// MIXER divider (looks like power of 2)
		0;
	G2D_TOP->G2D_SCLK_GATE |= (1uL << 1) | (1uL << 0);	// Gate open: 0x02: rot, 0x01: mixer
	G2D_TOP->G2D_HCLK_GATE |= (1uL << 1) | (1uL << 0);	// Gate open: 0x02: rot, 0x01: mixer
	G2D_TOP->G2D_AHB_RESET |= (1uL << 1) | (1uL << 0);	// De-assert reset: 0x02: rot, 0x01: mixer

	// https://github.com/lianghuixin/licee4.4/blob/bfee1d63fa355a54630244307296a00a973b70b0/linux-4.4/drivers/char/sunxi_g2d/g2d_bsp_v2.c

}
#endif /* WITHMDMAHW */

#if ! (LCDMODE_DUMMY || LCDMODE_HD44780)

#if LCDMODE_PIXELSIZE == 1
// Функция получает координаты и работает над буфером в горизонтальной ориентации.
static void
hwacc_fillrect_u8(
	uint8_t * __restrict buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t col,	// начальная координата
	uint_fast16_t row,	// начальная координата
	uint_fast16_t w,	// ширниа
	uint_fast16_t h,	// высота
	uint_fast8_t color	// цвет
	)
{
	if (w == 0 || h == 0)
		return;
	enum { PIXEL_SIZE = sizeof * buffer };
	enum { PIXEL_SIZE_CODE = 0 };

#if WITHMDMAHW && (CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1)
	// MDMA implementation

	//static ALIGNX_BEGIN volatile uint8_t tgcolor [(DCACHEROWSIZE + sizeof (uint8_t) - 1) / sizeof (uint8_t)] ALIGNX_END;	/* значение цвета для заполнения области памяти */
	//tgcolor [0] = color;
	MDMA_DATA = color;	// регистр выделенного канала MDMA используется для хранения значение цвета. Переиферия не кэшируется.
	(void) MDMA_DATA;

	//arm_hardware_flush((uintptr_t) & tgcolor, sizeof tgcolor);
	arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));

	MDMA_CH->CDAR = (uintptr_t) colmain_mem_at(buffer, dx, dy, col, row); // dest address
	MDMA_CH->CSAR = (uintptr_t) & MDMA_DATA;
	const uint_fast32_t tlen = mdma_tlen(w * PIXEL_SIZE, PIXEL_SIZE);
	const uint_fast32_t sbus = mdma_getbus(MDMA_CH->CSAR);
	const uint_fast32_t dbus = mdma_getbus(MDMA_CH->CDAR);
	const uint_fast32_t sinc = 0x00; // Source increment mode: 00: Source address pointer is fixed
	const uint_fast32_t dinc = 0x02; // Destination increment mode: 10: Destination address pointer is incremented
	const uint_fast32_t sburst = mdma_getburst(tlen, sbus, sinc);
	const uint_fast32_t dburst = mdma_getburst(tlen, dbus, dinc);
	MDMA_CH->CTCR =
		(sinc << MDMA_CTCR_SINC_Pos) | 	// Source increment mode: 00: Source address pointer is fixed
		(PIXEL_SIZE_CODE << MDMA_CTCR_SSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_SINCOS_Pos) |
		(sburst << MDMA_CTCR_SBURST_Pos) |
		(dinc << MDMA_CTCR_DINC_Pos) |	// Destination increment mode: 10: Destination address pointer is incremented
		(PIXEL_SIZE_CODE << MDMA_CTCR_DSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_DINCOS_Pos) |
		(dburst << MDMA_CTCR_DBURST_Pos) |	// Destination burst transfer configuration
		((tlen - 1) << MDMA_CTCR_TLEN_Pos) |		// buffer Transfer Length (number of bytes - 1)
		(0x00uL << MDMA_CTCR_PKE_Pos) |
		(0x00uL << MDMA_CTCR_PAM_Pos) |
		(0x02uL << MDMA_CTCR_TRGM_Pos) |		// Trigger Mode: 10: Each MDMA request (software or hardware) triggers a repeated block transfer (if the block repeat is 0, a single block is transferred)
		(0x01uL << MDMA_CTCR_SWRM_Pos) |		// 1: hardware request are ignored. Transfer is triggered by software writing 1 to the SWRQ bit
		(0x01uL << MDMA_CTCR_BWM_Pos) |
		0;
	MDMA_CH->CBNDTR =
		((PIXEL_SIZE * (w)) << MDMA_CBNDTR_BNDT_Pos) |	// Block Number of data bytes to transfer
		(0x00uL << MDMA_CBNDTR_BRSUM_Pos) |	// Block Repeat Source address Update Mode: 0 - increment
		(0x00uL << MDMA_CBNDTR_BRDUM_Pos) |	// Block Repeat Destination address Update Mode: 0 - increment
		((h - 1) << MDMA_CBNDTR_BRC_Pos) |		// Block Repeat Count
		0;
	MDMA_CH->CBRUR =
		((PIXEL_SIZE * (0)) << MDMA_CBRUR_SUV_Pos) |				// Source address Update Value
		((PIXEL_SIZE * (GXADJ(dx) - w)) << MDMA_CBRUR_DUV_Pos) |		// Destination address Update Value
		0;

	MDMA_CH->CTBR = (MDMA_CH->CTBR & ~ (MDMA_CTBR_SBUS_Msk | MDMA_CTBR_DBUS_Msk)) |
		(sbus << MDMA_CTBR_SBUS_Pos) |
		(dbus << MDMA_CTBR_DBUS_Pos) |
		0;

	mdma_startandwait();

#elif WITHMDMAHW && (CPUSTYPE_T113)
	/* Использование G2D для формирования изображений */
	#warning Implement for CPUSTYPE_T113

#else /* WITHMDMAHW */
	// программная реализация

	const unsigned dxadj = GXADJ(dx);
	uint8_t * tbuffer = colmain_mem_at(buffer, dx, dy, col, row); // dest address
	while (h --)
	{
		memset(tbuffer, color, w);
		tbuffer += dxadj;
	}

#endif /* WITHMDMAHW */
}

#endif /* LCDMODE_PIXELSIZE == 1 */
//
//uint32_t mixer_get_irq(void){
//	uint32_t reg_val = 0;
//
//	reg_val = read_wvalue(G2D_STATUS_REG);
//
//	return reg_val;
//}
//
//uint32_t mixer_get_irq0(void){
//	uint32_t reg_val = 0;
//
//	reg_val = read_wvalue(G2D_CMDQ_STS_REG);
//
//	return reg_val;
//}
//
//uint32_t mixer_clear_init(void){
//
//	G2D_V0->G2D_STATUS_REG, 0x300);
//	G2D_V0->G2D_CONTROL_REG, 0x0);
//
//	return 0;
//}
//
//uint32_t mixer_clear_init0(void){
//
//	G2D_V0->G2D_CMDQ_STS_REG, 0x100);
//	G2D_V0->G2D_CMDQ_CTL_REG, 0x0);
//
//	return 0;
//}

#if LCDMODE_PIXELSIZE == 2
// Функция получает координаты и работает над буфером в горизонтальной ориентации.
static void
hwacc_fillrect_u16(
	uint16_t * __restrict buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t col,	// начальная координата
	uint_fast16_t row,	// начальная координата
	uint_fast16_t w,	// ширниа
	uint_fast16_t h,	// высота
	uint_fast16_t color	// цвет
	)
{
	if (w == 0 || h == 0)
		return;
	enum { PIXEL_SIZE = sizeof * buffer };
	enum { PIXEL_SIZE_CODE = 1 };


#if WITHMDMAHW && (CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1)
	// MDMA implementation

	//static ALIGNX_BEGIN volatile uint16_t tgcolor [DCACHEROWSIZE / sizeof (uint16_t)] ALIGNX_END;	/* значение цвета для заполнения области памяти */
	//tgcolor [0] = color;
	MDMA_DATA = color;	// регистр выделенного канала MDMA используется для хранения значение цвета. Переиферия не кэшируется.
	(void) MDMA_DATA;

	//arm_hardware_flush((uintptr_t) & tgcolor, sizeof tgcolor);
	arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));

	MDMA_CH->CDAR = (uintptr_t) colmain_mem_at(buffer, dx, dy, col, row); // dest address
	MDMA_CH->CSAR = (uintptr_t) & MDMA_DATA;
	const uint_fast32_t tlen = mdma_tlen(w * PIXEL_SIZE, PIXEL_SIZE);
	const uint_fast32_t sbus = mdma_getbus(MDMA_CH->CSAR);
	const uint_fast32_t dbus = mdma_getbus(MDMA_CH->CDAR);
	const uint_fast32_t sinc = 0x00; // Source increment mode: 00: Source address pointer is fixed
	const uint_fast32_t dinc = 0x02; // Destination increment mode: 10: Destination address pointer is incremented
	const uint_fast32_t sburst = mdma_getburst(tlen, sbus, sinc);
	const uint_fast32_t dburst = mdma_getburst(tlen, dbus, dinc);
	MDMA_CH->CTCR =
		(sinc << MDMA_CTCR_SINC_Pos) | 	// Source increment mode: 00: Source address pointer is fixed
		(PIXEL_SIZE_CODE << MDMA_CTCR_SSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_SINCOS_Pos) |
		(sburst << MDMA_CTCR_SBURST_Pos) |
		(dinc << MDMA_CTCR_DINC_Pos) |	// Destination increment mode: 10: Destination address pointer is incremented
		(PIXEL_SIZE_CODE << MDMA_CTCR_DSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_DINCOS_Pos) |
		(dburst << MDMA_CTCR_DBURST_Pos) |	// Destination burst transfer configuration
		((tlen - 1) << MDMA_CTCR_TLEN_Pos) |		// buffer Transfer Length (number of bytes - 1)
		(0x00uL << MDMA_CTCR_PKE_Pos) |
		(0x00uL << MDMA_CTCR_PAM_Pos) |
		(0x02uL << MDMA_CTCR_TRGM_Pos) |		// Trigger Mode: 10: Each MDMA request (software or hardware) triggers a repeated block transfer (if the block repeat is 0, a single block is transferred)
		(0x01uL << MDMA_CTCR_SWRM_Pos) |		// 1: hardware request are ignored. Transfer is triggered by software writing 1 to the SWRQ bit
		(0x01uL << MDMA_CTCR_BWM_Pos) |
		0;
	MDMA_CH->CBNDTR =
		((PIXEL_SIZE * (w)) << MDMA_CBNDTR_BNDT_Pos) |	// Block Number of data bytes to transfer
		(0x00uL << MDMA_CBNDTR_BRSUM_Pos) |	// Block Repeat Source address Update Mode: 0 - increment
		(0x00uL << MDMA_CBNDTR_BRDUM_Pos) |	// Block Repeat Destination address Update Mode: 0 - increment
		((h - 1) << MDMA_CBNDTR_BRC_Pos) |		// Block Repeat Count
		0;
	MDMA_CH->CBRUR =
		((PIXEL_SIZE * (0)) << MDMA_CBRUR_SUV_Pos) |				// Source address Update Value
		((PIXEL_SIZE * (GXADJ(dx) - w)) << MDMA_CBRUR_DUV_Pos) |		// Destination address Update Value
		0;

	MDMA_CH->CTBR = (MDMA_CH->CTBR & ~ (MDMA_CTBR_SBUS_Msk | MDMA_CTBR_DBUS_Msk)) |
		(sbus << MDMA_CTBR_SBUS_Pos) |
		(dbus << MDMA_CTBR_DBUS_Pos) |
		0;

	//PRINTF("MDMA_CH->CDAR=%08X,dbus=%d, MDMA_CH->CSAR=%08X,sbus=%d, tlen=%u, BNDT-%08lX\n", MDMA_CH->CDAR, dbus, MDMA_CH->CSAR, sbus, tlen, (MDMA_CH->CBNDTR & MDMA_CBNDTR_BNDT_Msk) >> MDMA_CBNDTR_BNDT_Pos);
	mdma_startandwait();

#elif WITHDMA2DHW
	// DMA2D implementation

	// just writes the color defined in the DMA2D_OCOLR register
	// to the area located at the address pointed by the DMA2D_OMAR
	// and defined in the DMA2D_NLR and DMA2D_OOR.

	arm_hardware_flush_invalidate((uintptr_t) buffer, sizeof (* buffer) * GXSIZE(dx, dy));

	/* целевой растр */
	DMA2D->OMAR = (uintptr_t) & buffer [row * GXADJ(dx) + col];
	DMA2D->OOR = (DMA2D->OOR & ~ (DMA2D_OOR_LO)) |
		((GXADJ(dx) - w) << DMA2D_OOR_LO_Pos) |
		0;

	DMA2D->NLR = (DMA2D->NLR & ~ (DMA2D_NLR_NL | DMA2D_NLR_PL)) |
		(h << DMA2D_NLR_NL_Pos) |
		(w << DMA2D_NLR_PL_Pos) |
		0;

	DMA2D->OCOLR =
		color |
		0;

	DMA2D->OPFCCR = (DMA2D->OPFCCR & ~ (DMA2D_OPFCCR_CM)) |
		(2 * DMA2D_OPFCCR_CM_0) |	/* 010: RGB565 Color mode - framebuffer pixel format */
		0;

	/* запустить операцию */
	DMA2D->CR = (DMA2D->CR & ~ (DMA2D_CR_MODE)) |
		3 * DMA2D_CR_MODE_0 |	// 11: Register-to-memory (no FG nor BG, only output stage active)
		1 * DMA2D_CR_START |
		0;

	/* ожидаем выполнения операции */
	while ((DMA2D->CR & DMA2D_CR_START) != 0)
		hardware_nonguiyield();
	__DMB();

	ASSERT((DMA2D->ISR & DMA2D_ISR_CEIF) == 0);	// Configuration Error
	ASSERT((DMA2D->ISR & DMA2D_ISR_TEIF) == 0);	// Transfer Error

#elif WITHMDMAHW && (CPUSTYPE_T113)
	/* Использование G2D для формирования изображений */

	if (w == 1)
	{
		/* Горизонтальные линии в 1 пиксель рисовать умеет аппаратура. */
		// программная реализация
		const unsigned t = GXADJ(dx) - w;
		//buffer += (GXADJ(dx) * row) + col;
		volatile uint16_t * tbuffer = colmain_mem_at(buffer, dx, dy, col, row); // dest address
		while (h --)
		{
			unsigned n = w;
			while (n --)
				* tbuffer ++ = color;
			tbuffer += t;
		}
		arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));
		return;
	}
	const unsigned stride = GXADJ(dx) * PIXEL_SIZE;
	//const uintptr_t addr = (uintptr_t) & buffer [row * GXADJ(dx) + col];
	const uintptr_t addr = (uintptr_t) colmain_mem_at(buffer, dx, dy, col, row);
	const uint_fast32_t sizehw = ((h - 1) << 16) | ((w - 1) << 0);
	arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));


	ASSERT((G2D_MIXER->G2D_MIXER_CTL & (1uL << 31)) == 0);

	const uint_fast32_t c24 = COLOR24(COLORMAIN_R(color), COLORMAIN_G(color), COLORMAIN_B(color));

	G2D_V0->V0_PITCH0 = PIXEL_SIZE;//PIXEL_SIZE;	// Y
	G2D_V0->V0_PITCH1 = 0;	// U
	G2D_V0->V0_PITCH2 = 0;	// V

	G2D_V0->V0_HDS_CTL0 = 0;
	G2D_V0->V0_HDS_CTL1 = 0;
	G2D_V0->V0_VDS_CTL0 = 0;
	G2D_V0->V0_VDS_CTL1 = 0;

	G2D_V0->V0_COOR = 0;
	G2D_V0->V0_MBSIZE = sizehw;
	G2D_V0->V0_SIZE = sizehw;

	G2D_BLD->BLD_BK_COLOR = c24;	/* всегда RGB888. этим цветом заполняется */
	G2D_WB->WB_ATT = G2D_FMT_RGB565;//G2D_FMT_RGB565; //G2D_FMT_XRGB8888;
	//G2D_WB->WB_ATT = G2D_FMT_XRGB8888;//G2D_FMT_RGB565; //G2D_FMT_XRGB8888;
	G2D_WB->WB_SIZE = sizehw;	/* расположение компонент размера проверено */
	G2D_WB->WB_PITCH0 = stride;
	G2D_WB->WB_LADD0 = addr;
	G2D_WB->WB_HADD0 = 0;

	//printhex(G2D_BLD, G2D_BLD, sizeof * G2D_BLD);
	G2D_BLD->BLD_SIZE = sizehw;
	G2D_BLD->BLD_CH_ISIZE0 = sizehw;
	G2D_BLD->BLD_CH_OFFSET0 = 0;// ((row) << 16) | ((col) << 0);

	G2D_MIXER->G2D_MIXER_CTL |= (1uL << 31);	/* start the module */
	for (;;)
	{
		const uint_fast32_t sts = G2D_MIXER->G2D_MIXER_INT;
		G2D_MIXER->G2D_MIXER_INT = sts;
		if (((sts & (1uL << 0)) != 0))
			break;
		hardware_nonguiyield();
	}
	ASSERT((G2D_MIXER->G2D_MIXER_CTL & (1uL << 31)) == 0);

#else /* WITHMDMAHW, WITHDMA2DHW */
	// программная реализация
	const unsigned t = GXADJ(dx) - w;
	//buffer += (GXADJ(dx) * row) + col;
	volatile uint16_t * tbuffer = colmain_mem_at(buffer, dx, dy, col, row); // dest address
	while (h --)
	{
		unsigned n = w;
		while (n --)
			* tbuffer ++ = color;
		tbuffer += t;
	}

#endif /* WITHMDMAHW, WITHDMA2DHW */
}

#endif /* LCDMODE_PIXELSIZE == 2 */

#if LCDMODE_PIXELSIZE == 3
// Функция получает координаты и работает над буфером в горизонтальной ориентации.
static void
hwacc_fillrect_u24(
	PACKEDCOLORMAIN_T * __restrict buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t col,	// начальная координата
	uint_fast16_t row,	// начальная координата
	uint_fast16_t w,	// ширниа
	uint_fast16_t h,	// высота
	uint_fast32_t color	// цвет
	)
{
	if (w == 0 || h == 0)
		return;
	enum { PIXEL_SIZE = sizeof * buffer };
	//enum { PIXEL_SIZE_CODE = 1 };

	ASSERT(sizeof (* buffer) == 3);

#if 0 && WITHMDMAHW && (CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1)
	// MDMA implementation

	//static ALIGNX_BEGIN volatile PACKEDCOLORMAIN_T tgcolor [(DCACHEROWSIZE + sizeof (PACKEDCOLORMAIN_T) - 1) / sizeof (PACKEDCOLORMAIN_T)] ALIGNX_END;	/* значение цвета для заполнения области памяти */
	//tgcolor [0] = color;
	MDMA_DATA = color;	// регистр выделенного канала MDMA используется для хранения значение цвета. Переиферия не кэшируется.
	(void) MDMA_DATA;
	#error MDMA implementation need

	//arm_hardware_flush((uintptr_t) & tgcolor, sizeof tgcolor);
	arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));

	MDMA_CH->CDAR = (uintptr_t) colmain_mem_at(buffer, dx, dy, col, row); // dest address
	MDMA_CH->CSAR = (uintptr_t) & MDMA_DATA;
	const uint_fast32_t tlen = mdma_tlen(w * PIXEL_SIZE, PIXEL_SIZE);
	const uint_fast32_t sbus = mdma_getbus(MDMA_CH->CSAR);
	const uint_fast32_t dbus = mdma_getbus(MDMA_CH->CDAR);
	const uint_fast32_t sinc = 0x00; // Source increment mode: 00: Source address pointer is fixed
	const uint_fast32_t dinc = 0x02; // Destination increment mode: 10: Destination address pointer is incremented
	const uint_fast32_t sburst = mdma_getburst(tlen, sbus, sinc);
	const uint_fast32_t dburst = mdma_getburst(tlen, dbus, dinc);
	MDMA_CH->CTCR =
		(sinc << MDMA_CTCR_SINC_Pos) | 	// Source increment mode: 00: Source address pointer is fixed
		(PIXEL_SIZE_CODE << MDMA_CTCR_SSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_SINCOS_Pos) |
		(sburst << MDMA_CTCR_SBURST_Pos) |
		(dinc << MDMA_CTCR_DINC_Pos) |	// Destination increment mode: 10: Destination address pointer is incremented
		(PIXEL_SIZE_CODE << MDMA_CTCR_DSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_DINCOS_Pos) |
		(dburst << MDMA_CTCR_DBURST_Pos) |	// Destination burst transfer configuration
		((tlen - 1) << MDMA_CTCR_TLEN_Pos) |		// buffer Transfer Length (number of bytes - 1)
		(0x00uL << MDMA_CTCR_PKE_Pos) |
		(0x00uL << MDMA_CTCR_PAM_Pos) |
		(0x02uL << MDMA_CTCR_TRGM_Pos) |		// Trigger Mode: 10: Each MDMA request (software or hardware) triggers a repeated block transfer (if the block repeat is 0, a single block is transferred)
		(0x01uL << MDMA_CTCR_SWRM_Pos) |		// 1: hardware request are ignored. Transfer is triggered by software writing 1 to the SWRQ bit
		(0x01uL << MDMA_CTCR_BWM_Pos) |
		0;
	MDMA_CH->CBNDTR =
		((PIXEL_SIZE * (w)) << MDMA_CBNDTR_BNDT_Pos) |	// Block Number of data bytes to transfer
		(0x00uL << MDMA_CBNDTR_BRSUM_Pos) |	// Block Repeat Source address Update Mode: 0 - increment
		(0x00uL << MDMA_CBNDTR_BRDUM_Pos) |	// Block Repeat Destination address Update Mode: 0 - increment
		((h - 1) << MDMA_CBNDTR_BRC_Pos) |		// Block Repeat Count
		0;
	MDMA_CH->CBRUR =
		((PIXEL_SIZE * (0)) << MDMA_CBRUR_SUV_Pos) |				// Source address Update Value
		((PIXEL_SIZE * (GXADJ(dx) - w)) << MDMA_CBRUR_DUV_Pos) |		// Destination address Update Value
		0;

	MDMA_CH->CTBR = (MDMA_CH->CTBR & ~ (MDMA_CTBR_SBUS_Msk | MDMA_CTBR_DBUS_Msk)) |
		(sbus << MDMA_CTBR_SBUS_Pos) |
		(dbus << MDMA_CTBR_DBUS_Pos) |
		0;

	mdma_startandwait();

#elif WITHDMA2DHW
	// DMA2D implementation

	// just writes the color defined in the DMA2D_OCOLR register
	// to the area located at the address pointed by the DMA2D_OMAR
	// and defined in the DMA2D_NLR and DMA2D_OOR.

	arm_hardware_flush_invalidate((uintptr_t) buffer, sizeof (* buffer) * GXSIZE(dx, dy));

	/* целевой растр */
	DMA2D->OMAR = (uintptr_t) & buffer [row * GXADJ(dx) + col];
	DMA2D->OOR = (DMA2D->OOR & ~ (DMA2D_OOR_LO)) |
		((GXADJ(dx) - w) << DMA2D_OOR_LO_Pos) |
		0;

	DMA2D->NLR = (DMA2D->NLR & ~ (DMA2D_NLR_NL | DMA2D_NLR_PL)) |
		(h << DMA2D_NLR_NL_Pos) |
		(w << DMA2D_NLR_PL_Pos) |
		0;

	DMA2D->OCOLR =
		color |
		0;

	DMA2D->OPFCCR = (DMA2D->OPFCCR & ~ (DMA2D_OPFCCR_CM)) |
		(1 * DMA2D_OPFCCR_CM_0) |	/* 001: RGB888 Color mode - framebuffer pixel format */
		0;

	/* запустить операцию */
	DMA2D->CR = (DMA2D->CR & ~ (DMA2D_CR_MODE)) |
		3 * DMA2D_CR_MODE_0 |	// 11: Register-to-memory (no FG nor BG, only output stage active)
		1 * DMA2D_CR_START |
		0;

	/* ожидаем выполнения операции */
	while ((DMA2D->CR & DMA2D_CR_START) != 0)
		hardware_nonguiyield();
	__DMB();

	ASSERT((DMA2D->ISR & DMA2D_ISR_CEIF) == 0);	// Configuration Error
	ASSERT((DMA2D->ISR & DMA2D_ISR_TEIF) == 0);	// Transfer Error

#elif WITHMDMAHW && (CPUSTYPE_T113)
	/* Использование G2D для формирования изображений */
	#warinig Implement for CPUSTYPE_T113
	const unsigned stride = GXADJ(dx);
	(void) G2D;

#else
	// программная реализация

	const unsigned t = GXADJ(dx) - w;
	//buffer += (GXADJ(dx) * row) + col;
	volatile PACKEDCOLORMAIN_T * tbuffer = colmain_mem_at(buffer, dx, dy, col, row); // dest address
	while (h --)
	{
		//PACKEDCOLORMAIN_T * const startmem = buffer;

		unsigned n = w;
		while (n --)
			* tbuffer ++ = color;
		tbuffer += t;
	}

#endif
}

#endif /* LCDMODE_PIXELSIZE == 3 */

#if LCDMODE_PIXELSIZE == 4
// Функция получает координаты и работает над буфером в горизонтальной ориентации.
static void
hwacc_fillrect_u32(
	uint32_t * __restrict buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t col,	// начальная координата
	uint_fast16_t row,	// начальная координата
	uint_fast16_t w,	// ширниа
	uint_fast16_t h,	// высота
	uint_fast32_t color	// цвет
	)
{
	if (w == 0 || h == 0)
		return;
	enum { PIXEL_SIZE = sizeof * buffer };
	enum { PIXEL_SIZE_CODE = 2 };	// word (32-bit)

#if WITHMDMAHW && (CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1)
	// MDMA implementation

	//static ALIGNX_BEGIN volatile uint32_t tgcolor [(DCACHEROWSIZE + sizeof (uint32_t) - 1) / sizeof (uint32_t)] ALIGNX_END;	/* значение цвета для заполнения области памяти */
	//tgcolor [0] = color;
	MDMA_DATA = color;	// регистр выделенного канала MDMA используется для хранения значение цвета. Переиферия не кэшируется.
	(void) MDMA_DATA;

	//arm_hardware_flush((uintptr_t) & tgcolor, sizeof tgcolor);
	arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));

	MDMA_CH->CDAR = (uintptr_t) colmain_mem_at(buffer, dx, dy, col, row); // dest address
	MDMA_CH->CSAR = (uintptr_t) & MDMA_DATA;
	const uint_fast32_t tlen = mdma_tlen(w * PIXEL_SIZE, PIXEL_SIZE);
	const uint_fast32_t sbus = mdma_getbus(MDMA_CH->CSAR);
	const uint_fast32_t dbus = mdma_getbus(MDMA_CH->CDAR);
	const uint_fast32_t sinc = 0x00; // Source increment mode: 00: Source address pointer is fixed
	const uint_fast32_t dinc = 0x02; // Destination increment mode: 10: Destination address pointer is incremented
	const uint_fast32_t sburst = mdma_getburst(tlen, sbus, sinc);
	const uint_fast32_t dburst = mdma_getburst(tlen, dbus, dinc);
	MDMA_CH->CTCR =
		(sinc << MDMA_CTCR_SINC_Pos) | 	// Source increment mode: 00: Source address pointer is fixed
		(PIXEL_SIZE_CODE << MDMA_CTCR_SSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_SINCOS_Pos) |
		(sburst << MDMA_CTCR_SBURST_Pos) |
		(dinc << MDMA_CTCR_DINC_Pos) |	// Destination increment mode: 10: Destination address pointer is incremented
		(PIXEL_SIZE_CODE << MDMA_CTCR_DSIZE_Pos) |
		(PIXEL_SIZE_CODE << MDMA_CTCR_DINCOS_Pos) |
		(dburst << MDMA_CTCR_DBURST_Pos) |	// Destination burst transfer configuration
		((tlen - 1) << MDMA_CTCR_TLEN_Pos) |		// buffer Transfer Length (number of bytes - 1)
		(0x00uL << MDMA_CTCR_PKE_Pos) |
		(0x00uL << MDMA_CTCR_PAM_Pos) |
		(0x02uL << MDMA_CTCR_TRGM_Pos) |		// Trigger Mode: 10: Each MDMA request (software or hardware) triggers a repeated block transfer (if the block repeat is 0, a single block is transferred)
		(0x01uL << MDMA_CTCR_SWRM_Pos) |		// 1: hardware request are ignored. Transfer is triggered by software writing 1 to the SWRQ bit
		(0x01uL << MDMA_CTCR_BWM_Pos) |
		0;
	MDMA_CH->CBNDTR =
		((PIXEL_SIZE * (w)) << MDMA_CBNDTR_BNDT_Pos) |	// Block Number of data bytes to transfer
		(0x00uL << MDMA_CBNDTR_BRSUM_Pos) |	// Block Repeat Source address Update Mode: 0 - increment
		(0x00uL << MDMA_CBNDTR_BRDUM_Pos) |	// Block Repeat Destination address Update Mode: 0 - increment
		((h - 1) << MDMA_CBNDTR_BRC_Pos) |		// Block Repeat Count
		0;
	MDMA_CH->CBRUR =
		((PIXEL_SIZE * (0)) << MDMA_CBRUR_SUV_Pos) |				// Source address Update Value
		((PIXEL_SIZE * (GXADJ(dx) - w)) << MDMA_CBRUR_DUV_Pos) |		// Destination address Update Value
		0;

	MDMA_CH->CTBR = (MDMA_CH->CTBR & ~ (MDMA_CTBR_SBUS_Msk | MDMA_CTBR_DBUS_Msk)) |
		(sbus << MDMA_CTBR_SBUS_Pos) |
		(dbus << MDMA_CTBR_DBUS_Pos) |
		0;

	mdma_startandwait();

#elif WITHDMA2DHW
	// DMA2D implementation

	// just writes the color defined in the DMA2D_OCOLR register
	// to the area located at the address pointed by the DMA2D_OMAR
	// and defined in the DMA2D_NLR and DMA2D_OOR.

	arm_hardware_flush_invalidate((uintptr_t) buffer, sizeof (* buffer) * GXSIZE(dx, dy));

	/* целевой растр */
	DMA2D->OMAR = (uintptr_t) & buffer [row * GXADJ(dx) + col];
	DMA2D->OOR = (DMA2D->OOR & ~ (DMA2D_OOR_LO)) |
		((GXADJ(dx) - w) << DMA2D_OOR_LO_Pos) |
		0;

	DMA2D->NLR = (DMA2D->NLR & ~ (DMA2D_NLR_NL | DMA2D_NLR_PL)) |
		(h << DMA2D_NLR_NL_Pos) |
		(w << DMA2D_NLR_PL_Pos) |
		0;

	DMA2D->OCOLR =
		color |
		0;

	DMA2D->OPFCCR = (DMA2D->OPFCCR & ~ (DMA2D_OPFCCR_CM)) |
		(2 * DMA2D_OPFCCR_CM_0) |	/* 010: RGB565 Color mode - framebuffer pixel format */
		0;

	/* запустить операцию */
	DMA2D->CR = (DMA2D->CR & ~ (DMA2D_CR_MODE)) |
		3 * DMA2D_CR_MODE_0 |	// 11: Register-to-memory (no FG nor BG, only output stage active)
		1 * DMA2D_CR_START |
		0;

	/* ожидаем выполнения операции */
	while ((DMA2D->CR & DMA2D_CR_START) != 0)
		hardware_nonguiyield();
	__DMB();

	ASSERT((DMA2D->ISR & DMA2D_ISR_CEIF) == 0);	// Configuration Error
	ASSERT((DMA2D->ISR & DMA2D_ISR_TEIF) == 0);	// Transfer Error

#elif WITHMDMAHW && (CPUSTYPE_T113)
	/* Использование G2D для формирования изображений */

	if (w == 1)
	{
		/* Горизонтальные линии в 1 пиксель рисовать умеет аппаратура. */
		// программная реализация
		const unsigned t = GXADJ(dx) - w;
		//buffer += (GXADJ(dx) * row) + col;
		volatile uint32_t * tbuffer = colmain_mem_at(buffer, dx, dy, col, row); // dest address
		while (h --)
		{
			unsigned n = w;
			while (n --)
				* tbuffer ++ = color;
			tbuffer += t;
		}
		arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));
		return;
	}
	const unsigned stride = GXADJ(dx) * PIXEL_SIZE;
	//const uintptr_t addr = (uintptr_t) & buffer [row * GXADJ(dx) + col];
	const uintptr_t addr = (uintptr_t) colmain_mem_at(buffer, dx, dy, col, row);
	const uint_fast32_t sizehw = ((h - 1) << 16) | ((w - 1) << 0);
	arm_hardware_flush_invalidate((uintptr_t) buffer, PIXEL_SIZE * GXSIZE(dx, dy));


	ASSERT((G2D_MIXER->G2D_MIXER_CTL & (1uL << 31)) == 0);

	G2D_V0->V0_PITCH0 = PIXEL_SIZE;//PIXEL_SIZE;	// Y
	G2D_V0->V0_PITCH1 = 0;	// U
	G2D_V0->V0_PITCH2 = 0;	// V

	G2D_V0->V0_HDS_CTL0 = 0;
	G2D_V0->V0_HDS_CTL1 = 0;
	G2D_V0->V0_VDS_CTL0 = 0;
	G2D_V0->V0_VDS_CTL1 = 0;

	G2D_V0->V0_COOR = 0;
	G2D_V0->V0_MBSIZE = sizehw;
	G2D_V0->V0_SIZE = sizehw;

	G2D_BLD->BLD_BK_COLOR = color;	/* всегда RGB888. этим цветом заполняется */
	//G2D_WB->WB_ATT = G2D_FMT_RGB565;//G2D_FMT_RGB565; //G2D_FMT_XRGB8888;
	G2D_WB->WB_ATT = G2D_FMT_XRGB8888;//G2D_FMT_RGB565; //G2D_FMT_XRGB8888;
	G2D_WB->WB_SIZE = sizehw;	/* расположение компонент размера проверено */
	G2D_WB->WB_PITCH0 = stride;
	G2D_WB->WB_LADD0 = addr;
	G2D_WB->WB_HADD0 = 0;

	//printhex(G2D_BLD, G2D_BLD, sizeof * G2D_BLD);
	G2D_BLD->BLD_SIZE = sizehw;
	G2D_BLD->BLD_CH_ISIZE0 = sizehw;
	G2D_BLD->BLD_CH_OFFSET0 = 0;// ((row) << 16) | ((col) << 0);

	G2D_MIXER->G2D_MIXER_CTL |= (1uL << 31);	/* start the module */
	for (;;)
	{
		const uint_fast32_t sts = G2D_MIXER->G2D_MIXER_INT;
		G2D_MIXER->G2D_MIXER_INT = sts;
		if (((sts & (1uL << 0)) != 0))
			break;
		hardware_nonguiyield();
	}
	ASSERT((G2D_MIXER->G2D_MIXER_CTL & (1uL << 31)) == 0);

#else /* WITHMDMAHW, WITHDMA2DHW */
	// программная реализация
	const unsigned t = GXADJ(dx) - w;
	//buffer += (GXADJ(dx) * row) + col;
	volatile uint32_t * tbuffer = colmain_mem_at(buffer, dx, dy, col, row); // dest address
	while (h --)
	{
		unsigned n = w;
		while (n --)
			* tbuffer ++ = color;
		tbuffer += t;
	}

#endif /* WITHMDMAHW, WITHDMA2DHW */
}

#endif /* LCDMODE_PIXELSIZE == 4 */

// получить адрес требуемой позиции в буфере
PACKEDCOLORMAIN_T *
colmain_mem_at_debug(
	PACKEDCOLORMAIN_T * __restrict buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	const char * file,
	int line
	)
{
	if (x >= dx || y >= dy)
	{
		PRINTF("colmain_mem_at(%s/%d): dx=%u, dy=%u, x=%d, y=%d, savestring='%s', savewhere='%s'\n", file, line, dx, dy, x, y, savestring, savewhere);
	}
	ASSERT(x < dx);
	ASSERT(y < dy);
#if LCDMODE_HORFILL
	return & buffer [y * GXADJ(dx) + x];
#else /* LCDMODE_HORFILL */
	return & buffer [y * GXADJ(dx) + x];
#endif /* LCDMODE_HORFILL */
}


/// Нарисовать вертикальную цветную полосу
// Формат RGB565
void
display_colorbuf_xor_vline(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t col,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t row0,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	uint_fast16_t h,	// высота
	COLORPIP_T color
	)
{
	ASSERT(row0 < dy);
	ASSERT((row0 + h) <= dy);
	while (h --)
		colpip_point_xor(buffer, dx, dy, col, row0 ++, color);
}

// Нарисовать вертикальную цветную полосу
// Формат RGB565
void
display_colorbuf_set_vline(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t col,	// горизонтальная координата начального пикселя (0..dx-1) слева направо
	uint_fast16_t row0,	// вертикальная координата начального пикселя (0..dy-1) сверху вниз
	uint_fast16_t h,	// высота
	COLORPIP_T color
	)
{
	ASSERT(row0 < dy);
	ASSERT((row0 + h) <= dy);
	/* рисуем прямоугольник шириной в 1 пиксель */
	//colmain_fillrect(buffer, dx, dy, col, row0, 1, h, color);
	while (h --)
		colpip_point(buffer, dx, dy, col, row0 ++, color);
}

// Нарисовать горизонтальную цветную полосу
// Формат RGB565
void
display_colorbuf_set_hline(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t col0,	// горизонтальная координата начального пикселя (0..dx-1) слева направо
	uint_fast16_t row0,	// вертикальная координата начального пикселя (0..dy-1) сверху вниз
	uint_fast16_t w,	// ширина
	COLORPIP_T color
	)
{
	ASSERT(row0 < dy);
	ASSERT((col0 + w) <= dx);
	/* рисуем прямоугольник высотой в 1 пиксель */
	//colmain_fillrect(buffer, dx, dy, col0, row0, w, 1, color);
	while (w --)
		colpip_point(buffer, dx, dy, col0 ++, row0, color);
}

// заполнение прямоугольной области в видеобуфере
void colpip_fillrect(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t x,	// начальная координата
	uint_fast16_t y,	// начальная координата
	uint_fast16_t w,	// ширниа
	uint_fast16_t h,	// высота
	COLORPIP_T color	// цвет
	)
{
	ASSERT(x < dx);
	ASSERT((x + w) <= dx);
	ASSERT(y < dy);
	ASSERT((y + h) <= dy);

#if LCDMODE_HORFILL

	#if LCDMODE_PIP_L8
		hwacc_fillrect_u8(buffer, dx, dy, x, y, w, h, color);

	#elif LCDMODE_PIP_RGB565
		hwacc_fillrect_u16(buffer, dx, dy, x, y, w, h, color);

	#elif LCDMODE_PIP_L24
		hwacc_fillrect_u24(buffer, dx, dy, x, y, w, h, color);
	#elif LCDMODE_MAIN_L8
		hwacc_fillrect_u8(buffer, dx, dy, x, y, w, h, color);

	#elif LCDMODE_MAIN_RGB565
		hwacc_fillrect_u16(buffer, dx, dy, x, y, w, h, color);

	#elif LCDMODE_MAIN_L24
		hwacc_fillrect_u24(buffer, dx, dy, x, y, w, h, color);

	#elif LCDMODE_MAIN_ARGB888
		hwacc_fillrect_u32(buffer, dx, dy, x, y, w, h, color);

	#endif

#else /* LCDMODE_HORFILL */

	#if LCDMODE_PIP_L8
		hwacc_fillrect_u8(buffer, dy, dx, y, x, h, w, color);

	#elif LCDMODE_PIP_RGB565
		hwacc_fillrect_u16(buffer, dy, dx, y, x, h, w, color);

	#elif LCDMODE_PIP_L24
		hwacc_fillrect_u24((buffer, dy, dx, y, x, h, w, color);

	#elif LCDMODE_MAIN_L8
		hwacc_fillrect_u8(buffer, dy, dx, y, x, h, w, color);

	#elif LCDMODE_MAIN_RGB565
		hwacc_fillrect_u16(buffer, dy, dx, y, x, h, w, color);

	#elif LCDMODE_MAIN_L24
		hwacc_fillrect_u24((buffer, dy, dx, y, x, h, w, color);

	#elif LCDMODE_MAIN_ARGB888
		hwacc_fillrect_u32((buffer, dy, dx, y, x, h, w, color);

	#endif

#endif /* LCDMODE_HORFILL */
}



void colmain_putpixel(
	PACKEDCOLORMAIN_T * buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	COLORMAIN_T color
	)
{
	ASSERT(x < dx);
	ASSERT(y < dy);
	PACKEDCOLORMAIN_T * const tgr = colmain_mem_at(buffer, dx, dy, x, y);
	#if LCDMODE_LTDC_L24
		tgr->r = color >> 16;
		tgr->g = color >> 8;
		tgr->b = color >> 0;
	#else /* LCDMODE_LTDC_L24 */
		* tgr = color;
	#endif /* LCDMODE_LTDC_L24 */
}

/*-----------------------------------------------------  V_Bre
 * void V_Bre (int xn, int yn, int xk, int yk)
 *
 * Подпрограмма иллюстрирующая построение вектора из точки
 * (xn,yn) в точку (xk, yk) методом Брезенхема.
 *
 * Построение ведется от точки с меньшими  координатами
 * к точке с большими координатами с единичным шагом по
 * координате с большим приращением.
 *
 * В общем случае исходный вектор проходит не через вершины
 * растровой сетки, а пересекает ее стороны.
 * Пусть приращение по X больше приращения по Y и оба они > 0.
 * Для очередного значения X нужно выбрать одну двух ближайших
 * координат сетки по Y.
 * Для этого проверяется как проходит  исходный  вектор - выше
 * или ниже середины расстояния между ближайшими значениями Y.
 * Если выше середины,  то Y-координату  надо  увеличить на 1,
 * иначе оставить прежней.
 * Для этой проверки анализируется знак переменной s,
 * соответствующей разности между истинным положением и
 * серединой расстояния между ближайшими Y-узлами сетки.
 */

void colmain_line(
	PACKEDCOLORMAIN_T * buffer,
	const uint_fast16_t bx,	// ширина буфера
	const uint_fast16_t by,	// высота буфера
	int xn, int yn,
	int xk, int yk,
	COLORMAIN_T color,
	int antialiasing
	)
{
	ASSERT(xn < bx);
	ASSERT(xk < bx);
	ASSERT(yn < by);
	ASSERT(yn < by);
	int  dx, dy, s, sx, sy, kl, incr1, incr2;
	char swap;
	const COLORMAIN_T sc = getshadedcolor(color, DEFAULT_ALPHA);

	/* Вычисление приращений и шагов */
	if ((dx = xk - xn) < 0)
	{
		dx = - dx;
		sx = - 1;
	}
	else if (dx > 0)
		sx = + 1;
	else
		sx = 0;

	if ((dy = yk - yn) < 0)
	{
		dy = - dy;
		sy = - 1;
	}
	else if (dy > 0)
		sy = + 1;
	else
		sy = 0;

	/* Учет наклона */
	if ((kl = dx) < (s = dy))
	{
		dx = s;  dy = kl;  kl = s; swap = 1;
	}
	else
	{
		swap = 0;
	}

	s = (incr1 = 2 * dy) - dx; /* incr1 - констан. перевычисления */
	/* разности если текущее s < 0  и  */
	/* s - начальное значение разности */
	incr2 = 2 * dx;         /* Константа для перевычисления    */
	/* разности если текущее s >= 0    */
	colmain_putpixel(buffer, bx, by, xn, yn, color); /* Первый  пиксел вектора       */

	/*static */ uint_fast16_t xold, yold;
	xold = xn;
	yold = yn;
	while (-- kl >= 0)
	{
		if (s >= 0)
		{
			if (swap)
				xn += sx;
			else
				yn += sy;
			s-= incr2;
		}
		if (swap)
			yn += sy;
		else
			xn += sx;
		s += incr1;

		colmain_putpixel(buffer, bx, by, xn, yn, color); /* Текущая  точка  вектора   */

		if (antialiasing)
		{
			if (((xold == xn - 1) || (xold == xn + 1)) && ((yold == yn - 1) || (yold == yn + 1)))
			{
				if (color != * colmain_mem_at(buffer, bx, by, xn, yold))
					colmain_putpixel(buffer, bx, by, xn, yold, sc);

				if (color != * colmain_mem_at(buffer, bx, by, xold, yn))
					colmain_putpixel(buffer, bx, by, xold, yn, sc);
//				colmain_putpixel(buffer, bx, by, xn, yn, sc);		// нужны дополнительные цвета для этих 2х точек
//				colmain_putpixel(buffer, bx, by, xold, yold, sc);
			}
			xold = xn;
			yold = yn;
		}
	}
}  /* V_Bre */


/* заливка замкнутого контура */
void display_floodfill(
	PACKEDCOLORMAIN_T * buffer,
	uint_fast16_t dx,	// ширина буфера
	uint_fast16_t dy,	// высота буфера
	uint_fast16_t x,	// начальная координата
	uint_fast16_t y,	// начальная координата
	COLORMAIN_T newColor,
	COLORMAIN_T oldColor,
	uint_fast8_t type	// 0 - быстрая закраска (только выпуклый контур), 1 - медленная закраска любого контура
	)
{
	ASSERT(x < dx);
	ASSERT(y < dy);
	PACKEDCOLORMAIN_T * tgr = colmain_mem_at(buffer, dx, dy, x, y);

	if (type) 	// медленная закраска любого контура
	{

		if (* tgr == oldColor && * tgr != newColor)
		{
			* tgr = newColor;
			display_floodfill(buffer, dx, dy, x + 1, y, newColor, oldColor, 1);
			display_floodfill(buffer, dx, dy, x - 1, y, newColor, oldColor, 1);
			display_floodfill(buffer, dx, dy, x, y + 1, newColor, oldColor, 1);
			display_floodfill(buffer, dx, dy, x, y - 1, newColor, oldColor, 1);
		}
	}
	else 		// быстрая закраска (только выпуклый контур)
	{
		uint_fast16_t y0 = y, x_l = x, x_p = x;

		while(* tgr != newColor)		// поиск первой строки в контуре для закраски
		{
			tgr = colmain_mem_at(buffer, dx, dy, x, --y0);
		}
		y0++;

		do
		{
			x_l = x;		// добавить проверку на необходимость поиска новых границ
			x_p = x;

			// поиск левой границы строки
			do
			{
				tgr = colmain_mem_at(buffer, dx, dy, --x_l, y0);
			} while(* tgr != newColor);

			// поиск правой границы строки
			do
			{
				tgr = colmain_mem_at(buffer, dx, dy, ++x_p, y0);
			} while(* tgr != newColor);

			// закраска найденной линии
			colmain_line(buffer, dx, dy, x_l, y0, x_p, y0, newColor, 0);

			// переход на следующую строку
			tgr = colmain_mem_at(buffer, dx, dy, x, ++y0);
		} while(* tgr != newColor);
	}
}

// Заполнение буфера сполшным цветом
// Эта функция используется только в тесте
void colpip_fill(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	COLORPIP_T color
	)
{
#if LCDMODE_HORFILL

	#if LCDMODE_PIP_L8
		hwacc_fillrect_u8(buffer, dx, dy, 0, 0, dx, dy, color);

	#elif LCDMODE_PIP_RGB565
		hwacc_fillrect_u16(buffer, dx, dy, 0, 0, dx, dy, color);

	#elif LCDMODE_PIP_L24
		hwacc_fillrect_u24(buffer, dx, dy, 0, 0, dx, dy, color);

	#elif LCDMODE_MAIN_L8
		hwacc_fillrect_u8(buffer, dx, dy, 0, 0, dx, dy, color);

	#elif LCDMODE_MAIN_RGB565
		hwacc_fillrect_u16(buffer, dx, dy, 0, 0, dx, dy, color);

	#elif LCDMODE_MAIN_L24
		hwacc_fillrect_u24(buffer, dx, dy, 0, 0, dx, dy, color);

	#elif LCDMODE_MAIN_ARGB888
		hwacc_fillrect_u32(buffer, dx, dy, 0, 0, dx, dy, color);

	#endif

#else /* LCDMODE_HORFILL */

	#if LCDMODE_PIP_L8
		hwacc_fillrect_u8(buffer, dy, dx, 0, 0, dy, dx, color);

	#elif LCDMODE_PIP_RGB565
		hwacc_fillrect_u16(buffer, dy, dx, 0, 0, dy, dx, color);

	#elif LCDMODE_PIP_L24
		hwacc_fillrect_u24(buffer, dy, dx, 0, 0, dy, dx, color);

	#elif LCDMODE_MAIN_L8
		hwacc_fillrect_u8(buffer, dy, dx, 0, 0, dy, dx, color);

	#elif LCDMODE_MAIN_RGB565
		hwacc_fillrect_u16(buffer, dy, dx, 0, 0, dy, dx, color);

	#elif LCDMODE_MAIN_L24
		hwacc_fillrect_u24(buffer, dy, dx, 0, 0, dy, dx, color);

	#elif LCDMODE_MAIN_ARGB888
		hwacc_fillrect_u32(buffer, dy, dx, 0, 0, dy, dx, color);

	#endif

#endif /* LCDMODE_HORFILL */
}

// Заполнение буфера сполшным цветом
// Эта функция используется только в тесте
void gtg_fill(const GTG_t * gtg, COLORPIP_T color)
{
	colpip_fill(gtg->buffer, gtg->dx, gtg->dy, color);
}


// поставить цветную точку.
void colpip_point(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t col,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t row,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	COLORPIP_T color
	)
{
	* colmain_mem_at(buffer, dx, dy, col, row) = color;
}

// поставить цветную точку.
void gtg_point(
	const GTG_t * gtg,
	uint_fast16_t col,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t row,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	COLORPIP_T color
	)
{
	* colmain_mem_at(gtg->buffer, gtg->dx, gtg->dy, col, row) = color;
}

// поставить цветную точку (модификация с сохранением старого изоьражения).
void colpip_point_xor(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t col,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t row,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	COLORPIP_T color
	)
{
	* colmain_mem_at(buffer, dx, dy, col, row) ^= color;
}


// копирование в большее или равное окно
// размер пикселя - определяется конфигурацией.
// MDMA, DMA2D или программа
// для случая когда горизонтальные пиксели в видеопямяти располагаются подряд
void hwaccel_copy(
	uintptr_t dstinvalidateaddr,	// параметры invalidate получателя
	int_fast32_t dstinvalidatesize,
	PACKEDCOLORMAIN_T * __restrict dst,
	uint_fast16_t tdx,	// ширина буфера
	uint_fast16_t tdy,	// высота буфера
	uintptr_t srcinvalidateaddr,	// параметры clean источника
	int_fast32_t srcinvalidatesize,
	const PACKEDCOLORMAIN_T * __restrict src,
	uint_fast16_t sdx,	// ширина буфера
	uint_fast16_t sdy	// высота буфера
	)
{
	if (sdx == 0 || sdy == 0)
		return;

#if WITHMDMAHW && (CPUSTYLE_STM32H7XX || CPUSTYLE_STM32MP1)
	// MDMA реализация

	arm_hardware_flush_invalidate(dstinvalidateaddr, dstinvalidatesize);
	arm_hardware_flush(srcinvalidateaddr, srcinvalidatesize);

	MDMA_CH->CDAR = (uintptr_t) dst;
	MDMA_CH->CSAR = (uintptr_t) src;
	const uint_fast32_t tlen = mdma_tlen(sdx * sizeof (PACKEDCOLORMAIN_T), sizeof (PACKEDCOLORMAIN_T));
	const uint_fast32_t sbus = mdma_getbus(MDMA_CH->CSAR);
	const uint_fast32_t dbus = mdma_getbus(MDMA_CH->CDAR);
	const uint_fast32_t sinc = 0x02; // Source increment mode: 10: address pointer is incremented
	const uint_fast32_t dinc = 0x02; // Destination increment mode: 10: Destination address pointer is incremented
	const uint_fast32_t sburst = mdma_getburst(tlen, sbus, sinc);
	const uint_fast32_t dburst = mdma_getburst(tlen, dbus, dinc);
	MDMA_CH->CTCR =
		(sinc << MDMA_CTCR_SINC_Pos) | 	// Source increment mode: 10: address pointer is incremented
		(MDMA_CTCR_xSIZE_MAIN << MDMA_CTCR_SSIZE_Pos) |
		(MDMA_CTCR_xSIZE_MAIN << MDMA_CTCR_SINCOS_Pos) |
		(sburst << MDMA_CTCR_SBURST_Pos) |
		(dinc << MDMA_CTCR_DINC_Pos) |	// Destination increment mode: 10: Destination address pointer is incremented
		(MDMA_CTCR_xSIZE_MAIN << MDMA_CTCR_DSIZE_Pos) |
		(MDMA_CTCR_xSIZE_MAIN << MDMA_CTCR_DINCOS_Pos) |
		(dburst << MDMA_CTCR_DBURST_Pos) |	// Destination burst transfer configuration
		((tlen - 1) << MDMA_CTCR_TLEN_Pos) |		// buffer Transfer Length (number of bytes - 1)
		(0x00uL << MDMA_CTCR_PKE_Pos) |
		(0x00uL << MDMA_CTCR_PAM_Pos) |
		(0x02uL << MDMA_CTCR_TRGM_Pos) |		// Trigger Mode: 10: Each MDMA request (software or hardware) triggers a repeated block transfer (if the block repeat is 0, a single block is transferred)
		(0x01uL << MDMA_CTCR_SWRM_Pos) |		// 1: hardware request are ignored. Transfer is triggered by software writing 1 to the SWRQ bit
		(0x01uL << MDMA_CTCR_BWM_Pos) |
		0;
	MDMA_CH->CBNDTR =
		((sizeof (PACKEDCOLORMAIN_T) * (sdx)) << MDMA_CBNDTR_BNDT_Pos) |	// Block Number of data bytes to transfer
		(0x00uL << MDMA_CBNDTR_BRSUM_Pos) |	// Block Repeat Source address Update Mode: 0 - increment
		(0x00uL << MDMA_CBNDTR_BRDUM_Pos) |	// Block Repeat Destination address Update Mode: 0 - increment
		((sdy - 1) << MDMA_CBNDTR_BRC_Pos) |		// Block Repeat Count
		0;
	MDMA_CH->CBRUR =
		((sizeof (PACKEDCOLORMAIN_T) * (GXADJ(sdx) - sdx)) << MDMA_CBRUR_SUV_Pos) |		// Source address Update Value
		((sizeof (PACKEDCOLORMAIN_T) * (GXADJ(tdx) - sdx)) << MDMA_CBRUR_DUV_Pos) |		// Destination address Update Value
		0;

	MDMA_CH->CTBR = (MDMA_CH->CTBR & ~ (MDMA_CTBR_SBUS_Msk | MDMA_CTBR_DBUS_Msk)) |
		(sbus << MDMA_CTBR_SBUS_Pos) |
		(dbus << MDMA_CTBR_DBUS_Pos) |
		0;

	mdma_startandwait();

#elif WITHDMA2DHW
	// DMA2D реализация
	// See DMA2D_FGCMAR for L8
	arm_hardware_flush_invalidate(dstinvalidateaddr, dstinvalidatesize);
	arm_hardware_flush(srcinvalidateaddr, srcinvalidatesize);

	/* исходный растр */
	DMA2D->FGMAR = (uintptr_t) src;
	//	The line offset used for the foreground image, expressed in pixel when the LOM bit is
	//	reset and in byte when the LOM bit is set.
	DMA2D->FGOR = (DMA2D->FGOR & ~ (DMA2D_FGOR_LO)) |
		((GXADJ(sdx) - sdx) << DMA2D_FGOR_LO_Pos) |
		0;
	/* целевой растр */
	DMA2D->OMAR = (uintptr_t) dst;
	DMA2D->OOR = (DMA2D->OOR & ~ (DMA2D_OOR_LO)) |
		((GXADJ(tdx) - sdx) << DMA2D_OOR_LO_Pos) |
		0;
	/* размер пересылаемого растра */
	DMA2D->NLR = (DMA2D->NLR & ~ (DMA2D_NLR_NL | DMA2D_NLR_PL)) |
		((sdy) << DMA2D_NLR_NL_Pos) |
		((sdx) << DMA2D_NLR_PL_Pos) |
		0;
	/* формат пикселя */
	DMA2D->FGPFCCR = (DMA2D->FGPFCCR & ~ (DMA2D_FGPFCCR_CM | DMA2D_FGPFCCR_AM)) |
		0 * DMA2D_FGPFCCR_AM |
		DMA2D_FGPFCCR_CM_VALUE_MAIN |	/* Color mode - framebuffer pixel format */
		0;

	/* запустить операцию */
	DMA2D->CR = (DMA2D->CR & ~ (DMA2D_CR_MODE | DMA2D_CR_LOM)) |
		0 * DMA2D_CR_LOM | // 0: Line offsets are expressed in pixels
		0 * DMA2D_CR_MODE_0 |	// 000: Memory-to-memory (FG fetch only)
		1 * DMA2D_CR_START |
		0;

	/* ожидаем выполнения операции */
	while ((DMA2D->CR & DMA2D_CR_START) != 0)
		hardware_nonguiyield();

	ASSERT((DMA2D->ISR & DMA2D_ISR_CEIF) == 0);	// Configuration Error
	ASSERT((DMA2D->ISR & DMA2D_ISR_TEIF) == 0);	// Transfer Error

	__DMB();

#else
	// программная реализация

	// для случая когда горизонтальные пиксели в видеопямяти источника располагаются подряд
	if (tdx == sdx)
	{
		const size_t len = (size_t) GXSIZE(sdx, sdy) * sizeof * src;
		// ширина строки одинаковая в получателе и источнике
		memcpy(dst, src, len);
	}
	else
	{
		const size_t len = sdx * sizeof * src;
		while (sdy --)
		{
			memcpy(dst, src, len);
			//arm_hardware_flush((uintptr_t) dst, len);
			src += GXADJ(sdx);
			dst += GXADJ(tdx);
		}
	}

#endif
}

// копирование буфера с поворотом вправо на 90 градусов (четверть оборота).
void hwaccel_ra90(
	PACKEDCOLORPIP_T * __restrict tbuffer,
	uint_fast16_t tdx,	// размер получателя
	uint_fast16_t tdy,
	uint_fast16_t tx,	// горизонтальная координата пикселя (0..dx-1) слева направо - в исходном нижний
	uint_fast16_t ty,	// вертикальная координата пикселя (0..dy-1) сверху вниз - в исходном левый
	const PACKEDCOLORPIP_T * __restrict sbuffer,
	uint_fast16_t sdx,	// размер источника
	uint_fast16_t sdy
	)
{
	if (sdx == 0 || sdy == 0)
		return;

	uint_fast16_t x;	// x получателя
	for (x = 0; x < sdy; ++ x)
	{
		uint_fast16_t y;	// y получателя
		for (y = 0; y < sdx; ++ y)
		{
			const COLORPIP_T pixel = * colmain_mem_at((PACKEDCOLORMAIN_T *) sbuffer, sdx, sdy, y, sdy - 1 - x);	// выборка из исхолного битмапа
			* colmain_mem_at(tbuffer, tdx, tdy, tx + x, ty + y) = pixel;
		}
	}
}


// Routine to draw a line in the RGB565 color to the LCD.
// The line is drawn from (xmin,ymin) to (xmax,ymax).
// The algorithm used to draw the line is "Bresenham's line
// algorithm".
#define SWAP(a, b)  do { (a) ^= (b); (b) ^= (a); (a) ^= (b); } while (0)
// Нарисовать линию указанным цветом
void colpip_line(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x0,
	uint_fast16_t y0,
	uint_fast16_t x1,
	uint_fast16_t y1,
	COLORPIP_T color
	)
{
	int xmin = x0;
	int xmax = x1;
	int ymin = y0;
	int ymax = y1;
	int Dx = xmax - xmin;
	int Dy = ymax - ymin;
	int steep = (abs(Dy) >= abs(Dx));
	if (steep) {
	   SWAP(xmin, ymin);
	   SWAP(xmax, ymax);
	   // recompute Dx, Dy after swap
	   Dx = xmax - xmin;
	   Dy = ymax - ymin;
	}
	int xstep = 1;
	if (Dx < 0) {
	   xstep = -1;
	   Dx = -Dx;
	}
	int ystep = 1;
	if (Dy < 0) {
	   ystep = -1;
	   Dy = -Dy;
	}

   int TwoDy = 2 * Dy;
   int TwoDyTwoDx = TwoDy - 2 * Dx; // 2*Dy - 2*Dx
   int E = TwoDy - Dx; //2*Dy - Dx
   int y = ymin;
   int xDraw, yDraw;
   int x;
   for (x = xmin; x != xmax; x += xstep) {
       if (steep) {
           xDraw = y;
           yDraw = x;
       } else {
           xDraw = x;
           yDraw = y;
       }
       // plot
	   colpip_point(buffer, dx, dy, xDraw, yDraw, color);
       // next
       if (E > 0) {
           E += TwoDyTwoDx; //E += 2*Dy - 2*Dx;
           y = y + ystep;
       } else {
           E += TwoDy; //E += 2*Dy;
       }
   }
}

#undef SWAP

// Нарисовать закрашенный или пустой прямоугольник
void colpip_rect(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,	// размер буфера
	uint_fast16_t dy,	// размер буфера
	uint_fast16_t x1,	// начальная координата
	uint_fast16_t y1,	// начальная координата
	uint_fast16_t x2,	// конечная координата (включена в заполняемую облсть)
	uint_fast16_t y2,	// конечная координата (включена в заполняемую облсть)
	COLORPIP_T color,
	uint_fast8_t fill
	)
{
	ASSERT(x2 > x1);
	ASSERT(y2 > y1);
	ASSERT(x2 < dx);
	ASSERT(y2 < dy);

	if (fill != 0)
	{
		const uint_fast16_t w = x2 - x1 + 1;	// размер по горизонтали
		const uint_fast16_t h = y2 - y1 + 1;	// размер по вертикали

		ASSERT((x1 + w) <= dx);
		ASSERT((y1 + h) <= dy);

		if (w < 3 || h < 3)
			return;

		colpip_fillrect(buffer, dx, dy, x1, y1, w, h, color);
	}
	else
	{
		colpip_line(buffer, dx, dy, x1, y1, x2, y1, color);		// верхняя горизонталь
		colpip_line(buffer, dx, dy, x1, y2, x2, y2, color);		// нижняя горизонталь
		colpip_line(buffer, dx, dy, x1, y1, x1, y2, color);		// левая вертикаль
		colpip_line(buffer, dx, dy, x2, y1, x2, y2, color);		// правая вертикаль
	}
}

#if WITHDISPLAYSNAPSHOT && WITHUSEAUDIOREC

static uint_fast8_t snapshot_req;
/* запись видимого изображения */
void
display_snapshot(
	PACKEDCOLORMAIN_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy
	)
{
	if (snapshot_req != 0)
	{
		snapshot_req = 0;
		/* запись файла */
		display_snapshot_write(buffer, dx, dy);
	}
}

void display_snapshot_req(void)
{
	snapshot_req = 1;
}

#else /* WITHDISPLAYSNAPSHOT && WITHUSEAUDIOREC */
/* stub */
/* запись видимого изображения */
void
display_snapshot(
	PACKEDCOLORMAIN_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy
	)
{
}

/* stub */
void display_snapshot_req(void)
{
}
#endif /* WITHDISPLAYSNAPSHOT && WITHUSEAUDIOREC */

void
colmain_fillrect(
	PACKEDCOLORMAIN_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x, uint_fast16_t y, 	// координаты в пикселях
	uint_fast16_t w, uint_fast16_t h, 	// размеры в пикселях
	COLORMAIN_T fgcolor
	)
{
	//PRINTF("colmain_fillrect: x/y, w/h: %u/%u, %u/%u, color=%08lX\n", x, y, w, h, fgcolor);

#if LCDMODE_HORFILL

	#if LCDMODE_MAIN_L8
		hwacc_fillrect_u8(buffer, dx, dy, x, y, w, h, fgcolor);
	#elif LCDMODE_LTDC_L24
		hwacc_fillrect_u24(buffer, dx, dy, x, y, w, h, fgcolor);
	#elif LCDMODE_MAIN_ARGB888
		hwacc_fillrect_u32(buffer, dx, dy, x, y, w, h, fgcolor);
	#else
		hwacc_fillrect_u16(buffer, dx, dy, x, y, w, h, fgcolor);
	#endif

#else /* LCDMODE_HORFILL */

	#if LCDMODE_MAIN_L8
		hwacc_fillrect_u8(buffer, dy, dx, y, x, h, w, fgcolor);
	#elif LCDMODE_LTDC_L24
		hwacc_fillrect_u24(buffer, dy, dx, y, x, h, w, fgcolor);
	#elif LCDMODE_MAIN_ARGB888
		hwacc_fillrect_u32(buffer, dy, dx, y, x, h, w, fgcolor);
	#else
		hwacc_fillrect_u16(buffer, dy, dx, y, x, h, w, fgcolor);
	#endif

#endif /* LCDMODE_HORFILL */
}

#if LCDMODE_HORFILL
// для случая когда горизонтальные пиксели в видеопямяти располагаются подряд
#if 0
// функции работы с colorbuffer не занимаются выталкиванеим кэш-памяти
static void RAMFUNC ltdcpip_horizontal_pixels(
	PACKEDCOLORPIP_T * __restrict tgr,		// target raster
	const FLASHMEM uint8_t * __restrict raster,
	uint_fast16_t width	// number of bits (start from LSB first byte in raster)
	)
{
	uint_fast16_t col;
	uint_fast16_t w = width;

	for (col = 0; w >= 8; col += 8, w -= 8)
	{
		const FLASHMEM PACKEDCOLORPIP_T * const pcl = (* byte2runpip) [* raster ++];
		memcpy(tgr + col, pcl, sizeof (* tgr) * 8);
	}
	if (w != 0)
	{
		const FLASHMEM PACKEDCOLORPIP_T * const pcl = (* byte2runpip) [* raster];
		memcpy(tgr + col, pcl, sizeof (* tgr) * w);
	}
	// функции работы с colorbuffer не занимаются выталкиванеим кэш-памяти
	//arm_hardware_flush((uintptr_t) tgr, sizeof (* tgr) * width);
}
#endif

// функции работы с colorbuffer не занимаются выталкиванеим кэш-памяти
// Фон не трогаем
static void RAMFUNC ltdcmain_horizontal_pixels_tbg(
	PACKEDCOLORMAIN_T * __restrict tgr,		// target raster
	const FLASHMEM uint8_t * __restrict raster,
	uint_fast16_t width,	// number of bits (start from LSB first byte in raster)
	COLORPIP_T fg
	)
{
	uint_fast16_t w = width;

	for (; w >= 8; w -= 8, tgr += 8)
	{
		const uint_fast8_t v = * raster ++;
		if (v & 0x01)	tgr [0] = fg;
		if (v & 0x02)	tgr [1] = fg;
		if (v & 0x04)	tgr [2] = fg;
		if (v & 0x08)	tgr [3] = fg;
		if (v & 0x10)	tgr [4] = fg;
		if (v & 0x20)	tgr [5] = fg;
		if (v & 0x40)	tgr [6] = fg;
		if (v & 0x80)	tgr [7] = fg;
	}
	if (w != 0)
	{
		uint_fast8_t vlast = * raster;
		do
		{
			if (vlast & 0x01)
				* tgr = fg;
			++ tgr;
			vlast >>= 1;
		} while (-- w);
	}
}

// функции работы с colorbuffer не занимаются выталкиванеим кэш-памяти
// Фон не трогаем
// удвоенный по ширине растр
static void RAMFUNC ltdcmain_horizontal_x2_pixels_tbg(
	PACKEDCOLORMAIN_T * __restrict tgr,		// target raster
	const FLASHMEM uint8_t * __restrict raster,
	uint_fast16_t width,	// number of bits (start from LSB first byte in raster)
	COLORPIP_T fg
	)
{
	uint_fast16_t w = width;

	for (; w >= 8; w -= 8, tgr += 16)
	{
		const uint_fast8_t v = * raster ++;
		if (v & 0x01)	{ tgr [ 0] = tgr [ 1] = fg; }
		if (v & 0x02)	{ tgr [ 2] = tgr [ 3] = fg; }
		if (v & 0x04)	{ tgr [ 4] = tgr [ 5] = fg; }
		if (v & 0x08)	{ tgr [ 6] = tgr [ 7] = fg; }
		if (v & 0x10)	{ tgr [ 8] = tgr [ 9] = fg; }
		if (v & 0x20)	{ tgr [10] = tgr [11] = fg; }
		if (v & 0x40)	{ tgr [12] = tgr [13] = fg; }
		if (v & 0x80)	{ tgr [14] = tgr [15] = fg; }
	}
	if (w != 0)
	{
		uint_fast8_t vlast = * raster;
		do
		{
			if (vlast & 0x01)
				tgr [ 0] = tgr [ 1] = fg;
			tgr += 2;
			vlast >>= 1;
		} while (-- w);
	}
}

#if 0//SMALLCHARW
// return new x coordinate
static uint_fast16_t
RAMFUNC_NONILINE
ltdcmain_horizontal_put_char_small(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,
	uint_fast16_t y,
	char cc
	)
{
	const uint_fast8_t width = SMALLCHARW;
	const uint_fast8_t c = smallfont_decode((unsigned char) cc);
	uint_fast8_t cgrow;
	for (cgrow = 0; cgrow < SMALLCHARH; ++ cgrow)
	{
		PACKEDCOLORMAIN_T * const tgr = colmain_mem_at(buffer, dx, dy, x, y + cgrow);
		ltdcpip_horizontal_pixels(tgr, S1D13781_smallfont_LTDC [c] [cgrow], width);
	}
	return x + width;
}
#endif /* SMALLCHARW */

#if defined (SMALLCHARW)
// возвращаем на сколько пикселей вправо занимет отрисованный символ
// Фон не трогаем
// return new x coordinate
static uint_fast16_t RAMFUNC_NONILINE ltdcpip_horizontal_put_char_small_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,
	uint_fast16_t y,
	char cc,
	COLOR565_T fg
	)
{
	const uint_fast8_t width = SMALLCHARW;
	const uint_fast8_t c = smallfont_decode((unsigned char) cc);
	uint_fast8_t cgrow;
	for (cgrow = 0; cgrow < SMALLCHARH; ++ cgrow)
	{
		PACKEDCOLORMAIN_T * const tgr = colmain_mem_at(buffer, dx, dy, x, y + cgrow);
		ltdcmain_horizontal_pixels_tbg(tgr, S1D13781_smallfont_LTDC [c] [cgrow], width, fg);
	}
	return x + width;
}

// возвращаем на сколько пикселей вправо занимет отрисованный символ
// Фон не трогаем
// return new x coordinate
static uint_fast16_t RAMFUNC_NONILINE ltdcpip_horizontal_x2_put_char_small_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,
	uint_fast16_t y,
	char cc,
	COLOR565_T fg
	)
{
	const uint_fast8_t width = SMALLCHARW;
	const uint_fast8_t c = smallfont_decode((unsigned char) cc);
	uint_fast8_t cgrow;
	for (cgrow = 0; cgrow < SMALLCHARH; ++ cgrow)
	{
		PACKEDCOLORMAIN_T * const tgr0 = colmain_mem_at(buffer, dx, dy, x, y + cgrow * 2 + 0);
		ltdcmain_horizontal_x2_pixels_tbg(tgr0, S1D13781_smallfont_LTDC [c] [cgrow], width, fg);
		PACKEDCOLORMAIN_T * const tgr1 = colmain_mem_at(buffer, dx, dy, x, y + cgrow * 2 + 1);
		ltdcmain_horizontal_x2_pixels_tbg(tgr1, S1D13781_smallfont_LTDC [c] [cgrow], width, fg);
	}
	return x + width * 2;
}

uint_fast16_t display_put_char_small_xy(uint_fast16_t x, uint_fast16_t y, uint_fast8_t c, COLOR565_T fg)
{
	PACKEDCOLORMAIN_T * const fr = colmain_fb_draw();
	return ltdcpip_horizontal_put_char_small_tbg(fr, DIM_X, DIM_Y, x, y, c, fg);
}
#endif /* defined (SMALLCHARW) */

#if SMALLCHARW2
// возвращаем на сколько пикселей вправо занимет отрисованный символ
// Фон не трогаем
// return new x coordinate
static uint_fast16_t RAMFUNC_NONILINE ltdcpip_horizontal_put_char_small2_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,
	uint_fast16_t y,
	char cc,
	COLORPIP_T fg
	)
{
	const uint_fast8_t width = SMALLCHARW2;
	const uint_fast8_t c = smallfont_decode((unsigned char) cc);
	uint_fast8_t cgrow;
	for (cgrow = 0; cgrow < SMALLCHARH2; ++ cgrow)
	{
		PACKEDCOLORMAIN_T * const tgr = colmain_mem_at(buffer, dx, dy, x, y + cgrow);
		ltdcmain_horizontal_pixels_tbg(tgr, S1D13781_smallfont2_LTDC [c] [cgrow], width, fg);
	}
	return x + width;
}
#endif /* SMALLCHARW2 */

#if SMALLCHARW3
// возвращаем на сколько пикселей вправо занимет отрисованный символ
// Фон не трогаем
// return new x coordinate
static uint_fast16_t RAMFUNC_NONILINE ltdcpip_horizontal_put_char_small3_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,
	uint_fast16_t y,
	char cc,
	COLORPIP_T fg
	)
{
	const uint_fast8_t width = SMALLCHARW3;
	const uint_fast8_t c = smallfont_decode((unsigned char) cc);
	uint_fast8_t cgrow;
	for (cgrow = 0; cgrow < SMALLCHARH3; ++ cgrow)
	{
		PACKEDCOLORMAIN_T * const tgr = colmain_mem_at(buffer, dx, dy, x, y + cgrow);
		ltdcmain_horizontal_pixels_tbg(tgr, & S1D13781_smallfont3_LTDC [c] [cgrow], width, fg);
	}
	return x + width;
}
#endif /* SMALLCHARW3 */



#if defined (SMALLCHARW)

// Используется при выводе на графический индикатор,
// transparent background - не меняем цвет фона.
void
colpip_string_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	const char * s,
	COLORPIP_T fg		// цвет вывода текста
	)
{
	char c;

	ASSERT(s != NULL);
	while ((c = * s ++) != '\0')
	{
		x = ltdcpip_horizontal_put_char_small_tbg(buffer, dx, dy, x, y, c, fg);
	}
}
// Используется при выводе на графический индикатор,
// transparent background - не меняем цвет фона.
void
colpip_string_x2_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	const char * s,
	COLORPIP_T fg		// цвет вывода текста
	)
{
	char c;

	ASSERT(s != NULL);
	while ((c = * s ++) != '\0')
	{
		x = ltdcpip_horizontal_x2_put_char_small_tbg(buffer, dx, dy, x, y, c, fg);
	}
}

// Используется при выводе на графический индикатор,
// с поворотом
void
colpip_string_x2ra90_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	const char * s,
	COLORPIP_T fg,		// цвет вывода текста
	COLORPIP_T bg		// цвет фона
	)
{
	char c;
	enum { TDX = SMALLCHARW * 2, TDY = SMALLCHARH * 2 };
	static RAMFRAMEBUFF ALIGNX_BEGIN PACKEDCOLORPIP_T scratch [GXSIZE(TDX, TDY)] ALIGNX_END;

	ASSERT(s != NULL);
	while ((c = * s ++) != '\0')
	{
		colpip_fillrect(scratch, TDX, TDY, 0, 0, TDX, TDY, bg);
		ltdcpip_horizontal_x2_put_char_small_tbg(scratch, TDX, TDY, 0, 0, c, fg);
		hwaccel_ra90(buffer, dx, dy, x, y, scratch, TDX, TDY);
		y += TDX;
	}
}

// Используется при выводе на графический индикатор,
// transparent background - не меняем цвет фона.
void
colpip_string_count(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	COLORPIP_T fg,		// цвет вывода текста
	const char * s,		// строка для вывода
	size_t len			// количество символов
	)
{
	ASSERT(s != NULL);
	while (len --)
	{
		const char c = * s ++;
		x = ltdcpip_horizontal_put_char_small_tbg(buffer, dx, dy, x, y, c, fg);
	}
}
// Используется при выводе на графический индикатор,
void
colpip_string_x2_count(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	COLORPIP_T fg,		// цвет вывода текста
	const char * s,		// строка для вывода
	size_t len			// количество символов
	)
{
	ASSERT(s != NULL);
	while (len --)
	{
		const char c = * s ++;
		x = ltdcpip_horizontal_x2_put_char_small_tbg(buffer, dx, dy, x, y, c, fg);
	}
}

// Используется при выводе на графический индикатор,
// transparent background - не меняем цвет фона.
void
colpip_string_x2ra90_count(
	PACKEDCOLORPIP_T * buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	COLORPIP_T fg,		// цвет вывода текста
	COLORPIP_T bg,		// цвет вывода текста
	const char * s,		// строка для вывода
	size_t len			// количество символов
	)
{
	enum { TDX = SMALLCHARW * 2, TDY = SMALLCHARH * 2 };
	static RAMFRAMEBUFF ALIGNX_BEGIN PACKEDCOLORPIP_T scratch [GXSIZE(TDX, TDY)] ALIGNX_END;

	ASSERT(s != NULL);
	while (len --)
	{
		const char c = * s ++;
		colpip_fillrect(scratch, TDX, TDY, 0, 0, TDX, TDY, bg);
		ltdcpip_horizontal_x2_put_char_small_tbg(scratch, TDX, TDY, 0, 0, c, fg);
		hwaccel_ra90(buffer, dx, dy, x, y, scratch, TDX, TDY);
		y += TDX;
	}
}
#endif /* defined (SMALLCHARW) */

#if defined (SMALLCHARW2)

// Используется при выводе на графический индикатор,
// transparent background - не меняем цвет фона.
void
colpip_string2_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	const char * s,
	COLORPIP_T fg		// цвет вывода текста
	)
{
	char c;
	ASSERT(s != NULL);
	while ((c = * s ++) != '\0')
	{
		x = ltdcpip_horizontal_put_char_small2_tbg(buffer, dx, dy, x, y, c, fg);
	}
}

// Возвращает ширину строки в пикселях
uint_fast16_t strwidth2(
	const char * s
	)
{
	ASSERT(s != NULL);
	return SMALLCHARW2 * strlen(s);
}

#endif /* defined (SMALLCHARW2) */

#if defined (SMALLCHARW3)
// Используется при выводе на графический индикатор,
// transparent background - не меняем цвет фона.
void
colpip_string3_tbg(
	PACKEDCOLORPIP_T * __restrict buffer,
	uint_fast16_t dx,
	uint_fast16_t dy,
	uint_fast16_t x,	// горизонтальная координата пикселя (0..dx-1) слева направо
	uint_fast16_t y,	// вертикальная координата пикселя (0..dy-1) сверху вниз
	const char * s,
	COLORPIP_T fg		// цвет вывода текста
	)
{
	char c;

	ASSERT(s != NULL);
	while ((c = * s ++) != '\0')
	{
		x = ltdcpip_horizontal_put_char_small3_tbg(buffer, dx, dy, x, y, c, fg);
	}
}

// Возвращает ширину строки в пикселях
uint_fast16_t strwidth3(
	const char * s
	)
{
	ASSERT(s != NULL);
	return SMALLCHARW3 * strlen(s);
}

#endif /* defined (SMALLCHARW3) */


#if defined (SMALLCHARW) && defined (SMALLCHARH)
// Возвращает ширину строки в пикселях
uint_fast16_t strwidth(
	const char * s
	)
{
	ASSERT(s != NULL);
	return SMALLCHARW * strlen(s);
}

#endif /* defined (SMALLCHARW) && defined (SMALLCHARH) */

#else /* LCDMODE_HORFILL */

#endif /* LCDMODE_HORFILL */

// скоприовать прямоугольник с типом пикселей соответствующим pip
void colpip_plot(
	uintptr_t dstinvalidateaddr,	// параметры clean invalidate получателя
	int_fast32_t dstinvalidatesize,
	PACKEDCOLORPIP_T * dst,	// получатель
	uint_fast16_t tdx,	// получатель Размеры окна в пикселях
	uint_fast16_t tdy,	// получатель
	uint_fast16_t x,	// получатель Позиция
	uint_fast16_t y,	// получатель
	uintptr_t srcinvalidateaddr,	// параметры clean источника
	int_fast32_t srcinvalidatesize,
	const PACKEDCOLORPIP_T * src, 	// источник
	uint_fast16_t sdx,	// источник Размеры окна в пикселях
	uint_fast16_t sdy	// источник
	)
{
	ASSERT(src != NULL);
	ASSERT(dst != NULL);
	ASSERT(tdx >= sdx);
	ASSERT(tdy >= sdy);

	//ASSERT(((uintptr_t) src % DCACHEROWSIZE) == 0);	// TODO: добавиль парамтр для flush исходного растра
#if LCDMODE_HORFILL
	hwaccel_copy(
		dstinvalidateaddr, dstinvalidatesize,	// target area clean invalidate parameters
		colmain_mem_at(dst, tdx, tdy, x, y), tdx, tdy,
		srcinvalidateaddr, srcinvalidatesize,	// параметры clean источника
		src, sdx, sdy
		);
#else /* LCDMODE_HORFILL */
	hwaccel_copy(
		dstinvalidateaddr, dstinvalidatesize,	// target area clean invalidate parameters
		colmain_mem_at(dst, tdx, tdy, x, y), tdx, tdy,
		srcinvalidateaddr, srcinvalidatesize,	// параметры clean источника
		src, sdx, sdy
		);
#endif /* LCDMODE_HORFILL */
}

// скоприовать прямоугольник с типом пикселей соответствующим pip
// с поворотом вправо на 90 градусов
void colpip_plot_ra90(
	uintptr_t dstinvalidateaddr,	// параметры clean invalidate получателя
	int_fast32_t dstinvalidatesize,
	PACKEDCOLORPIP_T * dst,	// получатель
	uint_fast16_t tdx,	// получатель Размеры окна в пикселях
	uint_fast16_t tdy,	// получатель
	uint_fast16_t x,	// получатель Позиция
	uint_fast16_t y,	// получатель
	uintptr_t srcinvalidateaddr,	// параметры clean источника
	int_fast32_t srcinvalidatesize,
	const PACKEDCOLORPIP_T * src, 	// источник
	uint_fast16_t sdx,	// источник Размеры окна в пикселях
	uint_fast16_t sdy	// источник
	)
{
	ASSERT(src != NULL);
	ASSERT(dst != NULL);
	ASSERT(tdx >= sdx);
	ASSERT(tdy >= sdy);

	//ASSERT(((uintptr_t) src % DCACHEROWSIZE) == 0);	// TODO: добавиль парамтр для flush исходного растра
#if LCDMODE_HORFILL
	hwaccel_ra90(
		//dstinvalidateaddr, dstinvalidatesize,	// target area clean invalidate parameters
		dst, tdx, tdy,
		x, y,
		//srcinvalidateaddr, srcinvalidatesize,	// параметры clean источника
		src, sdx, sdy
		);
#else /* LCDMODE_HORFILL */
	hwaccel_ra90(
		//dstinvalidateaddr, dstinvalidatesize,	// target area clean invalidate parameters
		dst, tdx, tdy,
		x, y,
		//srcinvalidateaddr, srcinvalidatesize,	// параметры clean источника
		src, sdx, sdy
		);
#endif /* LCDMODE_HORFILL */
}


#if SMALLCHARH3

static uint_fast16_t
RAMFUNC_NONILINE ltdc_horizontal_put_char_small3(
	PACKEDCOLORMAIN_T * const __restrict buffer,
	const uint_fast16_t dx,
	const uint_fast16_t dy,
	uint_fast16_t x, uint_fast16_t y,
	char cc
	)
{
	const uint_fast8_t c = smallfont_decode((unsigned char) cc);
	return ltdc_horizontal_put_char_unified(S1D13781_smallfont3_LTDC [0], SMALLCHARW3, SMALLCHARW3, SMALLCHARH3, sizeof S1D13781_smallfont3_LTDC [0], buffer, dx, dy, x, y, c);
//	const uint_fast8_t width = SMALLCHARW3;
//	const uint_fast8_t c = smallfont_decode((unsigned char) cc);
//	uint_fast8_t cgrow;
//	for (cgrow = 0; cgrow < SMALLCHARH3; ++ cgrow)
//	{
//		PACKEDCOLORMAIN_T * const tgr = colmain_mem_at(buffer, dx, dy, x, y + cgrow);
//		ltdc_horizontal_pixels(tgr, & S1D13781_smallfont3_LTDC [c] [cgrow], width);
//	}
//	return x + width;
}

static void
display_string3(uint_fast16_t x, uint_fast16_t y, const char * s, uint_fast8_t lowhalf)
{
	PACKEDCOLORMAIN_T * const buffer = colmain_fb_draw();
	const uint_fast16_t dx = DIM_X;
	const uint_fast16_t dy = DIM_Y;
	char c;
//	ltdc_secondoffs = 0;
//	ltdc_h = SMALLCHARH3;
	while ((c = * s ++) != '\0')
		x = ltdc_horizontal_put_char_small3(buffer, dx, dy, x, y, c);
}

void
colmain_string3_at_xy(
	PACKEDCOLORMAIN_T * const __restrict buffer,
	const uint_fast16_t dx,
	const uint_fast16_t dy,
	uint_fast16_t x,
	uint_fast16_t y,
	const char * __restrict s
	)
{
	char c;
//	ltdc_secondoffs = 0;
//	ltdc_h = SMALLCHARH3;
	while ((c = * s ++) != '\0')
		x = ltdc_horizontal_put_char_small3(buffer, dx, dy, x, y, c);
}

void
display_string3_at_xy(uint_fast16_t x, uint_fast16_t y, const char * __restrict s, COLORMAIN_T fg, COLORMAIN_T bg)
{
	uint_fast8_t lowhalf = HALFCOUNT_SMALL - 1;
	colmain_setcolors(fg, bg);
	do
	{
		display_string3(x, y + lowhalf, s, lowhalf);
	} while (lowhalf --);
}

#endif /* SMALLCHARH3 */


#if LCDMODE_COLORED

// Установить прозрачность для прямоугольника
void display_transparency(
	uint_fast16_t x1, uint_fast16_t y1,
	uint_fast16_t x2, uint_fast16_t y2,
	uint_fast8_t alpha	// на сколько затемнять цвета (0 - чёрный, 255 - без изменений)
	)
{
	PACKEDCOLORMAIN_T * const buffer = colmain_fb_draw();
	const uint_fast16_t dx = DIM_X;
	const uint_fast16_t dy = DIM_Y;
#if 1
	uint_fast16_t y;

	for (y = y1; y <= y2; y ++)
	{
		uint_fast16_t x;
		const uint_fast32_t yt = (uint_fast32_t) GXADJ(dx) * y;
		//ASSERT(y < dy);
		for (x = x1; x <= x2; x ++)
		{
			//ASSERT(x < dx);
			buffer [yt + x] = getshadedcolor(buffer [yt + x], alpha);
		}
	}
#else
	uint_fast16_t y;

	for (y = y1; y <= y2; y ++)
	{
		for (uint_fast16_t x = x1; x <= x2; x ++)
		{
			PACKEDCOLORMAIN_T * const p = colmain_mem_at(buffer, dx, dy, x, y);
			* p = getshadedcolor(* p, alpha);
		}
	}
#endif
}

static uint_fast8_t scalecolor(
	uint_fast8_t cv,	// color component value
	uint_fast8_t maxv,	// maximal color component value
	uint_fast8_t rmaxv	// resulting maximal color component value
	)
{
	return (cv * rmaxv) / maxv;
}

/* модифицировать цвет в RGB24 */
static COLOR24_T
color24_shaded(
	COLOR24_T dot,
	uint_fast8_t alpha	// на сколько затемнять цвета (0 - чёрный, 255 - без изменений)
	)
{
	//return dot;	// test
	if (dot == 0)
		return COLOR24(alpha >> 2, alpha >> 2, alpha >> 2);
	const uint_fast8_t r = scalecolor((dot >> 16) & 0xFF, 255, alpha);
	const uint_fast8_t g = scalecolor((dot >> 8) & 0xFF, 255, alpha);
	const uint_fast8_t b = scalecolor((dot >> 0) & 0xFF, 255, alpha);
	return COLOR24(r, g, b);
}

/* модифицировать цвет в RGB24 */
static COLOR24_T
color24_aliased(
	COLOR24_T dot
	)
{
	return color24_shaded(dot, DEFAULT_ALPHA);	// test
}


/* модифицировать цвет */
COLORPIP_T getshadedcolor(
	COLORPIP_T dot, // исходный цвет
	uint_fast8_t alpha	// на сколько затемнять цвета (0 - чёрный, 255 - без изменений)
	)
{
#if defined (COLORPIP_SHADED)

	return dot |= COLORPIP_SHADED;

#elif LCDMODE_MAIN_RGB565

	if (dot == COLORPIP_BLACK)
	{
		return TFTRGB(alpha, alpha, alpha); // back gray
	}
	else
	{
		// распаковка дисплейного представления
		const uint_fast8_t r = ((dot >> 11) & 0x001f) * 8;	// result in 0..255
		const uint_fast8_t g = ((dot >> 5) & 0x003f) * 4;	// result in 0..255
		const uint_fast8_t b = ((dot >> 0) & 0x001f) * 8;	// result in 0..255

		const COLOR24_T c = color24_shaded(COLOR24(r, g, b), alpha);
		return TFTRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, (c >> 0) & 0xFF);
	}

#elif LCDMODE_MAIN_ARGB888 && (CPUSTYLE_XC7Z || CPUSTYLE_XCZU) && ! WITHTFT_OVER_LVDS

	if (dot == COLORPIP_BLACK)
	{
		return TFTRGB(alpha, alpha, alpha); // back gray
	}
	else
	{
		// распаковка дисплейного представления
		const uint_fast8_t r = ((dot >> 16) & 0x00FF) * 1;	// result in 0..255
		const uint_fast8_t g = ((dot >> 0) & 0x00FF) * 1;	// result in 0..255
		const uint_fast8_t b = ((dot >> 8) & 0x00FF) * 1;	// result in 0..255

		const COLOR24_T c = color24_shaded(COLOR24(r, g, b), alpha);
		return TFTRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, (c >> 0) & 0xFF);
	}


#elif LCDMODE_MAIN_ARGB888

	if (dot == COLORPIP_BLACK)
	{
		return TFTRGB565(alpha, alpha, alpha); // back gray
	}
	else
	{
		// распаковка дисплейного представления
		const uint_fast8_t r = ((dot >> 16) & 0x00FF) * 1;	// result in 0..255
		const uint_fast8_t g = ((dot >> 8) & 0x00FF) * 1;	// result in 0..255
		const uint_fast8_t b = ((dot >> 0) & 0x00FF) * 1;	// result in 0..255

		const COLOR24_T c = color24_shaded(COLOR24(r, g, b), alpha);
		return TFTRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, (c >> 0) & 0xFF);
	}


#elif LCDMODE_PIP_RGB24

	if (dot == COLORPIP_BLACK)
	{
		return COLOR24(alpha, alpha, alpha); // back gray
	}
	else
	{
		return color24_shaded(dot, alpha);
	}



#else /*  */
	//#warning LCDMODE_PIP_L8 or LCDMODE_PIP_RGB565 not defined
	return dot;

#endif /* LCDMODE_PIP_L8 */
}

#if defined (COLORPIP_SHADED)

static void fillpair_xltrgb24(COLOR24_T * xltable, unsigned i, COLOR24_T c)
{
	ASSERT(i < 128);
	xltable [i] = c;
	xltable [i | COLORPIP_SHADED] = color24_shaded(c, DEFAULT_ALPHA);
}

static void fillfour_xltrgb24(COLOR24_T * xltable, unsigned i, COLOR24_T c)
{
	ASSERT(i < 128 - 16);
	xltable [i] = c;
	xltable [i | COLORPIP_SHADED] = color24_shaded(c, DEFAULT_ALPHA);
	xltable [i | COLORPIP_ALIASED] =  color24_aliased(c);
	xltable [i | COLORPIP_SHADED | COLORPIP_ALIASED] = color24_aliased(color24_shaded(c, DEFAULT_ALPHA));
}

#endif /* defined (COLORPIP_SHADED) */

#endif /* LCDMODE_COLORED */

void display2_xltrgb24(COLOR24_T * xltable)
{
	unsigned i;

	//PRINTF("display2_xltrgb24: init indexed colors\n");

	for (i = 0; i < 256; ++ i)
	{
		xltable [i] = COLOR24(i, i, i);
	}

#if defined (COLORPIP_SHADED)

	// часть цветов с 0-го индекса используется в отображении водопада
	// остальные в дизайне
	for (i = 0; i < COLORPIP_BASE; ++ i)
	{
		fillpair_xltrgb24(xltable, i, colorgradient(i, COLORPIP_BASE - 1));
	}

#if 0
	{
		/* тестовое заполнение палитры для проверки целостности сигналов к TFT
		 * Используется совместно с test: вывод палитры на экран
		 */
		enum { TESTSIZE = 64 };
		for (i = 0; i < 256; ++ i)
		{
			xltable [i] = COLOR24(0, 0, 0);
		}

	#if 0
		/* RED */
		for (i = 0; i < TESTSIZE; ++ i)
		{
			uint_fast8_t c = scalecolor(i, TESTSIZE - 1, 255);
			fillpair_xltrgb24(xltable, i, COLOR24(1 * c, 0 * c, 0 * c));	// проверить результат перед попыткой применить целочисленные вычисления!
		}
	#elif 0
		/* GREEN */
		for (i = 0; i < TESTSIZE; ++ i)
		{
			uint_fast8_t c = scalecolor(i, TESTSIZE - 1, 255);
			fillpair_xltrgb24(xltable, i, COLOR24(0 * c, 1 * c, 0 * c));	// проверить результат перед попыткой применить целочисленные вычисления!
		}
	#else
		/* BLUE */
		for (i = 0; i < TESTSIZE; ++ i)
		{
			uint_fast8_t c = scalecolor(i, TESTSIZE - 1, 255);
			fillpair_xltrgb24(xltable, i, COLOR24(0 * c, 0 * c, 1 * c));	// проверить результат перед попыткой применить целочисленные вычисления!
		}
	#endif
	}
#endif
	// Цвета используемые в дизайне

	fillfour_xltrgb24(xltable, COLORPIP_YELLOW    	, COLOR24(0xFF, 0xFF, 0x00));
	fillfour_xltrgb24(xltable, COLORPIP_ORANGE    	, COLOR24(0xFF, 0xA5, 0x00));
	fillfour_xltrgb24(xltable, COLORPIP_BLACK     	, COLOR24(0x00, 0x00, 0x00));
	fillfour_xltrgb24(xltable, COLORPIP_WHITE     	, COLOR24(0xFF, 0xFF, 0xFF));
	fillfour_xltrgb24(xltable, COLORPIP_GRAY      	, COLOR24(0x60, 0x60, 0x60));
	fillfour_xltrgb24(xltable, COLORPIP_DARKGREEN 	, COLOR24(0x00, 0x80, 0x00));
	fillfour_xltrgb24(xltable, COLORPIP_BLUE      	, COLOR24(0x00, 0x00, 0xFF));
	fillfour_xltrgb24(xltable, COLORPIP_GREEN     	, COLOR24(0x00, 0xFF, 0x00));
	fillfour_xltrgb24(xltable, COLORPIP_RED       	, COLOR24(0xFF, 0x00, 0x00));
	fillfour_xltrgb24(xltable, COLORPIP_DARKGRAY    , COLOR24(0x10, 0x10, 0x10));

	fillfour_xltrgb24(xltable, COLORMAIN_LOCKED	  	, COLOR24(0x3C, 0x3C, 0x00));
	// код (COLORPIP_BASE + 15) освободися. GUI_MENUSELECTCOLOR?

#if COLORSTYLE_ATS52
	// new (for ats52)
	fillfour_xltrgb24(xltable, COLORPIP_GRIDCOLOR		, COLOR24(0x80, 0x00, 0x00));		//COLOR_GRAY - center marker
	fillfour_xltrgb24(xltable, COLORPIP_GRIDCOLOR2		, COLOR24(0x60, 0x60, 0x60));		//COLOR_DARKRED - other markers
	fillfour_xltrgb24(xltable, COLORPIP_SPECTRUMBG		, COLOR24(0x00, 0x40, 0x40));		// фон спектра вне полосы пропускания
	fillfour_xltrgb24(xltable, COLORMAIN_SPECTRUMBG2	, COLOR24(0x00, 0x80, 0x80));		// фон спектра - полоса пропускания приемника
	fillfour_xltrgb24(xltable, COLORPIP_SPECTRUMFG		, COLOR24(0x00, 0xFF, 0x00));		//COLOR_GREEN

#else /* COLORSTYLE_ATS52 */
	// old
	fillfour_xltrgb24(xltable, COLORPIP_GRIDCOLOR      	, COLOR24(0x80, 0x80, 0x00));        //COLOR_GRAY - center marker
	fillfour_xltrgb24(xltable, COLORPIP_GRIDCOLOR2     	, COLOR24(0x80, 0x00, 0x00));        //COLOR_DARKRED - other markers
	fillfour_xltrgb24(xltable, COLORPIP_SPECTRUMBG     	, COLOR24(0x00, 0x00, 0x00));            // фон спектра вне полосы пропускания
	fillfour_xltrgb24(xltable, COLORMAIN_SPECTRUMBG2   	, COLOR24(0x00, 0x80, 0x80));        // фон спектра - полоса пропускания приемника
	fillfour_xltrgb24(xltable, COLORPIP_SPECTRUMFG		, COLOR24(0x00, 0xFF, 0x00));		//COLOR_GREEN

#endif /* COLORSTYLE_ATS52 */

#elif LCDMODE_COLORED && ! LCDMODE_DUMMY	/* LCDMODE_MAIN_L8 && LCDMODE_PIP_L8 */
	//PRINTF("display2_xltrgb24: init RRRRRGGG GGGBBBBB colos\n");
	// Обычная таблица - все цвета могут быть использованы как индекс
	// Водопад отображается без использования инлдексов цветов

	for (i = 0; i < 256; ++ i)
	{
		uint_fast8_t r = ((i & 0xe0) << 0) | ((i & 0xe0) >> 3) | ((i & 0xe0) >> 6);		// 3 bit red
		uint_fast8_t g = ((i & 0x1c) << 3) | ((i & 0x1c) << 0) | ((i & 0x1c) >> 3) ;	// 3 bit green
		uint_fast8_t b = ((i & 0x03) << 6) | ((i & 0x03) << 4) | ((i & 0x03) << 2) | ((i & 0x03) << 0);	// 2 bit blue
		xltable [i] = COLOR24(r, g, b);
	}

#else
	#warning Monochrome display without indexing colors
#endif /* LCDMODE_MAIN_L8 && LCDMODE_PIP_L8 */
}

#endif /* ! (LCDMODE_DUMMY || LCDMODE_HD44780) */

#if WITHDMA2DHW

void arm_hardware_dma2d_initialize(void)
{
#if CPUSTYLE_STM32H7XX
	/* Enable the DMA2D Clock */
	RCC->AHB3ENR |= RCC_AHB3ENR_DMA2DEN;	/* DMA2D clock enable */
	(void) RCC->AHB3ENR;

#else /* CPUSTYLE_STM32H7XX */
	/* Enable the DMA2D Clock */
	RCC->AHB1ENR |= RCC_AHB1ENR_DMA2DEN;	/* DMA2D clock enable */
	(void) RCC->AHB1ENR;

#endif /* CPUSTYLE_STM32H7XX */

	/* set AXI master timer */
	DMA2D->AMTCR = (DMA2D->AMTCR & ~ (DMA2D_AMTCR_DT_Msk | DMA2D_AMTCR_EN_Msk)) |
		(DMA2D_AMTCR_DT_VALUE << DMA2D_AMTCR_DT_Pos) |
		(DMA2D_AMTCR_DT_ENABLE << DMA2D_AMTCR_EN_Pos) |
		0;
#if 0
	static ALIGNX_BEGIN uint32_t clut [256] ALIGNX_END;
	memset(clut, 0xFF, sizeof clut);
	arm_hardware_flush((uintptr_t) clut, sizeof clut);
	DMA2D->FGCMAR = (uintptr_t) clut;
	DMA2D->BGCMAR = (uintptr_t) clut;
#endif
}

#endif /* WITHDMA2DHW */


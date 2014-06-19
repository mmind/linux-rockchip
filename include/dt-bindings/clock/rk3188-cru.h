/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* core clocks from 1 */
#define ARMCLK			1
#define CORE_PERI		2
#define CORE_L2C		3

/* sclk gates (special clocks) from 64 */
#define SCLK_UART0		64
#define SCLK_UART1		65
#define SCLK_UART2		66
#define SCLK_UART3		67
#define SCLK_MAC		68
#define SCLK_SPI0		69
#define SCLK_SPI1		70
#define SCLK_SARADC		71
#define SCLK_SDMMC		72
#define SCLK_SDIO		73
#define SCLK_EMMC		74
#define SCLK_I2S0		75
#define SCLK_SPDIF		76
#define SCLK_CIF		77
#define SCLK_OTGPHY0		78
#define SCLK_OTGPHY1		79
#define SCLK_HSADC		80
#define SCLK_TIMER0		81
#define SCLK_TIMER1		82
#define SCLK_TIMER2		83
#define SCLK_TIMER3		84
#define SCLK_TIMER4		85
#define SCLK_TIMER5		86
#define SCLK_TIMER6		87
#define SCLK_JTAG		88
#define SCLK_SMC		89
#define SCLK_CIF0		90

#define DCLK_LCDC0		190
#define DCLK_LCDC1		191

/* aclk gates from 192*/
#define ACLK_DMAC0		192
#define ACLK_DMAC1		193
#define ACLK_GPS		194
#define ACLK_LCDC0		195
#define ACLK_LCDC1		196
#define ACLK_GPU		197
#define ACLK_SMC		198
#define ACLK_CIF		199
#define ACLK_IPP		200
#define ACLK_RGA		201
#define ACLK_CIF0		202

/* pclk gates from 320*/
#define PCLK_GRF		320
#define PCLK_PMU		321
#define PCLK_TIMER0		322
#define PCLK_TIMER2		323
#define PCLK_PWM01		324
#define PCLK_PWM23		325
#define PCLK_SPI0		326
#define PCLK_SPI1		327
#define PCLK_SARADC		328
#define PCLK_WDT		329
#define PCLK_UART0		330
#define PCLK_UART1		331
#define PCLK_UART2		332
#define PCLK_UART3		333
#define PCLK_I2C0		334
#define PCLK_I2C1		335
#define PCLK_I2C2		336
#define PCLK_I2C3		337
#define PCLK_I2C4		338
#define PCLK_GPIO0		339
#define PCLK_GPIO1		340
#define PCLK_GPIO2		341
#define PCLK_GPIO3		342
#define PCLK_EFUSE		343
#define PCLK_TZPC		344

/* hclk gates from 448 */
#define HCLK_SDMMC		448
#define HCLK_SDIO		449
#define HCLK_EMMC		450
#define HCLK_OTG0		451
#define HCLK_EMAC		452
#define HCLK_SPDIF		453
#define HCLK_I2S		454
#define HCLK_OTG1		455
#define HCLK_HSIC		456
#define HCLK_HSADC		457
#define HCLK_PIDF		458
#define HCLK_LCDC0		459
#define HCLK_LCDC1		460
#define HCLK_ROM		461
#define HCLK_CIF		462
#define HCLK_IPP		463
#define HCLK_RGA		464
#define HCLK_NANDC		465

#define CLK_NR_CLKS		576

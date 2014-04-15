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

/*
 * Keep gate clock numbers sorted according to their gate and index.
 * This also adds space in front for 31 special clocks.
 */
#define CLK_GATE(_reg, _bit) ((_reg + 2) * 16 + _bit)

/* special clocks not ending in a gate */
#define SCLK_ARMCLK	1
#define SCLK_UART0	2
#define SCLK_UART1	3
#define SCLK_UART2	4
#define SCLK_UART3	5
#define SCLK_MAC	6

/* gated clock used by peripherals */
#define CORE_PERI	CLK_GATE(0, 0)

#define SCLK_MMC0	CLK_GATE(2, 11)
#define SCLK_MMC1	CLK_GATE(2, 13)
#define SCLK_MMC2	CLK_GATE(2, 14)

#define ACLK_DMAC0	CLK_GATE(5, 0)
#define ACLK_DMAC1	CLK_GATE(5, 1)
#define PCLK_GRF	CLK_GATE(5, 4)
#define PCLK_PMU	CLK_GATE(5, 5)
#define HCLK_MMC0	CLK_GATE(5, 10)
#define HCLK_MMC1	CLK_GATE(5, 11)
#define HCLK_MMC2	CLK_GATE(5, 12)
#define HCLK_OTG0	CLK_GATE(5, 13)

#define HCLK_EMAC	CLK_GATE(7, 0)
#define HCLK_SPDIF	CLK_GATE(7, 1)
#define HCLK_I2S	CLK_GATE(7, 2)
#define HCLK_OTG1	CLK_GATE(7, 3)
#define HCLK_HSIC	CLK_GATE(7, 4)
#define HCLK_HSADC	CLK_GATE(7, 5)
#define HCLK_PIDF	CLK_GATE(7, 6)
#define PCLK_TIMER0	CLK_GATE(7, 7)
#define PCLK_TIMER2	CLK_GATE(7, 9)
#define PCLK_PWM01	CLK_GATE(7, 10)
#define PCLK_PWM23	CLK_GATE(7, 11)
#define PCLK_SPI0	CLK_GATE(7, 12)
#define PCLK_SPI1	CLK_GATE(7, 13)
#define PCLK_SARADC	CLK_GATE(7, 14)
#define PCLK_WDT	CLK_GATE(7, 15)

#define PCLK_UART0	CLK_GATE(8, 0)
#define PCLK_UART1	CLK_GATE(8, 1)
#define PCLK_UART2	CLK_GATE(8, 2)
#define PCLK_UART3	CLK_GATE(8, 3)
#define PCLK_I2C0	CLK_GATE(8, 4)
#define PCLK_I2C1	CLK_GATE(8, 5)
#define PCLK_I2C2	CLK_GATE(8, 6)
#define PCLK_I2C3	CLK_GATE(8, 7)
#define PCLK_I2C4	CLK_GATE(8, 8)
#define PCLK_GPIO0	CLK_GATE(8, 9)
#define PCLK_GPIO1	CLK_GATE(8, 10)
#define PCLK_GPIO2	CLK_GATE(8, 11)
#define PCLK_GPIO3	CLK_GATE(8, 12)
#define ACLK_GPS	CLK_GATE(8, 13)

#define CORE_L2C	CLK_GATE(9, 4)

#define NR_CLKS		(CORE_L2C + 1)

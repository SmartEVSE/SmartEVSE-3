#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

#define CH32V203 1
#define FUNCONF_USE_HSE 1               // Use External Oscillator
#define FUNCONF_PLL_MULTIPLIER 12       // 12 x 8Mhz = 96Mhz Sysclock

#define FUNCONF_USE_DEBUGPRINTF 0
#define FUNCONF_USE_UARTPRINTF  1
#define FUNCONF_UART_PRINTF_BAUD 500000

#endif


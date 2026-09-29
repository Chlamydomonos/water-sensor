/**
 * @file    hal.c
 * @brief   STC8G1K17A 硬件抽象层实现
 */
#include "hal.h"

/*============================ GPIO 模式设置 ==============================*/
/**
 * 设置 P3 第 pin_no 位 (0~7) 的 GPIO 模式，不影响其他位。
 *
 * STC8 寄存器:  P3M1 / P3M0 每一位对应一根 IO。
 *   PxM1 n 位 = M1_n ; PxM0 n 位 = M0_n
 *   组合表见 hal.h 注释。
 */
void hal_gpio_p3_mode(unsigned char pin_no, unsigned char mode) {
    unsigned char m1, m0;
    unsigned char mask = (unsigned char)(1u << pin_no);

    m1 = (mode & 0x02u) ? mask : 0x00u;
    m0 = (mode & 0x01u) ? mask : 0x00u;

    /* 先清对应位，再置新值 */
    P3M1 = (unsigned char)((P3M1 & ~mask) | m1);
    P3M0 = (unsigned char)((P3M0 & ~mask) | m0);
}

void hal_gpio_p5_mode(unsigned char pin_no, unsigned char mode) {
    unsigned char m1, m0;
    unsigned char mask = (unsigned char)(1u << pin_no);

    m1 = (mode & 0x02u) ? mask : 0x00u;
    m0 = (mode & 0x01u) ? mask : 0x00u;

    P5M1 = (unsigned char)((P5M1 & ~mask) | m1);
    P5M0 = (unsigned char)((P5M0 & ~mask) | m0);
}

/*============================ Timer0 外部计数器 ==========================*/
/**
 * Timer0 配置为 16 位计数器，对 T0 (P5.4) 输入引脚脉冲计数。
 *
 * TMOD 寄存器 T0 占用低 4 位:
 *   bit3 GATE  = 0
 *   bit2 C/T   = 1  (计数模式, 由 T0/P5.4 外部脉冲驱动)
 *   bit1 M1    = 0
 *   bit0 M0    = 1  (模式 1: 16 位不可重装计数器)
 * AUXR 中 T0x12 (位 7) = 0 保持 1T 还是 12T?
 *   - 8051 传统 12T; STC8 T0x12=1 时为 1T。
 *   - 但 C/T=1 外部计数模式下, T0x12 不影响计数行为 (对外部脉冲总是 1:1)。
 *   - 这里仅清 0 保持默认，避免干扰。
 */
void hal_timer0_init_counter(void) {
    /* 关闭 Timer0 以便安全配置 */
    TR0 = 0;
    /* TMOD 低 4 位 = 0001b => GATE=0, C/T=1, M1=0, M0=1 */
    TMOD = (unsigned char)((TMOD & 0xF0u) | 0x05u);
    /* 计数初值清 0 */
    TH0 = 0;
    TL0 = 0;
    /* 清溢出标志 */
    TF0 = 0;
}

unsigned int hal_timer0_read(void) {
    unsigned char high, low;
    /* 读 16 位计数器需先 TL0 后 TH0，标准做法: 读两次确认一致防抖 */
    do {
        low = TL0;
        high = TH0;
    } while (low != TL0); /* 若读期间发生 TL0→TH0 进位，重读 */

    return (unsigned int)(((unsigned int)high << 8) | low);
}

/*============================ INT1 下降沿中断 ============================*/
void hal_int1_init_falling(void) {
    /* IT1 = 1  下降沿触发 */
    IT1 = 1;
    /* 清中断标志 */
    IE1 = 0;
    /* 允许 INT1 中断 */
    EX1 = 1;
}

/*============================ 关闭 P5.4 外部复位 =========================*/
void hal_disable_p54_reset(void) {
    /* RSTCFG 是扩展 SFR (xdata SFR 区), 必须 EAXFR=1 才能访问 */
    P_SW2 |= 0x80u;  /* EAXFR = 1 */
    RSTCFG = 0x00u;  /* 关闭 P5.4 外部复位功能 */
    P_SW2 &= ~0x80u; /* EAXFR = 0, 恢复 */
}

/*============================ 看门狗 ====================================*/
/**
 * WDT_CONTR (0xC1):
 *   bit7 -      保留
 *   bit6 -      保留
 *   bit5 EN_WDT 使能
 *   bit4 CLR_WDT 喂狗 (写 1 复位计数器)
 *   bit3 IDLE_WDT 空闲时仍计数
 *   bit2:0 PS  预分频
 *
 * WDT 时钟源为内部 ~32kHz 独立看门狗振荡器，与 SYSCLK 无关:
 *   T_wdt = 2^(PS+1) * 32ms (典型值, 来自 STC8 手册)
 *   PS=0 => 64ms ; PS=1 => 128ms ; PS=2 => 256ms
 * 选 PS=1 => ~128ms 防死锁。
 *
 * 注: WDT 一旦开启无法关闭 (硬件限制)。在 hal_wdt_enable 中先喂一次再使能。
 */
void hal_wdt_enable(void) {
    /* IDLE_WDT=1, PS=001 => 0x29 ; 加 CLR_WDT (0x10) 减半, 0x39
       使能位 EN_WDT=0x20 在写时一并设置 */
    WDT_CONTR = 0x39u; /* CLR_WDT=1, IDLE_WDT=1, PS=001, EN_WDT=1 */
}

/*============================ 阻塞延时 ===================================*/
/**
 * 阻塞延时约 ms 毫秒 (按 F_OSC 校准)。
 *
 * 校准依据 (Keil C51 编译典型值, 1T STC8 内核):
 *   - 内层每次循环 DJNZ+跳转 ≈ 4 个机器周期
 *   - 1ms 内机器周期数 = F_OSC / 1000
 *   -所需循环数 = F_OSC / 1000 / 4
 *   - F_OSC = 11.0592MHz 时 → 11059 / 4 ≈ 2765
 *
 * 因 unsigned char 最大 255，需用两层循环嵌套实现:
 *   外层循环 = DELAY_OUTER_LOOPS (约 11 次)
 *   内层循环 = 252 (留出余量, 11 * 252 = 2772 ≈ 2765 + 微小裕量)
 *
 * 仅用于 LED 闪烁、上电稳定等, 不参与协议时序 (协议时序由硬件 Timer0
 * 外部计数 + 主循环无阻塞轮询驱动, 与 SYSCLK 无关)。
 */
#define DELAY_OUTER_LOOPS ((unsigned char)(F_OSC / 1000UL / 4UL / 252UL))

void hal_delay_ms(unsigned int ms) {
    unsigned int i;
    unsigned char j, k;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < DELAY_OUTER_LOOPS; j++) {
            for (k = 0; k < 252u; k++) {
                /* 空循环 */
            }
        }
    }
}

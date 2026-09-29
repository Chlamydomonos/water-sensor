/**
 * @file    hal.h
 * @brief   STC8G1K17A 硬件抽象层 (HAL) - GPIO / Timer0 / WDT / 复位配置
 *
 * 仅为从机固件服务，覆盖:
 *   - GPIO 方向配置 (准双向 / 推挽 / 开漏 / 高阻)
 *   - Timer0 配置为 16 位外部脉冲计数器 (T0 引脚 = P5.4)
 *   - INT1 下降沿中断配置 (检测 START 条件)
 *   - RSTCFG 关闭 P5.4 外部复位 (释放该脚给 Timer0)
 *   - 看门狗使能 + 喂狗
 *
 * 关联: docs/slave-design.md §2 / §3 ; communication-protocol.md §8
 */
#ifndef __HAL_H__
#define __HAL_H__

#include "config.h"
#include <STC/STC8G.H>

/*============================ GPIO 模式位编码 ==============================*/
/* STC8 GPIO 由 PxM1 / PxM0 两位共同决定，组合如下:
 *   M1 M0 = 0 0  => 准双向口 (weak pull-up)
 *   M1 M0 = 0 1  => 推挽输出
 *   M1 M0 = 1 0  => 输入高阻
 *   M1 M0 = 1 1  => 开漏输出
 */
#define GPIO_MODE_QUASI 0x00u     /* 准双向 (上拉) */
#define GPIO_MODE_PUSHPULL 0x01u  /* 推挽输出 */
#define GPIO_MODE_INPUT_HIZ 0x02u /* 高阻输入 */
#define GPIO_MODE_OPENDRAIN 0x03u /* 开漏 */

/**
 * @brief  设置 P3 任一位的 GPIO 模式 (不改其他位)
 * @param  pin   形如 P32 (sbit)
 * @param  mode  GPIO_MODE_*
 * 注意:   sbit 不能直接做宏参数，因此改用 P3 端口 + 位号的传统写法。
 *         为简化代码，这里直接对 P3M1 / P3M0 操作。
 */
extern void hal_gpio_p3_mode(unsigned char pin_no, unsigned char mode);
extern void hal_gpio_p5_mode(unsigned char pin_no, unsigned char mode);

/*============================ DATA / CLK / LED 引脚快捷控制 ===============*/
/* DATA 引脚必须在 "开漏驱动 0/1" 与 "高阻输入(释放)" 之间切换 */

/** DATA 切到高阻输入 (释放总线) */
#define HAL_DATA_RELEASE() hal_gpio_p3_mode(3, GPIO_MODE_INPUT_HIZ)

/** DATA 切到开漏输出模式 (此后写 P33 = 0 拉低, P33 = 1 释放当前位) */
#define HAL_DATA_DRIVE() hal_gpio_p3_mode(3, GPIO_MODE_OPENDRAIN)

/** DATA 输出电平 (须先调用 HAL_DATA_DRIVE 切到开漏模式) */
#define HAL_DATA_OUT(bit) (PIN_DATA = (bit))

/** 读 DATA 电平 */
#define HAL_DATA_IN() (PIN_DATA)

/** 读 CLK 电平 */
#define HAL_CLK_IN() (PIN_CLK)

/** LED 控制 (HIGH=亮 / LOW=灭) */
#define HAL_LED_ON() (PIN_LED = 1)
#define HAL_LED_OFF() (PIN_LED = 0)
#define HAL_LED_TOGGLE() (PIN_LED = !PIN_LED)

/*============================ Timer0 (TLC555 脉冲计数) ====================*/
/**
 * 配置 Timer0 为 16 位外部脉冲计数器:
 *   - TMOD[2] C/T  = 1   (计数模式，由 T0 引脚/P5.4 上升沿递增)
 *   - TMOD[1:0] M1 M0 = 0 1  (模式 1: 16 位计数器)
 *   - TMOD[3] GATE = 0      (仅 TR0 控制启停)
 * 16 位计数器溢出值 65535，远大于 ~5000 计数 (10ms @ 500kHz)，无需软件干预。
 */
extern void hal_timer0_init_counter(void);

/** 启动 Timer0 计数 (TR0 = 1) */
#define HAL_T0_START() (TR0 = 1)
/** 停止 Timer0 计数 */
#define HAL_T0_STOP() (TR0 = 0)
/** 清零 Timer0 (TL0/TH0) */
#define HAL_T0_RESET()                                                                                                 \
    do {                                                                                                               \
        TH0 = 0;                                                                                                       \
        TL0 = 0;                                                                                                       \
    } while (0)
/** 读取 Timer0 当前 16 位计数值 */
extern unsigned int hal_timer0_read(void);

/*============================ INT1 (START 检测) ==========================*/
/**
 * 配置 INT1 (P3.3) 为下降沿触发外部中断，用于检测协议 START 条件。
 *   - TCON[2] IT1 = 1  下降沿触发
 *   - IE[2]  EX1 = 1  允许 INT1 中断
 *   (EA 全局中断使能由 hal_enable_irq 集中打开)
 */
extern void hal_int1_init_falling(void);

/*============================ 中断 / 复位 / 看门狗 ========================*/
/** 全局开中断 (EA = 1) */
#define HAL_ENABLE_IRQ() (EA = 1)
/** 全局关中断 (EA = 0) */
#define HAL_DISABLE_IRQ() (EA = 0)

/**
 * 关闭 P5.4 外部复位功能，确保该脚只作 Timer0 外部计数输入。
 *   - P5.4 默认是 RST，需写 RSTCFG = 0 才能当 GPIO/T0。
 *   - RSTCFG 是扩展 SFR，访问前必须 P_SW2 |= 0x80 (EAXFR=1)。
 */
extern void hal_disable_p54_reset(void);

/**
 * 使能看门狗，预分频使超时 ~128ms:
 *   - WDT_CONTR 位: EN_WDT[5] / CLR_WDT[4] / IDLE_WDT[3] / PS[2:0]
 *   - WDT 时钟源为内部 ~32kHz 独立看门狗振荡器，与 SYSCLK 无关。
 *   - PS=1 => 2^2 * 32ms ≈ 128ms，足够防死锁。
 */
extern void hal_wdt_enable(void);
/** 喂狗 (重启看门狗) */
#define HAL_WDT_FEED() (WDT_CONTR |= 0x10) /* CLR_WDT */

/*============================ 延时 ======================================*/
/** 阻塞延时约 ms 毫秒 (按 F_OSC 校准, 当前 11.0592MHz) */
extern void hal_delay_ms(unsigned int ms);

#endif /* __HAL_H__ */

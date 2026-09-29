/**
 * @file    config.h
 * @brief   从机固件全局配置：地址 / 时序 / 协议常量 / CRC 参数
 *
 * 关联文档:
 *   - docs/communication-protocol.md  (协议规范 v1.2)
 *   - docs/slave-design.md            (STC8G1K17A SOP-8 引脚分配)
 *
 * 每台从机编译前需修改 SLAVE_ADDR (0~15)。
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

/*=========================== 从机地址 (必须修改) =============================*/
/**
 * 从机唯一地址，取值范围 0~15。
 * 量产时每台从机固化为不同地址；DIY 调试可改此宏后重新编译烧录。
 */
#ifndef SLAVE_ADDR
#define SLAVE_ADDR 0x03u /* 0~15, 默认 0 号，调试用 */
#endif

/*============================= 系统时钟 ====================================*/
/**
 * STC8G1K17A 使用内部高频 RC 振荡器 (IRC)，出厂默认 24MHz，经 CLKDIV 分频得 SYSCLK。
 *
 * 11.0592MHz 不能由 24MHz 整数分频得到 (24/11.0592 ≈ 2.17)，故采用以下方式:
 *
 *   烧录时通过 STC-ISP 工具配置 IRC 校准值 (IRTRIM / IRCBAND)，
 *   使内部 IRC 直接输出 22.1184MHz，再设 CLKDIV = 2 得 11.0592MHz。
 *
 *   STC-ISP 烧录软件操作 (stc8g ISP 选项卡):
 *     1) "IRC 频率" 选择 11.0592MHz (或在高级选项写入 IRTRIM 校准值)
 *     2) "CLKDIV"      写入 0x02 (SYSCLK = IRC/2)
 *     3) 烧录，重启后 SYSCLK 即为 11.0592MHz
 *
 * 11.0592MHz 下协议时序不变 (Timer0 外部脉冲计数 1:1, 与主频无关)，
 * 仅提升主循环 / CRC / 中断响应的速度 (~11 倍裕量)。
 */
#define F_OSC 11059200UL /* 系统主时钟 (Hz)，由 STC-ISP 配置 IRC 实现 */

/*============================= 协议时序常量 ================================*/
/* 见 communication-protocol.md §3.1 / §7.1 */
#define MEASURE_CLK_COUNT 50u   /* 测量阶段 CLK 周期数 (10ms @ 5kHz) */
#define TRANSFER_TOTAL_CLK 400u /* 数据传输阶段总 CLK 数 (16 x 25) */
#define SLOT_CLK_COUNT 25u      /* 每从机槽位 CLK 数 (24 bit + 1 保护带) */
#define SLOT_DATA_BITS 24u      /* 每槽位有效数据位 */
#define FRAME_TOTAL_CLK (1u + MEASURE_CLK_COUNT + 1u + TRANSFER_TOTAL_CLK)

/*============================= CRC-8 参数 ==================================*/
/* 见 communication-protocol.md §5.2: CRC-8-Dallas/Maxim */
#define CRC8_POLY_REFL 0x8Cu /* 反转多项式 (x^8+x^5+x^4+1) */
#define CRC8_INIT 0x00u      /* 初始值 */

/*============================= 引脚分配 ====================================*/
/* 见 docs/slave-design.md §2.1 STC8G1K17A SOP-8 引脚映射
 *
 *   PIN1  P5.4 / T0      -> TLC555 脉冲输入 (Timer0 外部计数)
 *   PIN3  P5.5           -> 状态 LED (推挽)
 *   PIN7  P3.2 / INT0    <- 总线 CLK (高阻输入)
 *   PIN8  P3.3 / INT1   <-> 总线 DATA (开漏 / 高阻 分时)
 */
#define PIN_CLK P32  /* P3.2 */
#define PIN_DATA P33 /* P3.3 */
#define PIN_T0 P54   /* P5.4 (仅作注释; Timer0 硬件计数不需软件读取) */
#define PIN_LED P55  /* P5.5 */

/*============================= LED 行为 ====================================*/
/* 见 slave-design.md §5.2 */
#define LED_ERROR_BLINK_TIMES 3u
#define LED_ERROR_BLINK_ON_MS 100u /* 循环延时由 hal_delay_ms 根据 F_OSC 校准 */
#define LED_ERROR_BLINK_OFF_MS 100u

/*============================= 调试开关 ===================================*/
/* 打开后台测试钩子 (不会改变协议行为) */
#define ENABLE_SELF_TEST 0

#endif /* __CONFIG_H__ */

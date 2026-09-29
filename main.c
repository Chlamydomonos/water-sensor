/**
 * @file    main.c
 * @brief   STC8G1K17A 灌溉从机主程序
 *
 * 关联文档:
 *   - docs/communication-protocol.md   (主机↔从机协议规范 v1.2)
 *   - docs/slave-design.md             (STC8G1K17A + TLC555 + PCB 探头)
 *
 * 程序结构:
 *   config.h     - 地址 / 时序 / CRC 参数
 *   hal.h/.c     - GPIO / Timer0 / INT1 / WDT / RSTCFG
 *   crc8.h/.c    - CRC-8-Dallas/Maxim
 *   protocol.h/.c- 三阶段状态机 (IDLE -> MEASURE -> TRANSFER)
 *   main.c       - 初始化 + 主循环 (轮询 CLK 边沿 + 喂狗 + LED 错误指示)
 *
 * 主循环无阻塞: 喂狗 → 协议轮询。LED 见 slave-design.md §5.2:
 *   常亮    = 待命中
 *   快闪3次 = 上一帧 CRC 异常等错误 (诊断提示)
 */
#include "config.h"
#include "crc8.h"
#include "hal.h"
#include "protocol.h"
#include <STC/STC8G.H>

/* 错误闪烁子程序: 见 slave-design.md §5.2 */
static void led_error_blink(void) {
    unsigned char i;
    for (i = 0; i < LED_ERROR_BLINK_TIMES; i++) {
        HAL_LED_ON();
        hal_delay_ms(LED_ERROR_BLINK_ON_MS);
        HAL_LED_OFF();
        hal_delay_ms(LED_ERROR_BLINK_OFF_MS);
    }
}

void main(void) {
    /* 上电稳定延时, 让电源 / TLC555 振荡建立 */
    hal_delay_ms(50);

    /* 协议状态机初始化 (内部已完成 GPIO / Timer0 / INT1 / RSTCFG / 开中断) */
    protocol_init();

    /* 使能看门狗 (防死锁, ~128ms 超时) */
    hal_wdt_enable();

    /* 待命指示: LED 常亮 */
    HAL_LED_ON();

    /*============================ 主循环 ============================*/
    while (1) {
        HAL_WDT_FEED();  /* 喂狗 */
        protocol_poll(); /* 检测 CLK 边沿 / 推进状态机 */

        /* 错误指示: 若需排查, 可在协议层异常时置位 g_proto.error_flag */
        if (g_proto.error_flag) {
            HAL_DISABLE_IRQ();
            led_error_blink();
            HAL_ENABLE_IRQ();
            g_proto.error_flag = 0u;
            HAL_LED_ON(); /* 恢复待命指示 */
        }
    }
}
/**
 * @file    protocol.c
 * @brief   通信协议状态机实现
 *
 * 状态流 (communication-protocol.md §3.2):
 *
 *   IDLE ──(INT1 下降沿 & CLK=H) START──> MEASURE
 *   MEASURE ──(CLK=L 窗口内 DATA=H) MEASURE_END──> TRANSFER
 *   TRANSFER ──(slot_counter 达到 400)──> IDLE
 *
 * 重同步策略 (§6.1): 接收到合法 START (CLK=H 时 DATA↓) 即强制重启测量,
 * 用于协议不同步的恢复。
 *
 * 时序安全 (§4.4 + §5.3):
 *   - MEASURE 阶段 T0 硬件自动计数, 无需软件干预
 *   - MEASURE_END 时刻锁存 T0, 同步计算 CRC (~130us @ 1MHz)
 *   - CRC 完成时间远小于 slave 自身槽位的 bit16 (CRC MSB) 时刻,
 *     故即便 slave #0 (slot 0) 也有充足裕量
 *   - INT1 仅响应 CLK=H 时的 DATA 下降沿; TRANSFER 期间 DATA 仅在 CLK=L
 *     窗口翻转, 不会误触发 START (天然过滤)
 */
#include "protocol.h"
#include "config.h"
#include "crc8.h"
#include "hal.h"

protocol_state_t g_proto;

/* CLK 状态跟踪 (idle=1=HIGH)
 * 由 INT1 ISR 和主循环 poll 共同读写; 8051 单字节访问原子, 无需关中断保护 */
static unsigned char s_last_clk = 1u;

/*============================ 内部处理函数 ===============================*/
static void handle_measure_end(void) {
    unsigned int pc;
    unsigned char crc;

    HAL_T0_STOP();
    pc = hal_timer0_read(); /* 锁存 TLC555 脉冲计数 */
    crc = crc8_pulse(pc);   /* ~130us; 早于本机槽位 bit16 的需求 */

    g_proto.pulse_count = pc;
    g_proto.crc = crc;
    g_proto.data_24bit = ((unsigned long)pc << 8) | (unsigned long)crc;
    g_proto.slot_counter = 0u;
    g_proto.phase = PHASE_TRANSFER;

    /* 释放 DATA: 主机在 MEASURE_END 也将 DATA 切为输入, 总线由上拉维持 HIGH */
    HAL_DATA_RELEASE();
    /* 当前 CLK 仍为 LOW (在 LOW 窗口内检测到 MEASURE_END), 同步状态机 */
    s_last_clk = 0u;
}

static void handle_transfer_falling(void) {
    unsigned int my_start = (unsigned int)SLAVE_ADDR * (unsigned int)SLOT_CLK_COUNT;
    unsigned int my_data_end = my_start + (unsigned int)SLOT_DATA_BITS; /* +24 */
    unsigned char bit_index;
    unsigned char bit_val;

    if (g_proto.slot_counter >= my_start && g_proto.slot_counter < my_data_end) {
        /* 本机数据窗口: 用开漏驱动对应 bit (MSB 先发) */
        bit_index = (unsigned char)(23u - (unsigned char)(g_proto.slot_counter - my_start));
        bit_val = (unsigned char)((g_proto.data_24bit >> bit_index) & 1u);
        HAL_DATA_DRIVE();      /* 切到开漏模式 */
        HAL_DATA_OUT(bit_val); /* 0=拉低; 1=开漏释放(由上拉拉高) */
    } else if (g_proto.slot_counter >= my_data_end) {
        /* 保护带 (my_start+24) 或超出本机槽位: 释放总线 */
        HAL_DATA_RELEASE();
    }
    /* else: 早于本机槽位, 维持释放状态 (已在 MEASURE_END 释放) */

    g_proto.slot_counter++;
    if (g_proto.slot_counter >= TRANSFER_TOTAL_CLK) {
        /* 全部 400 CLK 完成, 回到空闲 */
        g_proto.phase = PHASE_IDLE;
        HAL_DATA_RELEASE();
        s_last_clk = 1u; /* 重置 CLK 跟踪到空闲 HIGH */
    }
}

/*============================ 公开 API ==================================*/
void protocol_init(void) {
    HAL_DISABLE_IRQ();

    g_proto.phase = PHASE_IDLE;
    g_proto.pulse_count = 0u;
    g_proto.crc = 0u;
    g_proto.slot_counter = 0u;
    g_proto.measure_clk = 0u;
    g_proto.bad_start = 0u;
    g_proto.error_flag = 0u;
    g_proto.data_24bit = 0u;
    s_last_clk = 1u; /* 空闲时 CLK=HIGH (上拉维持) */

    /* GPIO 初始状态:
     *   P3.2 CLK  : 高阻输入 (总线输入)
     *   P3.3 DATA : 高阻输入 (空闲时释放, 由上拉维持 HIGH)
     *   P5.4 T0   : 高阻输入 (Timer0 外部计数)
     *   P5.5 LED  : 推挽输出
     */
    hal_gpio_p3_mode(2u, GPIO_MODE_INPUT_HIZ);
    hal_gpio_p3_mode(3u, GPIO_MODE_INPUT_HIZ);
    hal_gpio_p5_mode(4u, GPIO_MODE_INPUT_HIZ);
    hal_gpio_p5_mode(5u, GPIO_MODE_PUSHPULL);
    HAL_DATA_RELEASE();
    HAL_LED_OFF();

    /* 外设: 关闭 P5.4 复用 RST -> Timer0 外部计数器 -> INT1 下降沿 */
    hal_disable_p54_reset();
    hal_timer0_init_counter();
    hal_int1_init_falling();

    HAL_ENABLE_IRQ();
}

/**
 * INT1 中断服务 (向量 2): P3.3 DATA 下降沿。
 * 仅当 CLK (P3.2) = HIGH 时判定为合法 START (含重同步)。
 */
void int1_isr(void) interrupt 2 {
    if (HAL_CLK_IN() == 1u) {
        /* 合法 START: 清零 T0, 进入 MEASURE */
        HAL_T0_RESET();
        HAL_T0_START();
        g_proto.phase = PHASE_MEASURE;
        g_proto.measure_clk = 0u;
        g_proto.slot_counter = 0u;
        s_last_clk = 1u; /* START 时刻 CLK=HIGH */
    } else {
        /* CLK=L 时 DATA 下降 -> 干扰或传输阶段翻转, 忽略 */
        g_proto.bad_start++;
    }
}

void protocol_poll(void) {
    unsigned char cur_clk;

    if (g_proto.phase == PHASE_IDLE) {
        return; /* 等待 START (由 INT1 ISR 触发) */
    }

    cur_clk = HAL_CLK_IN();

    if (g_proto.phase == PHASE_MEASURE) {
        /* 统计测量 CLK 数 (诊断) */
        if (s_last_clk == 1u && cur_clk == 0u) {
            g_proto.measure_clk++;
        }
        /* 在 CLK=LOW 窗口内检测 DATA 是否被主机拉高 (MEASURE_END) */
        if (cur_clk == 0u && HAL_DATA_IN() == 1u) {
            handle_measure_end();
        }
        s_last_clk = cur_clk;
    } else if (g_proto.phase == PHASE_TRANSFER) {
        /* 检测 CLK H->L 下降沿, 推进槽位 */
        if (s_last_clk == 1u && cur_clk == 0u) {
            handle_transfer_falling();
        }
        s_last_clk = cur_clk;
    }
}

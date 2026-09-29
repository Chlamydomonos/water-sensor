/**
 * @file    protocol.h
 * @brief   通信协议状态机: START 检测 / MEASURE_END / 24-bit 数据传输
 *
 * 关联: communication-protocol.md §3 / §4 / §8.4 ; slave-design.md §2.3.2
 *
 * 三阶段状态机:
 *   IDLE      -> (INT1 下降沿 & CLK=H) START -> MEASURE
 *   MEASURE   -> (CLK↓ & DATA=H) MEASURE_END -> 锁存/CRC/切 DATA 模式 -> TRANSFER
 *   TRANSFER  -> 每 CLK↓ 递增 slot_counter, 在本地址槽位驱动 DATA ; 满 400 -> IDLE
 */
#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <STC/STC8G.H>

/*============================ 协议状态 ====================================*/
typedef enum { PHASE_IDLE = 0, PHASE_MEASURE, PHASE_TRANSFER } protocol_phase_t;

/*============================ 全局状态 (供调试/观测) =====================*/
typedef struct {
    protocol_phase_t phase;    /* 当前相位 */
    unsigned int pulse_count;  /* 锁存后的 TLC555 脉冲计数 (16-bit) */
    unsigned char crc;         /* 与 pulse_count 对应的 CRC-8 */
    unsigned int slot_counter; /* 传输阶段全局 CLK 计数 (0..399) */
    unsigned char measure_clk; /* 实际计到的测量 CLK 数 (用于诊断) */
    unsigned char bad_start;   /* INT1 中断但 CLK=L 的误触发次数 */
    unsigned char error_flag;  /* 1 = 上次帧 CRC/状态异常, 触发 LED 闪烁 */
    unsigned long data_24bit;  /* 组装后的 24-bit 数据 (诊断用) */
} protocol_state_t;

extern protocol_state_t g_proto; /* idata 全局状态 */

/*============================ API ========================================*/
/**
 * 初始化协议状态机 (设置 phase=IDLE, slot=0, DATA=高阻 等)
 * 在 hal_init() 之后调用。
 */
extern void protocol_init(void);

/**
 * INT1 下降沿中断处理: 检测 START 条件。
 * 由 main.c 中的 INT1 中断服务函数调用 (或直接挂接)。
 */
extern void protocol_on_int1_falling(void);

/**
 * 主循环协议处理器: 检测 CLK 下降沿并推进状态机。
 * 应在 main.c 主循环中无阻塞地反复调用。
 */
extern void protocol_poll(void);

#endif /* __PROTOCOL_H__ */

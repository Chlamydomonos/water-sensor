/**
 * @file    crc8.h
 * @brief   CRC-8-Dallas/Maxim 校验 (x^8+x^5+x^4+1, init=0x00, refl)
 *
 * 关联: communication-protocol.md §5.2 / §5.3
 */
#ifndef __CRC8_H__
#define __CRC8_H__

#include <STC/STC8G.H>

/**
 * @brief  计算给定 16 位脉冲计数的高、低字节 CRC-8
 * @param  pulse_count  16 位 TLC555 脉冲计数值 (高字节先处理)
 * @return CRC-8 校验值
 *
 * 协议规定: 数据格式 [pulse_count_hi, pulse_count_lo]，CRC 范围 = 这 2 字节。
 */
extern unsigned char crc8_pulse(unsigned int pulse_count);

/**
 * @brief  通用 CRC-8 计算 (Dallas/Maxim, 反射)
 * @param  buf  字节数组指针
 * @param  len  字节长度
 * @return CRC-8 结果
 *
 * 注: 参数名避开 Keil C51 关键字 `data` (internal RAM 区)
 */
extern unsigned char crc8_bytes(unsigned char *buf, unsigned char len);

#endif /* __CRC8_H__ */

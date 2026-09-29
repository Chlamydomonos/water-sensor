/**
 * @file    crc8.c
 * @brief   CRC-8-Dallas/Maxim 字节级算法实现
 *
 * 算法 (communication-protocol.md §5.3):
 *   crc = 0x00
 *   for byte in bytes:
 *       crc ^= byte
 *       for i in 0..7:
 *           if crc & 0x01:  crc = (crc >> 1) ^ 0x8C
 *           else:           crc = (crc >> 1)
 *   return crc
 *
 * 8051 实现: ~80 字节 ROM, ~1ms @ 1MHz (2 字节输入)。
 */
#include "crc8.h"
#include "config.h"

unsigned char crc8_bytes(unsigned char *buf, unsigned char len) {
    unsigned char crc = CRC8_INIT;
    unsigned char i;

    while (len-- != 0) {
        crc ^= *buf++;
        for (i = 0; i < 8u; i++) {
            if (crc & 0x01u) {
                crc = (unsigned char)((crc >> 1) ^ CRC8_POLY_REFL);
            } else {
                crc = (unsigned char)(crc >> 1);
            }
        }
    }
    return crc;
}

unsigned char crc8_pulse(unsigned int pulse_count) {
    unsigned char buf[2];
    buf[0] = (unsigned char)(pulse_count >> 8); /* 高字节先 */
    buf[1] = (unsigned char)(pulse_count & 0xFFu);
    return crc8_bytes(buf, 2u);
}

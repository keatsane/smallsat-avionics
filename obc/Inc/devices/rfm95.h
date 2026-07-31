/**
 * @file   rfm95.h
 * @brief  rfm95 lora radio - the always-on tt&c beacon, sharing spi3 with the camera
 *
 * LoRa is the low-rate half of the dual-link plan: a beacon that is always on and carries mode,
 * faults and link state, against the nRF24's high-rate payload path that only runs in DOWNLINK.
 * That split is what makes DOWNLINK a real mode rather than a label - the fast radio competes for
 * power, so it is not free to leave running.
 *
 * The wire format does not change to get here. Frames are the same [AA 55][id][len][payload][crc]
 * this project has used since phase 1, and that is the whole of REQ-TLM-004: a transport swap
 * behind an unchanged format.
 */

#ifndef RFM95_H
#define RFM95_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  reset the radio and put it in LoRa mode at the configured frequency
 * @return true if the chip answered with its expected version and accepted the mode
 *
 * Follows the camera's bring-up shape: prove the part is there and talking before configuring
 * anything, so a dead radio is reported as a dead radio rather than as a stream of writes that
 * silently went nowhere.
 */
bool rfm95_init(void);

/**
 * @brief  is the radio configured and answering
 * @return true when init succeeded and the chip still reads back its version
 */
bool rfm95_alive(void);

/**
 * @brief  transmit one framed packet
 * @param  data  the bytes to send, already framed
 * @param  len   how many, up to the fifo's 255
 * @return true if the packet was handed to the radio
 *
 * Blocks only long enough to load the fifo and start the transmit; the air time is not waited on.
 * Whether the last transmit finished is rfm95_tx_done's question, which keeps a slow radio from
 * stalling whichever task is feeding it.
 */
bool rfm95_send(const uint8_t* data, size_t len);

/**
 * @brief  has the last transmit finished
 * @return true when the radio is idle and ready for another packet
 */
bool rfm95_tx_done(void);

/**
 * @brief  take one received packet, if the radio has one waiting
 * @param  buf  destination
 * @param  max  its size
 * @return bytes written, 0 when nothing arrived
 *
 * The radio sits in receive whenever it is not transmitting, which is better than 90% of every
 * second - a beacon is 57 ms once a second. Returns 0 while a transmit is in flight: the link is
 * half duplex, so the vehicle is genuinely deaf for that 57 ms and pretending otherwise would
 * just hide a lost command. Every command is acked, so an unheard one is visible and resent.
 */
size_t rfm95_receive(uint8_t* buf, size_t max);

/**
 * @brief  frames handed to the radio since boot
 */
uint32_t rfm95_sent(void);

/**
 * @brief  transmits that never reported done and were forced back to receive
 * @return the count since boot - nonzero means the radio stopped answering mid-transmit
 */
uint32_t rfm95_tx_timeouts(void);

/**
 * @brief  raw register read - bring-up and hil diagnostics
 * @param  reg  register address
 * @return the byte read
 */
uint8_t rfm95_reg(uint8_t reg);

#ifdef __cplusplus
}
#endif

#endif  // RFM95_H

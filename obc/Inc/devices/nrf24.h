/**
 * @file   nrf24.h
 * @brief  nrf24l01+ radio - the high-rate payload downlink, third device on spi3
 *
 * The other half of the dual-link plan. LoRa beacons vehicle state at 1 Hz because that is all
 * its air time allows; this carries image data at roughly a megabit, and only in DOWNLINK. That
 * asymmetry is what makes DOWNLINK a real mode rather than a label - the fast radio costs power
 * and is not free to leave running.
 *
 * The wire format is unchanged, again. A frame is longer than one nrf24 packet, so the driver
 * chops it and the far end concatenates: the receiver's frame decoder already resyncs on AA 55
 * and checks a CRC, so the radio is a byte pipe exactly like a uart. Shrinking the frame to fit
 * a packet would have been the wrong fix - the format not changing per transport is the whole of
 * REQ-TLM-004.
 */

#ifndef NRF24_H
#define NRF24_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  configure the radio for one-way transmit
 * @return true if the chip answered a write-then-read round trip
 *
 * There is no version register on this part, so presence is proven by writing the 5-byte transmit
 * address and reading it back - a floating bus does not return five matching bytes.
 */
bool nrf24_init(void);

/**
 * @brief  is the radio configured and still answering
 */
bool nrf24_alive(void);

/**
 * @brief  send a byte stream, split across as many packets as it takes
 * @param  data  bytes to send
 * @param  len   how many
 * @return true if every packet was accepted by the fifo
 *
 * Does not wait for the air time. A full transmit fifo means the caller is outrunning the radio,
 * and the packet is dropped rather than blocking whichever task is feeding it.
 */
bool nrf24_send(const uint8_t* data, size_t len);

/**
 * @brief  has everything handed over actually gone out
 */
bool nrf24_tx_empty(void);

/**
 * @brief  packets dropped because the transmit fifo was full
 */
uint32_t nrf24_dropped(void);

#ifdef __cplusplus
}
#endif

#endif  // NRF24_H

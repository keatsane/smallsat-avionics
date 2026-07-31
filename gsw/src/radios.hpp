/**
 * @file   radios.hpp
 * @brief  both receivers - the rfm95 beacon and the nrf24 payload link
 *
 * Two radios on one SPI bus with separate chip selects, mirroring the satellite. Neither parses
 * anything: each hands its received bytes to a callback, and what the caller does with them is
 * not the radio's business. That is what keeps the ground station a byte pipe.
 *
 * Every modem setting here is copied from the satellite's drivers on purpose. A mismatch between
 * the two ends produces silence rather than an error, which is the most expensive kind of bug to
 * chase - so the constants are stated in both places and each says the other exists.
 */

#ifndef GSW_RADIOS_HPP
#define GSW_RADIOS_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// what a radio does with the bytes it received - one call per packet
using rx_sink_t = void (*)(const uint8_t* data, size_t len);

/** @brief bring up the on-board rfm95 in continuous receive; false if it never answered */
bool lora_begin();

/** @brief poll for a received packet, handing any bytes to the sink */
void lora_poll(rx_sink_t sink);

/** @brief packets the lora has received since boot */
uint32_t lora_packets();

/**
 * @brief  transmit one framed packet, then go straight back to listening
 * @return true if the radio accepted it
 *
 * The link is half duplex, so transmitting is the exception and receiving is the resting state -
 * on both ends. A command going up costs a few tens of milliseconds of deafness at each side.
 */
bool lora_send(const uint8_t* data, size_t len);

/** @brief bring up the nrf24 as a receiver on the satellite's channel and address */
bool nrf24_begin();

/** @brief poll for received packets, handing any bytes to the sink */
void nrf24_poll(rx_sink_t sink);

/** @brief true when the nrf24 has packets waiting - draining them beats drawing a screen */
bool nrf24_pending();

/** @brief packets the nrf24 has received since boot */
uint32_t nrf24_packets();

#endif  // GSW_RADIOS_HPP

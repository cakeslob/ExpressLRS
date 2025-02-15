#if (defined(GPIO_PIN_PWM_OUTPUTS) && defined(PLATFORM_ESP32) && !defined(PLATFORM_ESP32_C3))
//
// Name:		DShotRMT.cpp
// Created: 	20.03.2021 00:49:15
// Author:  	derdoktor667
//

#include "DShotRMT.h"

static bool has_configed_intr = false; // only need to attach the transmission-complete callback once

extern "C" {

// channel status
enum {
	CHSTS_IDLE,
	CHSTS_TX,
	CHSTS_RX,
};

static uint8_t ch_status[RMT_MAX_CHANNELS];
static rmt_config_t* rmt_ch_cfg[RMT_MAX_CHANNELS]; // rmt_config_t contains both the channel number and the GPIO number, so it's nice to have a copy, these pointers point to the cached copy inside the class object
static bool ch_is_bidir[RMT_MAX_CHANNELS];
static uint16_t itm_cnt[RMT_MAX_CHANNELS];
static RingbufHandle_t rx_ringbuf[RMT_MAX_CHANNELS];

//ensures that the rx callback code is always in iram, which is essential for speed
#define CONFIG_RMT_ISR_IRAM_SAFE 1
#if CONFIG_RMT_ISR_IRAM_SAFE
#define TEST_RMT_CALLBACK_ATTR IRAM_ATTR
#else
#define TEST_RMT_CALLBACK_ATTR
#endif

TEST_RMT_CALLBACK_ATTR static void tx_complete_cb(rmt_channel_t channel, void *arg)
{
	if (ch_is_bidir[channel] == false) {
		ch_status[channel] = CHSTS_IDLE;
		return; // not bidirectional, nothing to do
	}

	rmt_config_t* cfg = rmt_ch_cfg[channel];
	cfg->rmt_mode = RMT_MODE_RX;
	cfg->rx_config = {
			.idle_threshold = 0,
			.filter_ticks_thresh = 0,
			.filter_en = 0,
		};
	gpio_ll_od_enable(GPIO_LL_GET_HW(GPIO_PORT_0), cfg->gpio_num);
	rmt_config(cfg);
	//rmt_set_pin(channel, RMT_MODE_RX, cfg->gpio_num);
	rmt_rx_start(channel, true);
	rmt_get_ringbuf_handle(channel, &(rx_ringbuf[channel]));
	ch_status[channel] = CHSTS_RX;
	itm_cnt[channel] = 0; // this signals a new start, can be read from the class object
}

} // extern C

// nibble mapping for GCR
static const unsigned char GCR_encode[16] =
{
	0x19, 0x1B, 0x12, 0x13,
	0x1D, 0x15, 0x16, 0x17,
	0x1A, 0x09, 0x0A, 0x0B,
	0x1E, 0x0D, 0x0E, 0x0F
};

// 5 bits > 4 bits (0xff => invalid)
static const unsigned char GCR_decode[32] =
{
	0xFF, 0xFF, 0xFF, 0xFF, // 0 - 3
	0xFF, 0xFF, 0xFF, 0xFF, // 4 - 7
	0xFF, 9, 10, 11, // 8 - 11
	0xFF, 13, 14, 15, // 12 - 15

	0xFF, 0xFF, 2, 3, // 16 - 19
	0xFF, 5, 6, 7, // 20 - 23
	0xFF, 0, 8, 1, // 24 - 27
	0xFF, 4, 12, 0xFF, // 28 - 31
};

DShotRMT::DShotRMT(gpio_num_t gpio, rmt_channel_t rmtChannel) : gpio_num(gpio), rmt_channel(rmtChannel) {
	// ...create clean packet
	encode_dshot_to_rmt(DSHOT_NULL_PACKET);
}

DShotRMT::~DShotRMT() {
	rmt_tx_stop(rmt_channel);
	rmt_driver_uninstall(rmt_channel);
}

bool DShotRMT::begin(dshot_mode_t dshot_mode, bool is_bidirectional) {
	mode = dshot_mode;
	bidirectional = is_bidirectional;

	uint16_t ticks_per_bit;

	switch (mode) {
		case DSHOT150:
			ticks_per_bit = 64; // ...Bit Period Time 6.67 us
			ticks_zero_high = 24; // ...zero time 2.50 us
			ticks_one_high = 48; // ...one time 5.00 us
			bidirectional = false;
			break;

		case DSHOT300:
			ticks_per_bit = 32; // ...Bit Period Time 3.33 us
			ticks_zero_high = 12; // ...zero time 1.25 us
			ticks_one_high = 24; // ...one time 2.50 us
			break;

		case DSHOT600:
			ticks_per_bit = 16; // ...Bit Period Time 1.67 us
			ticks_zero_high = 6; // ...zero time 0.625 us
			ticks_one_high = 12; // ...one time 1.25 us
			break;

		case DSHOT1200:
			ticks_per_bit = 8; // ...Bit Period Time 0.83 us
			ticks_zero_high = 3; // ...zero time 0.313 us
			ticks_one_high = 6; // ...one time 0.625 us
			break;

		// ...because having a default is "good style"
		default:
			ticks_per_bit = 0; // ...Bit Period Time endless
			ticks_zero_high = 0; // ...no bits, no time
			ticks_one_high = 0; // ......no bits, no time
			break;
	}

	// ...calc low signal timing
	ticks_zero_low = (ticks_per_bit - ticks_zero_high);
	ticks_one_low = (ticks_per_bit - ticks_one_high);

	rmt_config_t dshot_tx_rmt_config = {
		.rmt_mode = RMT_MODE_TX,
		.channel = rmt_channel,
		.gpio_num = gpio_num,
		.clk_div = DSHOT_CLK_DIVIDER,
		.mem_block_num = uint8_t(RMT_CHANNEL_MAX - uint8_t(rmt_channel)),
		.tx_config = {
			.idle_level = bidirectional ? RMT_IDLE_LEVEL_HIGH : RMT_IDLE_LEVEL_LOW,
			.carrier_en = false,
			.loop_en = false,
			.idle_output_en = true,
		},
	};
	memcpy(&rmt_cfg_cache, &dshot_tx_rmt_config, sizeof(rmt_config_t));
	rmt_ch_cfg[rmt_channel] = &rmt_cfg_cache;
	ch_is_bidir[rmt_channel] = bidirectional;
	ch_status[rmt_channel] = CHSTS_IDLE;

	// ...pause "bit" added to each frame
	if (bidirectional) {
		dshot_tx_rmt_item[DSHOT_PAUSE_BIT].level0 = HIGH;
		dshot_tx_rmt_item[DSHOT_PAUSE_BIT].level1 = HIGH;
	} else {
		dshot_tx_rmt_item[DSHOT_PAUSE_BIT].level0 = LOW;
		dshot_tx_rmt_item[DSHOT_PAUSE_BIT].level1 = LOW;
	}

	dshot_tx_rmt_item[DSHOT_PAUSE_BIT].duration1 = 0;
	dshot_tx_rmt_item[DSHOT_PAUSE_BIT].duration0 = 10000 - (16*ticks_per_bit) - 1;

	// setup the RMT end marker
	dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].duration0 = 0;
	dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].level0 = HIGH;
	dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].duration1 = 0;
	dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].level1 = LOW;

	// ...setup selected dshot mode
	rmt_config(&dshot_tx_rmt_config);

	if (has_configed_intr == false) {
		rmt_register_tx_end_callback(tx_complete_cb, NULL);
		has_configed_intr = true;
	}

	// ...essential step, return the result
	return rmt_driver_install(dshot_tx_rmt_config.channel, 0, 0);
}

void DShotRMT::set_looping(bool x) {
	rmt_cfg_cache.tx_config.loop_en = x;
	autolooping = x;
	if (x) {
		bidirectional = false;
	}
	rmt_config(&rmt_cfg_cache);
}

// ...the config part is done, now the calculating and sending part
void DShotRMT::send_dshot_value(uint16_t throttle_value, telemetric_request_t telemetric_request) {
	dshot_packet_t dshot_rmt_packet = { };

	// ...packets are the same for bidirectional mode
	dshot_rmt_packet.throttle_value = throttle_value;
	dshot_rmt_packet.telemetric_request = telemetric_request;
	dshot_rmt_packet.checksum = this->calc_dshot_chksum(dshot_rmt_packet);

	output_rmt_data(dshot_rmt_packet);
}

rmt_item32_t* DShotRMT::encode_dshot_to_rmt(uint16_t parsed_packet) {
	// ...is bidirecional mode activated
	if (bidirectional) {
		// ..."invert" the signal duration
		for (int i = 0; i < DSHOT_PAUSE_BIT; i++, parsed_packet <<= 1) 	{
			if (parsed_packet & 0b1000000000000000) {
				// set one
				dshot_tx_rmt_item[i].duration0 = ticks_one_low;
				dshot_tx_rmt_item[i].duration1 = ticks_one_high;
			}
			else {
				// set zero
				dshot_tx_rmt_item[i].duration0 = ticks_zero_low;
				dshot_tx_rmt_item[i].duration1 = ticks_zero_high;
			}

			dshot_tx_rmt_item[i].level0 = LOW;
			dshot_tx_rmt_item[i].level1 = HIGH;
		}
	}

	// ..."normal" DShot mode / "bidirectional" mode OFF
	else {
		for (int i = 0; i < DSHOT_PAUSE_BIT; i++, parsed_packet <<= 1) 	{
			if (parsed_packet & 0b1000000000000000) {
				// set one
				dshot_tx_rmt_item[i].duration0 = ticks_one_high;
				dshot_tx_rmt_item[i].duration1 = ticks_one_low;
			}
			else {
				// set zero
				dshot_tx_rmt_item[i].duration0 = ticks_zero_high;
				dshot_tx_rmt_item[i].duration1 = ticks_zero_low;
			}

			dshot_tx_rmt_item[i].level0 = HIGH;
			dshot_tx_rmt_item[i].level1 = LOW;
		}
	}

	return dshot_tx_rmt_item;
}

// ...just returns the checksum
// DOES NOT APPEND CHECKSUM!!!
uint16_t DShotRMT::calc_dshot_chksum(const dshot_packet_t& dshot_packet) {
	// ...same initial 12bit data for bidirectional or "normal" mode
	uint16_t packet = (dshot_packet.throttle_value << 1) | dshot_packet.telemetric_request;

	if (bidirectional) {
		// ...calc the checksum "inverted" / bidirectional mode
		return (~(packet ^ (packet >> 4) ^ (packet >> 8))) & 0x0F;
	} else {
		// ...calc the checksum "normal" mode
		return (packet ^ (packet >> 4) ^ (packet >> 8)) & 0x0F;
	}
}

uint16_t DShotRMT::prepare_rmt_data(const dshot_packet_t& dshot_packet) {
	auto chksum = calc_dshot_chksum(dshot_packet);

	// ..."construct" the packet
	uint16_t prepared_to_encode = (dshot_packet.throttle_value << 1) | dshot_packet.telemetric_request;
	prepared_to_encode = (prepared_to_encode << 4) | chksum;

	return prepared_to_encode;
}

// ...finally output using ESP32 RMT
void DShotRMT::output_rmt_data(const dshot_packet_t& dshot_packet) {
	encode_dshot_to_rmt(prepare_rmt_data(dshot_packet));

	if (ch_status[rmt_channel] == CHSTS_RX) {
		gpio_ll_od_disable(GPIO_LL_GET_HW(GPIO_PORT_0), gpio_num);
	}

	rmt_tx_stop(rmt_channel);
	rmt_fill_tx_items(rmt_channel, dshot_tx_rmt_item, DSHOT_PACKET_LENGTH, 0);

	rmt_tx_start(rmt_channel, true);
	ch_status[rmt_channel] = CHSTS_TX;
	itm_cnt[rmt_channel] = 0;

	if (bidirectional && dshot_rx_pulses == NULL) {
		dshot_rx_pulses = (uint16_t*)malloc(DSHOT_PACKET_LENGTH * 2 * sizeof(uint16_t));
		if (dshot_rx_pulses == NULL) {
			bidirectional = false;
			ch_is_bidir[rmt_channel] = false;
		}
	}
}

bool DShotRMT::poll_rx() {
	if (ch_status[rmt_channel] != CHSTS_RX || dshot_rx_pulses == NULL) {
		return false;
	}
	rmt_item32_t *items = NULL;
	size_t length = 0;
	items = (rmt_item32_t *) xRingbufferReceive(&(rx_ringbuf[rmt_channel]), &length, 0);
	if (items) {
		length /= 4; // one RMT = 4 Bytes
		if (itm_cnt[rmt_channel] == 0) {
			rx_itm_idx = 0;
		}
		for (int i = 0; i < length && rx_itm_idx < DSHOT_PACKET_LENGTH * 2; i++) {
			rmt_item32_t* item = &(items[i]);
			if (rx_itm_idx == 0 && item->level0 != 0) {
				dshot_rx_pulses[0] = item->duration1;
				rx_itm_idx += 1;
			}
			else {
				dshot_rx_pulses[rx_itm_idx]     = item->duration0;
				dshot_rx_pulses[rx_itm_idx + 1] = item->duration1;
				rx_itm_idx += 2;
			}
		}
		itm_cnt[rmt_channel] = rx_itm_idx;
	}
	if (rx_itm_idx >= 32) {
		return proc_rx();
	}
	return false;
}

bool DShotRMT::proc_rx()
{
	if (rx_itm_idx < 32) {
		return false;
	}

	unsigned short bitTime = ticks_one_high;
	unsigned short bitCount0 = 0;
	unsigned short bitCount1 = 0;
	unsigned short bitShiftLevel = 20; // 21 bits, including 0
	unsigned int assembledFrame = 0;

	int i, j;

	for (i = 0; i < rx_itm_idx; i += 2)
	{
		bitCount0 = dshot_rx_pulses[i    ] / bitTime + (dshot_rx_pulses[i    ] % bitTime > bitTime - 4);
		bitCount1 = dshot_rx_pulses[i + 1] / bitTime + (dshot_rx_pulses[i + 1] % bitTime > bitTime - 4);
		bitShiftLevel -= bitCount0;
		for (j = 0; j < bitCount1; ++j)
		{
			assembledFrame |= 1 << bitShiftLevel;
			--bitShiftLevel;
		}
	}
	for (i = bitShiftLevel; i >= 0; --i)
	{
		assembledFrame |= 1 << i;
	}
	assembledFrame = (assembledFrame ^ (assembledFrame >> 1));
	unsigned char nibble = 0;
	unsigned char fiveBitSubset = 0;
	unsigned int decodedFrame = 0;
	//y = &decodedFrame;
	//remove GCR encoding
	for (i = 0; i < 4; ++i)
	{
		//bitmask out the encoded quintuple
		fiveBitSubset = (assembledFrame >> (i * 5)) & 0b11111; //shift over in sets of 5
		//use a lookup table to get the corresponding nibble
		nibble = GCR_decode[fiveBitSubset];
		//append nibble to the frame
		decodedFrame |= nibble << (i * 4);
	}

	//mask out components of the frame
	uint16_t frameData = (decodedFrame >> 4) & (0b111111111111);
	uint8_t crc = decodedFrame & (0b1111);
	uint8_t alsocrc = (~(frameData ^ (frameData >> 4) ^ (frameData >> 8))) & 0x0F;

	//stop processing if the checksum is invalid
	if (crc != alsocrc)
	{
		//error_packets += 1; //for now, the only error packets we will track are the ones where the checksum fails
		//we don't update the RPM pointer that was passed to us
		//return ERR_CHECKSUM_FAIL;
		return false;
	}

	bool ret = true;

	// determine packet type
	if (frameData & 0b000100000000 || (~frameData & 0b111100000000) == 0b111100000000) //is erpm packet (4th bit is 1 or all four bits are 0)
	{
		telem_erpm = decode_eRPM_telemetry_value(frameData);
		telem_erpm_timestamp = telem_timestamp = millis();
	}
	else // is extended telemetry packet
	{
		extended_telem_type_t packet_type = (extended_telem_type_t)((frameData >> 8) & 0b1111);
		uint16_t value = (frameData & 0b11111111);
		switch (packet_type)
		{
			case TELEM_TYPE_ERPM:
				telem_erpm = decode_eRPM_telemetry_value(frameData);
				telem_timestamp = millis();
				break;
			case TELEM_TYPE_TEMPRATURE:
				telem_temperature = value;
				telem_timestamp = millis();
				break;
			case TELEM_TYPE_VOLTAGE:
				telem_voltage = value;
				telem_timestamp = millis();
				break;
			case TELEM_TYPE_CURRENT:
				telem_current = value;
				telem_timestamp = millis();
				break;
			case TELEM_TYPE_DEBUG_A:
				telem_debug_a = value;
				telem_timestamp = millis();
				break;
			case TELEM_TYPE_DEBUG_B:
				telem_debug_b = value;
				telem_timestamp = millis();
				break;
			case TELEM_TYPE_STRESS_LEVEL:
				telem_stress = value;
				telem_timestamp = millis();
				break;
			case TELEM_TYPE_STATUS:
				telem_status = value;
				telem_timestamp = millis();
				break;
			default:
				ret = false;
				break;
		}
	}
	return ret;
}

extern "C" {

uint32_t decode_eRPM_telemetry_value(uint16_t value)
{
    // eRPM range
    if (value == 0x0fff) {
        return 0;
    }

    // Convert value to 16 bit from the GCR telemetry format (eeem mmmm mmmm)
    value = (value & 0x01ff) << ((value & 0xfe00) >> 9);
    if (!value) {
        return 0;
    }

    // Convert period to erpm * 100
    return (1000000 * 60 / 100 + value / 2) / value;
}


// stolen from betaflight (src/main/drivers/dshot.c)
// Used with serial esc telem as well as dshot telem
uint32_t erpmToRpm(uint16_t erpm, uint16_t motorPoleCount)
{
    //  rpm = (erpm * 100) / (motorConfig()->motorPoleCount / 2)
    return (erpm * 200) / motorPoleCount;
}

} // extern C

#endif

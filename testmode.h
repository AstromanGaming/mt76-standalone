/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */
#ifndef __STANDALONE_MT76_TESTMODE_H
#define __STANDALONE_MT76_TESTMODE_H

#define STANDALONE_MT76_TM_TIMEOUT	10

/**
 * enum standalone_mt76_testmode_attr - testmode attributes inside NL80211_ATTR_TESTDATA
 *
 * @STANDALONE_MT76_TM_ATTR_UNSPEC: (invalid attribute)
 *
 * @STANDALONE_MT76_TM_ATTR_RESET: reset parameters to default (flag)
 * @STANDALONE_MT76_TM_ATTR_STATE: test state (u32), see &enum standalone_mt76_testmode_state
 *
 * @STANDALONE_MT76_TM_ATTR_MTD_PART: mtd partition used for eeprom data (string)
 * @STANDALONE_MT76_TM_ATTR_MTD_OFFSET: offset of eeprom data within the partition (u32)
 *
 * @STANDALONE_MT76_TM_ATTR_TX_COUNT: configured number of frames to send when setting
 *	state to STANDALONE_MT76_TM_STATE_TX_FRAMES (u32)
 * @STANDALONE_MT76_TM_ATTR_TX_PENDING: pending frames during STANDALONE_MT76_TM_STATE_TX_FRAMES (u32)
 * @STANDALONE_MT76_TM_ATTR_TX_LENGTH: packet tx mpdu length (u32)
 * @STANDALONE_MT76_TM_ATTR_TX_RATE_MODE: packet tx mode (u8, see &enum standalone_mt76_testmode_tx_mode)
 * @STANDALONE_MT76_TM_ATTR_TX_RATE_NSS: packet tx number of spatial streams (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_RATE_IDX: packet tx rate/MCS index (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_RATE_SGI: packet tx use short guard interval (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_RATE_LDPC: packet tx enable LDPC (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_RATE_STBC: packet tx enable STBC (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_LTF: packet tx LTF, set 0 to 2 for 1x, 2x, and 4x LTF (u8)
 *
 * @STANDALONE_MT76_TM_ATTR_TX_ANTENNA: tx antenna mask (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_POWER_CONTROL: enable tx power control (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_POWER: per-antenna tx power array (nested, u8 attrs)
 *
 * @STANDALONE_MT76_TM_ATTR_FREQ_OFFSET: RF frequency offset (u32)
 *
 * @STANDALONE_MT76_TM_ATTR_STATS: statistics (nested, see &enum standalone_mt76_testmode_stats_attr)
 *
 * @STANDALONE_MT76_TM_ATTR_TX_SPE_IDX: tx spatial extension index (u8)
 *
 * @STANDALONE_MT76_TM_ATTR_TX_DUTY_CYCLE: packet tx duty cycle (u8)
 * @STANDALONE_MT76_TM_ATTR_TX_IPG: tx inter-packet gap, in unit of us (u32)
 * @STANDALONE_MT76_TM_ATTR_TX_TIME: packet transmission time, in unit of us (u32)
 *
 * @STANDALONE_MT76_TM_ATTR_DRV_DATA: driver specific netlink attrs (nested)
 *
 * @STANDALONE_MT76_TM_ATTR_MAC_ADDRS: array of nested MAC addresses (nested)
 */
enum standalone_mt76_testmode_attr {
	STANDALONE_MT76_TM_ATTR_UNSPEC,

	STANDALONE_MT76_TM_ATTR_RESET,
	STANDALONE_MT76_TM_ATTR_STATE,

	STANDALONE_MT76_TM_ATTR_MTD_PART,
	STANDALONE_MT76_TM_ATTR_MTD_OFFSET,

	STANDALONE_MT76_TM_ATTR_TX_COUNT,
	STANDALONE_MT76_TM_ATTR_TX_LENGTH,
	STANDALONE_MT76_TM_ATTR_TX_RATE_MODE,
	STANDALONE_MT76_TM_ATTR_TX_RATE_NSS,
	STANDALONE_MT76_TM_ATTR_TX_RATE_IDX,
	STANDALONE_MT76_TM_ATTR_TX_RATE_SGI,
	STANDALONE_MT76_TM_ATTR_TX_RATE_LDPC,
	STANDALONE_MT76_TM_ATTR_TX_RATE_STBC,
	STANDALONE_MT76_TM_ATTR_TX_LTF,

	STANDALONE_MT76_TM_ATTR_TX_ANTENNA,
	STANDALONE_MT76_TM_ATTR_TX_POWER_CONTROL,
	STANDALONE_MT76_TM_ATTR_TX_POWER,

	STANDALONE_MT76_TM_ATTR_FREQ_OFFSET,

	STANDALONE_MT76_TM_ATTR_STATS,

	STANDALONE_MT76_TM_ATTR_TX_SPE_IDX,

	STANDALONE_MT76_TM_ATTR_TX_DUTY_CYCLE,
	STANDALONE_MT76_TM_ATTR_TX_IPG,
	STANDALONE_MT76_TM_ATTR_TX_TIME,

	STANDALONE_MT76_TM_ATTR_DRV_DATA,

	STANDALONE_MT76_TM_ATTR_MAC_ADDRS,

	/* keep last */
	NUM_STANDALONE_MT76_TM_ATTRS,
	STANDALONE_MT76_TM_ATTR_MAX = NUM_STANDALONE_MT76_TM_ATTRS - 1,
};

/**
 * enum standalone_mt76_testmode_state - statistics attributes
 *
 * @STANDALONE_MT76_TM_STATS_ATTR_TX_PENDING: pending tx frames (u32)
 * @STANDALONE_MT76_TM_STATS_ATTR_TX_QUEUED: queued tx frames (u32)
 * @STANDALONE_MT76_TM_STATS_ATTR_TX_QUEUED: completed tx frames (u32)
 *
 * @STANDALONE_MT76_TM_STATS_ATTR_RX_PACKETS: number of rx packets (u64)
 * @STANDALONE_MT76_TM_STATS_ATTR_RX_FCS_ERROR: number of rx packets with FCS error (u64)
 * @STANDALONE_MT76_TM_STATS_ATTR_LAST_RX: information about the last received packet
 *	see &enum standalone_mt76_testmode_rx_attr
 */
enum standalone_mt76_testmode_stats_attr {
	STANDALONE_MT76_TM_STATS_ATTR_UNSPEC,
	STANDALONE_MT76_TM_STATS_ATTR_PAD,

	STANDALONE_MT76_TM_STATS_ATTR_TX_PENDING,
	STANDALONE_MT76_TM_STATS_ATTR_TX_QUEUED,
	STANDALONE_MT76_TM_STATS_ATTR_TX_DONE,

	STANDALONE_MT76_TM_STATS_ATTR_RX_PACKETS,
	STANDALONE_MT76_TM_STATS_ATTR_RX_FCS_ERROR,
	STANDALONE_MT76_TM_STATS_ATTR_LAST_RX,

	/* keep last */
	NUM_STANDALONE_MT76_TM_STATS_ATTRS,
	STANDALONE_MT76_TM_STATS_ATTR_MAX = NUM_STANDALONE_MT76_TM_STATS_ATTRS - 1,
};


/**
 * enum standalone_mt76_testmode_rx_attr - packet rx information
 *
 * @STANDALONE_MT76_TM_RX_ATTR_FREQ_OFFSET: frequency offset (s32)
 * @STANDALONE_MT76_TM_RX_ATTR_RCPI: received channel power indicator (array, u8)
 * @STANDALONE_MT76_TM_RX_ATTR_IB_RSSI: internal inband RSSI (array, s8)
 * @STANDALONE_MT76_TM_RX_ATTR_WB_RSSI: internal wideband RSSI (array, s8)
 * @STANDALONE_MT76_TM_RX_ATTR_SNR: signal-to-noise ratio (u8)
 */
enum standalone_mt76_testmode_rx_attr {
	STANDALONE_MT76_TM_RX_ATTR_UNSPEC,

	STANDALONE_MT76_TM_RX_ATTR_FREQ_OFFSET,
	STANDALONE_MT76_TM_RX_ATTR_RCPI,
	STANDALONE_MT76_TM_RX_ATTR_IB_RSSI,
	STANDALONE_MT76_TM_RX_ATTR_WB_RSSI,
	STANDALONE_MT76_TM_RX_ATTR_SNR,

	/* keep last */
	NUM_STANDALONE_MT76_TM_RX_ATTRS,
	STANDALONE_MT76_TM_RX_ATTR_MAX = NUM_STANDALONE_MT76_TM_RX_ATTRS - 1,
};

/**
 * enum standalone_mt76_testmode_state - phy test state
 *
 * @STANDALONE_MT76_TM_STATE_OFF: test mode disabled (normal operation)
 * @STANDALONE_MT76_TM_STATE_IDLE: test mode enabled, but idle
 * @STANDALONE_MT76_TM_STATE_TX_FRAMES: send a fixed number of test frames
 * @STANDALONE_MT76_TM_STATE_RX_FRAMES: receive packets and keep statistics
 * @STANDALONE_MT76_TM_STATE_TX_CONT: waveform tx without time gap
 * @STANDALONE_MT76_TM_STATE_ON: test mode enabled used in offload firmware
 */
enum standalone_mt76_testmode_state {
	STANDALONE_MT76_TM_STATE_OFF,
	STANDALONE_MT76_TM_STATE_IDLE,
	STANDALONE_MT76_TM_STATE_TX_FRAMES,
	STANDALONE_MT76_TM_STATE_RX_FRAMES,
	STANDALONE_MT76_TM_STATE_TX_CONT,
	STANDALONE_MT76_TM_STATE_ON,

	/* keep last */
	NUM_STANDALONE_MT76_TM_STATES,
	STANDALONE_MT76_TM_STATE_MAX = NUM_STANDALONE_MT76_TM_STATES - 1,
};

/**
 * enum standalone_mt76_testmode_tx_mode - packet tx phy mode
 *
 * @STANDALONE_MT76_TM_TX_MODE_CCK: legacy CCK mode
 * @STANDALONE_MT76_TM_TX_MODE_OFDM: legacy OFDM mode
 * @STANDALONE_MT76_TM_TX_MODE_HT: 802.11n MCS
 * @STANDALONE_MT76_TM_TX_MODE_VHT: 802.11ac MCS
 * @STANDALONE_MT76_TM_TX_MODE_HE_SU: 802.11ax single-user MIMO
 * @STANDALONE_MT76_TM_TX_MODE_HE_EXT_SU: 802.11ax extended-range SU
 * @STANDALONE_MT76_TM_TX_MODE_HE_TB: 802.11ax trigger-based
 * @STANDALONE_MT76_TM_TX_MODE_HE_MU: 802.11ax multi-user MIMO
 */
enum standalone_mt76_testmode_tx_mode {
	STANDALONE_MT76_TM_TX_MODE_CCK,
	STANDALONE_MT76_TM_TX_MODE_OFDM,
	STANDALONE_MT76_TM_TX_MODE_HT,
	STANDALONE_MT76_TM_TX_MODE_VHT,
	STANDALONE_MT76_TM_TX_MODE_HE_SU,
	STANDALONE_MT76_TM_TX_MODE_HE_EXT_SU,
	STANDALONE_MT76_TM_TX_MODE_HE_TB,
	STANDALONE_MT76_TM_TX_MODE_HE_MU,

	/* keep last */
	NUM_STANDALONE_MT76_TM_TX_MODES,
	STANDALONE_MT76_TM_TX_MODE_MAX = NUM_STANDALONE_MT76_TM_TX_MODES - 1,
};

#endif

/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */

#ifndef __STANDALONE_MT76_H
#define __STANDALONE_MT76_H

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/leds.h>
#include <linux/usb.h>
#include <linux/average.h>
#if (IS_BUILTIN(CONFIG_NET_AIROHA_NPU) || IS_MODULE(CONFIG_NET_AIROHA_NPU))
#include <linux/soc/airoha/airoha_offload.h>
#else
#include "airoha_offload.h"
#endif
#include <linux/soc/mediatek/mtk_wed.h>
#include <net/netlink.h>
#include <net/mac80211.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,6,0)
#include <net/page_pool.h>
#else
#include <net/page_pool/helpers.h>
#endif
#include "util.h"
#include "testmode.h"

#define MT_MCU_RING_SIZE	32
#define MT_RX_BUF_SIZE		2048
#define MT_SKB_HEAD_LEN		256

#define MT_MAX_NON_AQL_PKT	16
#define MT_TXQ_FREE_THR		32

#define STANDALONE_MT76_TOKEN_FREE_THR	64

#define STANDALONE_MT76_WED_WDS_MIN	256
#define STANDALONE_MT76_WED_WDS_MAX	272

#define MT_QFLAG_WED_RING	GENMASK(1, 0)
#define MT_QFLAG_WED_TYPE	GENMASK(4, 2)
#define MT_QFLAG_WED		BIT(5)
#define MT_QFLAG_WED_RRO	BIT(6)
#define MT_QFLAG_WED_RRO_EN	BIT(7)
#define MT_QFLAG_EMI_EN		BIT(8)
#define MT_QFLAG_NPU		BIT(9)

#define __MT_WED_Q(_type, _n)	(MT_QFLAG_WED | \
				 FIELD_PREP(MT_QFLAG_WED_TYPE, _type) | \
				 FIELD_PREP(MT_QFLAG_WED_RING, _n))
#define __MT_WED_RRO_Q(_type, _n)	(MT_QFLAG_WED_RRO | __MT_WED_Q(_type, _n))

#define MT_WED_Q_TX(_n)		__MT_WED_Q(STANDALONE_MT76_WED_Q_TX, _n)
#define MT_WED_Q_RX(_n)		__MT_WED_Q(STANDALONE_MT76_WED_Q_RX, _n)
#define MT_WED_Q_TXFREE		__MT_WED_Q(STANDALONE_MT76_WED_Q_TXFREE, 0)
#define MT_WED_RRO_Q_DATA(_n)	__MT_WED_RRO_Q(STANDALONE_MT76_WED_RRO_Q_DATA, _n)
#define MT_WED_RRO_Q_MSDU_PG(_n)	__MT_WED_RRO_Q(STANDALONE_MT76_WED_RRO_Q_MSDU_PG, _n)
#define MT_WED_RRO_Q_IND	__MT_WED_RRO_Q(STANDALONE_MT76_WED_RRO_Q_IND, 0)
#define MT_WED_RRO_Q_RXDMAD_C	__MT_WED_RRO_Q(STANDALONE_MT76_WED_RRO_Q_RXDMAD_C, 0)

#define __MT_NPU_Q(_type, _n)	(MT_QFLAG_NPU | \
				 FIELD_PREP(MT_QFLAG_WED_TYPE, _type) | \
				 FIELD_PREP(MT_QFLAG_WED_RING, _n))
#define MT_NPU_Q_TX(_n)		__MT_NPU_Q(STANDALONE_MT76_WED_Q_TX, _n)
#define MT_NPU_Q_RX(_n)		__MT_NPU_Q(STANDALONE_MT76_WED_Q_RX, _n)
#define MT_NPU_Q_TXFREE(_n)	(FIELD_PREP(MT_QFLAG_WED_TYPE, STANDALONE_MT76_WED_Q_TXFREE) | \
				 FIELD_PREP(MT_QFLAG_WED_RING, _n))

struct standalone_mt76_dev;
struct standalone_mt76_phy;
struct standalone_mt76_wcid;
struct standalone_mt76s_intr;
struct standalone_mt76_chanctx;
struct standalone_mt76_vif_link;

struct standalone_mt76_reg_pair {
	u32 reg;
	u32 value;
};

enum standalone_mt76_bus_type {
	STANDALONE_MT76_BUS_MMIO,
	STANDALONE_MT76_BUS_USB,
	STANDALONE_MT76_BUS_SDIO,
};

enum standalone_mt76_wed_type {
	STANDALONE_MT76_WED_Q_TX,
	STANDALONE_MT76_WED_Q_TXFREE,
	STANDALONE_MT76_WED_Q_RX,
	STANDALONE_MT76_WED_RRO_Q_DATA,
	STANDALONE_MT76_WED_RRO_Q_MSDU_PG,
	STANDALONE_MT76_WED_RRO_Q_IND,
	STANDALONE_MT76_WED_RRO_Q_RXDMAD_C,
};

enum standalone_mt76_hwrro_mode {
	STANDALONE_MT76_HWRRO_OFF,
	STANDALONE_MT76_HWRRO_V3,
	STANDALONE_MT76_HWRRO_V3_1,
};

struct standalone_mt76_bus_ops {
	u32 (*rr)(struct standalone_mt76_dev *dev, u32 offset);
	void (*wr)(struct standalone_mt76_dev *dev, u32 offset, u32 val);
	u32 (*rmw)(struct standalone_mt76_dev *dev, u32 offset, u32 mask, u32 val);
	void (*write_copy)(struct standalone_mt76_dev *dev, u32 offset, const void *data,
			   int len);
	void (*read_copy)(struct standalone_mt76_dev *dev, u32 offset, void *data,
			  int len);
	int (*wr_rp)(struct standalone_mt76_dev *dev, u32 base,
		     const struct standalone_mt76_reg_pair *rp, int len);
	int (*rd_rp)(struct standalone_mt76_dev *dev, u32 base,
		     struct standalone_mt76_reg_pair *rp, int len);
	enum standalone_mt76_bus_type type;
};

#define standalone_mt76_is_usb(dev) ((dev)->bus->type == STANDALONE_MT76_BUS_USB)
#define standalone_mt76_is_mmio(dev) ((dev)->bus->type == STANDALONE_MT76_BUS_MMIO)
#define standalone_mt76_is_sdio(dev) ((dev)->bus->type == STANDALONE_MT76_BUS_SDIO)

enum standalone_mt76_txq_id {
	MT_TXQ_VO = IEEE80211_AC_VO,
	MT_TXQ_VI = IEEE80211_AC_VI,
	MT_TXQ_BE = IEEE80211_AC_BE,
	MT_TXQ_BK = IEEE80211_AC_BK,
	MT_TXQ_PSD,
	MT_TXQ_BEACON,
	MT_TXQ_CAB,
	__MT_TXQ_MAX
};

enum standalone_mt76_mcuq_id {
	MT_MCUQ_WM,
	MT_MCUQ_WA,
	MT_MCUQ_FWDL,
	__MT_MCUQ_MAX
};

enum standalone_mt76_rxq_id {
	MT_RXQ_MAIN,
	MT_RXQ_MCU,
	MT_RXQ_MCU_WA,
	MT_RXQ_BAND1,
	MT_RXQ_BAND1_WA,
	MT_RXQ_MAIN_WA,
	MT_RXQ_BAND2,
	MT_RXQ_BAND2_WA,
	MT_RXQ_RRO_BAND0,
	MT_RXQ_RRO_BAND1,
	MT_RXQ_RRO_BAND2,
	MT_RXQ_MSDU_PAGE_BAND0,
	MT_RXQ_MSDU_PAGE_BAND1,
	MT_RXQ_MSDU_PAGE_BAND2,
	MT_RXQ_TXFREE_BAND0,
	MT_RXQ_TXFREE_BAND1,
	MT_RXQ_TXFREE_BAND2,
	MT_RXQ_RRO_IND,
	MT_RXQ_RRO_RXDMAD_C,
	MT_RXQ_NPU0,
	MT_RXQ_NPU1,
	__MT_RXQ_MAX
};

enum standalone_mt76_band_id {
	MT_BAND0,
	MT_BAND1,
	MT_BAND2,
	__MT_MAX_BAND
};

enum standalone_mt76_cipher_type {
	MT_CIPHER_NONE,
	MT_CIPHER_WEP40,
	MT_CIPHER_TKIP,
	MT_CIPHER_TKIP_NO_MIC,
	MT_CIPHER_AES_CCMP,
	MT_CIPHER_WEP104,
	MT_CIPHER_BIP_CMAC_128,
	MT_CIPHER_WEP128,
	MT_CIPHER_WAPI,
	MT_CIPHER_CCMP_CCX,
	MT_CIPHER_CCMP_256,
	MT_CIPHER_GCMP,
	MT_CIPHER_GCMP_256,
};

enum standalone_mt76_dfs_state {
	MT_DFS_STATE_UNKNOWN,
	MT_DFS_STATE_DISABLED,
	MT_DFS_STATE_CAC,
	MT_DFS_STATE_ACTIVE,
};

#define STANDALONE_MT76_RNR_SCAN_MAX_BSSIDS       16
struct standalone_mt76_scan_rnr_param {
	u8 bssid[STANDALONE_MT76_RNR_SCAN_MAX_BSSIDS][ETH_ALEN];
	u8 channel[STANDALONE_MT76_RNR_SCAN_MAX_BSSIDS];
	u8 random_mac[ETH_ALEN];
	u8 seq_num;
	u8 bssid_num;
	u32 sreq_flag;
};

struct standalone_mt76_queue_buf {
	dma_addr_t addr;
	u16 len:15,
	    skip_unmap:1;
};

struct standalone_mt76_tx_info {
	struct standalone_mt76_queue_buf buf[32];
	struct sk_buff *skb;
	int nbuf;
	u32 info;
};

struct standalone_mt76_queue_entry {
	union {
		void *buf;
		struct sk_buff *skb;
	};
	union {
		struct standalone_mt76_txwi_cache *txwi;
		struct urb *urb;
		int buf_sz;
	};
	dma_addr_t dma_addr[2];
	u16 dma_len[2];
	u16 wcid;
	bool skip_buf0:1;
	bool skip_buf1:1;
	bool done:1;
};

struct standalone_mt76_queue_regs {
	u32 desc_base;
	u32 ring_size;
	u32 cpu_idx;
	u32 dma_idx;
} __packed __aligned(4);

struct standalone_mt76_queue {
	struct standalone_mt76_queue_regs __iomem *regs;

	spinlock_t lock;
	spinlock_t cleanup_lock;
	struct standalone_mt76_queue_entry *entry;
	struct standalone_mt76_rro_desc *rro_desc;
	struct standalone_mt76_desc *desc;

	u16 first;
	u16 head;
	u16 tail;
	u8 hw_idx;
	u8 ep;
	int ndesc;
	int queued;
	int buf_size;
	bool stopped;
	bool blocked;

	u8 buf_offset;
	u16 flags;
	u8 magic_cnt;

	__le16 *emi_cpu_idx;

	struct mtk_wed_device *wed;
	struct standalone_mt76_dev *dev;
	u32 wed_regs;

	dma_addr_t desc_dma;
	struct sk_buff *rx_head;
	struct page_pool *page_pool;
};

struct standalone_mt76_mcu_ops {
	unsigned int max_retry;
	u32 headroom;
	u32 tailroom;

	int (*mcu_send_msg)(struct standalone_mt76_dev *dev, int cmd, const void *data,
			    int len, bool wait_resp);
	int (*mcu_skb_prepare_msg)(struct standalone_mt76_dev *dev, struct sk_buff *skb,
				   int cmd, int *seq);
	int (*mcu_skb_send_msg)(struct standalone_mt76_dev *dev, struct sk_buff *skb,
				int cmd, int *seq);
	int (*mcu_parse_response)(struct standalone_mt76_dev *dev, int cmd,
				  struct sk_buff *skb, int seq);
	u32 (*mcu_rr)(struct standalone_mt76_dev *dev, u32 offset);
	void (*mcu_wr)(struct standalone_mt76_dev *dev, u32 offset, u32 val);
	int (*mcu_wr_rp)(struct standalone_mt76_dev *dev, u32 base,
			 const struct standalone_mt76_reg_pair *rp, int len);
	int (*mcu_rd_rp)(struct standalone_mt76_dev *dev, u32 base,
			 struct standalone_mt76_reg_pair *rp, int len);
	int (*mcu_restart)(struct standalone_mt76_dev *dev);
};

struct standalone_mt76_queue_ops {
	int (*init)(struct standalone_mt76_dev *dev,
		    int (*poll)(struct napi_struct *napi, int budget));

	int (*alloc)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
		     int idx, int n_desc, int bufsize,
		     u32 ring_base);

	int (*tx_queue_skb)(struct standalone_mt76_phy *phy, struct standalone_mt76_queue *q,
			    enum standalone_mt76_txq_id qid, struct sk_buff *skb,
			    struct standalone_mt76_wcid *wcid, struct ieee80211_sta *sta);

	int (*tx_queue_skb_raw)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
				struct sk_buff *skb, u32 tx_info);

	void *(*dequeue)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, bool flush,
			 int *len, u32 *info, bool *more);

	void (*rx_reset)(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id qid);

	void (*tx_cleanup)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
			   bool flush);

	void (*rx_queue_init)(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id qid,
			      int (*poll)(struct napi_struct *napi, int budget));

	void (*rx_cleanup)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q);

	void (*kick)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q);

	void (*reset_q)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
			bool reset_idx);
};

enum standalone_mt76_phy_type {
	MT_PHY_TYPE_CCK,
	MT_PHY_TYPE_OFDM,
	MT_PHY_TYPE_HT,
	MT_PHY_TYPE_HT_GF,
	MT_PHY_TYPE_VHT,
	MT_PHY_TYPE_HE_SU = 8,
	MT_PHY_TYPE_HE_EXT_SU,
	MT_PHY_TYPE_HE_TB,
	MT_PHY_TYPE_HE_MU,
	MT_PHY_TYPE_EHT_SU = 13,
	MT_PHY_TYPE_EHT_TRIG,
	MT_PHY_TYPE_EHT_MU,
	__MT_PHY_TYPE_MAX,
};

struct standalone_mt76_sta_stats {
	u64 tx_mode[__MT_PHY_TYPE_MAX];
	u64 tx_bw[5];		/* 20, 40, 80, 160, 320 */
	u64 tx_nss[4];		/* 1, 2, 3, 4 */
	u64 tx_mcs[16];		/* mcs idx */
	u64 tx_bytes;
	/* WED TX */
	u32 tx_packets;		/* unit: MSDU */
	u32 tx_retries;
	u32 tx_failed;
	/* WED RX */
	u64 rx_bytes;
	u32 rx_packets;
	u32 rx_errors;
	u32 rx_drops;
};

enum standalone_mt76_wcid_flags {
	MT_WCID_FLAG_CHECK_PS,
	MT_WCID_FLAG_PS,
	MT_WCID_FLAG_4ADDR,
	MT_WCID_FLAG_HDR_TRANS,
	MT_WCID_FLAG_TDLS_PEER,
};

#define STANDALONE_MT76_N_WCIDS 1088
#define STANDALONE_MT76_BEACON_MON_MAX_MISS	7

/* stored in ieee80211_tx_info::hw_queue */
#define MT_TX_HW_QUEUE_PHY		GENMASK(3, 2)

DECLARE_EWMA(signal, 10, 8);

#define MT_WCID_TX_INFO_RATE		GENMASK(15, 0)
#define MT_WCID_TX_INFO_NSS		GENMASK(17, 16)
#define MT_WCID_TX_INFO_TXPWR_ADJ	GENMASK(25, 18)
#define MT_WCID_TX_INFO_SET		BIT(31)

struct standalone_mt76_wcid {
	struct standalone_mt76_rx_tid __rcu *aggr[IEEE80211_NUM_TIDS];

	atomic_t non_aql_packets;
	unsigned long flags;

	struct ewma_signal rssi;
	int inactive_count;

	struct rate_info rate;
	unsigned long ampdu_state;

	u16 idx;
	u8 hw_key_idx;
	u8 hw_key_idx2;

	u8 offchannel:1;
	u8 sta:1;
	u8 sta_disabled:1;
	u8 amsdu:1;
	u8 phy_idx:2;
	u8 link_id:4;
	bool link_valid;

	u8 rx_check_pn;
	u8 rx_key_pn[IEEE80211_NUM_TIDS + 1][6];
	u16 cipher;

	u32 tx_info;
	bool sw_iv;

	struct list_head tx_list;
	struct sk_buff_head tx_pending;
	struct sk_buff_head tx_offchannel;

	struct list_head list;
	struct idr pktid;

	struct standalone_mt76_sta_stats stats;

	struct list_head poll_list;

	struct standalone_mt76_wcid *def_wcid;
};

struct standalone_mt76_txq {
	u16 wcid;

	u16 agg_ssn;
	bool send_bar;
	bool aggr;
};

/* data0 */
#define RRO_IND_DATA0_IND_REASON_MASK	GENMASK(31, 28)
#define RRO_IND_DATA0_START_SEQ_MASK	GENMASK(27, 16)
#define RRO_IND_DATA0_SEQ_ID_MASK	GENMASK(11, 0)
/* data1 */
#define RRO_IND_DATA1_MAGIC_CNT_MASK	GENMASK(31, 29)
#define RRO_IND_DATA1_IND_COUNT_MASK	GENMASK(12, 0)
struct standalone_mt76_wed_rro_ind {
	__le32 data0;
	__le32 data1;
};

struct standalone_mt76_txwi_cache {
	struct list_head list;
	dma_addr_t dma_addr;

	union {
		struct sk_buff *skb;
		void *ptr;
	};

	u8 qid;
	u8 phy_idx;
};

struct standalone_mt76_rx_tid {
	struct rcu_head rcu_head;

	struct standalone_mt76_dev *dev;

	spinlock_t lock;
	struct delayed_work reorder_work;

	u16 id;
	u16 head;
	u16 size;
	u16 nframes;

	u8 num;

	u8 started:1, stopped:1, timer_pending:1;

	struct sk_buff *reorder_buf[];
};

#define MT_TX_CB_DMA_DONE		BIT(0)
#define MT_TX_CB_TXS_DONE		BIT(1)
#define MT_TX_CB_TXS_FAILED		BIT(2)

#define MT_PACKET_ID_MASK		GENMASK(6, 0)
#define MT_PACKET_ID_NO_ACK		0
#define MT_PACKET_ID_NO_SKB		1
#define MT_PACKET_ID_WED		2
#define MT_PACKET_ID_FIRST		3
#define MT_PACKET_ID_HAS_RATE		BIT(7)
/* This is timer for when to give up when waiting for TXS callback,
 * with starting time being the time at which the DMA_DONE callback
 * was seen (so, we know packet was processed then, it should not take
 * long after that for firmware to send the TXS callback if it is going
 * to do so.)
 */
#define MT_TX_STATUS_SKB_TIMEOUT	(HZ / 4)

struct standalone_mt76_tx_cb {
	unsigned long jiffies;
	u16 wcid;
	u8 pktid;
	u8 flags;
};

enum {
	STANDALONE_MT76_STATE_INITIALIZED,
	STANDALONE_MT76_STATE_REGISTERED,
	STANDALONE_MT76_STATE_RUNNING,
	STANDALONE_MT76_STATE_MCU_RUNNING,
	STANDALONE_MT76_SCANNING,
	STANDALONE_MT76_HW_SCANNING,
	STANDALONE_MT76_HW_SCHED_SCANNING,
	STANDALONE_MT76_RESTART,
	STANDALONE_MT76_RESET,
	STANDALONE_MT76_MCU_RESET,
	STANDALONE_MT76_REMOVED,
	STANDALONE_MT76_READING_STATS,
	STANDALONE_MT76_STATE_POWER_OFF,
	STANDALONE_MT76_STATE_SUSPEND,
	STANDALONE_MT76_STATE_ROC,
	STANDALONE_MT76_STATE_PM,
	STANDALONE_MT76_STATE_WED_RESET,
};

enum standalone_mt76_sta_event {
	STANDALONE_MT76_STA_EVENT_ASSOC,
	STANDALONE_MT76_STA_EVENT_AUTHORIZE,
	STANDALONE_MT76_STA_EVENT_DISASSOC,
};

struct standalone_mt76_hw_cap {
	bool has_2ghz;
	bool has_5ghz;
	bool has_6ghz;
};

#define MT_DRV_TXWI_NO_FREE		BIT(0)
#define MT_DRV_TX_ALIGNED4_SKBS		BIT(1)
#define MT_DRV_SW_RX_AIRTIME		BIT(2)
#define MT_DRV_RX_DMA_HDR		BIT(3)
#define MT_DRV_HW_MGMT_TXQ		BIT(4)
#define MT_DRV_AMSDU_OFFLOAD		BIT(5)
#define MT_DRV_IGNORE_TXS_FAILED	BIT(6)
#define MT_DRV_HW_PS_BUFFERING		BIT(7)

struct standalone_mt76_driver_ops {
	u32 drv_flags;
	u32 survey_flags;
	u16 txwi_size;
	u16 token_size;

	unsigned int link_data_size;

	void (*update_survey)(struct standalone_mt76_phy *phy);
	int (*set_channel)(struct standalone_mt76_phy *phy);

	int (*tx_prepare_skb)(struct standalone_mt76_dev *dev, void *txwi_ptr,
			      enum standalone_mt76_txq_id qid, struct standalone_mt76_wcid *wcid,
			      struct ieee80211_sta *sta,
			      struct standalone_mt76_tx_info *tx_info);

	void (*tx_complete_skb)(struct standalone_mt76_dev *dev,
				struct standalone_mt76_queue_entry *e);

	bool (*tx_status_data)(struct standalone_mt76_dev *dev, u8 *update);

	bool (*rx_check)(struct standalone_mt76_dev *dev, void *data, int len);

	void (*rx_skb)(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id q,
		       struct sk_buff *skb, u32 *info);

	void (*rx_poll_complete)(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id q);

	void (*rx_rro_ind_process)(struct standalone_mt76_dev *dev, void *data);
	int (*rx_rro_add_msdu_page)(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
				    dma_addr_t p, void *data);

	void (*sta_ps)(struct standalone_mt76_dev *dev, struct ieee80211_sta *sta,
		       bool ps);

	int (*sta_add)(struct standalone_mt76_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);

	int (*sta_event)(struct standalone_mt76_dev *dev, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, enum standalone_mt76_sta_event ev);

	void (*sta_remove)(struct standalone_mt76_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);

	int (*vif_link_add)(struct standalone_mt76_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct standalone_mt76_vif_link *mlink);

	void (*vif_link_remove)(struct standalone_mt76_phy *phy,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *link_conf,
				struct standalone_mt76_vif_link *mlink);
};

struct standalone_mt76_channel_state {
	u64 cc_active;
	u64 cc_busy;
	u64 cc_rx;
	u64 cc_bss_rx;
	u64 cc_tx;

	s8 noise;
};

struct standalone_mt76_sband {
	struct ieee80211_supported_band sband;
	struct standalone_mt76_channel_state *chan;
};

/* addr req mask */
#define MT_VEND_TYPE_EEPROM	BIT(31)
#define MT_VEND_TYPE_CFG	BIT(30)
#define MT_VEND_TYPE_MASK	(MT_VEND_TYPE_EEPROM | MT_VEND_TYPE_CFG)

#define MT_VEND_ADDR(type, n)	(MT_VEND_TYPE_##type | (n))
enum mt_vendor_req {
	MT_VEND_DEV_MODE =	0x1,
	MT_VEND_WRITE =		0x2,
	MT_VEND_POWER_ON =	0x4,
	MT_VEND_MULTI_WRITE =	0x6,
	MT_VEND_MULTI_READ =	0x7,
	MT_VEND_READ_EEPROM =	0x9,
	MT_VEND_WRITE_FCE =	0x42,
	MT_VEND_WRITE_CFG =	0x46,
	MT_VEND_READ_CFG =	0x47,
	MT_VEND_READ_EXT =	0x63,
	MT_VEND_WRITE_EXT =	0x66,
	MT_VEND_FEATURE_SET =	0x91,
};

enum standalone_mt76u_in_ep {
	MT_EP_IN_PKT_RX,
	MT_EP_IN_CMD_RESP,
	__MT_EP_IN_MAX,
};

enum standalone_mt76u_out_ep {
	MT_EP_OUT_INBAND_CMD,
	MT_EP_OUT_AC_BE,
	MT_EP_OUT_AC_BK,
	MT_EP_OUT_AC_VI,
	MT_EP_OUT_AC_VO,
	MT_EP_OUT_HCCA,
	__MT_EP_OUT_MAX,
};

struct standalone_mt76_mcu {
	struct mutex mutex;
	u32 msg_seq;
	int timeout;

	struct sk_buff_head res_q;
	wait_queue_head_t wait;
};

#define MT_TX_SG_MAX_SIZE	8
#define MT_RX_SG_MAX_SIZE	4
#define MT_NUM_TX_ENTRIES	256
#define MT_NUM_RX_ENTRIES	128
#define MCU_RESP_URB_SIZE	1024
struct standalone_mt76_usb {
	struct mutex usb_ctrl_mtx;
	u8 *data;
	u16 data_len;

	struct standalone_mt76_worker status_worker;
	struct standalone_mt76_worker rx_worker;

	struct work_struct stat_work;

	u8 out_ep[__MT_EP_OUT_MAX];
	u8 in_ep[__MT_EP_IN_MAX];
	bool sg_en;

	struct standalone_mt76u_mcu {
		u8 *data;
		/* multiple reads */
		struct standalone_mt76_reg_pair *rp;
		int rp_len;
		u32 base;
	} mcu;
};

#define STANDALONE_MT76S_XMIT_BUF_SZ	0x3fe00
#define STANDALONE_MT76S_NUM_TX_ENTRIES	256
#define STANDALONE_MT76S_NUM_RX_ENTRIES	512
struct standalone_mt76_sdio {
	struct standalone_mt76_worker txrx_worker;
	struct standalone_mt76_worker status_worker;
	struct standalone_mt76_worker net_worker;
	struct standalone_mt76_worker stat_worker;

	u8 *xmit_buf;
	u32 xmit_buf_sz;

	struct sdio_func *func;
	void *intr_data;
	u8 hw_ver;
	wait_queue_head_t wait;

	int pse_mcu_quota_max;
	struct {
		int pse_data_quota;
		int ple_data_quota;
		int pse_mcu_quota;
		int pse_page_size;
		int deficit;
	} sched;

	int (*parse_irq)(struct standalone_mt76_dev *dev, struct standalone_mt76s_intr *intr);
};

struct standalone_mt76_mmio {
	void __iomem *regs;
	spinlock_t irq_lock;
	u32 irqmask;

	struct mtk_wed_device wed;
	struct mtk_wed_device wed_hif2;
	struct completion wed_reset;
	struct completion wed_reset_complete;

	struct airoha_ppe_dev __rcu *ppe_dev;
	struct airoha_npu __rcu *npu;
	phys_addr_t phy_addr;
	int npu_type;
};

struct standalone_mt76_rx_status {
	union {
		struct standalone_mt76_wcid *wcid;
		u16 wcid_idx;
	};

	u32 reorder_time;

	u32 ampdu_ref;
	u32 timestamp;

	u8 iv[6];

	u8 phy_idx:2;
	u8 aggr:1;
	u8 qos_ctl;
	u16 seqno;

	u16 freq;
	u32 flag;
	u8 enc_flags;
	u8 encoding:3, bw:4;
	union {
		struct {
			u8 he_ru:3;
			u8 he_gi:2;
			u8 he_dcm:1;
		};
		struct {
			u8 ru:4;
			u8 gi:2;
		} eht;
	};

	u8 amsdu:1, first_amsdu:1, last_amsdu:1;
	u8 rate_idx;
	u8 nss:5, band:3;
	s8 signal;
	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];
};

struct standalone_mt76_freq_range_power {
	const struct cfg80211_sar_freq_ranges *range;
	s8 power;
};

struct standalone_mt76_testmode_ops {
	int (*set_state)(struct standalone_mt76_phy *phy, enum standalone_mt76_testmode_state state);
	int (*set_params)(struct standalone_mt76_phy *phy, struct nlattr **tb,
			  enum standalone_mt76_testmode_state new_state);
	int (*dump_stats)(struct standalone_mt76_phy *phy, struct sk_buff *msg);
};

struct standalone_mt76_testmode_data {
	enum standalone_mt76_testmode_state state;

	u32 param_set[DIV_ROUND_UP(NUM_STANDALONE_MT76_TM_ATTRS, 32)];
	struct sk_buff *tx_skb;

	u32 tx_count;
	u16 tx_mpdu_len;

	u8 tx_rate_mode;
	u8 tx_rate_idx;
	u8 tx_rate_nss;
	u8 tx_rate_sgi;
	u8 tx_rate_ldpc;
	u8 tx_rate_stbc;
	u8 tx_ltf;

	u8 tx_antenna_mask;
	u8 tx_spe_idx;

	u8 tx_duty_cycle;
	u32 tx_time;
	u32 tx_ipg;

	u32 freq_offset;

	u8 tx_power[4];
	u8 tx_power_control;

	u8 addr[3][ETH_ALEN];

	u32 tx_pending;
	u32 tx_queued;
	u16 tx_queued_limit;
	u32 tx_done;
	struct {
		u64 packets[__MT_RXQ_MAX];
		u64 fcs_error[__MT_RXQ_MAX];
	} rx_stats;
};

struct standalone_mt76_vif_link {
	u8 idx;
	u8 link_idx;
	u8 omac_idx;
	u8 band_idx;
	u8 wmm_idx;
	u8 scan_seq_num;
	u8 cipher;
	u8 basic_rates_idx;
	u8 mcast_rates_idx;
	u8 beacon_rates_idx;
	bool offchannel;
	unsigned long beacon_mon_last;
	u16 beacon_mon_interval;
	struct ieee80211_chanctx_conf *ctx;
	struct standalone_mt76_wcid *wcid;
	struct standalone_mt76_vif_data *mvif;
	struct rcu_head rcu_head;
};

struct standalone_mt76_vif_data {
	struct standalone_mt76_vif_link __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];
	struct standalone_mt76_vif_link __rcu *offchannel_link;

	struct standalone_mt76_phy *roc_phy;
	u16 valid_links;
	u8 deflink_id;
};

struct standalone_mt76_phy {
	struct ieee80211_hw *hw;
	struct standalone_mt76_dev *dev;
	void *priv;

	unsigned long state;
	unsigned int num_sta;
	u8 band_idx;

	spinlock_t tx_lock;
	struct list_head tx_list;
	struct standalone_mt76_queue *q_tx[__MT_TXQ_MAX];

	atomic_t mgmt_tx_pending;

	struct cfg80211_chan_def chandef;
	struct cfg80211_chan_def main_chandef;
	bool offchannel;
	bool radar_enabled;

	struct delayed_work roc_work;
	struct ieee80211_vif *roc_vif;
	struct standalone_mt76_vif_link *roc_link;

	struct standalone_mt76_chanctx *chanctx;

	struct standalone_mt76_channel_state *chan_state;
	enum standalone_mt76_dfs_state dfs_state;
	ktime_t survey_time;

	u32 aggr_stats[32];

	struct standalone_mt76_hw_cap cap;
	struct standalone_mt76_sband sband_2g;
	struct standalone_mt76_sband sband_5g;
	struct standalone_mt76_sband sband_6g;

	u8 macaddr[ETH_ALEN];

	int txpower_cur;
	u8 antenna_mask;
	u16 chainmask;

#ifdef CONFIG_NL80211_TESTMODE
	struct standalone_mt76_testmode_data test;
#endif

	struct delayed_work mac_work;
	u8 mac_work_count;

	struct {
		struct sk_buff *head;
		struct sk_buff **tail;
		u16 seqno;
	} rx_amsdu[__MT_RXQ_MAX];

	struct standalone_mt76_freq_range_power *frp;

	struct {
		struct led_classdev cdev;
		char name[32];
		bool al;
		u8 pin;
	} leds;
};

struct standalone_mt76_dev {
	struct standalone_mt76_phy phy; /* must be first */
	struct standalone_mt76_phy *phys[__MT_MAX_BAND];
	struct standalone_mt76_phy *band_phys[NUM_NL80211_BANDS];

	struct ieee80211_hw *hw;

	spinlock_t wed_lock;
	spinlock_t lock;
	spinlock_t cc_lock;

	u32 cur_cc_bss_rx;

	struct standalone_mt76_rx_status rx_ampdu_status;
	u32 rx_ampdu_len;
	u32 rx_ampdu_ref;

	struct mutex mutex;

	const struct standalone_mt76_bus_ops *bus;
	const struct standalone_mt76_driver_ops *drv;
	const struct standalone_mt76_mcu_ops *mcu_ops;
	struct device *dev;
	struct device *dma_dev;

	struct standalone_mt76_mcu mcu;

	struct net_device *napi_dev;
	struct net_device *tx_napi_dev;
	spinlock_t rx_lock;
	struct napi_struct napi[__MT_RXQ_MAX];
	struct sk_buff_head rx_skb[__MT_RXQ_MAX];
	struct tasklet_struct irq_tasklet;

	struct list_head txwi_cache;
	struct list_head rxwi_cache;
	struct standalone_mt76_queue *q_mcu[__MT_MCUQ_MAX];
	struct standalone_mt76_queue q_rx[__MT_RXQ_MAX];
	const struct standalone_mt76_queue_ops *queue_ops;
	int tx_dma_idx[4];
	enum standalone_mt76_hwrro_mode hwrro_mode;

	struct standalone_mt76_worker tx_worker;
	struct napi_struct tx_napi;

	spinlock_t token_lock;
	struct idr token;
	u16 wed_token_count;
	u16 token_count;
	u16 token_start;
	u16 token_size;

	spinlock_t rx_token_lock;
	struct idr rx_token;
	u16 rx_token_size;

	wait_queue_head_t tx_wait;
	/* spinclock used to protect wcid pktid linked list */
	spinlock_t status_lock;

	u32 wcid_mask[DIV_ROUND_UP(STANDALONE_MT76_N_WCIDS, 32)];

	u64 vif_mask;

	struct standalone_mt76_wcid global_wcid;
	struct standalone_mt76_wcid __rcu *wcid[STANDALONE_MT76_N_WCIDS];
	struct list_head wcid_list;

	struct list_head sta_poll_list;
	spinlock_t sta_poll_lock;

	u32 rev;

	struct tasklet_struct pre_tbtt_tasklet;
	int beacon_int;
	u8 beacon_mask;

	struct debugfs_blob_wrapper eeprom;
	struct debugfs_blob_wrapper otp;

	char alpha2[3];
	enum nl80211_dfs_regions region;

	struct standalone_mt76_scan_rnr_param rnr;

	u32 debugfs_reg;

	u8 csa_complete;

	u32 rxfilter;

	struct delayed_work scan_work;
	spinlock_t scan_lock;
	struct {
		struct cfg80211_scan_request *req;
		struct ieee80211_channel *chan;
		struct ieee80211_vif *vif;
		struct standalone_mt76_vif_link *mlink;
		struct standalone_mt76_phy *phy;
		int chan_idx;
		bool beacon_wait;
		bool beacon_received;
	} scan;

#ifdef CONFIG_NL80211_TESTMODE
	const struct standalone_mt76_testmode_ops *test_ops;
	struct {
		const char *name;
		u32 offset;
	} test_mtd;
#endif
	struct workqueue_struct *wq;

	union {
		struct standalone_mt76_mmio mmio;
		struct standalone_mt76_usb usb;
		struct standalone_mt76_sdio sdio;
	};

	atomic_t bus_hung;
};

/* per-phy stats.  */
struct standalone_mt76_mib_stats {
	u32 ack_fail_cnt;
	u32 fcs_err_cnt;
	u32 rts_cnt;
	u32 rts_retries_cnt;
	u32 ba_miss_cnt;
	u32 tx_bf_cnt;
	u32 tx_mu_bf_cnt;
	u32 tx_mu_mpdu_cnt;
	u32 tx_mu_acked_mpdu_cnt;
	u32 tx_su_acked_mpdu_cnt;
	u32 tx_bf_ibf_ppdu_cnt;
	u32 tx_bf_ebf_ppdu_cnt;

	u32 tx_bf_rx_fb_all_cnt;
	u32 tx_bf_rx_fb_eht_cnt;
	u32 tx_bf_rx_fb_he_cnt;
	u32 tx_bf_rx_fb_vht_cnt;
	u32 tx_bf_rx_fb_ht_cnt;

	u32 tx_bf_rx_fb_bw; /* value of last sample, not cumulative */
	u32 tx_bf_rx_fb_nc_cnt;
	u32 tx_bf_rx_fb_nr_cnt;
	u32 tx_bf_fb_cpl_cnt;
	u32 tx_bf_fb_trig_cnt;

	u32 tx_ampdu_cnt;
	u32 tx_stop_q_empty_cnt;
	u32 tx_mpdu_attempts_cnt;
	u32 tx_mpdu_success_cnt;
	u32 tx_pkt_ebf_cnt;
	u32 tx_pkt_ibf_cnt;

	u32 tx_rwp_fail_cnt;
	u32 tx_rwp_need_cnt;

	/* rx stats */
	u32 rx_fifo_full_cnt;
	u32 channel_idle_cnt;
	u32 primary_cca_busy_time;
	u32 secondary_cca_busy_time;
	u32 primary_energy_detect_time;
	u32 cck_mdrdy_time;
	u32 ofdm_mdrdy_time;
	u32 green_mdrdy_time;
	u32 rx_vector_mismatch_cnt;
	u32 rx_delimiter_fail_cnt;
	u32 rx_mrdy_cnt;
	u32 rx_len_mismatch_cnt;
	u32 rx_mpdu_cnt;
	u32 rx_ampdu_cnt;
	u32 rx_ampdu_bytes_cnt;
	u32 rx_ampdu_valid_subframe_cnt;
	u32 rx_ampdu_valid_subframe_bytes_cnt;
	u32 rx_pfdrop_cnt;
	u32 rx_vec_queue_overflow_drop_cnt;
	u32 rx_ba_cnt;

	u32 tx_amsdu[8];
	u32 tx_amsdu_cnt;

	/* mcu_muru_stats */
	u32 dl_cck_cnt;
	u32 dl_ofdm_cnt;
	u32 dl_htmix_cnt;
	u32 dl_htgf_cnt;
	u32 dl_vht_su_cnt;
	u32 dl_vht_2mu_cnt;
	u32 dl_vht_3mu_cnt;
	u32 dl_vht_4mu_cnt;
	u32 dl_he_su_cnt;
	u32 dl_he_ext_su_cnt;
	u32 dl_he_2ru_cnt;
	u32 dl_he_2mu_cnt;
	u32 dl_he_3ru_cnt;
	u32 dl_he_3mu_cnt;
	u32 dl_he_4ru_cnt;
	u32 dl_he_4mu_cnt;
	u32 dl_he_5to8ru_cnt;
	u32 dl_he_9to16ru_cnt;
	u32 dl_he_gtr16ru_cnt;

	u32 ul_hetrig_su_cnt;
	u32 ul_hetrig_2ru_cnt;
	u32 ul_hetrig_3ru_cnt;
	u32 ul_hetrig_4ru_cnt;
	u32 ul_hetrig_5to8ru_cnt;
	u32 ul_hetrig_9to16ru_cnt;
	u32 ul_hetrig_gtr16ru_cnt;
	u32 ul_hetrig_2mu_cnt;
	u32 ul_hetrig_3mu_cnt;
	u32 ul_hetrig_4mu_cnt;
};

struct standalone_mt76_power_limits {
	s8 cck[4];
	s8 ofdm[8];
	s8 mcs[4][10];
	s8 ru[7][12];
	s8 eht[16][16];

	struct {
		s8 cck[4];
		s8 ofdm[4];
		s8 ofdm_bf[4];
		s8 ru[7][10];
		s8 ru_bf[7][10];
	} path;
};

struct standalone_mt76_ethtool_worker_info {
	u64 *data;
	int idx;
	int initial_stat_idx;
	int worker_stat_count;
	int sta_count;
};

struct standalone_mt76_chanctx {
	struct standalone_mt76_phy *phy;
};

#define CCK_RATE(_idx, _rate) {					\
	.bitrate = _rate,					\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,			\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | (_idx),		\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (4 + _idx),	\
}

#define OFDM_RATE(_idx, _rate) {				\
	.bitrate = _rate,					\
	.hw_value = (MT_PHY_TYPE_OFDM << 8) | (_idx),		\
	.hw_value_short = (MT_PHY_TYPE_OFDM << 8) | (_idx),	\
}

extern struct ieee80211_rate standalone_mt76_rates[12];

#define __standalone_mt76_rr(dev, ...)	(dev)->bus->rr((dev), __VA_ARGS__)
#define __standalone_mt76_wr(dev, ...)	(dev)->bus->wr((dev), __VA_ARGS__)
#define __standalone_mt76_rmw(dev, ...)	(dev)->bus->rmw((dev), __VA_ARGS__)
#define __standalone_mt76_wr_copy(dev, ...)	(dev)->bus->write_copy((dev), __VA_ARGS__)
#define __standalone_mt76_rr_copy(dev, ...)	(dev)->bus->read_copy((dev), __VA_ARGS__)

#define __standalone_mt76_set(dev, offset, val)	__standalone_mt76_rmw(dev, offset, 0, val)
#define __standalone_mt76_clear(dev, offset, val)	__standalone_mt76_rmw(dev, offset, val, 0)

#define standalone_mt76_rr(dev, ...)	(dev)->standalone_mt76.bus->rr(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_wr(dev, ...)	(dev)->standalone_mt76.bus->wr(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_rmw(dev, ...)	(dev)->standalone_mt76.bus->rmw(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_wr_copy(dev, ...)	(dev)->standalone_mt76.bus->write_copy(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_rr_copy(dev, ...)	(dev)->standalone_mt76.bus->read_copy(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_wr_rp(dev, ...)	(dev)->standalone_mt76.bus->wr_rp(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_rd_rp(dev, ...)	(dev)->standalone_mt76.bus->rd_rp(&((dev)->standalone_mt76), __VA_ARGS__)


#define standalone_mt76_mcu_restart(dev, ...)	(dev)->standalone_mt76.mcu_ops->mcu_restart(&((dev)->standalone_mt76))

#define standalone_mt76_set(dev, offset, val)	standalone_mt76_rmw(dev, offset, 0, val)
#define standalone_mt76_clear(dev, offset, val)	standalone_mt76_rmw(dev, offset, val, 0)

#define standalone_mt76_get_field(_dev, _reg, _field)		\
	FIELD_GET(_field, standalone_mt76_rr(dev, _reg))

#define standalone_mt76_rmw_field(_dev, _reg, _field, _val)	\
	standalone_mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

#define __standalone_mt76_rmw_field(_dev, _reg, _field, _val)	\
	__standalone_mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

#define standalone_mt76_hw(dev) (dev)->mphy.hw

bool __standalone_mt76_poll(struct standalone_mt76_dev *dev, u32 offset, u32 mask, u32 val,
		 int timeout);

#define standalone_mt76_poll(dev, ...) __standalone_mt76_poll(&((dev)->standalone_mt76), __VA_ARGS__)

bool ____standalone_mt76_poll_msec(struct standalone_mt76_dev *dev, u32 offset, u32 mask, u32 val,
			int timeout, int kick);
#define __standalone_mt76_poll_msec(...)         ____standalone_mt76_poll_msec(__VA_ARGS__, 10)
#define standalone_mt76_poll_msec(dev, ...)      ____standalone_mt76_poll_msec(&((dev)->standalone_mt76), __VA_ARGS__, 10)
#define standalone_mt76_poll_msec_tick(dev, ...) ____standalone_mt76_poll_msec(&((dev)->standalone_mt76), __VA_ARGS__)

void standalone_mt76_mmio_init(struct standalone_mt76_dev *dev, void __iomem *regs);
void standalone_mt76_pci_disable_aspm(struct pci_dev *pdev);
bool standalone_mt76_pci_aspm_supported(struct pci_dev *pdev);

static inline u16 standalone_mt76_chip(struct standalone_mt76_dev *dev)
{
	return dev->rev >> 16;
}

static inline u16 standalone_mt76_rev(struct standalone_mt76_dev *dev)
{
	return dev->rev & 0xffff;
}

void standalone_mt76_wed_release_rx_buf(struct mtk_wed_device *wed);
void standalone_mt76_wed_offload_disable(struct mtk_wed_device *wed);
void standalone_mt76_wed_reset_complete(struct mtk_wed_device *wed);
void standalone_mt76_wed_dma_reset(struct standalone_mt76_dev *dev);
int standalone_mt76_wed_net_setup_tc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct net_device *netdev, enum tc_setup_type type,
			  void *type_data);
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
u32 standalone_mt76_wed_init_rx_buf(struct mtk_wed_device *wed, int size);
int standalone_mt76_wed_offload_enable(struct mtk_wed_device *wed);
int standalone_mt76_wed_dma_setup(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, bool reset);
#else
static inline u32 standalone_mt76_wed_init_rx_buf(struct mtk_wed_device *wed, int size)
{
	return 0;
}

static inline int standalone_mt76_wed_offload_enable(struct mtk_wed_device *wed)
{
	return 0;
}

static inline int standalone_mt76_wed_dma_setup(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
				     bool reset)
{
	return 0;
}
#endif /* CONFIG_NET_MEDIATEK_SOC_WED */

#define standalone_mt76xx_chip(dev) standalone_mt76_chip(&((dev)->standalone_mt76))
#define standalone_mt76xx_rev(dev) standalone_mt76_rev(&((dev)->standalone_mt76))

#define standalone_mt76_init_queues(dev, ...)		(dev)->standalone_mt76.queue_ops->init(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_queue_alloc(dev, ...)	(dev)->standalone_mt76.queue_ops->alloc(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_tx_queue_skb_raw(dev, ...)	(dev)->standalone_mt76.queue_ops->tx_queue_skb_raw(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_tx_queue_skb(dev, ...)	(dev)->standalone_mt76.queue_ops->tx_queue_skb(&((dev)->mphy), __VA_ARGS__)
#define standalone_mt76_queue_rx_reset(dev, ...)	(dev)->standalone_mt76.queue_ops->rx_reset(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_queue_tx_cleanup(dev, ...)	(dev)->standalone_mt76.queue_ops->tx_cleanup(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_queue_rx_init(dev, ...)	(dev)->standalone_mt76.queue_ops->rx_queue_init(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_queue_rx_cleanup(dev, ...)	(dev)->standalone_mt76.queue_ops->rx_cleanup(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_queue_kick(dev, ...)	(dev)->standalone_mt76.queue_ops->kick(&((dev)->standalone_mt76), __VA_ARGS__)
#define standalone_mt76_queue_reset(dev, ...)	(dev)->standalone_mt76.queue_ops->reset_q(&((dev)->standalone_mt76), __VA_ARGS__)

#define standalone_mt76_for_each_q_rx(dev, i)	\
	for (i = 0; i < ARRAY_SIZE((dev)->q_rx); i++)	\
		if ((dev)->q_rx[i].ndesc)


#define standalone_mt76_dereference(p, dev) \
	rcu_dereference_protected(p, lockdep_is_held(&(dev)->mutex))

static inline struct standalone_mt76_dev *standalone_mt76_wed_to_dev(struct mtk_wed_device *wed)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	if (wed->wlan.hif2)
		return container_of(wed, struct standalone_mt76_dev, mmio.wed_hif2);
#endif /* CONFIG_NET_MEDIATEK_SOC_WED */
	return container_of(wed, struct standalone_mt76_dev, mmio.wed);
}

static inline struct standalone_mt76_wcid *
__standalone_mt76_wcid_ptr(struct standalone_mt76_dev *dev, u16 idx)
{
	if (idx >= ARRAY_SIZE(dev->wcid))
		return NULL;
	return rcu_dereference(dev->wcid[idx]);
}

#define standalone_mt76_wcid_ptr(dev, idx) __standalone_mt76_wcid_ptr(&(dev)->standalone_mt76, idx)

struct standalone_mt76_dev *standalone_mt76_alloc_device(struct device *pdev, unsigned int size,
				   const struct ieee80211_ops *ops,
				   const struct standalone_mt76_driver_ops *drv_ops);
int standalone_mt76_register_device(struct standalone_mt76_dev *dev, bool vht,
			 struct ieee80211_rate *rates, int n_rates);
void standalone_mt76_unregister_device(struct standalone_mt76_dev *dev);
void standalone_mt76_free_device(struct standalone_mt76_dev *dev);
void standalone_mt76_reset_device(struct standalone_mt76_dev *dev);
void standalone_mt76_unregister_phy(struct standalone_mt76_phy *phy);

struct standalone_mt76_phy *standalone_mt76_alloc_radio_phy(struct standalone_mt76_dev *dev, unsigned int size,
				      u8 band_idx);
struct standalone_mt76_phy *standalone_mt76_alloc_phy(struct standalone_mt76_dev *dev, unsigned int size,
				const struct ieee80211_ops *ops,
				u8 band_idx);
int standalone_mt76_register_phy(struct standalone_mt76_phy *phy, bool vht,
		      struct ieee80211_rate *rates, int n_rates);
struct standalone_mt76_phy *standalone_mt76_vif_phy(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif);

struct dentry *standalone_mt76_register_debugfs_fops(struct standalone_mt76_phy *phy,
					  const struct file_operations *ops);
static inline struct dentry *standalone_mt76_register_debugfs(struct standalone_mt76_dev *dev)
{
	return standalone_mt76_register_debugfs_fops(&dev->phy, NULL);
}

int standalone_mt76_queues_read(struct seq_file *s, void *data);
void standalone_mt76_seq_puts_array(struct seq_file *file, const char *str,
			 s8 *val, int len);

int standalone_mt76_eeprom_init(struct standalone_mt76_dev *dev, int len);
int standalone_mt76_eeprom_override(struct standalone_mt76_phy *phy);
int standalone_mt76_get_of_data_from_mtd(struct standalone_mt76_dev *dev, void *eep, int offset, int len);
int standalone_mt76_get_of_data_from_nvmem(struct standalone_mt76_dev *dev, void *eep,
				const char *cell_name, int len);

struct standalone_mt76_queue *
standalone_mt76_init_queue(struct standalone_mt76_dev *dev, int qid, int idx, int n_desc,
		int ring_base, void *wed, u32 flags);
static inline int standalone_mt76_init_tx_queue(struct standalone_mt76_phy *phy, int qid, int idx,
				     int n_desc, int ring_base, void *wed,
				     u32 flags)
{
	struct standalone_mt76_queue *q;

	q = standalone_mt76_init_queue(phy->dev, qid, idx, n_desc, ring_base, wed, flags);
	if (IS_ERR(q))
		return PTR_ERR(q);

	phy->q_tx[qid] = q;

	return 0;
}

static inline int standalone_mt76_init_mcu_queue(struct standalone_mt76_dev *dev, int qid, int idx,
				      int n_desc, int ring_base)
{
	struct standalone_mt76_queue *q;

	q = standalone_mt76_init_queue(dev, qid, idx, n_desc, ring_base, NULL, 0);
	if (IS_ERR(q))
		return PTR_ERR(q);

	dev->q_mcu[qid] = q;

	return 0;
}

static inline struct standalone_mt76_phy *
standalone_mt76_dev_phy(struct standalone_mt76_dev *dev, u8 phy_idx)
{
	if ((phy_idx == MT_BAND1 && dev->phys[phy_idx]) ||
	    (phy_idx == MT_BAND2 && dev->phys[phy_idx]))
		return dev->phys[phy_idx];

	return &dev->phy;
}

static inline struct ieee80211_hw *
standalone_mt76_phy_hw(struct standalone_mt76_dev *dev, u8 phy_idx)
{
	return standalone_mt76_dev_phy(dev, phy_idx)->hw;
}

static inline u8 *
standalone_mt76_get_txwi_ptr(struct standalone_mt76_dev *dev, struct standalone_mt76_txwi_cache *t)
{
	return (u8 *)t - dev->drv->txwi_size;
}

/* increment with wrap-around */
static inline int standalone_mt76_incr(int val, int size)
{
	return (val + 1) & (size - 1);
}

/* decrement with wrap-around */
static inline int standalone_mt76_decr(int val, int size)
{
	return (val - 1) & (size - 1);
}

u8 standalone_mt76_ac_to_hwq(u8 ac);

static inline struct ieee80211_txq *
mtxq_to_txq(struct standalone_mt76_txq *mtxq)
{
	void *ptr = mtxq;

	return container_of(ptr, struct ieee80211_txq, drv_priv);
}

static inline struct ieee80211_sta *
wcid_to_sta(struct standalone_mt76_wcid *wcid)
{
	void *ptr = wcid;

	if (!wcid || !wcid->sta)
		return NULL;

	if (wcid->def_wcid)
		ptr = wcid->def_wcid;

	return container_of(ptr, struct ieee80211_sta, drv_priv);
}

static inline struct standalone_mt76_tx_cb *standalone_mt76_tx_skb_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct standalone_mt76_tx_cb) >
		     sizeof(IEEE80211_SKB_CB(skb)->status.status_driver_data));
	return ((void *)IEEE80211_SKB_CB(skb)->status.status_driver_data);
}

static inline void *standalone_mt76_skb_get_hdr(struct sk_buff *skb)
{
	struct standalone_mt76_rx_status mstat;
	u8 *data = skb->data;

	/* Alignment concerns */
	BUILD_BUG_ON(sizeof(struct ieee80211_radiotap_he) % 4);
	BUILD_BUG_ON(sizeof(struct ieee80211_radiotap_he_mu) % 4);

	mstat = *((struct standalone_mt76_rx_status *)skb->cb);

	if (mstat.flag & RX_FLAG_RADIOTAP_HE)
		data += sizeof(struct ieee80211_radiotap_he);
	if (mstat.flag & RX_FLAG_RADIOTAP_HE_MU)
		data += sizeof(struct ieee80211_radiotap_he_mu);

	return data;
}

static inline void standalone_mt76_insert_hdr_pad(struct sk_buff *skb)
{
	int len = ieee80211_get_hdrlen_from_skb(skb);

	if (len % 4 == 0)
		return;

	skb_push(skb, 2);
	memmove(skb->data, skb->data + 2, len);

	skb->data[len] = 0;
	skb->data[len + 1] = 0;
}

static inline bool standalone_mt76_is_skb_pktid(u8 pktid)
{
	if (pktid & MT_PACKET_ID_HAS_RATE)
		return false;

	return pktid >= MT_PACKET_ID_FIRST;
}

static inline u8 standalone_mt76_tx_power_path_delta(u8 path)
{
	static const u8 path_delta[5] = { 0, 6, 9, 12, 14 };
	u8 idx = path - 1;

	return (idx < ARRAY_SIZE(path_delta)) ? path_delta[idx] : 0;
}

static inline bool standalone_mt76_testmode_enabled(struct standalone_mt76_phy *phy)
{
#ifdef CONFIG_NL80211_TESTMODE
	return phy->test.state != STANDALONE_MT76_TM_STATE_OFF;
#else
	return false;
#endif
}

static inline bool standalone_mt76_is_testmode_skb(struct standalone_mt76_dev *dev,
					struct sk_buff *skb,
					struct ieee80211_hw **hw)
{
#ifdef CONFIG_NL80211_TESTMODE
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->phys); i++) {
		struct standalone_mt76_phy *phy = dev->phys[i];

		if (phy && skb == phy->test.tx_skb) {
			*hw = dev->phys[i]->hw;
			return true;
		}
	}
	return false;
#else
	return false;
#endif
}

void standalone_mt76_rx(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id q, struct sk_buff *skb);
void standalone_mt76_tx(struct standalone_mt76_phy *dev, struct ieee80211_sta *sta,
	     struct standalone_mt76_wcid *wcid, struct sk_buff *skb);
void standalone_mt76_wake_tx_queue(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
void standalone_mt76_stop_tx_queues(struct standalone_mt76_phy *phy, struct ieee80211_sta *sta,
			 bool send_bar);
void standalone_mt76_tx_check_agg_ssn(struct ieee80211_sta *sta, struct sk_buff *skb);
void standalone_mt76_txq_schedule(struct standalone_mt76_phy *phy, enum standalone_mt76_txq_id qid);
void standalone_mt76_txq_schedule_all(struct standalone_mt76_phy *phy);
void standalone_mt76_txq_schedule_pending(struct standalone_mt76_phy *phy);
void standalone_mt76_tx_worker_run(struct standalone_mt76_dev *dev);
void standalone_mt76_tx_worker(struct standalone_mt76_worker *w);
void standalone_mt76_release_buffered_frames(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta,
				  u16 tids, int nframes,
				  enum ieee80211_frame_release_type reason,
				  bool more_data);
void standalone_mt76_sta_ps_transition(struct standalone_mt76_dev *dev, struct standalone_mt76_wcid *wcid,
			    bool ps);
bool standalone_mt76_has_tx_pending(struct standalone_mt76_phy *phy);
int standalone_mt76_update_channel(struct standalone_mt76_phy *phy);
void standalone_mt76_update_survey(struct standalone_mt76_phy *phy);
void standalone_mt76_update_survey_active_time(struct standalone_mt76_phy *phy, ktime_t time);
int standalone_mt76_get_survey(struct ieee80211_hw *hw, int idx,
		    struct survey_info *survey);
int standalone_mt76_rx_signal(u8 chain_mask, s8 *chain_signal);
void standalone_mt76_set_stream_caps(struct standalone_mt76_phy *phy, bool vht);

int standalone_mt76_rx_aggr_start(struct standalone_mt76_dev *dev, struct standalone_mt76_wcid *wcid, u8 tid,
		       u16 ssn, u16 size);
void standalone_mt76_rx_aggr_stop(struct standalone_mt76_dev *dev, struct standalone_mt76_wcid *wcid, u8 tid);

void standalone_mt76_wcid_key_setup(struct standalone_mt76_dev *dev, struct standalone_mt76_wcid *wcid,
			 struct ieee80211_key_conf *key);

void standalone_mt76_tx_status_lock(struct standalone_mt76_dev *dev, struct sk_buff_head *list)
			 __acquires(&dev->status_lock);
void standalone_mt76_tx_status_unlock(struct standalone_mt76_dev *dev, struct sk_buff_head *list)
			   __releases(&dev->status_lock);

int standalone_mt76_tx_status_skb_add(struct standalone_mt76_dev *dev, struct standalone_mt76_wcid *wcid,
			   struct sk_buff *skb);
struct sk_buff *standalone_mt76_tx_status_skb_get(struct standalone_mt76_dev *dev,
				       struct standalone_mt76_wcid *wcid, int pktid,
				       struct sk_buff_head *list);
void standalone_mt76_tx_status_skb_done(struct standalone_mt76_dev *dev, struct sk_buff *skb,
			     struct sk_buff_head *list);
void __standalone_mt76_tx_complete_skb(struct standalone_mt76_dev *dev, u16 wcid, struct sk_buff *skb,
			    struct list_head *free_list);
static inline void
standalone_mt76_tx_complete_skb(struct standalone_mt76_dev *dev, u16 wcid, struct sk_buff *skb)
{
    __standalone_mt76_tx_complete_skb(dev, wcid, skb, NULL);
}

void standalone_mt76_tx_status_check(struct standalone_mt76_dev *dev, bool flush);
int standalone_mt76_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta,
		   enum ieee80211_sta_state old_state,
		   enum ieee80211_sta_state new_state);
void __standalone_mt76_sta_remove(struct standalone_mt76_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void standalone_mt76_sta_pre_rcu_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta);

int standalone_mt76_get_min_avg_rssi(struct standalone_mt76_dev *dev, u8 phy_idx);

s8 standalone_mt76_get_power_bound(struct standalone_mt76_phy *phy, s8 txpower);

int standalone_mt76_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     unsigned int link_id, int *dbm);
int standalone_mt76_init_sar_power(struct ieee80211_hw *hw,
			const struct cfg80211_sar_specs *sar);
int standalone_mt76_get_sar_power(struct standalone_mt76_phy *phy,
		       struct ieee80211_channel *chan,
		       int power);

void standalone_mt76_csa_check(struct standalone_mt76_dev *dev);
void standalone_mt76_csa_finish(struct standalone_mt76_dev *dev);

int standalone_mt76_get_antenna(struct ieee80211_hw *hw, int radio_idx, u32 *tx_ant,
		     u32 *rx_ant);
int standalone_mt76_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set);
void standalone_mt76_insert_ccmp_hdr(struct sk_buff *skb, u8 key_id);
int standalone_mt76_get_rate(struct standalone_mt76_dev *dev,
		  struct ieee80211_supported_band *sband,
		  int idx, bool cck);
int standalone_mt76_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_scan_request *hw_req);
void standalone_mt76_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void standalone_mt76_scan_rx_beacon(struct standalone_mt76_dev *dev, struct ieee80211_channel *chan);
void standalone_mt76_rx_beacon(struct standalone_mt76_phy *phy, struct sk_buff *skb);
void standalone_mt76_beacon_mon_check(struct standalone_mt76_phy *phy);
void standalone_mt76_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  const u8 *mac);
void standalone_mt76_sw_scan_complete(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif);
enum standalone_mt76_dfs_state standalone_mt76_phy_dfs_state(struct standalone_mt76_phy *phy);
int standalone_mt76_add_chanctx(struct ieee80211_hw *hw,
		     struct ieee80211_chanctx_conf *conf);
void standalone_mt76_remove_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf);
void standalone_mt76_change_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf,
			 u32 changed);
int standalone_mt76_assign_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_chanctx_conf *conf);
void standalone_mt76_unassign_vif_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link_conf,
			       struct ieee80211_chanctx_conf *conf);
int standalone_mt76_switch_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif_chanctx_switch *vifs,
			    int n_vifs,
			    enum ieee80211_chanctx_switch_mode mode);
int standalone_mt76_remain_on_channel(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_channel *chan, int duration,
			   enum ieee80211_roc_type type);
int standalone_mt76_cancel_remain_on_channel(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif);
int standalone_mt76_testmode_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      void *data, int len);
int standalone_mt76_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *skb,
		       struct netlink_callback *cb, void *data, int len);
int standalone_mt76_testmode_set_state(struct standalone_mt76_phy *phy, enum standalone_mt76_testmode_state state);
int standalone_mt76_testmode_alloc_skb(struct standalone_mt76_phy *phy, u32 len);

#ifdef CONFIG_STANDALONE_MT76_NPU
void standalone_mt76_npu_check_ppe(struct standalone_mt76_dev *dev, struct sk_buff *skb,
			u32 info);
int standalone_mt76_npu_dma_add_buf(struct standalone_mt76_phy *phy, struct standalone_mt76_queue *q,
			 struct sk_buff *skb, struct standalone_mt76_queue_buf *buf,
			 void *txwi_ptr);
int standalone_mt76_npu_rx_queue_init(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q);
int standalone_mt76_npu_fill_rx_queue(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q);
void standalone_mt76_npu_queue_cleanup(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q);
void standalone_mt76_npu_disable_irqs(struct standalone_mt76_dev *dev);
int standalone_mt76_npu_init(struct standalone_mt76_dev *dev, phys_addr_t phy_addr, int type);
void standalone_mt76_npu_deinit(struct standalone_mt76_dev *dev);
void standalone_mt76_npu_queue_setup(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q);
void standalone_mt76_npu_txdesc_cleanup(struct standalone_mt76_queue *q, int index);
int standalone_mt76_npu_net_setup_tc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct net_device *dev, enum tc_setup_type type,
			  void *type_data);
int standalone_mt76_npu_send_txrx_addr(struct standalone_mt76_dev *dev, int ifindex,
			    u32 direction, u32 i_count_addr,
			    u32 o_status_addr, u32 o_count_addr);
#else
static inline void standalone_mt76_npu_check_ppe(struct standalone_mt76_dev *dev,
				      struct sk_buff *skb, u32 info)
{
}

static inline int standalone_mt76_npu_dma_add_buf(struct standalone_mt76_phy *phy,
				       struct standalone_mt76_queue *q,
				       struct sk_buff *skb,
				       struct standalone_mt76_queue_buf *buf,
				       void *txwi_ptr)
{
	return -EOPNOTSUPP;
}

static inline int standalone_mt76_npu_fill_rx_queue(struct standalone_mt76_dev *dev,
					 struct standalone_mt76_queue *q)
{
	return 0;
}

static inline void standalone_mt76_npu_queue_cleanup(struct standalone_mt76_dev *dev,
					  struct standalone_mt76_queue *q)
{
}

static inline void standalone_mt76_npu_disable_irqs(struct standalone_mt76_dev *dev)
{
}

static inline int standalone_mt76_npu_init(struct standalone_mt76_dev *dev, phys_addr_t phy_addr,
				int type)
{
	return 0;
}

static inline void standalone_mt76_npu_deinit(struct standalone_mt76_dev *dev)
{
}

static inline void standalone_mt76_npu_queue_setup(struct standalone_mt76_dev *dev,
					struct standalone_mt76_queue *q)
{
}

static inline void standalone_mt76_npu_txdesc_cleanup(struct standalone_mt76_queue *q,
					   int index)
{
}

static inline int standalone_mt76_npu_net_setup_tc(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct net_device *dev,
					enum tc_setup_type type,
					void *type_data)
{
	return -EOPNOTSUPP;
}

static inline int standalone_mt76_npu_send_txrx_addr(struct standalone_mt76_dev *dev, int ifindex,
					  u32 direction, u32 i_count_addr,
					  u32 o_status_addr, u32 o_count_addr)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_STANDALONE_MT76_NPU */

static inline bool standalone_mt76_npu_device_active(struct standalone_mt76_dev *dev)
{
	return !!rcu_access_pointer(dev->mmio.npu);
}

static inline bool standalone_mt76_ppe_device_active(struct standalone_mt76_dev *dev)
{
	return !!rcu_access_pointer(dev->mmio.ppe_dev);
}

static inline int standalone_mt76_npu_send_msg(struct airoha_npu *npu, int ifindex,
				    enum airoha_npu_wlan_set_cmd cmd,
				    u32 val, gfp_t gfp)
{
	return airoha_npu_wlan_send_msg(npu, ifindex, cmd, &val, sizeof(val),
					gfp);
}

static inline int standalone_mt76_npu_get_msg(struct airoha_npu *npu, int ifindex,
				   enum airoha_npu_wlan_get_cmd cmd,
				   u32 *val, gfp_t gfp)
{
	return airoha_npu_wlan_get_msg(npu, ifindex, cmd, val, sizeof(*val),
				       gfp);
}

static inline void standalone_mt76_testmode_reset(struct standalone_mt76_phy *phy, bool disable)
{
#ifdef CONFIG_NL80211_TESTMODE
	enum standalone_mt76_testmode_state state = STANDALONE_MT76_TM_STATE_IDLE;

	if (disable || phy->test.state == STANDALONE_MT76_TM_STATE_OFF)
		state = STANDALONE_MT76_TM_STATE_OFF;

	standalone_mt76_testmode_set_state(phy, state);
#endif
}

extern const struct nla_policy standalone_mt76_tm_policy[NUM_STANDALONE_MT76_TM_ATTRS];

/* internal */
static inline struct ieee80211_hw *
standalone_mt76_tx_status_get_hw(struct standalone_mt76_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 phy_idx = (info->hw_queue & MT_TX_HW_QUEUE_PHY) >> 2;
	struct ieee80211_hw *hw = standalone_mt76_phy_hw(dev, phy_idx);

	info->hw_queue &= ~MT_TX_HW_QUEUE_PHY;

	return hw;
}

void standalone_mt76_put_txwi(struct standalone_mt76_dev *dev, struct standalone_mt76_txwi_cache *t);
void standalone_mt76_put_rxwi(struct standalone_mt76_dev *dev, struct standalone_mt76_txwi_cache *t);
struct standalone_mt76_txwi_cache *standalone_mt76_get_rxwi(struct standalone_mt76_dev *dev);
void standalone_mt76_free_pending_rxwi(struct standalone_mt76_dev *dev);
void standalone_mt76_rx_complete(struct standalone_mt76_dev *dev, struct sk_buff_head *frames,
		      struct napi_struct *napi);
void standalone_mt76_rx_poll_complete(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id q,
			   struct napi_struct *napi);
void standalone_mt76_rx_aggr_reorder(struct sk_buff *skb, struct sk_buff_head *frames);
void standalone_mt76_testmode_tx_pending(struct standalone_mt76_phy *phy);
void standalone_mt76_queue_tx_complete(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
			    struct standalone_mt76_queue_entry *e);
int __standalone_mt76_set_channel(struct standalone_mt76_phy *phy, struct cfg80211_chan_def *chandef,
		       bool offchannel);

static inline bool
standalone_mt76_offchannel_chandef(struct standalone_mt76_phy *phy, struct ieee80211_channel *chan,
			struct cfg80211_chan_def *chandef)
{
	cfg80211_chandef_create(chandef, chan, NL80211_CHAN_HT20);
	if (phy->main_chandef.chan != chan)
		return true;

	*chandef = phy->main_chandef;
	return false;
}
int standalone_mt76_set_channel(struct standalone_mt76_phy *phy, struct cfg80211_chan_def *chandef,
		     bool offchannel);
void standalone_mt76_scan_work(struct work_struct *work);
void standalone_mt76_abort_scan(struct standalone_mt76_dev *dev);
void standalone_mt76_roc_complete_work(struct work_struct *work);
void standalone_mt76_roc_complete(struct standalone_mt76_phy *phy);
void standalone_mt76_abort_roc(struct standalone_mt76_phy *phy);
struct standalone_mt76_vif_link *standalone_mt76_get_vif_phy_link(struct standalone_mt76_phy *phy,
					    struct ieee80211_vif *vif);
void standalone_mt76_put_vif_phy_link(struct standalone_mt76_phy *phy, struct ieee80211_vif *vif,
			   struct standalone_mt76_vif_link *mlink);
void standalone_mt76_offchannel_notify(struct standalone_mt76_phy *phy, bool offchannel);

/* usb */
static inline bool standalone_mt76u_urb_error(struct urb *urb)
{
	return urb->status &&
	       urb->status != -ECONNRESET &&
	       urb->status != -ESHUTDOWN &&
	       urb->status != -ENOENT;
}

static inline int
standalone_mt76u_bulk_msg(struct standalone_mt76_dev *dev, void *data, int len, int *actual_len,
	       int timeout, int ep)
{
	struct usb_interface *uintf = to_usb_interface(dev->dev);
	struct usb_device *udev = interface_to_usbdev(uintf);
	struct standalone_mt76_usb *usb = &dev->usb;
	unsigned int pipe;

	if (actual_len)
		pipe = usb_rcvbulkpipe(udev, usb->in_ep[ep]);
	else
		pipe = usb_sndbulkpipe(udev, usb->out_ep[ep]);

	return usb_bulk_msg(udev, pipe, data, len, actual_len, timeout);
}

void standalone_mt76_ethtool_page_pool_stats(struct standalone_mt76_dev *dev, u64 *data, int *index);
void standalone_mt76_ethtool_worker(struct standalone_mt76_ethtool_worker_info *wi,
			 struct standalone_mt76_sta_stats *stats, bool eht);
int standalone_mt76_skb_adjust_pad(struct sk_buff *skb, int pad);
int __standalone_mt76u_vendor_request(struct standalone_mt76_dev *dev, u8 req, u8 req_type,
			   u16 val, u16 offset, void *buf, size_t len);
int standalone_mt76u_vendor_request(struct standalone_mt76_dev *dev, u8 req,
			 u8 req_type, u16 val, u16 offset,
			 void *buf, size_t len);
void standalone_mt76u_single_wr(struct standalone_mt76_dev *dev, const u8 req,
		     const u16 offset, const u32 val);
void standalone_mt76u_read_copy(struct standalone_mt76_dev *dev, u32 offset,
		     void *data, int len);
u32 ___standalone_mt76u_rr(struct standalone_mt76_dev *dev, u8 req, u8 req_type, u32 addr);
void ___standalone_mt76u_wr(struct standalone_mt76_dev *dev, u8 req, u8 req_type,
		 u32 addr, u32 val);
int __standalone_mt76u_init(struct standalone_mt76_dev *dev, struct usb_interface *intf,
		 struct standalone_mt76_bus_ops *ops);
int standalone_mt76u_init(struct standalone_mt76_dev *dev, struct usb_interface *intf);
int standalone_mt76u_alloc_mcu_queue(struct standalone_mt76_dev *dev);
int standalone_mt76u_alloc_queues(struct standalone_mt76_dev *dev);
void standalone_mt76u_stop_tx(struct standalone_mt76_dev *dev);
void standalone_mt76u_stop_rx(struct standalone_mt76_dev *dev);
int standalone_mt76u_resume_rx(struct standalone_mt76_dev *dev);
void standalone_mt76u_queues_deinit(struct standalone_mt76_dev *dev);

int standalone_mt76s_init(struct standalone_mt76_dev *dev, struct sdio_func *func,
	       const struct standalone_mt76_bus_ops *bus_ops);
int standalone_mt76s_alloc_rx_queue(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id qid);
int standalone_mt76s_alloc_tx(struct standalone_mt76_dev *dev);
void standalone_mt76s_deinit(struct standalone_mt76_dev *dev);
void standalone_mt76s_sdio_irq(struct sdio_func *func);
void standalone_mt76s_txrx_worker(struct standalone_mt76_sdio *sdio);
bool standalone_mt76s_txqs_empty(struct standalone_mt76_dev *dev);
int standalone_mt76s_hw_init(struct standalone_mt76_dev *dev, struct sdio_func *func,
		  int hw_ver);
u32 standalone_mt76s_rr(struct standalone_mt76_dev *dev, u32 offset);
void standalone_mt76s_wr(struct standalone_mt76_dev *dev, u32 offset, u32 val);
u32 standalone_mt76s_rmw(struct standalone_mt76_dev *dev, u32 offset, u32 mask, u32 val);
u32 standalone_mt76s_read_pcr(struct standalone_mt76_dev *dev);
void standalone_mt76s_write_copy(struct standalone_mt76_dev *dev, u32 offset,
		      const void *data, int len);
void standalone_mt76s_read_copy(struct standalone_mt76_dev *dev, u32 offset,
		     void *data, int len);
int standalone_mt76s_wr_rp(struct standalone_mt76_dev *dev, u32 base,
		const struct standalone_mt76_reg_pair *data,
		int len);
int standalone_mt76s_rd_rp(struct standalone_mt76_dev *dev, u32 base,
		struct standalone_mt76_reg_pair *data, int len);

struct sk_buff *
__standalone_mt76_mcu_msg_alloc(struct standalone_mt76_dev *dev, const void *data,
		     int len, int data_len, gfp_t gfp);
static inline struct sk_buff *
standalone_mt76_mcu_msg_alloc(struct standalone_mt76_dev *dev, const void *data,
		   int data_len)
{
	return __standalone_mt76_mcu_msg_alloc(dev, data, data_len, data_len, GFP_KERNEL);
}

void standalone_mt76_mcu_rx_event(struct standalone_mt76_dev *dev, struct sk_buff *skb);
struct sk_buff *standalone_mt76_mcu_get_response(struct standalone_mt76_dev *dev,
				      unsigned long expires);
int standalone_mt76_mcu_send_and_get_msg(struct standalone_mt76_dev *dev, int cmd, const void *data,
			      int len, bool wait_resp, struct sk_buff **ret);
int standalone_mt76_mcu_skb_send_and_get_msg(struct standalone_mt76_dev *dev, struct sk_buff *skb,
				  int cmd, bool wait_resp, struct sk_buff **ret);
int __standalone_mt76_mcu_send_firmware(struct standalone_mt76_dev *dev, int cmd, const void *data,
			     int len, int max_len);
static inline int
standalone_mt76_mcu_send_firmware(struct standalone_mt76_dev *dev, int cmd, const void *data,
		       int len)
{
	int max_len = 4096 - dev->mcu_ops->headroom;

	return __standalone_mt76_mcu_send_firmware(dev, cmd, data, len, max_len);
}

static inline int
standalone_mt76_mcu_send_msg(struct standalone_mt76_dev *dev, int cmd, const void *data, int len,
		  bool wait_resp)
{
	return standalone_mt76_mcu_send_and_get_msg(dev, cmd, data, len, wait_resp, NULL);
}

static inline int
standalone_mt76_mcu_skb_send_msg(struct standalone_mt76_dev *dev, struct sk_buff *skb, int cmd,
		      bool wait_resp)
{
	return standalone_mt76_mcu_skb_send_and_get_msg(dev, skb, cmd, wait_resp, NULL);
}

void standalone_mt76_set_irq_mask(struct standalone_mt76_dev *dev, u32 addr, u32 clear, u32 set);

struct device_node *
standalone_mt76_find_power_limits_node(struct standalone_mt76_dev *dev);
struct device_node *
standalone_mt76_find_channel_node(struct device_node *np, struct ieee80211_channel *chan);

s8 standalone_mt76_get_rate_power_limits(struct standalone_mt76_phy *phy,
			      struct ieee80211_channel *chan,
			      struct standalone_mt76_power_limits *dest,
			      s8 target_power);

static inline bool standalone_mt76_queue_is_rx(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->q_rx); i++) {
		if (q == &dev->q_rx[i])
			return true;
	}

	return false;
}

static inline bool standalone_mt76_queue_is_wed_tx_free(struct standalone_mt76_queue *q)
{
	return (q->flags & MT_QFLAG_WED) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_Q_TXFREE;
}

static inline bool standalone_mt76_queue_is_wed_rro(struct standalone_mt76_queue *q)
{
	return q->flags & MT_QFLAG_WED_RRO;
}

static inline bool standalone_mt76_queue_is_wed_rro_ind(struct standalone_mt76_queue *q)
{
	return standalone_mt76_queue_is_wed_rro(q) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_RRO_Q_IND;
}

static inline bool standalone_mt76_queue_is_wed_rro_rxdmad_c(struct standalone_mt76_queue *q)
{
	return standalone_mt76_queue_is_wed_rro(q) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_RRO_Q_RXDMAD_C;
}

static inline bool standalone_mt76_queue_is_wed_rro_data(struct standalone_mt76_queue *q)
{
	return standalone_mt76_queue_is_wed_rro(q) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_RRO_Q_DATA;
}

static inline bool standalone_mt76_queue_is_wed_rro_msdu_pg(struct standalone_mt76_queue *q)
{
	return standalone_mt76_queue_is_wed_rro(q) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) ==
	       STANDALONE_MT76_WED_RRO_Q_MSDU_PG;
}

static inline bool standalone_mt76_queue_is_wed_rx(struct standalone_mt76_queue *q)
{
	return (q->flags & MT_QFLAG_WED) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_Q_RX;
}

static inline bool standalone_mt76_queue_is_emi(struct standalone_mt76_queue *q)
{
	return q->flags & MT_QFLAG_EMI_EN;
}

static inline bool standalone_mt76_queue_is_npu(struct standalone_mt76_queue *q)
{
	return q->flags & MT_QFLAG_NPU;
}

static inline bool standalone_mt76_queue_is_npu_tx(struct standalone_mt76_queue *q)
{
	return standalone_mt76_queue_is_npu(q) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_Q_TX;
}

static inline bool standalone_mt76_queue_is_npu_rx(struct standalone_mt76_queue *q)
{
	return standalone_mt76_queue_is_npu(q) &&
	       FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_Q_RX;
}

static inline bool standalone_mt76_queue_is_npu_txfree(struct standalone_mt76_queue *q)
{
	if (q->flags & MT_QFLAG_WED)
		return false;

	return FIELD_GET(MT_QFLAG_WED_TYPE, q->flags) == STANDALONE_MT76_WED_Q_TXFREE;
}

struct standalone_mt76_txwi_cache *
standalone_mt76_token_release(struct standalone_mt76_dev *dev, int token, bool *wake);
int standalone_mt76_token_consume(struct standalone_mt76_dev *dev, struct standalone_mt76_txwi_cache **ptxwi);
void __standalone_mt76_set_tx_blocked(struct standalone_mt76_dev *dev, bool blocked);
struct standalone_mt76_txwi_cache *standalone_mt76_rx_token_release(struct standalone_mt76_dev *dev, int token);
int standalone_mt76_rx_token_consume(struct standalone_mt76_dev *dev, void *ptr,
			  struct standalone_mt76_txwi_cache *r, dma_addr_t phys);
int standalone_mt76_create_page_pool(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q);
static inline void standalone_mt76_put_page_pool_buf(void *buf, bool allow_direct)
{
	struct page *page = virt_to_head_page(buf);

	page_pool_put_full_page(page->pp, page, allow_direct);
}

static inline void *
standalone_mt76_get_page_pool_buf(struct standalone_mt76_queue *q, u32 *offset, u32 size)
{
	struct page *page;

	page = page_pool_alloc_frag(q->page_pool, offset, size,
				    GFP_ATOMIC | __GFP_NOWARN | GFP_DMA32);
	if (!page)
		return NULL;

	return page_address(page) + *offset;
}

static inline void standalone_mt76_set_tx_blocked(struct standalone_mt76_dev *dev, bool blocked)
{
	spin_lock_bh(&dev->token_lock);
	__standalone_mt76_set_tx_blocked(dev, blocked);
	spin_unlock_bh(&dev->token_lock);
}

static inline int
standalone_mt76_token_get(struct standalone_mt76_dev *dev, struct standalone_mt76_txwi_cache **ptxwi)
{
	int token;

	spin_lock_bh(&dev->token_lock);
	token = idr_alloc(&dev->token, *ptxwi, 0, dev->token_size, GFP_ATOMIC);
	spin_unlock_bh(&dev->token_lock);

	return token;
}

static inline struct standalone_mt76_txwi_cache *
standalone_mt76_token_put(struct standalone_mt76_dev *dev, int token)
{
	struct standalone_mt76_txwi_cache *txwi;

	spin_lock_bh(&dev->token_lock);
	txwi = idr_remove(&dev->token, token);
	spin_unlock_bh(&dev->token_lock);

	return txwi;
}

void standalone_mt76_wcid_init(struct standalone_mt76_wcid *wcid, u8 band_idx);
void standalone_mt76_wcid_cleanup(struct standalone_mt76_dev *dev, struct standalone_mt76_wcid *wcid);
void standalone_mt76_wcid_add_poll(struct standalone_mt76_dev *dev, struct standalone_mt76_wcid *wcid);

static inline void
standalone_mt76_vif_init(struct ieee80211_vif *vif, struct standalone_mt76_vif_data *mvif)
{
	struct standalone_mt76_vif_link *mlink = (struct standalone_mt76_vif_link *)vif->drv_priv;

	mlink->mvif = mvif;
	rcu_assign_pointer(mvif->link[0], mlink);
}

void standalone_mt76_vif_cleanup(struct standalone_mt76_dev *dev, struct ieee80211_vif *vif);
u16 standalone_mt76_select_links(struct ieee80211_vif *vif, int max_active_links);

static inline struct standalone_mt76_vif_link *
standalone_mt76_vif_link(struct standalone_mt76_dev *dev, struct ieee80211_vif *vif, int link_id)
{
	struct standalone_mt76_vif_link *mlink = (struct standalone_mt76_vif_link *)vif->drv_priv;
	struct standalone_mt76_vif_data *mvif = mlink->mvif;

	if (!link_id)
		return mlink;

	return standalone_mt76_dereference(mvif->link[link_id], dev);
}

static inline struct standalone_mt76_vif_link *
standalone_mt76_vif_conf_link(struct standalone_mt76_dev *dev, struct ieee80211_vif *vif,
		   struct ieee80211_bss_conf *link_conf)
{
	struct standalone_mt76_vif_link *mlink = (struct standalone_mt76_vif_link *)vif->drv_priv;
	struct standalone_mt76_vif_data *mvif = mlink->mvif;

	if (link_conf == &vif->bss_conf || !link_conf->link_id)
		return mlink;

	return standalone_mt76_dereference(mvif->link[link_conf->link_id], dev);
}

static inline struct standalone_mt76_phy *
standalone_mt76_vif_link_phy(struct standalone_mt76_vif_link *mlink)
{
	struct standalone_mt76_chanctx *ctx;

	if (!mlink->ctx)
		return NULL;

	ctx = (struct standalone_mt76_chanctx *)mlink->ctx->drv_priv;

	return ctx->phy;
}

#endif

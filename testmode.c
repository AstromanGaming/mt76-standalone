// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca> */

#include <linux/random.h>
#include "standalone_mt76.h"

const struct nla_policy standalone_mt76_tm_policy[NUM_STANDALONE_MT76_TM_ATTRS] = {
	[STANDALONE_MT76_TM_ATTR_RESET] = { .type = NLA_FLAG },
	[STANDALONE_MT76_TM_ATTR_STATE] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_COUNT] = { .type = NLA_U32 },
	[STANDALONE_MT76_TM_ATTR_TX_LENGTH] = { .type = NLA_U32 },
	[STANDALONE_MT76_TM_ATTR_TX_RATE_MODE] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_RATE_NSS] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_RATE_IDX] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_RATE_SGI] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_RATE_LDPC] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_RATE_STBC] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_LTF] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_ANTENNA] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_SPE_IDX] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_POWER_CONTROL] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_POWER] = { .type = NLA_NESTED },
	[STANDALONE_MT76_TM_ATTR_TX_DUTY_CYCLE] = { .type = NLA_U8 },
	[STANDALONE_MT76_TM_ATTR_TX_IPG] = { .type = NLA_U32 },
	[STANDALONE_MT76_TM_ATTR_TX_TIME] = { .type = NLA_U32 },
	[STANDALONE_MT76_TM_ATTR_FREQ_OFFSET] = { .type = NLA_U32 },
	[STANDALONE_MT76_TM_ATTR_DRV_DATA] = { .type = NLA_NESTED },
};
EXPORT_SYMBOL_GPL(standalone_mt76_tm_policy);

void standalone_mt76_testmode_tx_pending(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct standalone_mt76_dev *dev = phy->dev;
	struct standalone_mt76_wcid *wcid = &dev->global_wcid;
	struct sk_buff *skb = td->tx_skb;
	struct standalone_mt76_queue *q;
	u16 tx_queued_limit;
	int qid;

	if (!skb || !td->tx_pending)
		return;

	qid = skb_get_queue_mapping(skb);
	q = phy->q_tx[qid];

	tx_queued_limit = td->tx_queued_limit ? td->tx_queued_limit : 1000;

	spin_lock_bh(&q->lock);

	while (td->tx_pending > 0 &&
	       td->tx_queued - td->tx_done < tx_queued_limit &&
	       q->queued < q->ndesc / 2) {
		int ret;

		ret = dev->queue_ops->tx_queue_skb(phy, q, qid, skb_get(skb),
						   wcid, NULL);
		if (ret < 0)
			break;

		td->tx_pending--;
		td->tx_queued++;
	}

	dev->queue_ops->kick(dev, q);

	spin_unlock_bh(&q->lock);
}

static u32
standalone_mt76_testmode_max_mpdu_len(struct standalone_mt76_phy *phy, u8 tx_rate_mode)
{
	switch (tx_rate_mode) {
	case STANDALONE_MT76_TM_TX_MODE_HT:
		return IEEE80211_MAX_MPDU_LEN_HT_7935;
	case STANDALONE_MT76_TM_TX_MODE_VHT:
	case STANDALONE_MT76_TM_TX_MODE_HE_SU:
	case STANDALONE_MT76_TM_TX_MODE_HE_EXT_SU:
	case STANDALONE_MT76_TM_TX_MODE_HE_TB:
	case STANDALONE_MT76_TM_TX_MODE_HE_MU:
		if (phy->sband_5g.sband.vht_cap.cap &
		    IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991)
			return IEEE80211_MAX_MPDU_LEN_VHT_7991;
		return IEEE80211_MAX_MPDU_LEN_VHT_11454;
	case STANDALONE_MT76_TM_TX_MODE_CCK:
	case STANDALONE_MT76_TM_TX_MODE_OFDM:
	default:
		return IEEE80211_MAX_FRAME_LEN;
	}
}

static void
standalone_mt76_testmode_free_skb(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_testmode_data *td = &phy->test;

	dev_kfree_skb(td->tx_skb);
	td->tx_skb = NULL;
}

int standalone_mt76_testmode_alloc_skb(struct standalone_mt76_phy *phy, u32 len)
{
#define MT_TXP_MAX_LEN	4095
	u16 fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA |
		 IEEE80211_FCTL_FROMDS;
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct sk_buff **frag_tail, *head;
	struct ieee80211_tx_info *info;
	struct ieee80211_hdr *hdr;
	u32 max_len, head_len;
	int nfrags, i;

	max_len = standalone_mt76_testmode_max_mpdu_len(phy, td->tx_rate_mode);
	if (len > max_len)
		len = max_len;
	else if (len < sizeof(struct ieee80211_hdr))
		len = sizeof(struct ieee80211_hdr);

	nfrags = len / MT_TXP_MAX_LEN;
	head_len = nfrags ? MT_TXP_MAX_LEN : len;

	if (len > IEEE80211_MAX_FRAME_LEN)
		fc |= IEEE80211_STYPE_QOS_DATA;

	head = alloc_skb(head_len, GFP_KERNEL);
	if (!head)
		return -ENOMEM;

	hdr = __skb_put_zero(head, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(fc);
	memcpy(hdr->addr1, td->addr[0], ETH_ALEN);
	memcpy(hdr->addr2, td->addr[1], ETH_ALEN);
	memcpy(hdr->addr3, td->addr[2], ETH_ALEN);
	skb_set_queue_mapping(head, IEEE80211_AC_BE);
	get_random_bytes(__skb_put(head, head_len - sizeof(*hdr)),
			 head_len - sizeof(*hdr));

	info = IEEE80211_SKB_CB(head);
	info->flags = IEEE80211_TX_CTL_INJECTED |
		      IEEE80211_TX_CTL_NO_ACK |
		      IEEE80211_TX_CTL_NO_PS_BUFFER;

	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, phy->band_idx);
	frag_tail = &skb_shinfo(head)->frag_list;

	for (i = 0; i < nfrags; i++) {
		struct sk_buff *frag;
		u16 frag_len;

		if (i == nfrags - 1)
			frag_len = len % MT_TXP_MAX_LEN;
		else
			frag_len = MT_TXP_MAX_LEN;

		frag = alloc_skb(frag_len, GFP_KERNEL);
		if (!frag) {
			standalone_mt76_testmode_free_skb(phy);
			dev_kfree_skb(head);
			return -ENOMEM;
		}

		get_random_bytes(__skb_put(frag, frag_len), frag_len);
		head->len += frag->len;
		head->data_len += frag->len;

		*frag_tail = frag;
		frag_tail = &(*frag_tail)->next;
	}

	standalone_mt76_testmode_free_skb(phy);
	td->tx_skb = head;

	return 0;
}
EXPORT_SYMBOL(standalone_mt76_testmode_alloc_skb);

static int
standalone_mt76_testmode_tx_init(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct ieee80211_tx_info *info;
	struct ieee80211_tx_rate *rate;
	u8 max_nss = hweight8(phy->antenna_mask);
	int ret;

	ret = standalone_mt76_testmode_alloc_skb(phy, td->tx_mpdu_len);
	if (ret)
		return ret;

	if (td->tx_rate_mode > STANDALONE_MT76_TM_TX_MODE_VHT)
		goto out;

	if (td->tx_antenna_mask)
		max_nss = min_t(u8, max_nss, hweight8(td->tx_antenna_mask));

	info = IEEE80211_SKB_CB(td->tx_skb);
	rate = &info->control.rates[0];
	rate->count = 1;
	rate->idx = td->tx_rate_idx;

	switch (td->tx_rate_mode) {
	case STANDALONE_MT76_TM_TX_MODE_CCK:
		if (phy->chandef.chan->band != NL80211_BAND_2GHZ)
			return -EINVAL;

		if (rate->idx > 4)
			return -EINVAL;
		break;
	case STANDALONE_MT76_TM_TX_MODE_OFDM:
		if (phy->chandef.chan->band != NL80211_BAND_2GHZ)
			break;

		if (rate->idx > 8)
			return -EINVAL;

		rate->idx += 4;
		break;
	case STANDALONE_MT76_TM_TX_MODE_HT:
		if (rate->idx > 8 * max_nss &&
			!(rate->idx == 32 &&
			  phy->chandef.width >= NL80211_CHAN_WIDTH_40))
			return -EINVAL;

		rate->flags |= IEEE80211_TX_RC_MCS;
		break;
	case STANDALONE_MT76_TM_TX_MODE_VHT:
		if (rate->idx > 9)
			return -EINVAL;

		if (td->tx_rate_nss > max_nss)
			return -EINVAL;

		ieee80211_rate_set_vht(rate, td->tx_rate_idx, td->tx_rate_nss);
		rate->flags |= IEEE80211_TX_RC_VHT_MCS;
		break;
	default:
		break;
	}

	if (td->tx_rate_sgi)
		rate->flags |= IEEE80211_TX_RC_SHORT_GI;

	if (td->tx_rate_ldpc)
		info->flags |= IEEE80211_TX_CTL_LDPC;

	if (td->tx_rate_stbc)
		info->flags |= IEEE80211_TX_CTL_STBC;

	if (td->tx_rate_mode >= STANDALONE_MT76_TM_TX_MODE_HT) {
		switch (phy->chandef.width) {
		case NL80211_CHAN_WIDTH_40:
			rate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
			break;
		case NL80211_CHAN_WIDTH_80:
			rate->flags |= IEEE80211_TX_RC_80_MHZ_WIDTH;
			break;
		case NL80211_CHAN_WIDTH_80P80:
		case NL80211_CHAN_WIDTH_160:
			rate->flags |= IEEE80211_TX_RC_160_MHZ_WIDTH;
			break;
		default:
			break;
		}
	}
out:
	return 0;
}

static void
standalone_mt76_testmode_tx_start(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct standalone_mt76_dev *dev = phy->dev;

	td->tx_queued = 0;
	td->tx_done = 0;
	td->tx_pending = td->tx_count;
	standalone_mt76_worker_schedule(&dev->tx_worker);
}

static void
standalone_mt76_testmode_tx_stop(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct standalone_mt76_dev *dev = phy->dev;

	standalone_mt76_worker_disable(&dev->tx_worker);

	td->tx_pending = 0;

	standalone_mt76_worker_enable(&dev->tx_worker);

	wait_event_timeout(dev->tx_wait, td->tx_done == td->tx_queued,
			   STANDALONE_MT76_TM_TIMEOUT * HZ);

	standalone_mt76_testmode_free_skb(phy);
}

static inline void
standalone_mt76_testmode_param_set(struct standalone_mt76_testmode_data *td, u16 idx)
{
	td->param_set[idx / 32] |= BIT(idx % 32);
}

static inline bool
standalone_mt76_testmode_param_present(struct standalone_mt76_testmode_data *td, u16 idx)
{
	return td->param_set[idx / 32] & BIT(idx % 32);
}

static void
standalone_mt76_testmode_init_defaults(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_testmode_data *td = &phy->test;

	if (td->tx_mpdu_len > 0)
		return;

	td->tx_mpdu_len = 1024;
	td->tx_count = 1;
	td->tx_rate_mode = STANDALONE_MT76_TM_TX_MODE_OFDM;
	td->tx_rate_nss = 1;

	memcpy(td->addr[0], phy->macaddr, ETH_ALEN);
	memcpy(td->addr[1], phy->macaddr, ETH_ALEN);
	memcpy(td->addr[2], phy->macaddr, ETH_ALEN);
}

static int
__standalone_mt76_testmode_set_state(struct standalone_mt76_phy *phy, enum standalone_mt76_testmode_state state)
{
	enum standalone_mt76_testmode_state prev_state = phy->test.state;
	struct standalone_mt76_dev *dev = phy->dev;
	int err;

	if (prev_state == STANDALONE_MT76_TM_STATE_TX_FRAMES)
		standalone_mt76_testmode_tx_stop(phy);

	if (state == STANDALONE_MT76_TM_STATE_TX_FRAMES) {
		err = standalone_mt76_testmode_tx_init(phy);
		if (err)
			return err;
	}

	err = dev->test_ops->set_state(phy, state);
	if (err) {
		if (state == STANDALONE_MT76_TM_STATE_TX_FRAMES)
			standalone_mt76_testmode_tx_stop(phy);

		return err;
	}

	if (state == STANDALONE_MT76_TM_STATE_TX_FRAMES)
		standalone_mt76_testmode_tx_start(phy);
	else if (state == STANDALONE_MT76_TM_STATE_RX_FRAMES) {
		memset(&phy->test.rx_stats, 0, sizeof(phy->test.rx_stats));
	}

	phy->test.state = state;

	return 0;
}

int standalone_mt76_testmode_set_state(struct standalone_mt76_phy *phy, enum standalone_mt76_testmode_state state)
{
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct ieee80211_hw *hw = phy->hw;

	if (state == td->state && state == STANDALONE_MT76_TM_STATE_OFF)
		return 0;

	if (state > STANDALONE_MT76_TM_STATE_OFF &&
	    (!test_bit(STANDALONE_MT76_STATE_RUNNING, &phy->state) ||
	     !(hw->conf.flags & IEEE80211_CONF_MONITOR)))
		return -ENOTCONN;

	if (state != STANDALONE_MT76_TM_STATE_IDLE &&
	    td->state != STANDALONE_MT76_TM_STATE_IDLE) {
		int ret;

		ret = __standalone_mt76_testmode_set_state(phy, STANDALONE_MT76_TM_STATE_IDLE);
		if (ret)
			return ret;
	}

	return __standalone_mt76_testmode_set_state(phy, state);

}
EXPORT_SYMBOL(standalone_mt76_testmode_set_state);

static int
standalone_mt76_tm_get_u8(struct nlattr *attr, u8 *dest, u8 min, u8 max)
{
	u8 val;

	if (!attr)
		return 0;

	val = nla_get_u8(attr);
	if (val < min || val > max)
		return -EINVAL;

	*dest = val;
	return 0;
}

int standalone_mt76_testmode_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      void *data, int len)
{
	struct standalone_mt76_phy *phy = hw->priv;
	struct standalone_mt76_dev *dev = phy->dev;
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct nlattr *tb[NUM_STANDALONE_MT76_TM_ATTRS];
	u32 state;
	int err;
	int i;

	if (!dev->test_ops)
		return -EOPNOTSUPP;

	err = nla_parse_deprecated(tb, STANDALONE_MT76_TM_ATTR_MAX, data, len,
				   standalone_mt76_tm_policy, NULL);
	if (err)
		return err;

	err = -EINVAL;

	mutex_lock(&dev->mutex);

	if (tb[STANDALONE_MT76_TM_ATTR_RESET]) {
		standalone_mt76_testmode_set_state(phy, STANDALONE_MT76_TM_STATE_OFF);
		memset(td, 0, sizeof(*td));
	}

	standalone_mt76_testmode_init_defaults(phy);

	if (tb[STANDALONE_MT76_TM_ATTR_TX_COUNT])
		td->tx_count = nla_get_u32(tb[STANDALONE_MT76_TM_ATTR_TX_COUNT]);

	if (tb[STANDALONE_MT76_TM_ATTR_TX_RATE_IDX])
		td->tx_rate_idx = nla_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_RATE_IDX]);

	if (standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_RATE_MODE], &td->tx_rate_mode,
			   0, STANDALONE_MT76_TM_TX_MODE_MAX) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_RATE_NSS], &td->tx_rate_nss,
			   1, hweight8(phy->antenna_mask)) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_RATE_SGI], &td->tx_rate_sgi, 0, 2) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_RATE_LDPC], &td->tx_rate_ldpc, 0, 1) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_RATE_STBC], &td->tx_rate_stbc, 0, 1) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_LTF], &td->tx_ltf, 0, 2) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_ANTENNA],
			   &td->tx_antenna_mask, 0, 0xff) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_SPE_IDX], &td->tx_spe_idx, 0, 27) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_DUTY_CYCLE],
			   &td->tx_duty_cycle, 0, 99) ||
	    standalone_mt76_tm_get_u8(tb[STANDALONE_MT76_TM_ATTR_TX_POWER_CONTROL],
			   &td->tx_power_control, 0, 1))
		goto out;

	if (tb[STANDALONE_MT76_TM_ATTR_TX_LENGTH]) {
		u32 val = nla_get_u32(tb[STANDALONE_MT76_TM_ATTR_TX_LENGTH]);

		if (val > standalone_mt76_testmode_max_mpdu_len(phy, td->tx_rate_mode) ||
		    val < sizeof(struct ieee80211_hdr))
			goto out;

		td->tx_mpdu_len = val;
	}

	if (tb[STANDALONE_MT76_TM_ATTR_TX_IPG])
		td->tx_ipg = nla_get_u32(tb[STANDALONE_MT76_TM_ATTR_TX_IPG]);

	if (tb[STANDALONE_MT76_TM_ATTR_TX_TIME])
		td->tx_time = nla_get_u32(tb[STANDALONE_MT76_TM_ATTR_TX_TIME]);

	if (tb[STANDALONE_MT76_TM_ATTR_FREQ_OFFSET])
		td->freq_offset = nla_get_u32(tb[STANDALONE_MT76_TM_ATTR_FREQ_OFFSET]);

	if (tb[STANDALONE_MT76_TM_ATTR_STATE]) {
		state = nla_get_u32(tb[STANDALONE_MT76_TM_ATTR_STATE]);
		if (state > STANDALONE_MT76_TM_STATE_MAX)
			goto out;
	} else {
		state = td->state;
	}

	if (tb[STANDALONE_MT76_TM_ATTR_TX_POWER]) {
		struct nlattr *cur;
		int idx = 0;
		int rem;

		nla_for_each_nested(cur, tb[STANDALONE_MT76_TM_ATTR_TX_POWER], rem) {
			if (nla_len(cur) != 1 ||
			    idx >= ARRAY_SIZE(td->tx_power))
				goto out;

			td->tx_power[idx++] = nla_get_u8(cur);
		}
	}

	if (tb[STANDALONE_MT76_TM_ATTR_MAC_ADDRS]) {
		struct nlattr *cur;
		int idx = 0;
		int rem;

		nla_for_each_nested(cur, tb[STANDALONE_MT76_TM_ATTR_MAC_ADDRS], rem) {
			if (nla_len(cur) != ETH_ALEN || idx >= 3)
				goto out;

			memcpy(td->addr[idx], nla_data(cur), ETH_ALEN);
			idx++;
		}
	}

	if (dev->test_ops->set_params) {
		err = dev->test_ops->set_params(phy, tb, state);
		if (err)
			goto out;
	}

	for (i = STANDALONE_MT76_TM_ATTR_STATE; i < ARRAY_SIZE(tb); i++)
		if (tb[i])
			standalone_mt76_testmode_param_set(td, i);

	err = 0;
	if (tb[STANDALONE_MT76_TM_ATTR_STATE])
		err = standalone_mt76_testmode_set_state(phy, state);

out:
	mutex_unlock(&dev->mutex);

	return err;
}
EXPORT_SYMBOL(standalone_mt76_testmode_cmd);

static int
standalone_mt76_testmode_dump_stats(struct standalone_mt76_phy *phy, struct sk_buff *msg)
{
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct standalone_mt76_dev *dev = phy->dev;
	u64 rx_packets = 0;
	u64 rx_fcs_error = 0;
	int i;

	if (dev->test_ops->dump_stats) {
		int ret;

		ret = dev->test_ops->dump_stats(phy, msg);
		if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(td->rx_stats.packets); i++) {
		rx_packets += td->rx_stats.packets[i];
		rx_fcs_error += td->rx_stats.fcs_error[i];
	}

	if (nla_put_u32(msg, STANDALONE_MT76_TM_STATS_ATTR_TX_PENDING, td->tx_pending) ||
	    nla_put_u32(msg, STANDALONE_MT76_TM_STATS_ATTR_TX_QUEUED, td->tx_queued) ||
	    nla_put_u32(msg, STANDALONE_MT76_TM_STATS_ATTR_TX_DONE, td->tx_done) ||
	    nla_put_u64_64bit(msg, STANDALONE_MT76_TM_STATS_ATTR_RX_PACKETS, rx_packets,
			      STANDALONE_MT76_TM_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(msg, STANDALONE_MT76_TM_STATS_ATTR_RX_FCS_ERROR, rx_fcs_error,
			      STANDALONE_MT76_TM_STATS_ATTR_PAD))
		return -EMSGSIZE;

	return 0;
}

int standalone_mt76_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *msg,
		       struct netlink_callback *cb, void *data, int len)
{
	struct standalone_mt76_phy *phy = hw->priv;
	struct standalone_mt76_dev *dev = phy->dev;
	struct standalone_mt76_testmode_data *td = &phy->test;
	struct nlattr *tb[NUM_STANDALONE_MT76_TM_ATTRS] = {};
	int err = 0;
	void *a;
	int i;

	if (!dev->test_ops)
		return -EOPNOTSUPP;

	if (cb->args[2]++ > 0)
		return -ENOENT;

	if (data) {
		err = nla_parse_deprecated(tb, STANDALONE_MT76_TM_ATTR_MAX, data, len,
					   standalone_mt76_tm_policy, NULL);
		if (err)
			return err;
	}

	mutex_lock(&dev->mutex);

	if (tb[STANDALONE_MT76_TM_ATTR_STATS]) {
		err = -EINVAL;

		a = nla_nest_start(msg, STANDALONE_MT76_TM_ATTR_STATS);
		if (a) {
			err = standalone_mt76_testmode_dump_stats(phy, msg);
			nla_nest_end(msg, a);
		}

		goto out;
	}

	standalone_mt76_testmode_init_defaults(phy);

	err = -EMSGSIZE;
	if (nla_put_u32(msg, STANDALONE_MT76_TM_ATTR_STATE, td->state))
		goto out;

	if (dev->test_mtd.name &&
	    (nla_put_string(msg, STANDALONE_MT76_TM_ATTR_MTD_PART, dev->test_mtd.name) ||
	     nla_put_u32(msg, STANDALONE_MT76_TM_ATTR_MTD_OFFSET, dev->test_mtd.offset)))
		goto out;

	if (nla_put_u32(msg, STANDALONE_MT76_TM_ATTR_TX_COUNT, td->tx_count) ||
	    nla_put_u32(msg, STANDALONE_MT76_TM_ATTR_TX_LENGTH, td->tx_mpdu_len) ||
	    nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_RATE_MODE, td->tx_rate_mode) ||
	    nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_RATE_NSS, td->tx_rate_nss) ||
	    nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_RATE_IDX, td->tx_rate_idx) ||
	    nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_RATE_SGI, td->tx_rate_sgi) ||
	    nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_RATE_LDPC, td->tx_rate_ldpc) ||
	    nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_RATE_STBC, td->tx_rate_stbc) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_LTF) &&
	     nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_LTF, td->tx_ltf)) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_ANTENNA) &&
	     nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_ANTENNA, td->tx_antenna_mask)) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_SPE_IDX) &&
	     nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_SPE_IDX, td->tx_spe_idx)) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_DUTY_CYCLE) &&
	     nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_DUTY_CYCLE, td->tx_duty_cycle)) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_IPG) &&
	     nla_put_u32(msg, STANDALONE_MT76_TM_ATTR_TX_IPG, td->tx_ipg)) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_TIME) &&
	     nla_put_u32(msg, STANDALONE_MT76_TM_ATTR_TX_TIME, td->tx_time)) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_POWER_CONTROL) &&
	     nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_TX_POWER_CONTROL, td->tx_power_control)) ||
	    (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_FREQ_OFFSET) &&
	     nla_put_u8(msg, STANDALONE_MT76_TM_ATTR_FREQ_OFFSET, td->freq_offset)))
		goto out;

	if (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_TX_POWER)) {
		a = nla_nest_start(msg, STANDALONE_MT76_TM_ATTR_TX_POWER);
		if (!a)
			goto out;

		for (i = 0; i < ARRAY_SIZE(td->tx_power); i++)
			if (nla_put_u8(msg, i, td->tx_power[i]))
				goto out;

		nla_nest_end(msg, a);
	}

	if (standalone_mt76_testmode_param_present(td, STANDALONE_MT76_TM_ATTR_MAC_ADDRS)) {
		a = nla_nest_start(msg, STANDALONE_MT76_TM_ATTR_MAC_ADDRS);
		if (!a)
			goto out;

		for (i = 0; i < 3; i++)
			if (nla_put(msg, i, ETH_ALEN, td->addr[i]))
				goto out;

		nla_nest_end(msg, a);
	}

	err = 0;

out:
	mutex_unlock(&dev->mutex);

	return err;
}
EXPORT_SYMBOL(standalone_mt76_testmode_dump);

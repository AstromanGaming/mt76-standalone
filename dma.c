// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */

#include <linux/dma-mapping.h>
#include "standalone_mt76.h"
#include "dma.h"
#include "standalone_mt76_connac.h"

static struct standalone_mt76_txwi_cache *
standalone_mt76_alloc_txwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t;
	dma_addr_t addr;
	u8 *txwi;
	int size;

	size = L1_CACHE_ALIGN(dev->drv->txwi_size + sizeof(*t));
	txwi = kzalloc(size, GFP_ATOMIC);
	if (!txwi)
		return NULL;

	addr = dma_map_single(dev->dma_dev, txwi, dev->drv->txwi_size,
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev->dma_dev, addr))) {
		kfree(txwi);
		return NULL;
	}

	t = (struct standalone_mt76_txwi_cache *)(txwi + dev->drv->txwi_size);
	t->dma_addr = addr;

	return t;
}

static struct standalone_mt76_txwi_cache *
standalone_mt76_alloc_rxwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t;

	t = kzalloc(L1_CACHE_ALIGN(sizeof(*t)), GFP_ATOMIC);
	if (!t)
		return NULL;

	t->ptr = NULL;
	return t;
}

static struct standalone_mt76_txwi_cache *
__standalone_mt76_get_txwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t = NULL;

	spin_lock(&dev->lock);
	if (!list_empty(&dev->txwi_cache)) {
		t = list_first_entry(&dev->txwi_cache, struct standalone_mt76_txwi_cache,
				     list);
		list_del(&t->list);
	}
	spin_unlock(&dev->lock);

	return t;
}

static struct standalone_mt76_txwi_cache *
__standalone_mt76_get_rxwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t = NULL;

	spin_lock_bh(&dev->wed_lock);
	if (!list_empty(&dev->rxwi_cache)) {
		t = list_first_entry(&dev->rxwi_cache, struct standalone_mt76_txwi_cache,
				     list);
		list_del(&t->list);
	}
	spin_unlock_bh(&dev->wed_lock);

	return t;
}

static struct standalone_mt76_txwi_cache *
standalone_mt76_get_txwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t = __standalone_mt76_get_txwi(dev);

	if (t)
		return t;

	return standalone_mt76_alloc_txwi(dev);
}

struct standalone_mt76_txwi_cache *
standalone_mt76_get_rxwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t = __standalone_mt76_get_rxwi(dev);

	if (t)
		return t;

	return standalone_mt76_alloc_rxwi(dev);
}
EXPORT_SYMBOL_GPL(standalone_mt76_get_rxwi);

void
standalone_mt76_put_txwi(struct standalone_mt76_dev *dev, struct standalone_mt76_txwi_cache *t)
{
	if (!t)
		return;

	spin_lock(&dev->lock);
	list_add(&t->list, &dev->txwi_cache);
	spin_unlock(&dev->lock);
}
EXPORT_SYMBOL_GPL(standalone_mt76_put_txwi);

void
standalone_mt76_put_rxwi(struct standalone_mt76_dev *dev, struct standalone_mt76_txwi_cache *t)
{
	if (!t)
		return;

	spin_lock_bh(&dev->wed_lock);
	list_add(&t->list, &dev->rxwi_cache);
	spin_unlock_bh(&dev->wed_lock);
}
EXPORT_SYMBOL_GPL(standalone_mt76_put_rxwi);

static void
standalone_mt76_free_pending_txwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t;

	local_bh_disable();
	while ((t = __standalone_mt76_get_txwi(dev)) != NULL) {
		dma_unmap_single(dev->dma_dev, t->dma_addr, dev->drv->txwi_size,
				 DMA_TO_DEVICE);
		kfree(standalone_mt76_get_txwi_ptr(dev, t));
	}
	local_bh_enable();
}

void
standalone_mt76_free_pending_rxwi(struct standalone_mt76_dev *dev)
{
	struct standalone_mt76_txwi_cache *t;

	local_bh_disable();
	while ((t = __standalone_mt76_get_rxwi(dev)) != NULL) {
		if (t->ptr)
			standalone_mt76_put_page_pool_buf(t->ptr, false);
		kfree(t);
	}
	local_bh_enable();
}
EXPORT_SYMBOL_GPL(standalone_mt76_free_pending_rxwi);

static void
standalone_mt76_dma_queue_magic_cnt_init(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q)
{
	if (!standalone_mt76_queue_is_wed_rro(q))
		return;

	q->magic_cnt = 0;
	if (standalone_mt76_queue_is_wed_rro_ind(q)) {
		struct standalone_mt76_wed_rro_desc *rro_desc;
		u32 data1 = FIELD_PREP(RRO_IND_DATA1_MAGIC_CNT_MASK,
				       MT_DMA_WED_IND_CMD_CNT - 1);
		int i;

		rro_desc = (struct standalone_mt76_wed_rro_desc *)q->desc;
		for (i = 0; i < q->ndesc; i++) {
			struct standalone_mt76_wed_rro_ind *cmd;

			cmd = (struct standalone_mt76_wed_rro_ind *)&rro_desc[i];
			cmd->data1 = cpu_to_le32(data1);
		}
	} else if (standalone_mt76_queue_is_wed_rro_rxdmad_c(q)) {
		struct standalone_mt76_rro_rxdmad_c *dmad = (void *)q->desc;
		u32 data3 = FIELD_PREP(RRO_RXDMAD_DATA3_MAGIC_CNT_MASK,
				       MT_DMA_MAGIC_CNT - 1);
		int i;

		for (i = 0; i < q->ndesc; i++)
			dmad[i].data3 = cpu_to_le32(data3);
	}
}

static void
standalone_mt76_dma_sync_idx(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q)
{
	if ((q->flags & MT_QFLAG_WED_RRO_EN) &&
	    (!is_standalone_mt7992(dev) || !standalone_mt76_npu_device_active(dev)))
		Q_WRITE(q, ring_size, MT_DMA_RRO_EN | q->ndesc);
	else
		Q_WRITE(q, ring_size, q->ndesc);

	if (standalone_mt76_queue_is_npu_tx(q)) {
		writel(q->ndesc, &q->regs->ring_size);
		writel(q->desc_dma, &q->regs->desc_base);
	}

	Q_WRITE(q, desc_base, q->desc_dma);
	q->head = Q_READ(q, dma_idx);
	q->tail = q->head;
}

void standalone_mt76_dma_queue_reset(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
			  bool reset_idx)
{
	if (!q || !q->ndesc)
		return;

	if (!standalone_mt76_queue_is_wed_rro_ind(q) &&
	    !standalone_mt76_queue_is_wed_rro_rxdmad_c(q) && !standalone_mt76_queue_is_npu(q)) {
		int i;

		/* clear descriptors */
		for (i = 0; i < q->ndesc; i++)
			q->desc[i].ctrl = cpu_to_le32(MT_DMA_CTL_DMA_DONE);
	}

	standalone_mt76_dma_queue_magic_cnt_init(dev, q);
	if (reset_idx) {
		if (standalone_mt76_queue_is_emi(q))
			*q->emi_cpu_idx = 0;
		else
			Q_WRITE(q, cpu_idx, 0);
		Q_WRITE(q, dma_idx, 0);
	}
	standalone_mt76_dma_sync_idx(dev, q);
}

static int
standalone_mt76_dma_add_rx_buf(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
		    struct standalone_mt76_queue_buf *buf, void *data)
{
	struct standalone_mt76_queue_entry *entry = &q->entry[q->head];
	struct standalone_mt76_txwi_cache *txwi = NULL;
	u32 buf1 = 0, ctrl, info = 0;
	struct standalone_mt76_desc *desc;
	int idx = q->head;
	int rx_token;

	if (standalone_mt76_queue_is_wed_rro_ind(q)) {
		struct standalone_mt76_wed_rro_desc *rro_desc;

		rro_desc = (struct standalone_mt76_wed_rro_desc *)q->desc;
		data = &rro_desc[q->head];
		goto done;
	} else if (standalone_mt76_queue_is_wed_rro_rxdmad_c(q)) {
		data = &q->desc[q->head];
		goto done;
	}

	desc = &q->desc[q->head];
	ctrl = FIELD_PREP(MT_DMA_CTL_SD_LEN0, buf[0].len);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	buf1 = FIELD_PREP(MT_DMA_CTL_SDP0_H, buf->addr >> 32);
#endif

	if (standalone_mt76_queue_is_wed_rx(q) || standalone_mt76_queue_is_wed_rro_data(q)) {
		txwi = standalone_mt76_get_rxwi(dev);
		if (!txwi)
			return -ENOMEM;

		rx_token = standalone_mt76_rx_token_consume(dev, data, txwi, buf->addr);
		if (rx_token < 0) {
			standalone_mt76_put_rxwi(dev, txwi);
			return -ENOMEM;
		}

		buf1 |= FIELD_PREP(MT_DMA_CTL_TOKEN, rx_token);
		ctrl |= MT_DMA_CTL_TO_HOST;

		txwi->qid = q - dev->q_rx;
	}

	if (standalone_mt76_queue_is_wed_rro_msdu_pg(q) &&
	    dev->drv->rx_rro_add_msdu_page) {
		if (dev->drv->rx_rro_add_msdu_page(dev, q, buf->addr, data))
			return -ENOMEM;
	}

	if (q->flags & MT_QFLAG_WED_RRO_EN) {
		info |= FIELD_PREP(MT_DMA_MAGIC_MASK, q->magic_cnt);
		if ((q->head + 1) == q->ndesc)
			q->magic_cnt = (q->magic_cnt + 1) % MT_DMA_MAGIC_CNT;
	}

	WRITE_ONCE(desc->buf0, cpu_to_le32(buf->addr));
	WRITE_ONCE(desc->buf1, cpu_to_le32(buf1));
	WRITE_ONCE(desc->ctrl, cpu_to_le32(ctrl));
	WRITE_ONCE(desc->info, cpu_to_le32(info));

done:
	entry->dma_addr[0] = buf->addr;
	entry->dma_len[0] = buf->len;
	entry->txwi = txwi;
	entry->buf = data;
	entry->wcid = 0xffff;
	entry->skip_buf1 = true;
	q->head = (q->head + 1) % q->ndesc;
	q->queued++;

	return idx;
}

static int
standalone_mt76_dma_add_buf(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
		 struct standalone_mt76_queue_buf *buf, int nbufs, u32 info,
		 struct sk_buff *skb, void *txwi)
{
	struct standalone_mt76_queue_entry *entry;
	struct standalone_mt76_desc *desc;
	int i, idx = -1;
	u32 ctrl, next;

	if (txwi) {
		q->entry[q->head].txwi = DMA_DUMMY_DATA;
		q->entry[q->head].skip_buf0 = true;
	}

	for (i = 0; i < nbufs; i += 2, buf += 2) {
		u32 buf0 = buf[0].addr, buf1 = 0;

		idx = q->head;
		next = (q->head + 1) % q->ndesc;

		desc = &q->desc[idx];
		entry = &q->entry[idx];

		if (buf[0].skip_unmap)
			entry->skip_buf0 = true;
		entry->skip_buf1 = i == nbufs - 1;

		entry->dma_addr[0] = buf[0].addr;
		entry->dma_len[0] = buf[0].len;

		ctrl = FIELD_PREP(MT_DMA_CTL_SD_LEN0, buf[0].len);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		info |= FIELD_PREP(MT_DMA_CTL_SDP0_H, buf[0].addr >> 32);
#endif
		if (i < nbufs - 1) {
			entry->dma_addr[1] = buf[1].addr;
			entry->dma_len[1] = buf[1].len;
			buf1 = buf[1].addr;
			ctrl |= FIELD_PREP(MT_DMA_CTL_SD_LEN1, buf[1].len);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			info |= FIELD_PREP(MT_DMA_CTL_SDP1_H,
					   buf[1].addr >> 32);
#endif
			if (buf[1].skip_unmap)
				entry->skip_buf1 = true;
		}

		if (i == nbufs - 1)
			ctrl |= MT_DMA_CTL_LAST_SEC0;
		else if (i == nbufs - 2)
			ctrl |= MT_DMA_CTL_LAST_SEC1;

		WRITE_ONCE(desc->buf0, cpu_to_le32(buf0));
		WRITE_ONCE(desc->buf1, cpu_to_le32(buf1));
		WRITE_ONCE(desc->info, cpu_to_le32(info));
		WRITE_ONCE(desc->ctrl, cpu_to_le32(ctrl));

		q->head = next;
		q->queued++;
	}

	q->entry[idx].txwi = txwi;
	q->entry[idx].skb = skb;
	q->entry[idx].wcid = 0xffff;

	return idx;
}

static void
standalone_mt76_dma_tx_cleanup_idx(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, int idx,
			struct standalone_mt76_queue_entry *prev_e)
{
	struct standalone_mt76_queue_entry *e = &q->entry[idx];

	if (!e->skip_buf0)
		dma_unmap_single(dev->dma_dev, e->dma_addr[0], e->dma_len[0],
				 DMA_TO_DEVICE);

	if (!e->skip_buf1)
		dma_unmap_single(dev->dma_dev, e->dma_addr[1], e->dma_len[1],
				 DMA_TO_DEVICE);

	if (e->txwi == DMA_DUMMY_DATA)
		e->txwi = NULL;

	*prev_e = *e;
	memset(e, 0, sizeof(*e));
}

static void
standalone_mt76_dma_kick_queue(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q)
{
	wmb();
	if (standalone_mt76_queue_is_emi(q))
		*q->emi_cpu_idx = cpu_to_le16(q->head);
	else
		Q_WRITE(q, cpu_idx, q->head);
}

static void
standalone_mt76_dma_tx_cleanup(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, bool flush)
{
	struct standalone_mt76_queue_entry entry;
	int last;

	if (!q || !q->ndesc)
		return;

	spin_lock_bh(&q->cleanup_lock);
	if (flush)
		last = -1;
	else
		last = Q_READ(q, dma_idx);

	while (q->queued > 0 && q->tail != last) {
		standalone_mt76_dma_tx_cleanup_idx(dev, q, q->tail, &entry);
		standalone_mt76_npu_txdesc_cleanup(q, q->tail);
		standalone_mt76_queue_tx_complete(dev, q, &entry);

		if (entry.txwi) {
			if (!(dev->drv->drv_flags & MT_DRV_TXWI_NO_FREE))
				standalone_mt76_put_txwi(dev, entry.txwi);
		}

		if (!flush && q->tail == last)
			last = Q_READ(q, dma_idx);
	}
	spin_unlock_bh(&q->cleanup_lock);

	if (flush) {
		spin_lock_bh(&q->lock);
		standalone_mt76_dma_sync_idx(dev, q);
		standalone_mt76_dma_kick_queue(dev, q);
		spin_unlock_bh(&q->lock);
	}

	if (!q->queued)
		wake_up(&dev->tx_wait);
}

static void *
standalone_mt76_dma_get_rxdmad_c_buf(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
			  int idx, int *len, bool *more)
{
	struct standalone_mt76_queue_entry *e = &q->entry[idx];
	struct standalone_mt76_rro_rxdmad_c *dmad = e->buf;
	u32 data1 = le32_to_cpu(dmad->data1);
	u32 data2 = le32_to_cpu(dmad->data2);
	struct standalone_mt76_txwi_cache *t;
	u16 rx_token_id;
	u8 ind_reason;
	void *buf;

	rx_token_id = FIELD_GET(RRO_RXDMAD_DATA2_RX_TOKEN_ID_MASK, data2);
	t = standalone_mt76_rx_token_release(dev, rx_token_id);
	if (!t)
		return ERR_PTR(-EAGAIN);

	q = &dev->q_rx[t->qid];
	dma_sync_single_for_cpu(dev->dma_dev, t->dma_addr,
				SKB_WITH_OVERHEAD(q->buf_size),
				page_pool_get_dma_dir(q->page_pool));

	if (len)
		*len = FIELD_GET(RRO_RXDMAD_DATA1_SDL0_MASK, data1);
	if (more)
		*more = !FIELD_GET(RRO_RXDMAD_DATA1_LS_MASK, data1);

	buf = t->ptr;
	ind_reason = FIELD_GET(RRO_RXDMAD_DATA2_IND_REASON_MASK, data2);
	if (ind_reason == MT_DMA_WED_IND_REASON_REPEAT ||
	    ind_reason == MT_DMA_WED_IND_REASON_OLDPKT) {
		standalone_mt76_put_page_pool_buf(buf, false);
		buf = ERR_PTR(-EAGAIN);
	}
	t->ptr = NULL;
	t->dma_addr = 0;

	standalone_mt76_put_rxwi(dev, t);

	return buf;
}

static void *
standalone_mt76_dma_get_buf(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, int idx,
		 int *len, u32 *info, bool *more, bool *drop, bool flush)
{
	struct standalone_mt76_queue_entry *e = &q->entry[idx];
	struct standalone_mt76_desc *desc = &q->desc[idx];
	u32 ctrl, desc_info, buf1;
	void *buf = e->buf;

	if (standalone_mt76_queue_is_wed_rro_rxdmad_c(q) && !flush)
		buf = standalone_mt76_dma_get_rxdmad_c_buf(dev, q, idx, len, more);

	if (standalone_mt76_queue_is_wed_rro(q))
		goto done;

	ctrl = le32_to_cpu(READ_ONCE(desc->ctrl));
	if (len) {
		*len = FIELD_GET(MT_DMA_CTL_SD_LEN0, ctrl);
		*more = !(ctrl & MT_DMA_CTL_LAST_SEC0);
	}

	desc_info = le32_to_cpu(desc->info);
	if (info)
		*info = desc_info;

	buf1 = le32_to_cpu(desc->buf1);
	standalone_mt76_dma_should_drop_buf(drop, ctrl, buf1, desc_info);

	if (standalone_mt76_queue_is_wed_rx(q)) {
		u32 token = FIELD_GET(MT_DMA_CTL_TOKEN, buf1);
		struct standalone_mt76_txwi_cache *t = standalone_mt76_rx_token_release(dev, token);

		if (!t)
			return NULL;

		dma_sync_single_for_cpu(dev->dma_dev, t->dma_addr,
				SKB_WITH_OVERHEAD(q->buf_size),
				page_pool_get_dma_dir(q->page_pool));

		buf = t->ptr;
		t->dma_addr = 0;
		t->ptr = NULL;

		standalone_mt76_put_rxwi(dev, t);
		if (drop)
			*drop |= !!(buf1 & MT_DMA_CTL_WO_DROP);
	} else {
		dma_sync_single_for_cpu(dev->dma_dev, e->dma_addr[0],
				SKB_WITH_OVERHEAD(q->buf_size),
				page_pool_get_dma_dir(q->page_pool));
	}

done:
	e->buf = NULL;
	return buf;
}

static void *
standalone_mt76_dma_dequeue(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, bool flush,
		 int *len, u32 *info, bool *more, bool *drop)
{
	int idx = q->tail;

	*more = false;
	if (!q->queued)
		return NULL;

	if (standalone_mt76_queue_is_wed_rro_data(q) || standalone_mt76_queue_is_wed_rro_msdu_pg(q))
		goto done;

	if (standalone_mt76_queue_is_wed_rro_ind(q)) {
		struct standalone_mt76_wed_rro_ind *cmd;
		u8 magic_cnt;

		if (flush)
			goto done;

		cmd = q->entry[idx].buf;
		magic_cnt = FIELD_GET(RRO_IND_DATA1_MAGIC_CNT_MASK,
				      le32_to_cpu(cmd->data1));
		if (magic_cnt != q->magic_cnt)
			return NULL;

		if (q->tail == q->ndesc - 1)
			q->magic_cnt = (q->magic_cnt + 1) % MT_DMA_WED_IND_CMD_CNT;
	} else if (standalone_mt76_queue_is_wed_rro_rxdmad_c(q)) {
		struct standalone_mt76_rro_rxdmad_c *dmad;
		u16 magic_cnt;

		if (flush)
			goto done;

		dmad = q->entry[idx].buf;
		magic_cnt = FIELD_GET(RRO_RXDMAD_DATA3_MAGIC_CNT_MASK,
				      le32_to_cpu(dmad->data3));
		if (magic_cnt != q->magic_cnt)
			return NULL;

		if (q->tail == q->ndesc - 1)
			q->magic_cnt = (q->magic_cnt + 1) % MT_DMA_MAGIC_CNT;
	} else {
		if (flush)
			q->desc[idx].ctrl |= cpu_to_le32(MT_DMA_CTL_DMA_DONE);
		else if (!(q->desc[idx].ctrl & cpu_to_le32(MT_DMA_CTL_DMA_DONE)))
			return NULL;
	}
done:
	q->tail = (q->tail + 1) % q->ndesc;
	q->queued--;

	return standalone_mt76_dma_get_buf(dev, q, idx, len, info, more, drop, flush);
}

static int
standalone_mt76_dma_tx_queue_skb_raw(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
			  struct sk_buff *skb, u32 tx_info)
{
	struct standalone_mt76_queue_buf buf = {};
	dma_addr_t addr;

	if (test_bit(STANDALONE_MT76_MCU_RESET, &dev->phy.state))
		goto error;

	if (q->queued + 1 >= q->ndesc - 1)
		goto error;

	addr = dma_map_single(dev->dma_dev, skb->data, skb->len,
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev->dma_dev, addr)))
		goto error;

	buf.addr = addr;
	buf.len = skb->len;

	spin_lock_bh(&q->lock);
	standalone_mt76_dma_add_buf(dev, q, &buf, 1, tx_info, skb, NULL);
	standalone_mt76_dma_kick_queue(dev, q);
	spin_unlock_bh(&q->lock);

	return 0;

error:
	dev_kfree_skb(skb);
	return -ENOMEM;
}

static int
standalone_mt76_dma_tx_queue_skb(struct standalone_mt76_phy *phy, struct standalone_mt76_queue *q,
		      enum standalone_mt76_txq_id qid, struct sk_buff *skb,
		      struct standalone_mt76_wcid *wcid, struct ieee80211_sta *sta)
{
	struct ieee80211_tx_status status = {
		.sta = sta,
	};
	struct standalone_mt76_tx_info tx_info = {
		.skb = skb,
	};
	struct standalone_mt76_dev *dev = phy->dev;
	struct ieee80211_hw *hw;
	int len, n = 0, ret = -ENOMEM;
	struct standalone_mt76_txwi_cache *t;
	struct sk_buff *iter;
	dma_addr_t addr;
	u8 *txwi;

	if (test_bit(STANDALONE_MT76_RESET, &phy->state))
		goto free_skb;

	/* TODO: Take into account unlinear skbs */
	if (standalone_mt76_npu_device_active(dev) && skb_linearize(skb))
		goto free_skb;

	t = standalone_mt76_get_txwi(dev);
	if (!t)
		goto free_skb;

	t->phy_idx = phy->band_idx;
	t->qid = qid;
	txwi = standalone_mt76_get_txwi_ptr(dev, t);

	skb->prev = skb->next = NULL;
	if (dev->drv->drv_flags & MT_DRV_TX_ALIGNED4_SKBS)
		standalone_mt76_insert_hdr_pad(skb);

	len = skb_headlen(skb);
	addr = dma_map_single(dev->dma_dev, skb->data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev->dma_dev, addr)))
		goto free;

	tx_info.buf[n].addr = t->dma_addr;
	tx_info.buf[n++].len = dev->drv->txwi_size;
	tx_info.buf[n].addr = addr;
	tx_info.buf[n++].len = len;

	skb_walk_frags(skb, iter) {
		if (n == ARRAY_SIZE(tx_info.buf))
			goto unmap;

		addr = dma_map_single(dev->dma_dev, iter->data, iter->len,
				      DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev->dma_dev, addr)))
			goto unmap;

		tx_info.buf[n].addr = addr;
		tx_info.buf[n++].len = iter->len;
	}
	tx_info.nbuf = n;

	if (q->queued + (tx_info.nbuf + 1) / 2 >= q->ndesc - 1) {
		ret = -ENOMEM;
		goto unmap;
	}

	dma_sync_single_for_cpu(dev->dma_dev, t->dma_addr, dev->drv->txwi_size,
				DMA_TO_DEVICE);
	ret = dev->drv->tx_prepare_skb(dev, txwi, qid, wcid, sta, &tx_info);
	dma_sync_single_for_device(dev->dma_dev, t->dma_addr, dev->drv->txwi_size,
				   DMA_TO_DEVICE);
	if (ret < 0)
		goto unmap;

	if (standalone_mt76_npu_device_active(dev))
		return standalone_mt76_npu_dma_add_buf(phy, q, skb, &tx_info.buf[1], txwi);

	return standalone_mt76_dma_add_buf(dev, q, tx_info.buf, tx_info.nbuf,
				tx_info.info, tx_info.skb, t);

unmap:
	for (n--; n > 0; n--)
		dma_unmap_single(dev->dma_dev, tx_info.buf[n].addr,
				 tx_info.buf[n].len, DMA_TO_DEVICE);

free:
#ifdef CONFIG_NL80211_TESTMODE
	/* fix tx_done accounting on queue overflow */
	if (standalone_mt76_is_testmode_skb(dev, skb, &hw)) {
		struct standalone_mt76_phy *phy = hw->priv;

		if (tx_info.skb == phy->test.tx_skb)
			phy->test.tx_done--;
	}
#endif

	standalone_mt76_put_txwi(dev, t);

free_skb:
	status.skb = tx_info.skb;
	hw = standalone_mt76_tx_status_get_hw(dev, tx_info.skb);
	spin_lock_bh(&dev->rx_lock);
	ieee80211_tx_status_ext(hw, &status);
	spin_unlock_bh(&dev->rx_lock);

	return ret;
}

static int
standalone_mt76_dma_rx_fill_buf(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
		     bool allow_direct)
{
	int len = SKB_WITH_OVERHEAD(q->buf_size);
	int frames = 0;

	if (!q->ndesc)
		return 0;

	while (q->queued < q->ndesc - 1) {
		struct standalone_mt76_queue_buf qbuf = {};
		void *buf = NULL;
		int offset;

		if (standalone_mt76_queue_is_wed_rro_ind(q) ||
		    standalone_mt76_queue_is_wed_rro_rxdmad_c(q))
			goto done;

		buf = standalone_mt76_get_page_pool_buf(q, &offset, q->buf_size);
		if (!buf)
			break;

		qbuf.addr = page_pool_get_dma_addr(virt_to_head_page(buf)) +
			    offset + q->buf_offset;
done:
		qbuf.len = len - q->buf_offset;
		qbuf.skip_unmap = false;
		if (standalone_mt76_dma_add_rx_buf(dev, q, &qbuf, buf) < 0) {
			standalone_mt76_put_page_pool_buf(buf, allow_direct);
			break;
		}
		frames++;
	}

	if (frames || standalone_mt76_queue_is_wed_rx(q))
		standalone_mt76_dma_kick_queue(dev, q);

	return frames;
}

int standalone_mt76_dma_rx_fill(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
		     bool allow_direct)
{
	int frames;

	spin_lock_bh(&q->lock);
	frames = standalone_mt76_dma_rx_fill_buf(dev, q, allow_direct);
	spin_unlock_bh(&q->lock);

	return frames;
}

static int
standalone_mt76_dma_alloc_queue(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q,
		     int idx, int n_desc, int bufsize,
		     u32 ring_base)
{
	int ret, size;

	spin_lock_init(&q->lock);
	spin_lock_init(&q->cleanup_lock);

	q->regs = dev->mmio.regs + ring_base + idx * MT_RING_SIZE;
	q->ndesc = n_desc;
	q->buf_size = bufsize;
	q->hw_idx = idx;
	q->dev = dev;

	if (standalone_mt76_queue_is_wed_rro_ind(q))
		size = sizeof(struct standalone_mt76_wed_rro_desc);
	else if (standalone_mt76_queue_is_npu_tx(q))
		size = sizeof(struct airoha_npu_tx_dma_desc);
	else if (standalone_mt76_queue_is_npu_rx(q))
		size = sizeof(struct airoha_npu_rx_dma_desc);
	else
		size = sizeof(struct standalone_mt76_desc);

	q->desc = dmam_alloc_coherent(dev->dma_dev, q->ndesc * size,
				      &q->desc_dma, GFP_KERNEL);
	if (!q->desc)
		return -ENOMEM;

	standalone_mt76_dma_queue_magic_cnt_init(dev, q);
	size = q->ndesc * sizeof(*q->entry);
	q->entry = devm_kzalloc(dev->dev, size, GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	ret = standalone_mt76_create_page_pool(dev, q);
	if (ret)
		return ret;

	standalone_mt76_npu_queue_setup(dev, q);
	ret = standalone_mt76_wed_dma_setup(dev, q, false);
	if (ret)
		return ret;

	if (mtk_wed_device_active(&dev->mmio.wed)) {
		if ((mtk_wed_get_rx_capa(&dev->mmio.wed) && standalone_mt76_queue_is_wed_rro(q)) ||
		    standalone_mt76_queue_is_wed_tx_free(q))
			return 0;
	}

	/* HW specific driver is supposed to reset brand-new EMI queues since
	 * it needs to set cpu index pointer.
	 */
	standalone_mt76_dma_queue_reset(dev, q, !standalone_mt76_queue_is_emi(q));

	return 0;
}

static void
standalone_mt76_dma_rx_cleanup(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q)
{
	void *buf;
	bool more;

	if (!q->ndesc)
		return;

	if (standalone_mt76_queue_is_npu(q)) {
		standalone_mt76_npu_queue_cleanup(dev, q);
		return;
	}

	do {
		spin_lock_bh(&q->lock);
		buf = standalone_mt76_dma_dequeue(dev, q, true, NULL, NULL, &more, NULL);
		spin_unlock_bh(&q->lock);

		if (!buf)
			break;

		if (mtk_wed_device_active(&dev->mmio.wed) &&
		    standalone_mt76_queue_is_wed_rro(q))
			continue;

		if (standalone_mt76_npu_device_active(dev) &&
		    standalone_mt76_queue_is_wed_rro(q))
			continue;

		if (!standalone_mt76_queue_is_wed_rro_rxdmad_c(q) &&
		    !standalone_mt76_queue_is_wed_rro_ind(q))
			standalone_mt76_put_page_pool_buf(buf, false);
	} while (1);

	spin_lock_bh(&q->lock);
	if (q->rx_head) {
		dev_kfree_skb(q->rx_head);
		q->rx_head = NULL;
	}

	spin_unlock_bh(&q->lock);
}

static void
standalone_mt76_dma_rx_reset(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id qid)
{
	struct standalone_mt76_queue *q = &dev->q_rx[qid];

	if (!q->ndesc)
		return;

	if (!standalone_mt76_queue_is_wed_rro_ind(q) &&
	    !standalone_mt76_queue_is_wed_rro_rxdmad_c(q) && !standalone_mt76_queue_is_npu(q)) {
		int i;

		for (i = 0; i < q->ndesc; i++)
			q->desc[i].ctrl = cpu_to_le32(MT_DMA_CTL_DMA_DONE);
	}

	standalone_mt76_dma_rx_cleanup(dev, q);

	/* reset WED rx queues */
	standalone_mt76_wed_dma_setup(dev, q, true);

	if (standalone_mt76_queue_is_wed_tx_free(q))
		return;

	if (mtk_wed_device_active(&dev->mmio.wed) &&
	    standalone_mt76_queue_is_wed_rro(q))
		return;

	if (standalone_mt76_npu_device_active(dev) &&
	    standalone_mt76_queue_is_wed_rro(q))
		return;

	if (standalone_mt76_queue_is_npu_txfree(q))
		return;

	standalone_mt76_dma_sync_idx(dev, q);
	if (standalone_mt76_queue_is_npu(q))
		standalone_mt76_npu_fill_rx_queue(dev, q);
	else
		standalone_mt76_dma_rx_fill(dev, q, false);
}

static void
standalone_mt76_add_fragment(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, void *data,
		  int len, bool more, u32 info, bool allow_direct)
{
	struct sk_buff *skb = q->rx_head;
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int nr_frags = shinfo->nr_frags;

	if (nr_frags < ARRAY_SIZE(shinfo->frags)) {
		struct page *page = virt_to_head_page(data);
		int offset = data - page_address(page) + q->buf_offset;

		skb_add_rx_frag(skb, nr_frags, page, offset, len, q->buf_size);
	} else {
		standalone_mt76_put_page_pool_buf(data, allow_direct);
	}

	if (more)
		return;

	q->rx_head = NULL;
	if (nr_frags < ARRAY_SIZE(shinfo->frags))
		dev->drv->rx_skb(dev, q - dev->q_rx, skb, &info);
	else
		dev_kfree_skb(skb);
}

static int
standalone_mt76_dma_rx_process(struct standalone_mt76_dev *dev, struct standalone_mt76_queue *q, int budget)
{
	int len, data_len, done = 0, dma_idx;
	struct sk_buff *skb;
	unsigned char *data;
	bool check_ddone = false;
	bool allow_direct = !standalone_mt76_queue_is_wed_rx(q);
	bool more;

	if ((q->flags & MT_QFLAG_WED_RRO_EN) ||
	    (IS_ENABLED(CONFIG_NET_MEDIATEK_SOC_WED) &&
	     standalone_mt76_queue_is_wed_tx_free(q))) {
		dma_idx = Q_READ(q, dma_idx);
		check_ddone = true;
	}

	while (done < budget) {
		bool drop = false;
		u32 info;

		if (check_ddone) {
			if (q->tail == dma_idx)
				dma_idx = Q_READ(q, dma_idx);

			if (q->tail == dma_idx)
				break;
		}

		data = standalone_mt76_dma_dequeue(dev, q, false, &len, &info, &more,
					&drop);
		if (!data)
			break;

		if (PTR_ERR(data) == -EAGAIN) {
			done++;
			continue;
		}

		if (standalone_mt76_queue_is_wed_rro_ind(q) && dev->drv->rx_rro_ind_process)
			dev->drv->rx_rro_ind_process(dev, data);

		if (standalone_mt76_queue_is_wed_rro(q) &&
		    !standalone_mt76_queue_is_wed_rro_rxdmad_c(q)) {
			done++;
			continue;
		}

		if (drop)
			goto free_frag;

		if (q->rx_head)
			data_len = q->buf_size;
		else
			data_len = SKB_WITH_OVERHEAD(q->buf_size);

		if (data_len < len + q->buf_offset) {
			dev_kfree_skb(q->rx_head);
			q->rx_head = NULL;
			goto free_frag;
		}

		if (q->rx_head) {
			standalone_mt76_add_fragment(dev, q, data, len, more, info,
					  allow_direct);
			continue;
		}

		if (!more && dev->drv->rx_check &&
		    !(dev->drv->rx_check(dev, data, len)))
			goto free_frag;

		skb = napi_build_skb(data, q->buf_size);
		if (!skb)
			goto free_frag;

		skb_reserve(skb, q->buf_offset);
		skb_mark_for_recycle(skb);

		*(u32 *)skb->cb = info;

		__skb_put(skb, len);
		done++;

		if (more) {
			q->rx_head = skb;
			continue;
		}

		dev->drv->rx_skb(dev, q - dev->q_rx, skb, &info);
		continue;

free_frag:
		standalone_mt76_put_page_pool_buf(data, allow_direct);
	}

	standalone_mt76_dma_rx_fill(dev, q, true);
	return done;
}

int standalone_mt76_dma_rx_poll(struct napi_struct *napi, int budget)
{
	struct standalone_mt76_dev *dev;
	int qid, done = 0, cur;

	dev = standalone_mt76_priv(napi->dev);
	qid = napi - dev->napi;

	rcu_read_lock();

	do {
		cur = standalone_mt76_dma_rx_process(dev, &dev->q_rx[qid], budget - done);
		standalone_mt76_rx_poll_complete(dev, qid, napi);
		done += cur;
	} while (cur && done < budget);

	rcu_read_unlock();

	if (done < budget && napi_complete(napi))
		dev->drv->rx_poll_complete(dev, qid);

	return done;
}
EXPORT_SYMBOL_GPL(standalone_mt76_dma_rx_poll);

static void
standalone_mt76_dma_rx_queue_init(struct standalone_mt76_dev *dev, enum standalone_mt76_rxq_id qid,
		       int (*poll)(struct napi_struct *napi, int budget))
{
	netif_napi_add(dev->napi_dev, &dev->napi[qid], poll);
	standalone_mt76_dma_rx_fill_buf(dev, &dev->q_rx[qid], false);
	napi_enable(&dev->napi[qid]);
}

static int
standalone_mt76_dma_init(struct standalone_mt76_dev *dev,
	      int (*poll)(struct napi_struct *napi, int budget))
{
	struct standalone_mt76_dev **priv;
	int i;

	dev->napi_dev = alloc_netdev_dummy(sizeof(struct standalone_mt76_dev *));
	if (!dev->napi_dev)
		return -ENOMEM;

	/* napi_dev private data points to standalone_mt76_dev parent, so, standalone_mt76_dev
	 * can be retrieved given napi_dev
	 */
	priv = netdev_priv(dev->napi_dev);
	*priv = dev;

	dev->tx_napi_dev = alloc_netdev_dummy(sizeof(struct standalone_mt76_dev *));
	if (!dev->tx_napi_dev) {
		free_netdev(dev->napi_dev);
		return -ENOMEM;
	}
	priv = netdev_priv(dev->tx_napi_dev);
	*priv = dev;

	snprintf(dev->napi_dev->name, sizeof(dev->napi_dev->name), "%s",
		 wiphy_name(dev->hw->wiphy));
	dev->napi_dev->threaded = 1;
	init_completion(&dev->mmio.wed_reset);
	init_completion(&dev->mmio.wed_reset_complete);

	standalone_mt76_for_each_q_rx(dev, i) {
		if (standalone_mt76_queue_is_wed_rro(&dev->q_rx[i]))
			continue;

		standalone_mt76_dma_rx_queue_init(dev, i, poll);
	}

	return 0;
}

static const struct standalone_mt76_queue_ops standalone_mt76_dma_ops = {
	.init = standalone_mt76_dma_init,
	.alloc = standalone_mt76_dma_alloc_queue,
	.reset_q = standalone_mt76_dma_queue_reset,
	.tx_queue_skb_raw = standalone_mt76_dma_tx_queue_skb_raw,
	.tx_queue_skb = standalone_mt76_dma_tx_queue_skb,
	.tx_cleanup = standalone_mt76_dma_tx_cleanup,
	.rx_queue_init = standalone_mt76_dma_rx_queue_init,
	.rx_cleanup = standalone_mt76_dma_rx_cleanup,
	.rx_reset = standalone_mt76_dma_rx_reset,
	.kick = standalone_mt76_dma_kick_queue,
};

void standalone_mt76_dma_attach(struct standalone_mt76_dev *dev)
{
	dev->queue_ops = &standalone_mt76_dma_ops;
}
EXPORT_SYMBOL_GPL(standalone_mt76_dma_attach);

void standalone_mt76_dma_cleanup(struct standalone_mt76_dev *dev)
{
	int i;

	standalone_mt76_worker_disable(&dev->tx_worker);
	napi_disable(&dev->tx_napi);
	netif_napi_del(&dev->tx_napi);

	for (i = 0; i < ARRAY_SIZE(dev->phys); i++) {
		struct standalone_mt76_phy *phy = dev->phys[i];
		int j;

		if (!phy)
			continue;

		for (j = 0; j < ARRAY_SIZE(phy->q_tx); j++)
			standalone_mt76_dma_tx_cleanup(dev, phy->q_tx[j], true);
	}

	for (i = 0; i < ARRAY_SIZE(dev->q_mcu); i++)
		standalone_mt76_dma_tx_cleanup(dev, dev->q_mcu[i], true);

	standalone_mt76_for_each_q_rx(dev, i) {
		struct standalone_mt76_queue *q = &dev->q_rx[i];

		netif_napi_del(&dev->napi[i]);
		standalone_mt76_dma_rx_cleanup(dev, q);

		page_pool_destroy(q->page_pool);
	}

	if (mtk_wed_device_active(&dev->mmio.wed))
		mtk_wed_device_detach(&dev->mmio.wed);

	if (mtk_wed_device_active(&dev->mmio.wed_hif2))
		mtk_wed_device_detach(&dev->mmio.wed_hif2);

	standalone_mt76_free_pending_txwi(dev);
	standalone_mt76_free_pending_rxwi(dev);
	free_netdev(dev->napi_dev);
	free_netdev(dev->tx_napi_dev);
}
EXPORT_SYMBOL_GPL(standalone_mt76_dma_cleanup);

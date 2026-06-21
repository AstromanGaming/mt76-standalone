/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */

#ifndef __STANDALONE_MT76_UTIL_H
#define __STANDALONE_MT76_UTIL_H

#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <net/mac80211.h>

struct standalone_mt76_worker
{
	struct task_struct *task;
	void (*fn)(struct standalone_mt76_worker *);
	unsigned long state;
};

enum {
	STANDALONE_MT76_WORKER_SCHEDULED,
	STANDALONE_MT76_WORKER_RUNNING,
};

#define STANDALONE_MT76_INCR(_var, _size) \
	(_var = (((_var) + 1) % (_size)))

int __standalone_mt76_wcid_alloc(u32 *mask, int min, int size);

static inline int standalone_mt76_wcid_alloc(u32 *mask, int size)
{
       return __standalone_mt76_wcid_alloc(mask, 0, size);
}

static inline void
standalone_mt76_wcid_mask_set(u32 *mask, int idx)
{
	mask[idx / 32] |= BIT(idx % 32);
}

static inline void
standalone_mt76_wcid_mask_clear(u32 *mask, int idx)
{
	mask[idx / 32] &= ~BIT(idx % 32);
}

static inline void
standalone_mt76_skb_set_moredata(struct sk_buff *skb, bool enable)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (enable)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREDATA);
	else
		hdr->frame_control &= ~cpu_to_le16(IEEE80211_FCTL_MOREDATA);
}

int __standalone_mt76_worker_fn(void *ptr);

static inline int
standalone_mt76_worker_setup(struct ieee80211_hw *hw, struct standalone_mt76_worker *w,
		  void (*fn)(struct standalone_mt76_worker *),
		  const char *name)
{
	const char *dev_name = wiphy_name(hw->wiphy);
	int ret;

	if (fn)
		w->fn = fn;
	w->task = kthread_run(__standalone_mt76_worker_fn, w,
			      "standalone_mt76-%s %s", name, dev_name);

	if (IS_ERR(w->task)) {
		ret = PTR_ERR(w->task);
		w->task = NULL;
		return ret;
	}

	return 0;
}

static inline void standalone_mt76_worker_schedule(struct standalone_mt76_worker *w)
{
	if (!w->task)
		return;

	if (!test_and_set_bit(STANDALONE_MT76_WORKER_SCHEDULED, &w->state) &&
	    !test_bit(STANDALONE_MT76_WORKER_RUNNING, &w->state))
		wake_up_process(w->task);
}

static inline void standalone_mt76_worker_disable(struct standalone_mt76_worker *w)
{
	if (!w->task)
		return;

	kthread_park(w->task);
	WRITE_ONCE(w->state, 0);
}

static inline void standalone_mt76_worker_enable(struct standalone_mt76_worker *w)
{
	if (!w->task)
		return;

	kthread_unpark(w->task);
	standalone_mt76_worker_schedule(w);
}

static inline void standalone_mt76_worker_teardown(struct standalone_mt76_worker *w)
{
	if (!w->task)
		return;

	kthread_stop(w->task);
	w->task = NULL;
}

#endif

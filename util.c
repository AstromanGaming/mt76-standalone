// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */

#include <linux/module.h>
#include "standalone_mt76.h"

bool __standalone_mt76_poll(struct standalone_mt76_dev *dev, u32 offset, u32 mask, u32 val,
		 int timeout)
{
	u32 cur;

	timeout /= 10;
	do {
		cur = __standalone_mt76_rr(dev, offset) & mask;
		if (cur == val)
			return true;

		udelay(10);
	} while (timeout-- > 0);

	return false;
}
EXPORT_SYMBOL_GPL(__standalone_mt76_poll);

bool ____standalone_mt76_poll_msec(struct standalone_mt76_dev *dev, u32 offset, u32 mask, u32 val,
			int timeout, int tick)
{
	u32 cur;

	timeout /= tick;
	do {
		cur = __standalone_mt76_rr(dev, offset) & mask;
		if (cur == val)
			return true;

		usleep_range(1000 * tick, 2000 * tick);
	} while (timeout-- > 0);

	return false;
}
EXPORT_SYMBOL_GPL(____standalone_mt76_poll_msec);

int __standalone_mt76_wcid_alloc(u32 *mask, int min, int size)
{
	u32 min_mask = ~0;
	int i, idx = 0, cur;

	mask += min / 32;
	min %= 32;
	if (min > 0)
		min_mask = ~((1 << min) - 1);

	for (i = 0; i < DIV_ROUND_UP(size, 32); i++) {
		idx = ffs(~mask[i] & min_mask);
		min_mask = ~0;
		if (!idx)
			continue;

		idx--;
		cur = i * 32 + idx;
		if (cur >= size)
			break;

		mask[i] |= BIT(idx);
		return cur;
	}

	return -1;
}
EXPORT_SYMBOL_GPL(__standalone_mt76_wcid_alloc);

int standalone_mt76_get_min_avg_rssi(struct standalone_mt76_dev *dev, u8 phy_idx)
{
	struct standalone_mt76_wcid *wcid;
	int i, j, min_rssi = 0;
	s8 cur_rssi;

	local_bh_disable();
	rcu_read_lock();

	for (i = 0; i < ARRAY_SIZE(dev->wcid_mask); i++) {
		u32 mask = dev->wcid_mask[i];

		if (!mask)
			continue;

		for (j = i * 32; mask; j++, mask >>= 1) {
			if (!(mask & 1))
				continue;

			wcid = __standalone_mt76_wcid_ptr(dev, j);
			if (!wcid || wcid->phy_idx != phy_idx)
				continue;

			spin_lock(&dev->rx_lock);
			if (wcid->inactive_count++ < 5)
				cur_rssi = -ewma_signal_read(&wcid->rssi);
			else
				cur_rssi = 0;
			spin_unlock(&dev->rx_lock);

			if (cur_rssi < min_rssi)
				min_rssi = cur_rssi;
		}
	}

	rcu_read_unlock();
	local_bh_enable();

	return min_rssi;
}
EXPORT_SYMBOL_GPL(standalone_mt76_get_min_avg_rssi);

int __standalone_mt76_worker_fn(void *ptr)
{
	struct standalone_mt76_worker *w = ptr;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (kthread_should_park()) {
			kthread_parkme();
			continue;
		}

		if (!test_and_clear_bit(STANDALONE_MT76_WORKER_SCHEDULED, &w->state)) {
			schedule();
			continue;
		}

		set_bit(STANDALONE_MT76_WORKER_RUNNING, &w->state);
		set_current_state(TASK_RUNNING);
		w->fn(w);
		cond_resched();
		clear_bit(STANDALONE_MT76_WORKER_RUNNING, &w->state);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__standalone_mt76_worker_fn);

MODULE_AUTHOR("Sam Bélanger <github@astromangaming.ca>");
MODULE_DESCRIPTION("MediaTek Standalone MT76x helpers");
MODULE_LICENSE("Dual BSD/GPL");

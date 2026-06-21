// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */
#include "standalone_mt76.h"

static int
standalone_mt76_reg_set(void *data, u64 val)
{
	struct standalone_mt76_dev *dev = data;

	__standalone_mt76_wr(dev, dev->debugfs_reg, val);
	return 0;
}

static int
standalone_mt76_reg_get(void *data, u64 *val)
{
	struct standalone_mt76_dev *dev = data;

	*val = __standalone_mt76_rr(dev, dev->debugfs_reg);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_regval, standalone_mt76_reg_get, standalone_mt76_reg_set,
			 "0x%08llx\n");

static int
standalone_mt76_napi_threaded_set(void *data, u64 val)
{
	struct standalone_mt76_dev *dev = data;

	if (!standalone_mt76_is_mmio(dev))
		return -EOPNOTSUPP;

	if (dev->napi_dev->threaded != val)
		return dev_set_threaded(dev->napi_dev, val);

	return 0;
}

static int
standalone_mt76_napi_threaded_get(void *data, u64 *val)
{
	struct standalone_mt76_dev *dev = data;

	*val = dev->napi_dev->threaded;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_napi_threaded, standalone_mt76_napi_threaded_get,
			 standalone_mt76_napi_threaded_set, "%llu\n");

int standalone_mt76_queues_read(struct seq_file *s, void *data)
{
	struct standalone_mt76_dev *dev = dev_get_drvdata(s->private);
	int i;

	seq_puts(s, "     queue | hw-queued |      head |      tail |\n");
	for (i = 0; i < ARRAY_SIZE(dev->phy.q_tx); i++) {
		struct standalone_mt76_queue *q = dev->phy.q_tx[i];

		if (!q)
			continue;

		seq_printf(s, " %9d | %9d | %9d | %9d |\n",
			   i, q->queued, q->head, q->tail);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt76_queues_read);

static int standalone_mt76_rx_queues_read(struct seq_file *s, void *data)
{
	struct standalone_mt76_dev *dev = dev_get_drvdata(s->private);
	int i, queued;

	seq_puts(s, "     queue | hw-queued |      head |      tail |\n");
	standalone_mt76_for_each_q_rx(dev, i) {
		struct standalone_mt76_queue *q = &dev->q_rx[i];

		queued = standalone_mt76_is_usb(dev) ? q->ndesc - q->queued : q->queued;
		seq_printf(s, " %9d | %9d | %9d | %9d |\n",
			   i, queued, q->head, q->tail);
	}

	return 0;
}

void standalone_mt76_seq_puts_array(struct seq_file *file, const char *str,
			 s8 *val, int len)
{
	int i;

	seq_printf(file, "%16s:", str);
	for (i = 0; i < len; i++)
		seq_printf(file, " %4d", val[i]);
	seq_puts(file, "\n");
}
EXPORT_SYMBOL_GPL(standalone_mt76_seq_puts_array);

struct dentry *
standalone_mt76_register_debugfs_fops(struct standalone_mt76_phy *phy,
			   const struct file_operations *ops)
{
	const struct file_operations *fops = ops ? ops : &fops_regval;
	struct standalone_mt76_dev *dev = phy->dev;
	struct dentry *dir;

	dir = debugfs_create_dir("standalone_mt76", phy->hw->wiphy->debugfsdir);
	debugfs_create_u8("led_pin", 0600, dir, &phy->leds.pin);
	debugfs_create_bool("led_active_low", 0600, dir, &phy->leds.al);
	debugfs_create_u32("regidx", 0600, dir, &dev->debugfs_reg);
	debugfs_create_file_unsafe("regval", 0600, dir, dev, fops);
	debugfs_create_file_unsafe("napi_threaded", 0600, dir, dev,
				   &fops_napi_threaded);
	debugfs_create_blob("eeprom", 0400, dir, &dev->eeprom);
	if (dev->otp.data)
		debugfs_create_blob("otp", 0400, dir, &dev->otp);
	debugfs_create_devm_seqfile(dev->dev, "rx-queues", dir,
				    standalone_mt76_rx_queues_read);

	return dir;
}
EXPORT_SYMBOL_GPL(standalone_mt76_register_debugfs_fops);

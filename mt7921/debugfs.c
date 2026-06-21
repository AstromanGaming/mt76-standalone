// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2020 MediaTek Inc. */

#include "standalone_mt7921.h"

static int
standalone_mt7921_reg_set(void *data, u64 val)
{
	struct standalone_mt792x_dev *dev = data;

	standalone_mt792x_mutex_acquire(dev);
	standalone_mt76_wr(dev, dev->standalone_mt76.debugfs_reg, val);
	standalone_mt792x_mutex_release(dev);

	return 0;
}

static int
standalone_mt7921_reg_get(void *data, u64 *val)
{
	struct standalone_mt792x_dev *dev = data;

	standalone_mt792x_mutex_acquire(dev);
	*val = standalone_mt76_rr(dev, dev->standalone_mt76.debugfs_reg);
	standalone_mt792x_mutex_release(dev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_regval, standalone_mt7921_reg_get, standalone_mt7921_reg_set,
			 "0x%08llx\n");
static int
standalone_mt7921_fw_debug_set(void *data, u64 val)
{
	struct standalone_mt792x_dev *dev = data;

	standalone_mt792x_mutex_acquire(dev);

	dev->fw_debug = (u8)val;
	standalone_mt7921_mcu_fw_log_2_host(dev, dev->fw_debug);

	standalone_mt792x_mutex_release(dev);

	return 0;
}

static int
standalone_mt7921_fw_debug_get(void *data, u64 *val)
{
	struct standalone_mt792x_dev *dev = data;

	*val = dev->fw_debug;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug, standalone_mt7921_fw_debug_get,
			 standalone_mt7921_fw_debug_set, "%lld\n");

DEFINE_SHOW_ATTRIBUTE(standalone_mt792x_tx_stats);

static void
standalone_mt7921_seq_puts_array(struct seq_file *file, const char *str,
		      s8 *val, int len)
{
	int i;

	seq_printf(file, "%-16s:", str);
	for (i = 0; i < len; i++)
		if (val[i] == 127)
			seq_printf(file, " %6s", "N.A");
		else
			seq_printf(file, " %6d", val[i]);
	seq_puts(file, "\n");
}

#define standalone_mt7921_print_txpwr_entry(prefix, rate)				\
({									\
	standalone_mt7921_seq_puts_array(s, #prefix " (user)",			\
			      txpwr.data[TXPWR_USER].rate,		\
			      ARRAY_SIZE(txpwr.data[TXPWR_USER].rate)); \
	standalone_mt7921_seq_puts_array(s, #prefix " (eeprom)",			\
			      txpwr.data[TXPWR_EEPROM].rate,		\
			      ARRAY_SIZE(txpwr.data[TXPWR_EEPROM].rate)); \
	standalone_mt7921_seq_puts_array(s, #prefix " (tmac)",			\
			      txpwr.data[TXPWR_MAC].rate,		\
			      ARRAY_SIZE(txpwr.data[TXPWR_MAC].rate));	\
})

static int
standalone_mt7921_txpwr(struct seq_file *s, void *data)
{
	struct standalone_mt792x_dev *dev = dev_get_drvdata(s->private);
	struct standalone_mt7921_txpwr txpwr;
	int ret;

	standalone_mt792x_mutex_acquire(dev);
	ret = standalone_mt7921_get_txpwr_info(dev, &txpwr);
	standalone_mt792x_mutex_release(dev);

	if (ret)
		return ret;

	seq_printf(s, "Tx power table (channel %d)\n", txpwr.ch);
	seq_printf(s, "%-16s  %6s %6s %6s %6s\n",
		   " ", "1m", "2m", "5m", "11m");
	standalone_mt7921_print_txpwr_entry(CCK, cck);

	seq_printf(s, "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
		   " ", "6m", "9m", "12m", "18m", "24m", "36m",
		   "48m", "54m");
	standalone_mt7921_print_txpwr_entry(OFDM, ofdm);

	seq_printf(s, "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
		   " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
		   "mcs6", "mcs7");
	standalone_mt7921_print_txpwr_entry(HT20, ht20);

	seq_printf(s, "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
		   " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
		   "mcs6", "mcs7", "mcs32");
	standalone_mt7921_print_txpwr_entry(HT40, ht40);

	seq_printf(s, "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
		   " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
		   "mcs6", "mcs7", "mcs8", "mcs9", "mcs10", "mcs11");
	standalone_mt7921_print_txpwr_entry(VHT20, vht20);
	standalone_mt7921_print_txpwr_entry(VHT40, vht40);
	standalone_mt7921_print_txpwr_entry(VHT80, vht80);
	standalone_mt7921_print_txpwr_entry(VHT160, vht160);
	standalone_mt7921_print_txpwr_entry(HE26, he26);
	standalone_mt7921_print_txpwr_entry(HE52, he52);
	standalone_mt7921_print_txpwr_entry(HE106, he106);
	standalone_mt7921_print_txpwr_entry(HE242, he242);
	standalone_mt7921_print_txpwr_entry(HE484, he484);
	standalone_mt7921_print_txpwr_entry(HE996, he996);
	standalone_mt7921_print_txpwr_entry(HE996x2, he996x2);

	return 0;
}

static int
standalone_mt7921_pm_set(void *data, u64 val)
{
	struct standalone_mt792x_dev *dev = data;
	struct standalone_mt76_connac_pm *pm = &dev->pm;

	if (standalone_mt76_is_usb(&dev->standalone_mt76))
		return -EOPNOTSUPP;

	mutex_lock(&dev->standalone_mt76.mutex);

	if (val == pm->enable_user)
		goto out;

	if (!pm->enable_user) {
		pm->stats.last_wake_event = jiffies;
		pm->stats.last_doze_event = jiffies;
	}
	/* make sure the chip is awake here and ps_work is scheduled
	 * just at end of the this routine.
	 */
	pm->enable = false;
	standalone_mt76_connac_pm_wake(&dev->mphy, pm);

	pm->enable_user = val;
	standalone_mt7921_set_runtime_pm(dev);
	standalone_mt76_connac_power_save_sched(&dev->mphy, pm);
out:
	mutex_unlock(&dev->standalone_mt76.mutex);

	return 0;
}

static int
standalone_mt7921_pm_get(void *data, u64 *val)
{
	struct standalone_mt792x_dev *dev = data;

	*val = dev->pm.enable_user;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_pm, standalone_mt7921_pm_get, standalone_mt7921_pm_set, "%lld\n");

static int
standalone_mt7921_deep_sleep_set(void *data, u64 val)
{
	struct standalone_mt792x_dev *dev = data;
	struct standalone_mt76_connac_pm *pm = &dev->pm;
	bool monitor = !!(dev->mphy.hw->conf.flags & IEEE80211_CONF_MONITOR);
	bool enable = !!val;

	if (standalone_mt76_is_usb(&dev->standalone_mt76))
		return -EOPNOTSUPP;

	standalone_mt792x_mutex_acquire(dev);
	if (pm->ds_enable_user == enable)
		goto out;

	pm->ds_enable_user = enable;
	pm->ds_enable = enable && !monitor;
	standalone_mt76_connac_mcu_set_deep_sleep(&dev->standalone_mt76, pm->ds_enable);
out:
	standalone_mt792x_mutex_release(dev);

	return 0;
}

static int
standalone_mt7921_deep_sleep_get(void *data, u64 *val)
{
	struct standalone_mt792x_dev *dev = data;

	*val = dev->pm.ds_enable_user;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_ds, standalone_mt7921_deep_sleep_get,
			 standalone_mt7921_deep_sleep_set, "%lld\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_pm_idle_timeout, standalone_mt792x_pm_idle_timeout_get,
			 standalone_mt792x_pm_idle_timeout_set, "%lld\n");

static int standalone_mt7921_chip_reset(void *data, u64 val)
{
	struct standalone_mt792x_dev *dev = data;
	int ret = 0;

	switch (val) {
	case 1:
		/* Reset wifisys directly. */
		standalone_mt792x_reset(&dev->standalone_mt76);
		break;
	default:
		/* Collect the core dump before reset wifisys. */
		standalone_mt792x_mutex_acquire(dev);
		ret = standalone_mt76_connac_mcu_chip_config(&dev->standalone_mt76);
		standalone_mt792x_mutex_release(dev);
		break;
	}

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_reset, NULL, standalone_mt7921_chip_reset, "%lld\n");

static int
standalone_mt7921s_sched_quota_read(struct seq_file *s, void *data)
{
	struct standalone_mt792x_dev *dev = dev_get_drvdata(s->private);
	struct standalone_mt76_sdio *sdio = &dev->standalone_mt76.sdio;

	seq_printf(s, "pse_data_quota\t%d\n", sdio->sched.pse_data_quota);
	seq_printf(s, "ple_data_quota\t%d\n", sdio->sched.ple_data_quota);
	seq_printf(s, "pse_mcu_quota\t%d\n", sdio->sched.pse_mcu_quota);
	seq_printf(s, "sched_deficit\t%d\n", sdio->sched.deficit);

	return 0;
}

int standalone_mt7921_init_debugfs(struct standalone_mt792x_dev *dev)
{
	struct dentry *dir;

	dir = standalone_mt76_register_debugfs_fops(&dev->mphy, &fops_regval);

	if (standalone_mt76_is_mmio(&dev->standalone_mt76))
		debugfs_create_devm_seqfile(dev->standalone_mt76.dev, "xmit-queues",
					    dir, standalone_mt792x_queues_read);
	else
		debugfs_create_devm_seqfile(dev->standalone_mt76.dev, "xmit-queues",
					    dir, standalone_mt76_queues_read);

	debugfs_create_devm_seqfile(dev->standalone_mt76.dev, "acq", dir,
				    standalone_mt792x_queues_acq);
	debugfs_create_devm_seqfile(dev->standalone_mt76.dev, "txpower_sku", dir,
				    standalone_mt7921_txpwr);
	debugfs_create_file("tx_stats", 0400, dir, dev, &standalone_mt792x_tx_stats_fops);
	debugfs_create_file("fw_debug", 0600, dir, dev, &fops_fw_debug);
	debugfs_create_file("runtime-pm", 0600, dir, dev, &fops_pm);
	debugfs_create_file("idle-timeout", 0600, dir, dev,
			    &fops_pm_idle_timeout);
	debugfs_create_file("chip_reset", 0600, dir, dev, &fops_reset);
	debugfs_create_devm_seqfile(dev->standalone_mt76.dev, "runtime_pm_stats", dir,
				    standalone_mt792x_pm_stats);
	debugfs_create_file("deep-sleep", 0600, dir, dev, &fops_ds);
	if (standalone_mt76_is_sdio(&dev->standalone_mt76))
		debugfs_create_devm_seqfile(dev->standalone_mt76.dev, "sched-quota", dir,
					    standalone_mt7921s_sched_quota_read);
	return 0;
}

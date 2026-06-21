// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca> */

#include <linux/etherdevice.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/thermal.h>
#include <linux/firmware.h>
#include "standalone_mt7921.h"
#include "../standalone_mt76_connac2_mac.h"
#include "mcu.h"
#include "regd.h"

static ssize_t standalone_mt7921_thermal_temp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	switch (to_sensor_dev_attr(attr)->index) {
	case 0: {
		struct standalone_mt792x_phy *phy = dev_get_drvdata(dev);
		struct standalone_mt792x_dev *mdev = phy->dev;
		int temperature;

		standalone_mt792x_mutex_acquire(mdev);
		temperature = standalone_mt7921_mcu_get_temperature(phy);
		standalone_mt792x_mutex_release(mdev);

		if (temperature < 0)
			return temperature;
		/* display in millidegree Celsius */
		return sprintf(buf, "%u\n", temperature * 1000);
	}
	default:
		return -EINVAL;
	}
}
static SENSOR_DEVICE_ATTR_RO(temp1_input, standalone_mt7921_thermal_temp, 0);

static struct attribute *standalone_mt7921_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(standalone_mt7921_hwmon);

static int standalone_mt7921_thermal_init(struct standalone_mt792x_phy *phy)
{
	struct wiphy *wiphy = phy->standalone_mt76->hw->wiphy;
	struct device *hwmon;
	const char *name;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return 0;

	name = devm_kasprintf(&wiphy->dev, GFP_KERNEL, "standalone_mt7921_%s",
			      wiphy_name(wiphy));
	if (!name)
		return -ENOMEM;

	hwmon = devm_hwmon_device_register_with_groups(&wiphy->dev, name, phy,
						       standalone_mt7921_hwmon_groups);
	return PTR_ERR_OR_ZERO(hwmon);
}

int standalone_mt7921_mac_init(struct standalone_mt792x_dev *dev)
{
	int i;

	standalone_mt76_rmw_field(dev, MT_MDP_DCR1, MT_MDP_DCR1_MAX_RX_LEN, 1536);
	/* enable hardware de-agg */
	standalone_mt76_set(dev, MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);
	/* enable hardware rx header translation */
	standalone_mt76_set(dev, MT_MDP_DCR0, MT_MDP_DCR0_RX_HDR_TRANS_EN);

	for (i = 0; i < STANDALONE_MT792x_WTBL_SIZE; i++)
		standalone_mt7921_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	for (i = 0; i < 2; i++)
		standalone_mt792x_mac_init_band(dev, i);

	return standalone_mt76_connac_mcu_set_rts_thresh(&dev->standalone_mt76, 0x92b, 0);
}
EXPORT_SYMBOL_GPL(standalone_mt7921_mac_init);

static int __standalone_mt7921_init_hardware(struct standalone_mt792x_dev *dev)
{
	int ret;

	/* force firmware operation mode into normal state,
	 * which should be set before firmware download stage.
	 */
	standalone_mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
	ret = standalone_mt792x_mcu_init(dev);
	if (ret)
		goto out;

	ret = standalone_mt76_eeprom_override(&dev->mphy);
	if (ret)
		goto out;

	ret = standalone_mt7921_mcu_set_eeprom(dev);
	if (ret)
		goto out;

	ret = standalone_mt7921_mac_init(dev);
out:
	return ret;
}

static int standalone_mt7921_init_hardware(struct standalone_mt792x_dev *dev)
{
	int ret, i;

	set_bit(STANDALONE_MT76_STATE_INITIALIZED, &dev->mphy.state);

	for (i = 0; i < STANDALONE_MT792x_MCU_INIT_RETRY_COUNT; i++) {
		ret = __standalone_mt7921_init_hardware(dev);
		if (!ret)
			break;

		standalone_mt792x_init_reset(dev);
	}

	if (i == STANDALONE_MT792x_MCU_INIT_RETRY_COUNT) {
		dev_err(dev->standalone_mt76.dev, "hardware init failed\n");
		return ret;
	}

	return 0;
}

static void standalone_mt7921_init_work(struct work_struct *work)
{
	struct standalone_mt792x_dev *dev = container_of(work, struct standalone_mt792x_dev,
					      init_work);
	int ret;

	ret = standalone_mt7921_init_hardware(dev);
	if (ret)
		return;

	standalone_mt76_set_stream_caps(&dev->mphy, true);
	standalone_mt7921_set_stream_he_caps(&dev->phy);
	standalone_mt792x_config_mac_addr_list(dev);

	ret = standalone_mt76_register_device(&dev->standalone_mt76, true, standalone_mt76_rates,
				   ARRAY_SIZE(standalone_mt76_rates));
	if (ret) {
		dev_err(dev->standalone_mt76.dev, "register device failed\n");
		return;
	}

	ret = standalone_mt7921_init_debugfs(dev);
	if (ret) {
		dev_err(dev->standalone_mt76.dev, "register debugfs failed\n");
		return;
	}

	ret = standalone_mt7921_thermal_init(&dev->phy);
	if (ret) {
		dev_err(dev->standalone_mt76.dev, "thermal init failed\n");
		return;
	}

	/* we support chip reset now */
	dev->hw_init_done = true;

	standalone_mt76_connac_mcu_set_deep_sleep(&dev->standalone_mt76, dev->pm.ds_enable);
}

int standalone_mt7921_register_device(struct standalone_mt792x_dev *dev)
{
	struct ieee80211_hw *hw = standalone_mt76_hw(dev);
	int ret;

	dev->phy.dev = dev;
	dev->phy.standalone_mt76 = &dev->standalone_mt76.phy;
	dev->standalone_mt76.phy.priv = &dev->phy;
	dev->standalone_mt76.tx_worker.fn = standalone_mt792x_tx_worker;

	INIT_DELAYED_WORK(&dev->pm.ps_work, standalone_mt792x_pm_power_save_work);
	INIT_WORK(&dev->pm.wake_work, standalone_mt792x_pm_wake_work);
	spin_lock_init(&dev->pm.wake.lock);
	mutex_init(&dev->pm.mutex);
	init_waitqueue_head(&dev->pm.wait);
	init_waitqueue_head(&dev->wait);
	if (standalone_mt76_is_sdio(&dev->standalone_mt76))
		init_waitqueue_head(&dev->standalone_mt76.sdio.wait);
	spin_lock_init(&dev->pm.txq_lock);
	INIT_DELAYED_WORK(&dev->mphy.mac_work, standalone_mt792x_mac_work);
	INIT_DELAYED_WORK(&dev->phy.scan_work, standalone_mt7921_scan_work);
	INIT_DELAYED_WORK(&dev->coredump.work, standalone_mt7921_coredump_work);
#if IS_ENABLED(CONFIG_IPV6)
	INIT_WORK(&dev->ipv6_ns_work, standalone_mt7921_set_ipv6_ns_work);
	skb_queue_head_init(&dev->ipv6_ns_list);
#endif
	skb_queue_head_init(&dev->phy.scan_event_list);
	skb_queue_head_init(&dev->coredump.msg_list);

	INIT_WORK(&dev->reset_work, standalone_mt7921_mac_reset_work);
	INIT_WORK(&dev->init_work, standalone_mt7921_init_work);

	INIT_WORK(&dev->phy.roc_work, standalone_mt7921_roc_work);
	timer_setup(&dev->phy.roc_timer, standalone_mt792x_roc_timer, 0);
	init_waitqueue_head(&dev->phy.roc_wait);

	dev->pm.idle_timeout = STANDALONE_MT792x_PM_TIMEOUT;
	dev->pm.stats.last_wake_event = jiffies;
	dev->pm.stats.last_doze_event = jiffies;

	if (!standalone_mt76_is_usb(&dev->standalone_mt76) &&
	    !is_standalone_mt7902(&dev->standalone_mt76)) {
		dev->pm.enable_user = true;
		dev->pm.enable = true;
		dev->pm.ds_enable_user = true;
		dev->pm.ds_enable = true;
	}

	if (!standalone_mt76_is_mmio(&dev->standalone_mt76))
		hw->extra_tx_headroom += MT_SDIO_TXD_SIZE + MT_SDIO_HDR_SIZE;

	standalone_mt792x_init_acpi_sar(dev);

	ret = standalone_mt792x_init_wcid(dev);
	if (ret)
		return ret;

	ret = standalone_mt792x_init_wiphy(hw);
	if (ret)
		return ret;

	hw->wiphy->reg_notifier = standalone_mt7921_regd_notifier;
	dev->mphy.sband_2g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_5g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
			(3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);
	if (is_standalone_mt7922(&dev->standalone_mt76))
		dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ |
			IEEE80211_VHT_CAP_SHORT_GI_160;

	dev->mphy.hw->wiphy->available_antennas_rx = dev->mphy.chainmask;
	dev->mphy.hw->wiphy->available_antennas_tx = dev->mphy.chainmask;

	queue_work(system_wq, &dev->init_work);

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt7921_register_device);

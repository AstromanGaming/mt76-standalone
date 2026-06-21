// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca> */

#include <linux/of.h>
#include "standalone_mt7921.h"
#include "regd.h"
#include "mcu.h"

static bool standalone_mt7921_disable_clc;
module_param_named(disable_clc, standalone_mt7921_disable_clc, bool, 0644);
MODULE_PARM_DESC(disable_clc, "disable CLC support");

bool standalone_mt7921_regd_clc_supported(struct standalone_mt792x_dev *dev)
{
	if (standalone_mt7921_disable_clc ||
	    standalone_mt76_is_usb(&dev->standalone_mt76))
		return false;

	return true;
}

static void
standalone_mt7921_regd_channel_update(struct wiphy *wiphy, struct standalone_mt792x_dev *dev)
{
#define IS_UNII_INVALID(idx, sfreq, efreq) \
	(!(dev->phy.clc_chan_conf & BIT(idx)) && (cfreq) >= (sfreq) && (cfreq) <= (efreq))
	struct ieee80211_supported_band *sband;
	struct standalone_mt76_dev *mdev = &dev->standalone_mt76;
	struct device_node *np, *band_np;
	struct ieee80211_channel *ch;
	int i, cfreq;

	np = standalone_mt76_find_power_limits_node(mdev);

	sband = wiphy->bands[NL80211_BAND_5GHZ];
	band_np = np ? of_get_child_by_name(np, "txpower-5g") : NULL;
	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		cfreq = ch->center_freq;

		if (np && (!band_np || !standalone_mt76_find_channel_node(band_np, ch))) {
			ch->flags |= IEEE80211_CHAN_DISABLED;
			continue;
		}

		/* UNII-4 */
		if (IS_UNII_INVALID(0, 5845, 5925))
			ch->flags |= IEEE80211_CHAN_DISABLED;
	}

	sband = wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband)
		return;

	band_np = np ? of_get_child_by_name(np, "txpower-6g") : NULL;
	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		cfreq = ch->center_freq;

		if (np && (!band_np || !standalone_mt76_find_channel_node(band_np, ch))) {
			ch->flags |= IEEE80211_CHAN_DISABLED;
			continue;
		}

		/* UNII-5/6/7/8 */
		if (IS_UNII_INVALID(1, 5925, 6425) ||
		    IS_UNII_INVALID(2, 6425, 6525) ||
		    IS_UNII_INVALID(3, 6525, 6875) ||
		    IS_UNII_INVALID(4, 6875, 7125))
			ch->flags |= IEEE80211_CHAN_DISABLED;
	}
}

int standalone_mt7921_mcu_regd_update(struct standalone_mt792x_dev *dev, u8 *alpha2,
			   enum environment_cap country_ie_env)
{
	struct standalone_mt76_dev *mdev = &dev->standalone_mt76;
	struct ieee80211_hw *hw = mdev->hw;
	struct wiphy *wiphy = hw->wiphy;
	int ret = 0;

	dev->regd_in_progress = true;

	standalone_mt792x_mutex_acquire(dev);
	if (!dev->regd_change)
		goto err;

	ret = standalone_mt7921_mcu_set_clc(dev, alpha2, country_ie_env);
	if (ret < 0)
		goto err;

	standalone_mt7921_regd_channel_update(wiphy, dev);

	ret = standalone_mt76_connac_mcu_set_channel_domain(hw->priv);
	if (ret < 0)
		goto err;

	ret = standalone_mt7921_set_tx_sar_pwr(hw, NULL);
	if (ret < 0)
		goto err;

err:
	standalone_mt792x_mutex_release(dev);
	dev->regd_change = false;
	dev->regd_in_progress = false;
	wake_up(&dev->wait);

	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt7921_mcu_regd_update);

void standalone_mt7921_regd_notifier(struct wiphy *wiphy,
			  struct regulatory_request *req)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct standalone_mt792x_dev *dev = standalone_mt792x_hw_dev(hw);
	struct standalone_mt76_connac_pm *pm = &dev->pm;
	struct standalone_mt76_dev *mdev = &dev->standalone_mt76;

	if (req->initiator == NL80211_REGDOM_SET_BY_USER &&
	    !dev->regd_user)
		dev->regd_user = true;

	/* do not need to update the same country twice */
	if (!memcmp(req->alpha2, mdev->alpha2, 2) &&
	    dev->country_ie_env == req->country_ie_env)
		return;

	memcpy(mdev->alpha2, req->alpha2, 2);
	mdev->region = req->dfs_region;
	dev->country_ie_env = req->country_ie_env;

	if (req->initiator == NL80211_REGDOM_SET_BY_USER) {
		if (dev->standalone_mt76.alpha2[0] == '0' && dev->standalone_mt76.alpha2[1] == '0')
			wiphy->regulatory_flags &= ~REGULATORY_COUNTRY_IE_IGNORE;
		else
			wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	}

	dev->regd_change = true;

	if (pm->suspended)
		return;

	standalone_mt7921_mcu_regd_update(dev, req->alpha2,
			       req->country_ie_env);
}

static bool
standalone_mt7921_regd_is_valid_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;

	if (alpha2[0] == '0' && alpha2[1] == '0')
		return true;

	if (isalpha(alpha2[0]) && isalpha(alpha2[1]))
		return true;

	return false;
}

int standalone_mt7921_regd_change(struct standalone_mt792x_phy *phy, char *alpha2)
{
	struct wiphy *wiphy = phy->standalone_mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct standalone_mt792x_dev *dev = standalone_mt792x_hw_dev(hw);
	struct standalone_mt76_dev *mdev = &dev->standalone_mt76;

	if (dev->hw_full_reset)
		return 0;

	if (!standalone_mt7921_regd_is_valid_alpha2(alpha2) ||
	    !standalone_mt7921_regd_clc_supported(dev) ||
	    dev->regd_user)
		return -EINVAL;

	if (mdev->alpha2[0] != '0' && mdev->alpha2[1] != '0')
		return 0;

	/* do not need to update the same country twice */
	if (!memcmp(alpha2, mdev->alpha2, 2))
		return 0;

	if (phy->chip_cap & STANDALONE_MT792x_CHIP_CAP_11D_EN)
		return regulatory_hint(wiphy, alpha2);
	else
		return standalone_mt7921_mcu_set_clc(dev, alpha2, ENVIRON_INDOOR);
}

int standalone_mt7921_regd_init(struct standalone_mt792x_phy *phy)
{
	struct wiphy *wiphy = phy->standalone_mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct standalone_mt792x_dev *dev = standalone_mt792x_hw_dev(hw);
	struct standalone_mt76_dev *mdev = &dev->standalone_mt76;

	if (phy->chip_cap & STANDALONE_MT792x_CHIP_CAP_11D_EN)
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE |
					   REGULATORY_DISABLE_BEACON_HINTS;
	else
		memzero_explicit(&mdev->alpha2, sizeof(mdev->alpha2));

	return 0;
}

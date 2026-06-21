// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */
#include "standalone_mt76.h"

static struct standalone_mt76_vif_link *
standalone_mt76_alloc_mlink(struct standalone_mt76_dev *dev, struct standalone_mt76_vif_data *mvif)
{
	struct standalone_mt76_vif_link *mlink;

	mlink = kzalloc(dev->drv->link_data_size, GFP_KERNEL);
	if (!mlink)
		return NULL;

	mlink->mvif = mvif;

	return mlink;
}

static int
standalone_mt76_phy_update_channel(struct standalone_mt76_phy *phy,
			struct ieee80211_chanctx_conf *conf)
{
	phy->radar_enabled = conf->radar_enabled;
	phy->main_chandef = conf->def;
	phy->chanctx = (struct standalone_mt76_chanctx *)conf->drv_priv;

	return __standalone_mt76_set_channel(phy, &phy->main_chandef, false);
}

int standalone_mt76_add_chanctx(struct ieee80211_hw *hw,
		     struct ieee80211_chanctx_conf *conf)
{
	struct standalone_mt76_chanctx *ctx = (struct standalone_mt76_chanctx *)conf->drv_priv;
	struct standalone_mt76_phy *phy = hw->priv;
	struct standalone_mt76_dev *dev = phy->dev;
	int ret = -EINVAL;

	phy = ctx->phy = dev->band_phys[conf->def.chan->band];
	if (WARN_ON_ONCE(!phy))
		return ret;

	if (dev->scan.phy == phy)
		standalone_mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);
	if (!phy->chanctx)
		ret = standalone_mt76_phy_update_channel(phy, conf);
	else
		ret = 0;
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt76_add_chanctx);

void standalone_mt76_remove_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf)
{
	struct standalone_mt76_chanctx *ctx = (struct standalone_mt76_chanctx *)conf->drv_priv;
	struct standalone_mt76_phy *phy = hw->priv;
	struct standalone_mt76_dev *dev = phy->dev;

	phy = ctx->phy;
	if (WARN_ON_ONCE(!phy))
		return;

	if (dev->scan.phy == phy)
		standalone_mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);
	if (phy->chanctx == ctx)
		phy->chanctx = NULL;
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(standalone_mt76_remove_chanctx);

void standalone_mt76_change_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf,
			 u32 changed)
{
	struct standalone_mt76_chanctx *ctx = (struct standalone_mt76_chanctx *)conf->drv_priv;
	struct standalone_mt76_phy *phy = ctx->phy;
	struct standalone_mt76_dev *dev = phy->dev;

	if (!(changed & (IEEE80211_CHANCTX_CHANGE_WIDTH |
			 IEEE80211_CHANCTX_CHANGE_RADAR)))
		return;

	if (phy->roc_vif)
		standalone_mt76_abort_roc(phy);

	cancel_delayed_work_sync(&phy->mac_work);

	mutex_lock(&dev->mutex);
	standalone_mt76_phy_update_channel(phy, conf);
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(standalone_mt76_change_chanctx);


int standalone_mt76_assign_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_chanctx_conf *conf)
{
	struct standalone_mt76_chanctx *ctx = (struct standalone_mt76_chanctx *)conf->drv_priv;
	struct standalone_mt76_vif_link *mlink = (struct standalone_mt76_vif_link *)vif->drv_priv;
	struct standalone_mt76_vif_data *mvif = mlink->mvif;
	int link_id = link_conf->link_id;
	struct standalone_mt76_phy *phy = ctx->phy;
	struct standalone_mt76_dev *dev = phy->dev;
	bool mlink_alloc = false;
	int ret = 0;

	if (dev->scan.vif == vif)
		standalone_mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		goto out;

	mlink = standalone_mt76_vif_conf_link(dev, vif, link_conf);
	if (!mlink) {
		mlink = standalone_mt76_alloc_mlink(dev, mvif);
		if (!mlink) {
			ret = -ENOMEM;
			goto out;
		}
		mlink_alloc = true;
	}

	mlink->ctx = conf;
	ret = dev->drv->vif_link_add(phy, vif, link_conf, mlink);
	if (ret) {
		if (mlink_alloc)
			kfree(mlink);
		goto out;
	}

	if (link_conf != &vif->bss_conf)
		rcu_assign_pointer(mvif->link[link_id], mlink);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt76_assign_vif_chanctx);

void standalone_mt76_unassign_vif_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link_conf,
			       struct ieee80211_chanctx_conf *conf)
{
	struct standalone_mt76_chanctx *ctx = (struct standalone_mt76_chanctx *)conf->drv_priv;
	struct standalone_mt76_vif_link *mlink = (struct standalone_mt76_vif_link *)vif->drv_priv;
	struct standalone_mt76_phy *phy = ctx->phy;
	struct standalone_mt76_dev *dev = phy->dev;

	if (dev->scan.vif == vif)
		standalone_mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		goto out;

	mlink = standalone_mt76_vif_conf_link(dev, vif, link_conf);
	if (!mlink)
		goto out;

	dev->drv->vif_link_remove(phy, vif, link_conf, mlink);
	mlink->ctx = NULL;
out:
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(standalone_mt76_unassign_vif_chanctx);

int standalone_mt76_switch_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif_chanctx_switch *vifs,
			    int n_vifs,
			    enum ieee80211_chanctx_switch_mode mode)
{
	struct standalone_mt76_chanctx *old_ctx = (struct standalone_mt76_chanctx *)vifs->old_ctx->drv_priv;
	struct standalone_mt76_chanctx *new_ctx = (struct standalone_mt76_chanctx *)vifs->new_ctx->drv_priv;
	struct ieee80211_chanctx_conf *conf = vifs->new_ctx;
	struct standalone_mt76_phy *old_phy = old_ctx->phy;
	struct standalone_mt76_phy *phy = hw->priv;
	struct standalone_mt76_dev *dev = phy->dev;
	struct standalone_mt76_vif_link *mlink;
	bool update_chan;
	int i, ret = 0;

	if (mode == CHANCTX_SWMODE_SWAP_CONTEXTS)
		phy = new_ctx->phy = dev->band_phys[conf->def.chan->band];
	else
		phy = new_ctx->phy;
	if (!phy)
		return -EINVAL;

	update_chan = phy->chanctx != new_ctx;
	if (update_chan) {
		if (dev->scan.phy == phy)
			standalone_mt76_abort_scan(dev);

		cancel_delayed_work_sync(&phy->mac_work);
	}

	mutex_lock(&dev->mutex);

	if (mode == CHANCTX_SWMODE_SWAP_CONTEXTS &&
	    phy != old_phy && old_phy->chanctx == old_ctx)
		old_phy->chanctx = NULL;

	if (update_chan)
		ret = standalone_mt76_phy_update_channel(phy, vifs->new_ctx);

	if (ret)
		goto out;

	if (old_phy == phy)
		goto skip_link_replace;

	for (i = 0; i < n_vifs; i++) {
		mlink = standalone_mt76_vif_conf_link(dev, vifs[i].vif, vifs[i].link_conf);
		if (!mlink)
			continue;

		dev->drv->vif_link_remove(old_phy, vifs[i].vif,
					  vifs[i].link_conf, mlink);

		ret = dev->drv->vif_link_add(phy, vifs[i].vif,
					     vifs[i].link_conf, mlink);
		if (ret)
			goto out;

	}

skip_link_replace:
	for (i = 0; i < n_vifs; i++) {
		mlink = standalone_mt76_vif_conf_link(dev, vifs[i].vif, vifs[i].link_conf);
		if (!mlink)
			continue;

		mlink->ctx = vifs->new_ctx;
		if (mlink->beacon_mon_interval)
			WRITE_ONCE(mlink->beacon_mon_last, jiffies);
	}

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt76_switch_vif_chanctx);

struct standalone_mt76_vif_link *standalone_mt76_get_vif_phy_link(struct standalone_mt76_phy *phy,
					    struct ieee80211_vif *vif)
{
	struct standalone_mt76_vif_link *mlink = (struct standalone_mt76_vif_link *)vif->drv_priv;
	struct standalone_mt76_vif_data *mvif = mlink->mvif;
	struct standalone_mt76_dev *dev = phy->dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(mvif->link); i++) {
		mlink = standalone_mt76_dereference(mvif->link[i], dev);
		if (!mlink)
			continue;

		if (standalone_mt76_vif_link_phy(mlink) == phy)
			return mlink;
	}

	if (!dev->drv->vif_link_add)
		return ERR_PTR(-EINVAL);

	mlink = standalone_mt76_alloc_mlink(dev, mvif);
	if (!mlink)
		return ERR_PTR(-ENOMEM);

	mlink->offchannel = true;
	ret = dev->drv->vif_link_add(phy, vif, &vif->bss_conf, mlink);
	if (ret) {
		kfree(mlink);
		return ERR_PTR(ret);
	}
	rcu_assign_pointer(mvif->offchannel_link, mlink);

	return mlink;
}

void standalone_mt76_put_vif_phy_link(struct standalone_mt76_phy *phy, struct ieee80211_vif *vif,
			   struct standalone_mt76_vif_link *mlink)
{
	struct standalone_mt76_dev *dev = phy->dev;
	struct standalone_mt76_vif_data *mvif;

	if (IS_ERR_OR_NULL(mlink) || !mlink->offchannel)
		return;

	mvif = mlink->mvif;

	rcu_assign_pointer(mvif->offchannel_link, NULL);
	dev->drv->vif_link_remove(phy, vif, &vif->bss_conf, mlink);
	kfree_rcu(mlink, rcu_head);
}

void standalone_mt76_roc_complete(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_vif_link *mlink = phy->roc_link;
	struct standalone_mt76_dev *dev = phy->dev;

	if (!phy->roc_vif)
		return;

	if (mlink)
		mlink->mvif->roc_phy = NULL;
	if (phy->chanctx && phy->main_chandef.chan && phy->offchannel &&
	    !test_bit(STANDALONE_MT76_MCU_RESET, &dev->phy.state)) {
		__standalone_mt76_set_channel(phy, &phy->main_chandef, false);
		standalone_mt76_offchannel_notify(phy, false);
	}
	standalone_mt76_put_vif_phy_link(phy, phy->roc_vif, phy->roc_link);
	phy->roc_vif = NULL;
	phy->roc_link = NULL;
	if (!test_bit(STANDALONE_MT76_MCU_RESET, &dev->phy.state))
		ieee80211_remain_on_channel_expired(phy->hw);
}

void standalone_mt76_roc_complete_work(struct work_struct *work)
{
	struct standalone_mt76_phy *phy = container_of(work, struct standalone_mt76_phy, roc_work.work);
	struct standalone_mt76_dev *dev = phy->dev;

	mutex_lock(&dev->mutex);
	standalone_mt76_roc_complete(phy);
	mutex_unlock(&dev->mutex);
}

void standalone_mt76_abort_roc(struct standalone_mt76_phy *phy)
{
	struct standalone_mt76_dev *dev = phy->dev;

	cancel_delayed_work_sync(&phy->roc_work);

	mutex_lock(&dev->mutex);
	standalone_mt76_roc_complete(phy);
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(standalone_mt76_abort_roc);

int standalone_mt76_remain_on_channel(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_channel *chan, int duration,
			   enum ieee80211_roc_type type)
{
	struct cfg80211_chan_def chandef = {};
	struct standalone_mt76_phy *phy = hw->priv;
	struct standalone_mt76_dev *dev = phy->dev;
	struct standalone_mt76_vif_link *mlink;
	bool offchannel;
	int ret = 0;

	phy = dev->band_phys[chan->band];
	if (!phy)
		return -EINVAL;

	cancel_delayed_work_sync(&phy->mac_work);

	mutex_lock(&dev->mutex);

	if (phy->roc_vif || dev->scan.phy == phy ||
	    test_bit(STANDALONE_MT76_MCU_RESET, &dev->phy.state)) {
		ret = -EBUSY;
		goto out;
	}

	mlink = standalone_mt76_get_vif_phy_link(phy, vif);
	if (IS_ERR(mlink)) {
		ret = PTR_ERR(mlink);
		goto out;
	}

	mlink->mvif->roc_phy = phy;
	phy->roc_vif = vif;
	phy->roc_link = mlink;

	offchannel = standalone_mt76_offchannel_chandef(phy, chan, &chandef);
	if (offchannel)
		standalone_mt76_offchannel_notify(phy, true);
	ret = __standalone_mt76_set_channel(phy, &chandef, offchannel);
	if (ret) {
		mlink->mvif->roc_phy = NULL;
		phy->roc_vif = NULL;
		phy->roc_link = NULL;
		standalone_mt76_put_vif_phy_link(phy, vif, mlink);
		goto out;
	}
	ieee80211_ready_on_channel(hw);
	ieee80211_queue_delayed_work(phy->hw, &phy->roc_work,
				     msecs_to_jiffies(duration));

out:
	mutex_unlock(&dev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt76_remain_on_channel);

int standalone_mt76_cancel_remain_on_channel(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	struct standalone_mt76_vif_link *mlink = (struct standalone_mt76_vif_link *)vif->drv_priv;
	struct standalone_mt76_vif_data *mvif = mlink->mvif;
	struct standalone_mt76_phy *phy = mvif->roc_phy;

	if (!phy)
		return 0;

	standalone_mt76_abort_roc(phy);

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt76_cancel_remain_on_channel);

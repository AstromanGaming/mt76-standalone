/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca> */

#ifndef __STANDALONE_MT7921_REGD_H
#define __STANDALONE_MT7921_REGD_H

struct standalone_mt792x_dev;
struct wiphy;
struct regulatory_request;

int standalone_mt7921_mcu_regd_update(struct standalone_mt792x_dev *dev, u8 *alpha2,
			   enum environment_cap country_ie_env);
void standalone_mt7921_regd_notifier(struct wiphy *wiphy,
			  struct regulatory_request *request);
bool standalone_mt7921_regd_clc_supported(struct standalone_mt792x_dev *dev);
int standalone_mt7921_regd_change(struct standalone_mt792x_phy *phy, char *alpha2);
int standalone_mt7921_regd_init(struct standalone_mt792x_phy *phy);

#endif

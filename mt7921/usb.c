// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 *
 * Author: Sam Bélanger <github@astromangaming.ca>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "standalone_mt7921.h"
#include "mcu.h"
#include "../standalone_mt76_connac2_mac.h"

static const struct usb_device_id standalone_mt7921u_device_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7961, 0xff, 0xff, 0xff),
		.driver_info = (kernel_ulong_t)STANDALONE_MT7921_FIRMWARE_WM },
	/* Comfast CF-952AX */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x3574, 0x6211, 0xff, 0xff, 0xff),
		.driver_info = (kernel_ulong_t)STANDALONE_MT7921_FIRMWARE_WM },
	/* Netgear, Inc. [A8000,AXE3000] */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0x9060, 0xff, 0xff, 0xff),
		.driver_info = (kernel_ulong_t)STANDALONE_MT7921_FIRMWARE_WM },
	/* Netgear, Inc. A7500 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0x9065, 0xff, 0xff, 0xff),
		.driver_info = (kernel_ulong_t)STANDALONE_MT7921_FIRMWARE_WM },
	/* TP-Link TXE50UH */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x35bc, 0x0107, 0xff, 0xff, 0xff),
		.driver_info = (kernel_ulong_t)STANDALONE_MT7921_FIRMWARE_WM },
	{ },
};

static int
standalone_mt7921u_mcu_send_message(struct standalone_mt76_dev *mdev, struct sk_buff *skb,
			 int cmd, int *seq)
{
	struct standalone_mt792x_dev *dev = container_of(mdev, struct standalone_mt792x_dev, standalone_mt76);
	u32 pad, ep;
	int ret;

	ret = standalone_mt76_connac2_mcu_fill_message(mdev, skb, cmd, seq);
	if (ret)
		return ret;

	mdev->mcu.timeout = 3 * HZ;

	if (cmd != MCU_CMD(FW_SCATTER))
		ep = MT_EP_OUT_INBAND_CMD;
	else
		ep = MT_EP_OUT_AC_BE;

	standalone_mt792x_skb_add_usb_sdio_hdr(dev, skb, 0);
	pad = round_up(skb->len, 4) + 4 - skb->len;
	__skb_put_zero(skb, pad);

	ret = standalone_mt76u_bulk_msg(&dev->standalone_mt76, skb->data, skb->len, NULL,
			     1000, ep);
	dev_kfree_skb(skb);

	return ret;
}

static int standalone_mt7921u_mcu_init(struct standalone_mt792x_dev *dev)
{
	static const struct standalone_mt76_mcu_ops mcu_ops = {
		.headroom = MT_SDIO_HDR_SIZE +
			    sizeof(struct standalone_mt76_connac2_mcu_txd),
		.tailroom = MT_USB_TAIL_SIZE,
		.mcu_skb_send_msg = standalone_mt7921u_mcu_send_message,
		.mcu_parse_response = standalone_mt7921_mcu_parse_response,
	};
	int ret;

	dev->standalone_mt76.mcu_ops = &mcu_ops;

	standalone_mt76_set(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);
	ret = standalone_mt7921_run_firmware(dev);
	if (ret)
		return ret;

	set_bit(STANDALONE_MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	standalone_mt76_clear(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	return 0;
}

static int standalone_mt7921u_mac_reset(struct standalone_mt792x_dev *dev)
{
	int err;

	standalone_mt792xu_reset_on_bus_error(dev);
	if (atomic_read(&dev->standalone_mt76.bus_hung))
		return 0;

	standalone_mt76_txq_schedule_all(&dev->mphy);
	standalone_mt76_worker_disable(&dev->standalone_mt76.tx_worker);

	set_bit(STANDALONE_MT76_RESET, &dev->mphy.state);
	set_bit(STANDALONE_MT76_MCU_RESET, &dev->mphy.state);

	wake_up(&dev->standalone_mt76.mcu.wait);
	skb_queue_purge(&dev->standalone_mt76.mcu.res_q);

	standalone_mt76u_stop_rx(&dev->standalone_mt76);
	standalone_mt76u_stop_tx(&dev->standalone_mt76);

	standalone_mt792xu_wfsys_reset(dev);

	clear_bit(STANDALONE_MT76_MCU_RESET, &dev->mphy.state);
	err = standalone_mt76u_resume_rx(&dev->standalone_mt76);
	if (err)
		goto out;

	err = standalone_mt792xu_mcu_power_on(dev);
	if (err)
		goto out;

	err = standalone_mt792xu_dma_init(dev, false);
	if (err)
		goto out;

	standalone_mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
	standalone_mt76_set(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	err = standalone_mt7921_run_firmware(dev);
	if (err)
		goto out;

	standalone_mt76_clear(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	err = standalone_mt7921_mcu_set_eeprom(dev);
	if (err)
		goto out;

	err = standalone_mt7921_mac_init(dev);
	if (err)
		goto out;

	err = __standalone_mt7921_start(&dev->phy);
out:
	clear_bit(STANDALONE_MT76_RESET, &dev->mphy.state);

	standalone_mt76_worker_enable(&dev->standalone_mt76.tx_worker);

	return err;
}

static int standalone_mt7921u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	static const struct standalone_mt76_driver_ops drv_ops = {
		.txwi_size = MT_SDIO_TXD_SIZE,
		.drv_flags = MT_DRV_RX_DMA_HDR | MT_DRV_HW_MGMT_TXQ |
			     MT_DRV_AMSDU_OFFLOAD,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.tx_prepare_skb = standalone_mt7921_usb_sdio_tx_prepare_skb,
		.tx_complete_skb = standalone_mt7921_usb_sdio_tx_complete_skb,
		.tx_status_data = standalone_mt7921_usb_sdio_tx_status_data,
		.rx_skb = standalone_mt7921_queue_rx_skb,
		.rx_check = standalone_mt7921_rx_check,
		.sta_add = standalone_mt7921_mac_sta_add,
		.sta_event = standalone_mt7921_mac_sta_event,
		.sta_remove = standalone_mt7921_mac_sta_remove,
		.update_survey = standalone_mt792x_update_channel,
		.set_channel = standalone_mt7921_set_channel,
	};
	static const struct standalone_mt792x_hif_ops hif_ops = {
		.mcu_init = standalone_mt7921u_mcu_init,
		.init_reset = standalone_mt792xu_init_reset,
		.reset = standalone_mt7921u_mac_reset,
	};
	static struct standalone_mt76_bus_ops bus_ops = {
		.rr = standalone_mt792xu_rr,
		.wr = standalone_mt792xu_wr,
		.rmw = standalone_mt792xu_rmw,
		.read_copy = standalone_mt76u_read_copy,
		.write_copy = standalone_mt792xu_copy,
		.type = STANDALONE_MT76_BUS_USB,
	};
	struct usb_device *udev = interface_to_usbdev(usb_intf);
	struct ieee80211_ops *ops;
	struct ieee80211_hw *hw;
	struct standalone_mt792x_dev *dev;
	struct standalone_mt76_dev *mdev;
	u8 features;
	int ret;

	ops = standalone_mt792x_get_mac80211_ops(&usb_intf->dev, &standalone_mt7921_ops,
				      (void *)id->driver_info, &features);
	if (!ops)
		return -ENOMEM;

	ops->stop = standalone_mt792xu_stop;
	mdev = standalone_mt76_alloc_device(&usb_intf->dev, sizeof(*dev), ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct standalone_mt792x_dev, standalone_mt76);
	dev->fw_features = features;
	dev->hif_ops = &hif_ops;
	atomic_set(&dev->standalone_mt76.bus_hung, false);
	standalone_mt792xu_reset_work_init(dev);

	usb_reset_device(udev);

	usb_set_intfdata(usb_intf, dev);

	ret = __standalone_mt76u_init(mdev, usb_intf, &bus_ops);
	if (ret < 0)
		goto error;

	mdev->rev = (standalone_mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (standalone_mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	if (standalone_mt76_get_field(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY)) {
		ret = standalone_mt792xu_wfsys_reset(dev);
		if (ret)
			goto error;
	}

	ret = standalone_mt792xu_mcu_power_on(dev);
	if (ret)
		goto error;

	ret = standalone_mt76u_alloc_mcu_queue(&dev->standalone_mt76);
	if (ret)
		goto error;

	ret = standalone_mt76u_alloc_queues(&dev->standalone_mt76);
	if (ret)
		goto error;

	ret = standalone_mt792xu_dma_init(dev, false);
	if (ret)
		goto error;

	hw = standalone_mt76_hw(dev);
	/* check hw sg support in order to enable AMSDU */
	hw->max_tx_fragments = mdev->usb.sg_en ? MT_HW_TXP_MAX_BUF_NUM : 1;

	ret = standalone_mt7921_register_device(dev);
	if (ret)
		goto error;

	return 0;

error:
	standalone_mt76u_queues_deinit(&dev->standalone_mt76);
	standalone_mt792xu_reset_work_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);

	standalone_mt76_free_device(&dev->standalone_mt76);

	return ret;
}

#ifdef CONFIG_PM
static int standalone_mt7921u_suspend(struct usb_interface *intf, pm_message_t state)
{
	struct standalone_mt792x_dev *dev = usb_get_intfdata(intf);
	struct standalone_mt76_connac_pm *pm = &dev->pm;
	int err;

	pm->suspended = true;
	flush_work(&dev->reset_work);

	err = standalone_mt76_connac_mcu_set_hif_suspend(&dev->standalone_mt76, true, true);
	if (err)
		goto failed;

	standalone_mt76u_stop_rx(&dev->standalone_mt76);
	standalone_mt76u_stop_tx(&dev->standalone_mt76);

	return 0;

failed:
	pm->suspended = false;

	if (err < 0)
		standalone_mt792x_reset(&dev->standalone_mt76);

	return err;
}

static int standalone_mt7921u_resume(struct usb_interface *intf)
{
	struct standalone_mt792x_dev *dev = usb_get_intfdata(intf);
	struct standalone_mt76_connac_pm *pm = &dev->pm;
	bool reinit = true;
	int err, i;

	for (i = 0; i < 10; i++) {
		u32 val = standalone_mt76_rr(dev, MT_WF_SW_DEF_CR_USB_MCU_EVENT);

		if (!(val & MT_WF_SW_SER_TRIGGER_SUSPEND)) {
			reinit = false;
			break;
		}
		if (val & MT_WF_SW_SER_DONE_SUSPEND) {
			standalone_mt76_wr(dev, MT_WF_SW_DEF_CR_USB_MCU_EVENT, 0);
			break;
		}

		msleep(20);
	}

	if (reinit || standalone_mt792x_dma_need_reinit(dev)) {
		err = standalone_mt792xu_dma_init(dev, true);
		if (err)
			goto failed;
	}

	err = standalone_mt76u_resume_rx(&dev->standalone_mt76);
	if (err < 0)
		goto failed;

	err = standalone_mt76_connac_mcu_set_hif_suspend(&dev->standalone_mt76, false, true);
failed:
	pm->suspended = false;

	if (err < 0)
		standalone_mt792x_reset(&dev->standalone_mt76);

	return err;
}
#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(usb, standalone_mt7921u_device_table);
MODULE_FIRMWARE(STANDALONE_MT7921_FIRMWARE_WM);
MODULE_FIRMWARE(STANDALONE_MT7921_ROM_PATCH);

static struct usb_driver standalone_mt7921u_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= standalone_mt7921u_device_table,
	.probe		= standalone_mt7921u_probe,
	.disconnect	= standalone_mt792xu_disconnect,
#ifdef CONFIG_PM
	.suspend	= standalone_mt7921u_suspend,
	.resume		= standalone_mt7921u_resume,
	.reset_resume	= standalone_mt7921u_resume,
#endif /* CONFIG_PM */
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(standalone_mt7921u_driver);

MODULE_DESCRIPTION("MediaTek Standalone MT7921U (USB) wireless driver");
MODULE_AUTHOR("Sam Bélanger <github@astromangaming.ca>");
MODULE_LICENSE("Dual BSD/GPL");

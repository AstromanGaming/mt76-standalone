// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 *
 * Author: Sam Bélanger <github@astromangaming.ca>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "standalone_mt792x.h"
#include "standalone_mt76_connac2_mac.h"

static int standalone_mt792xu_read32(struct standalone_mt76_dev *dev, u32 addr, void *buf)
{
	return __standalone_mt76u_vendor_request(dev, MT_VEND_READ_EXT,
				      USB_DIR_IN | MT_USB_TYPE_VENDOR,
				      (u16)(addr >> 16), (u16)addr,
				      buf, sizeof(__le32));
}

static void standalone_mt792xu_reset_work(struct work_struct *work)
{
	struct standalone_mt792x_dev *dev =
		container_of(work, struct standalone_mt792x_dev, usb_reset_work);
	struct usb_interface *usb_intf = to_usb_interface(dev->standalone_mt76.dev);

	if (usb_intf && usb_get_intfdata(usb_intf) == dev)
		usb_queue_reset_device(usb_intf);

	atomic_set(&dev->usb_reset_pending, 0);
}

void standalone_mt792xu_reset_work_init(struct standalone_mt792x_dev *dev)
{
	INIT_WORK(&dev->usb_reset_work, standalone_mt792xu_reset_work);
	atomic_set(&dev->usb_reset_pending, 0);
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_reset_work_init);

void standalone_mt792xu_reset_work_cleanup(struct standalone_mt792x_dev *dev)
{
	cancel_work_sync(&dev->usb_reset_work);
	atomic_set(&dev->usb_reset_pending, 0);
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_reset_work_cleanup);

int standalone_mt792xu_check_bus(struct standalone_mt792x_dev *dev)
{
	int ret;

	mutex_lock(&dev->standalone_mt76.usb.usb_ctrl_mtx);
	ret = standalone_mt792xu_read32(&dev->standalone_mt76, MT_HW_CHIPID, dev->standalone_mt76.usb.data);
	mutex_unlock(&dev->standalone_mt76.usb.usb_ctrl_mtx);

	if (ret == sizeof(__le32))
		return 0;

	return ret < 0 ? ret : -EIO;
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_check_bus);

int standalone_mt792xu_reset_on_bus_error(struct standalone_mt792x_dev *dev)
{
	int err = 0;

	if (!atomic_read(&dev->standalone_mt76.bus_hung))
		err = standalone_mt792xu_check_bus(dev);

	if (err) {
		atomic_set(&dev->standalone_mt76.bus_hung, true);

		if (!atomic_xchg(&dev->usb_reset_pending, 1)) {
			dev_warn(dev->standalone_mt76.dev,
				 "USB transport access failed (%d), queueing device reset\n",
				 err);

			schedule_work(&dev->usb_reset_work);
		}

		return err;
	}

	atomic_set(&dev->standalone_mt76.bus_hung, false);
	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_reset_on_bus_error);

u32 standalone_mt792xu_rr(struct standalone_mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___standalone_mt76u_rr(dev, MT_VEND_READ_EXT,
			  USB_DIR_IN | MT_USB_TYPE_VENDOR, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_rr);

void standalone_mt792xu_wr(struct standalone_mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___standalone_mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | MT_USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_wr);

u32 standalone_mt792xu_rmw(struct standalone_mt76_dev *dev, u32 addr, u32 mask, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	val |= ___standalone_mt76u_rr(dev, MT_VEND_READ_EXT,
			   USB_DIR_IN | MT_USB_TYPE_VENDOR, addr) & ~mask;
	___standalone_mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | MT_USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return val;
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_rmw);

void standalone_mt792xu_copy(struct standalone_mt76_dev *dev, u32 offset, const void *data, int len)
{
	struct standalone_mt76_usb *usb = &dev->usb;
	int ret, i = 0, batch_len;
	const u8 *val = data;

	len = round_up(len, 4);

	mutex_lock(&usb->usb_ctrl_mtx);
	while (i < len) {
		batch_len = min_t(int, usb->data_len, len - i);
		memcpy(usb->data, val + i, batch_len);
		ret = __standalone_mt76u_vendor_request(dev, MT_VEND_WRITE_EXT,
					     USB_DIR_OUT | MT_USB_TYPE_VENDOR,
					     (offset + i) >> 16, offset + i,
					     usb->data, batch_len);
		if (ret < 0)
			break;

		i += batch_len;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_copy);

int standalone_mt792xu_mcu_power_on(struct standalone_mt792x_dev *dev)
{
	int ret;

	ret = standalone_mt76u_vendor_request(&dev->standalone_mt76, MT_VEND_POWER_ON,
				   USB_DIR_OUT | MT_USB_TYPE_VENDOR,
				   0x0, 0x1, NULL, 0);
	if (ret)
		return ret;

	if (!standalone_mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_PWR_ON,
			    MT_TOP_MISC2_FW_PWR_ON, 500)) {
		dev_err(dev->standalone_mt76.dev, "Timeout for power on\n");
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_mcu_power_on);

static void standalone_mt792xu_cleanup(struct standalone_mt792x_dev *dev)
{
	clear_bit(STANDALONE_MT76_STATE_INITIALIZED, &dev->mphy.state);
	standalone_mt792xu_wfsys_reset(dev);
	skb_queue_purge(&dev->standalone_mt76.mcu.res_q);
	standalone_mt76u_queues_deinit(&dev->standalone_mt76);
}

static u32 standalone_mt792xu_uhw_rr(struct standalone_mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___standalone_mt76u_rr(dev, MT_VEND_DEV_MODE,
			  USB_DIR_IN | MT_USB_TYPE_UHW_VENDOR, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}

static void standalone_mt792xu_uhw_wr(struct standalone_mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___standalone_mt76u_wr(dev, MT_VEND_WRITE,
		    USB_DIR_OUT | MT_USB_TYPE_UHW_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}

static void standalone_mt792xu_dma_prefetch(struct standalone_mt792x_dev *dev)
{
#define DMA_PREFETCH_CONF(_idx_, _cnt_, _base_) \
	standalone_mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL((_idx_)), \
		 MT_WPDMA0_MAX_CNT_MASK | MT_WPDMA0_BASE_PTR_MASK, \
		 FIELD_PREP(MT_WPDMA0_MAX_CNT_MASK, (_cnt_)) | \
		 FIELD_PREP(MT_WPDMA0_BASE_PTR_MASK, (_base_)))

	DMA_PREFETCH_CONF(0, 4, 0x080);
	DMA_PREFETCH_CONF(1, 4, 0x0c0);
	DMA_PREFETCH_CONF(2, 4, 0x100);
	DMA_PREFETCH_CONF(3, 4, 0x140);
	DMA_PREFETCH_CONF(4, 4, 0x180);
	DMA_PREFETCH_CONF(16, 4, 0x280);
	DMA_PREFETCH_CONF(17, 4, 0x2c0);
}

static void standalone_mt792xu_wfdma_init(struct standalone_mt792x_dev *dev)
{
	int i;

	standalone_mt792xu_dma_prefetch(dev);

	standalone_mt76_clear(dev, MT_UWFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_OMIT_RX_INFO);
	standalone_mt76_set(dev, MT_UWFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 |
		 MT_WFDMA0_GLO_CFG_FW_DWLD_BYPASS_DMASHDL |
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		 MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	standalone_mt76_rmw(dev, MT_DMASHDL_REFILL, MT_DMASHDL_REFILL_MASK, 0xffe00000);
	standalone_mt76_clear(dev, MT_DMASHDL_PAGE, MT_DMASHDL_GROUP_SEQ_ORDER);
	standalone_mt76_rmw(dev, MT_DMASHDL_PKT_MAX_SIZE,
		 MT_DMASHDL_PKT_MAX_SIZE_PLE | MT_DMASHDL_PKT_MAX_SIZE_PSE,
		 FIELD_PREP(MT_DMASHDL_PKT_MAX_SIZE_PLE, 1) |
		 FIELD_PREP(MT_DMASHDL_PKT_MAX_SIZE_PSE, 0));
	for (i = 0; i < 5; i++)
		standalone_mt76_wr(dev, MT_DMASHDL_GROUP_QUOTA(i),
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MIN, 0x3) |
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MAX, 0xfff));
	for (i = 5; i < 16; i++)
		standalone_mt76_wr(dev, MT_DMASHDL_GROUP_QUOTA(i),
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MIN, 0x0) |
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MAX, 0x0));
	standalone_mt76_wr(dev, MT_DMASHDL_Q_MAP(0), 0x32013201);
	standalone_mt76_wr(dev, MT_DMASHDL_Q_MAP(1), 0x32013201);
	standalone_mt76_wr(dev, MT_DMASHDL_Q_MAP(2), 0x55555444);
	standalone_mt76_wr(dev, MT_DMASHDL_Q_MAP(3), 0x55555444);

	standalone_mt76_wr(dev, MT_DMASHDL_SCHED_SET(0), 0x76540132);
	standalone_mt76_wr(dev, MT_DMASHDL_SCHED_SET(1), 0xFEDCBA98);

	standalone_mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);
}

static int standalone_mt792xu_dma_rx_evt_ep4(struct standalone_mt792x_dev *dev)
{
	if (!standalone_mt76_poll(dev, MT_UWFDMA0_GLO_CFG,
		       MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 1000))
		return -ETIMEDOUT;

	standalone_mt76_clear(dev, MT_UWFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	standalone_mt76_set(dev, MT_WFDMA_HOST_CONFIG,
		 MT_WFDMA_HOST_CONFIG_USB_RXEVT_EP4_EN);
	standalone_mt76_set(dev, MT_UWFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	return 0;
}

static void standalone_mt792xu_epctl_rst_opt(struct standalone_mt792x_dev *dev, bool reset)
{
	u32 val;

	/* usb endpoint reset opt
	 * bits[4,9]: out blk ep 4-9
	 * bits[20,21]: in blk ep 4-5
	 * bits[22]: in int ep 6
	 */
	val = standalone_mt792xu_uhw_rr(&dev->standalone_mt76, MT_SSUSB_EPCTL_CSR_EP_RST_OPT);
	if (reset)
		val |= GENMASK(9, 4) | GENMASK(22, 20);
	else
		val &= ~(GENMASK(9, 4) | GENMASK(22, 20));
	standalone_mt792xu_uhw_wr(&dev->standalone_mt76, MT_SSUSB_EPCTL_CSR_EP_RST_OPT, val);
}

struct standalone_mt792xu_wfsys_desc {
	u32 rst_reg;
	u32 done_reg;
	u32 done_mask;
	u32 done_val;
	u32 delay_ms;
	bool need_status_sel;
};

static const struct standalone_mt792xu_wfsys_desc standalone_mt7921_wfsys_desc = {
	.rst_reg = MT_CBTOP_RGU_WF_SUBSYS_RST,
	.done_reg = MT_UDMA_CONN_INFRA_STATUS,
	.done_mask = MT_UDMA_CONN_WFSYS_INIT_DONE,
	.done_val = MT_UDMA_CONN_WFSYS_INIT_DONE,
	.delay_ms = 0,
	.need_status_sel = true,
};

static const struct standalone_mt792xu_wfsys_desc standalone_mt7925_wfsys_desc = {
	.rst_reg = STANDALONE_MT7925_CBTOP_RGU_WF_SUBSYS_RST,
	.done_reg = STANDALONE_MT7925_WFSYS_INIT_DONE_ADDR,
	.done_mask = U32_MAX,
	.done_val = STANDALONE_MT7925_WFSYS_INIT_DONE,
	.delay_ms = 20,
	.need_status_sel = false,
};

int standalone_mt792xu_dma_init(struct standalone_mt792x_dev *dev, bool resume)
{
	int err;

	standalone_mt792xu_wfdma_init(dev);

	standalone_mt76_clear(dev, MT_UDMA_WLCFG_0, MT_WL_RX_FLUSH);

	standalone_mt76_set(dev, MT_UDMA_WLCFG_0,
		 MT_WL_RX_EN | MT_WL_TX_EN |
		 MT_WL_RX_MPSZ_PAD0 | MT_TICK_1US_EN);
	standalone_mt76_clear(dev, MT_UDMA_WLCFG_0,
		   MT_WL_RX_AGG_TO | MT_WL_RX_AGG_LMT);
	standalone_mt76_clear(dev, MT_UDMA_WLCFG_1, MT_WL_RX_AGG_PKT_LMT);

	if (resume)
		return 0;

	err = standalone_mt792xu_dma_rx_evt_ep4(dev);
	if (err)
		return err;

	standalone_mt792xu_epctl_rst_opt(dev, false);

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_dma_init);

int standalone_mt792xu_wfsys_reset(struct standalone_mt792x_dev *dev)
{
	const struct standalone_mt792xu_wfsys_desc *desc = is_connac3(&dev->standalone_mt76) ?
						&standalone_mt7925_wfsys_desc :
						&standalone_mt7921_wfsys_desc;
	u32 val;
	int i;

	standalone_mt792xu_epctl_rst_opt(dev, false);

	val = standalone_mt792xu_uhw_rr(&dev->standalone_mt76, desc->rst_reg);
	val |= MT_CBTOP_RGU_WF_SUBSYS_RST_WF_WHOLE_PATH;
	standalone_mt792xu_uhw_wr(&dev->standalone_mt76, desc->rst_reg, val);

	if (desc->delay_ms)
		msleep(desc->delay_ms);
	else
		usleep_range(10, 20);

	val = standalone_mt792xu_uhw_rr(&dev->standalone_mt76, desc->rst_reg);
	val &= ~MT_CBTOP_RGU_WF_SUBSYS_RST_WF_WHOLE_PATH;
	standalone_mt792xu_uhw_wr(&dev->standalone_mt76, desc->rst_reg, val);

	if (desc->need_status_sel)
		standalone_mt792xu_uhw_wr(&dev->standalone_mt76, MT_UDMA_CONN_INFRA_STATUS_SEL, 0);

	for (i = 0; i < STANDALONE_MT792x_WFSYS_INIT_RETRY_COUNT; i++) {
		val = standalone_mt792xu_uhw_rr(&dev->standalone_mt76, desc->done_reg);
		if ((val & desc->done_mask) == desc->done_val)
			break;

		msleep(100);
	}

	if (i == STANDALONE_MT792x_WFSYS_INIT_RETRY_COUNT)
		return -ETIMEDOUT;

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_wfsys_reset);

int standalone_mt792xu_init_reset(struct standalone_mt792x_dev *dev)
{
	set_bit(STANDALONE_MT76_RESET, &dev->mphy.state);

	wake_up(&dev->standalone_mt76.mcu.wait);
	skb_queue_purge(&dev->standalone_mt76.mcu.res_q);

	standalone_mt76u_stop_rx(&dev->standalone_mt76);
	standalone_mt76u_stop_tx(&dev->standalone_mt76);

	standalone_mt792xu_wfsys_reset(dev);

	clear_bit(STANDALONE_MT76_RESET, &dev->mphy.state);

	return standalone_mt76u_resume_rx(&dev->standalone_mt76);
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_init_reset);

void standalone_mt792xu_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct standalone_mt792x_dev *dev = standalone_mt792x_hw_dev(hw);

	standalone_mt76u_stop_tx(&dev->standalone_mt76);
	standalone_mt792x_stop(hw, false);
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_stop);

void standalone_mt792xu_disconnect(struct usb_interface *usb_intf)
{
	struct standalone_mt792x_dev *dev = usb_get_intfdata(usb_intf);

	standalone_mt792xu_reset_work_cleanup(dev);
	cancel_work_sync(&dev->init_work);
	if (!test_bit(STANDALONE_MT76_STATE_INITIALIZED, &dev->mphy.state))
		return;

	standalone_mt76_unregister_device(&dev->standalone_mt76);
	standalone_mt792xu_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);

	standalone_mt76_free_device(&dev->standalone_mt76);
}
EXPORT_SYMBOL_GPL(standalone_mt792xu_disconnect);

MODULE_DESCRIPTION("MediaTek Standalone MT792x USB helpers");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Sam Bélanger <github@astromangaming.ca>");

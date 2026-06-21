// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca> */

#include <linux/module.h>
#include <linux/firmware.h>

#include "standalone_mt792x.h"
#include "dma.h"
#include "trace.h"

irqreturn_t standalone_mt792x_irq_handler(int irq, void *dev_instance)
{
	struct standalone_mt792x_dev *dev = dev_instance;

	if (test_bit(STANDALONE_MT76_REMOVED, &dev->standalone_mt76.phy.state))
		return IRQ_NONE;
	standalone_mt76_wr(dev, dev->irq_map->host_irq_enable, 0);

	if (!test_bit(STANDALONE_MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->standalone_mt76.irq_tasklet);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_irq_handler);

void standalone_mt792x_irq_tasklet(unsigned long data)
{
	struct standalone_mt792x_dev *dev = (struct standalone_mt792x_dev *)data;
	const struct standalone_mt792x_irq_map *irq_map = dev->irq_map;
	u32 intr, mask = 0;

	standalone_mt76_wr(dev, irq_map->host_irq_enable, 0);

	intr = standalone_mt76_rr(dev, MT_WFDMA0_HOST_INT_STA);
	intr &= dev->standalone_mt76.mmio.irqmask;
	standalone_mt76_wr(dev, MT_WFDMA0_HOST_INT_STA, intr);

	trace_dev_irq(&dev->standalone_mt76, intr, dev->standalone_mt76.mmio.irqmask);

	mask |= intr & (irq_map->rx.data_complete_mask |
			irq_map->rx.wm_complete_mask |
			irq_map->rx.wm2_complete_mask);
	if (intr & dev->irq_map->tx.mcu_complete_mask)
		mask |= dev->irq_map->tx.mcu_complete_mask;

	if (intr & MT_INT_MCU_CMD) {
		u32 intr_sw;

		intr_sw = standalone_mt76_rr(dev, MT_MCU_CMD);
		/* ack MCU2HOST_SW_INT_STA */
		standalone_mt76_wr(dev, MT_MCU_CMD, intr_sw);
		if (intr_sw & MT_MCU_CMD_WAKE_RX_PCIE) {
			mask |= irq_map->rx.data_complete_mask;
			intr |= irq_map->rx.data_complete_mask;
		}
	}

	standalone_mt76_set_irq_mask(&dev->standalone_mt76, irq_map->host_irq_enable, mask, 0);

	if (intr & dev->irq_map->tx.all_complete_mask)
		napi_schedule(&dev->standalone_mt76.tx_napi);

	if (intr & irq_map->rx.wm_complete_mask)
		napi_schedule(&dev->standalone_mt76.napi[MT_RXQ_MCU]);

	if (intr & irq_map->rx.wm2_complete_mask)
		napi_schedule(&dev->standalone_mt76.napi[MT_RXQ_MCU_WA]);

	if (intr & irq_map->rx.data_complete_mask)
		napi_schedule(&dev->standalone_mt76.napi[MT_RXQ_MAIN]);
}
EXPORT_SYMBOL_GPL(standalone_mt792x_irq_tasklet);

void standalone_mt792x_rx_poll_complete(struct standalone_mt76_dev *mdev, enum standalone_mt76_rxq_id q)
{
	struct standalone_mt792x_dev *dev = container_of(mdev, struct standalone_mt792x_dev, standalone_mt76);
	const struct standalone_mt792x_irq_map *irq_map = dev->irq_map;

	if (q == MT_RXQ_MAIN)
		standalone_mt76_connac_irq_enable(mdev, irq_map->rx.data_complete_mask);
	else if (q == MT_RXQ_MCU_WA)
		standalone_mt76_connac_irq_enable(mdev, irq_map->rx.wm2_complete_mask);
	else
		standalone_mt76_connac_irq_enable(mdev, irq_map->rx.wm_complete_mask);
}
EXPORT_SYMBOL_GPL(standalone_mt792x_rx_poll_complete);

int standalone_mt792x_dma_alloc_queues(struct standalone_mt792x_dev *dev,
			    const struct standalone_mt792x_dma_layout *layout)
{
	int ret;

	standalone_mt76_dma_attach(&dev->standalone_mt76);

	ret = standalone_mt792x_dma_disable(dev, true);
	if (ret)
		return ret;

	/* init tx queue */
	ret = standalone_mt76_connac_init_tx_queues(dev->phy.standalone_mt76, layout->tx_data0.qid,
					 layout->tx_data0.n_desc,
					 layout->tx_data0.ring_base,
					 NULL, 0);
	if (ret)
		return ret;

	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, 0x4);

	/* command to WM */
	ret = standalone_mt76_init_mcu_queue(&dev->standalone_mt76, MT_MCUQ_WM,
				  layout->tx_mcu.qid,
				  layout->tx_mcu.n_desc,
				  layout->tx_mcu.ring_base);
	if (ret)
		return ret;

	/* firmware download */
	ret = standalone_mt76_init_mcu_queue(&dev->standalone_mt76, MT_MCUQ_FWDL,
				  layout->tx_fwdl.qid,
				  layout->tx_fwdl.n_desc,
				  layout->tx_fwdl.ring_base);
	if (ret)
		return ret;

	/* rx event */
	ret = standalone_mt76_queue_alloc(dev, &dev->standalone_mt76.q_rx[MT_RXQ_MCU],
			       layout->rx_mcu.qid,
			       layout->rx_mcu.n_desc,
			       MT_RX_BUF_SIZE,
			       layout->rx_mcu.ring_base);
	if (ret)
		return ret;

	/* rx data */
	ret = standalone_mt76_queue_alloc(dev, &dev->standalone_mt76.q_rx[MT_RXQ_MAIN],
			       layout->rx_data.qid,
			       layout->rx_data.n_desc,
			       MT_RX_BUF_SIZE,
			       layout->rx_data.ring_base);
	return ret;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_dma_alloc_queues);

#define PREFETCH(base, depth)	((base) << 16 | (depth))

static void standalone_mt7925_dma_prefetch_setup(struct standalone_mt792x_dev *dev)
{
	/* rx ring */
	standalone_mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0000, 0x4));
	standalone_mt76_wr(dev, MT_WFDMA0_RX_RING1_EXT_CTRL, PREFETCH(0x0040, 0x4));
	standalone_mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x0080, 0x4));
	standalone_mt76_wr(dev, MT_WFDMA0_RX_RING3_EXT_CTRL, PREFETCH(0x00c0, 0x4));

	/* tx ring */
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, PREFETCH(0x0100, 0x10));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING1_EXT_CTRL, PREFETCH(0x0200, 0x10));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING2_EXT_CTRL, PREFETCH(0x0300, 0x10));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING3_EXT_CTRL, PREFETCH(0x0400, 0x10));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH(0x0500, 0x4));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x0540, 0x4));
}

static void standalone_mt7925_wfdma_setup(struct standalone_mt792x_dev *dev)
{
	standalone_mt76_rmw(dev, MT_UWFDMA0_GLO_CFG_EXT1, BIT(28), BIT(28));
	standalone_mt76_set(dev, MT_WFDMA0_INT_RX_PRI, 0x0F00);
	standalone_mt76_set(dev, MT_WFDMA0_INT_TX_PRI, 0x7F00);
}

static void standalone_mt7927_dma_prefetch_setup(struct standalone_mt792x_dev *dev)
{
	standalone_mt76_wr(dev, MT_WFDMA_PREFETCH_CTRL,
		standalone_mt76_rr(dev, MT_WFDMA_PREFETCH_CTRL));
	standalone_mt76_wr(dev, MT_WFDMA_PREFETCH_CFG0, 0x660077);
	standalone_mt76_wr(dev, MT_WFDMA_PREFETCH_CFG1, 0x1100);
	standalone_mt76_wr(dev, MT_WFDMA_PREFETCH_CFG2, 0x30004f);
	standalone_mt76_wr(dev, MT_WFDMA_PREFETCH_CFG3, 0x542200);
	standalone_mt76_wr(dev, MT_WFDMA0_RX_RING4_EXT_CTRL, PREFETCH(0x0000, 0x8));
	standalone_mt76_wr(dev, MT_WFDMA0_RX_RING6_EXT_CTRL, PREFETCH(0x0080, 0x8));
	standalone_mt76_wr(dev, MT_WFDMA0_RX_RING7_EXT_CTRL, PREFETCH(0x0100, 0x4));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x0140, 0x4));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH(0x0180, 0x10));
	standalone_mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, PREFETCH(0x0280, 0x4));
}

static void standalone_mt7927_wfdma_setup(struct standalone_mt792x_dev *dev)
{
	standalone_mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_ADDR_EXT_EN |
		 MT_WFDMA0_GLO_CFG_FW_DWLD_BYPASS_DMASHDL);
	standalone_mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_CSR_LBK_RX_Q_SEL_EN);
	standalone_mt76_rmw(dev, MT_WFDMA0_GLO_CFG_EXT1, BIT(28), BIT(28));
	standalone_mt76_set(dev, MT_WFDMA0_INT_RX_PRI, 0x0F00);
	standalone_mt76_set(dev, MT_WFDMA0_INT_TX_PRI, 0x7F00);
}

static void standalone_mt792x_dma_prefetch(struct standalone_mt792x_dev *dev)
{
	if (is_standalone_mt7927(&dev->standalone_mt76)) {
		standalone_mt7927_dma_prefetch_setup(dev);
	} else if (is_standalone_mt7925(&dev->standalone_mt76)) {
		standalone_mt7925_dma_prefetch_setup(dev);
	} else if (is_standalone_mt7902(&dev->standalone_mt76)) {
		/* rx ring */
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0000, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING1_EXT_CTRL, PREFETCH(0x0040, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x0080, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING3_EXT_CTRL, PREFETCH(0x00c0, 0x4));
		/* tx ring */
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, PREFETCH(0x0100, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING1_EXT_CTRL, PREFETCH(0x0140, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING2_EXT_CTRL, PREFETCH(0x0180, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING3_EXT_CTRL, PREFETCH(0x01c0, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING4_EXT_CTRL, PREFETCH(0x0200, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING5_EXT_CTRL, PREFETCH(0x0240, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING6_EXT_CTRL, PREFETCH(0x0280, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH(0x02c0, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x0300, 0x4));
	} else {
		/* rx ring */
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x40, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING3_EXT_CTRL, PREFETCH(0x80, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING4_EXT_CTRL, PREFETCH(0xc0, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_RX_RING5_EXT_CTRL, PREFETCH(0x100, 0x4));
		/* tx ring */
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, PREFETCH(0x140, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING1_EXT_CTRL, PREFETCH(0x180, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING2_EXT_CTRL, PREFETCH(0x1c0, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING3_EXT_CTRL, PREFETCH(0x200, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING4_EXT_CTRL, PREFETCH(0x240, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING5_EXT_CTRL, PREFETCH(0x280, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING6_EXT_CTRL, PREFETCH(0x2c0, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x340, 0x4));
		standalone_mt76_wr(dev, MT_WFDMA0_TX_RING17_EXT_CTRL, PREFETCH(0x380, 0x4));
	}
}

int standalone_mt792x_dma_enable(struct standalone_mt792x_dev *dev)
{
	/* configure perfetch settings */
	standalone_mt792x_dma_prefetch(dev);

	/* reset dma idx */
	standalone_mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
	if (is_standalone_mt7925(&dev->standalone_mt76))
		standalone_mt76_wr(dev, MT_WFDMA0_RST_DRX_PTR, ~0);

	/* configure delay interrupt */
	standalone_mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);

	standalone_mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
		 MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
		 MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |
		 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		 FIELD_PREP(MT_WFDMA0_GLO_CFG_DMA_SIZE, 3) |
		 MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK |
		 MT_WFDMA0_GLO_CFG_RX_WB_DDONE |
		 MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
		 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	standalone_mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	if (is_standalone_mt7927(&dev->standalone_mt76))
		standalone_mt7927_wfdma_setup(dev);
	else if (is_standalone_mt7925(&dev->standalone_mt76))
		standalone_mt7925_wfdma_setup(dev);

	standalone_mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);

	/* enable interrupts for TX/RX rings */
	standalone_mt76_connac_irq_enable(&dev->standalone_mt76,
			       dev->irq_map->tx.all_complete_mask |
			       dev->irq_map->rx.data_complete_mask |
			       dev->irq_map->rx.wm2_complete_mask |
			       dev->irq_map->rx.wm_complete_mask |
			       MT_INT_MCU_CMD);
	standalone_mt76_set(dev, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_dma_enable);

static int
standalone_mt792x_dma_reset(struct standalone_mt792x_dev *dev, bool force)
{
	int i, err;

	err = standalone_mt792x_dma_disable(dev, force);
	if (err)
		return err;

	/* reset hw queues */
	for (i = 0; i < __MT_TXQ_MAX; i++)
		standalone_mt76_queue_reset(dev, dev->mphy.q_tx[i], true);

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		standalone_mt76_queue_reset(dev, dev->standalone_mt76.q_mcu[i], true);

	standalone_mt76_for_each_q_rx(&dev->standalone_mt76, i)
		standalone_mt76_queue_reset(dev, &dev->standalone_mt76.q_rx[i], true);

	standalone_mt76_tx_status_check(&dev->standalone_mt76, true);

	return standalone_mt792x_dma_enable(dev);
}

int standalone_mt792x_wpdma_reset(struct standalone_mt792x_dev *dev, bool force)
{
	int i, err;

	/* clean up hw queues */
	for (i = 0; i < ARRAY_SIZE(dev->standalone_mt76.phy.q_tx); i++)
		standalone_mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], true);

	for (i = 0; i < ARRAY_SIZE(dev->standalone_mt76.q_mcu); i++)
		standalone_mt76_queue_tx_cleanup(dev, dev->standalone_mt76.q_mcu[i], true);

	standalone_mt76_for_each_q_rx(&dev->standalone_mt76, i)
		standalone_mt76_queue_rx_cleanup(dev, &dev->standalone_mt76.q_rx[i]);

	if (force) {
		err = standalone_mt792x_wfsys_reset(dev);
		if (err)
			return err;
	}
	err = standalone_mt792x_dma_reset(dev, force);
	if (err)
		return err;

	standalone_mt76_for_each_q_rx(&dev->standalone_mt76, i)
		standalone_mt76_queue_rx_reset(dev, i);

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_wpdma_reset);

int standalone_mt792x_wpdma_reinit_cond(struct standalone_mt792x_dev *dev)
{
	struct standalone_mt76_connac_pm *pm = &dev->pm;
	int err;

	/* check if the wpdma must be reinitialized */
	if (standalone_mt792x_dma_need_reinit(dev)) {
		/* disable interrutpts */
		standalone_mt76_wr(dev, dev->irq_map->host_irq_enable, 0);
		standalone_mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);

		err = standalone_mt792x_wpdma_reset(dev, false);
		if (err) {
			dev_err(dev->standalone_mt76.dev, "wpdma reset failed\n");
			return err;
		}

		/* enable interrutpts */
		standalone_mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
		pm->stats.lp_wake++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_wpdma_reinit_cond);

int standalone_mt792x_dma_disable(struct standalone_mt792x_dev *dev, bool force)
{
	/* disable WFDMA0 */
	standalone_mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
		   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	if (!standalone_mt76_poll_msec_tick(dev, MT_WFDMA0_GLO_CFG,
				 MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
				 MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 100, 1))
		return -ETIMEDOUT;

	if (is_standalone_mt7927(&dev->standalone_mt76)) {
		standalone_mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
		standalone_mt76_wr(dev, MT_WFDMA0_RST_DRX_PTR, ~0);
	}

	/* disable dmashdl */
	standalone_mt76_clear(dev, MT_WFDMA0_GLO_CFG_EXT0,
		   MT_WFDMA0_CSR_TX_DMASHDL_ENABLE);
	standalone_mt76_set(dev, MT_DMASHDL_SW_CONTROL, MT_DMASHDL_DMASHDL_BYPASS);

	if (force) {
		/* reset */
		standalone_mt76_clear(dev, MT_WFDMA0_RST,
			   MT_WFDMA0_RST_DMASHDL_ALL_RST |
			   MT_WFDMA0_RST_LOGIC_RST);

		standalone_mt76_set(dev, MT_WFDMA0_RST,
			 MT_WFDMA0_RST_DMASHDL_ALL_RST |
			 MT_WFDMA0_RST_LOGIC_RST);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_dma_disable);

void standalone_mt792x_dma_cleanup(struct standalone_mt792x_dev *dev)
{
	/* disable */
	standalone_mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
		   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	standalone_mt76_poll_msec_tick(dev, MT_WFDMA0_GLO_CFG,
			    MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
			    MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 100, 1);

	/* reset */
	standalone_mt76_clear(dev, MT_WFDMA0_RST,
		   MT_WFDMA0_RST_DMASHDL_ALL_RST |
		   MT_WFDMA0_RST_LOGIC_RST);

	standalone_mt76_set(dev, MT_WFDMA0_RST,
		 MT_WFDMA0_RST_DMASHDL_ALL_RST |
		 MT_WFDMA0_RST_LOGIC_RST);

	standalone_mt76_dma_cleanup(&dev->standalone_mt76);
}
EXPORT_SYMBOL_GPL(standalone_mt792x_dma_cleanup);

int standalone_mt792x_poll_tx(struct napi_struct *napi, int budget)
{
	struct standalone_mt792x_dev *dev;

	dev = container_of(napi, struct standalone_mt792x_dev, standalone_mt76.tx_napi);

	if (!standalone_mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		napi_complete(napi);
		queue_work(dev->standalone_mt76.wq, &dev->pm.wake_work);
		return 0;
	}

	standalone_mt76_connac_tx_cleanup(&dev->standalone_mt76);
	if (napi_complete(napi))
		standalone_mt76_connac_irq_enable(&dev->standalone_mt76,
				       dev->irq_map->tx.all_complete_mask);
	standalone_mt76_connac_pm_unref(&dev->mphy, &dev->pm);

	return 0;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_poll_tx);

int standalone_mt792x_poll_rx(struct napi_struct *napi, int budget)
{
	struct standalone_mt792x_dev *dev;
	int done;

	dev = standalone_mt76_priv(napi->dev);

	if (!standalone_mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		napi_complete(napi);
		queue_work(dev->standalone_mt76.wq, &dev->pm.wake_work);
		return 0;
	}
	done = standalone_mt76_dma_rx_poll(napi, budget);
	standalone_mt76_connac_pm_unref(&dev->mphy, &dev->pm);

	return done;
}
EXPORT_SYMBOL_GPL(standalone_mt792x_poll_rx);

static void standalone_mt7927_sema_status_read(struct standalone_mt792x_dev *dev, u32 addr)
{
	u32 remap;

	remap = standalone_mt76_rr(dev, STANDALONE_MT7927_PCIE2AP_REMAP_WF_0_54);
	standalone_mt76_wr(dev, STANDALONE_MT7927_PCIE2AP_REMAP_WF_0_54,
		(remap & ~STANDALONE_MT7927_PCIE2AP_REMAP_WF_0_54_MASK) |
		STANDALONE_MT7927_PCIE2AP_REMAP_WF_0_54_VAL);
	usleep_range(10, 20);

	standalone_mt76_rr(dev, addr);

	standalone_mt76_wr(dev, STANDALONE_MT7927_PCIE2AP_REMAP_WF_0_54, remap);
	usleep_range(10, 20);
}

static int standalone_mt7927_wfsys_reset(struct standalone_mt792x_dev *dev)
{
	struct standalone_mt76_dev *mdev = &dev->standalone_mt76;
	u32 val;

	standalone_mt7927_sema_status_read(dev, STANDALONE_MT7927_SEMA_OWN_STA);

	standalone_mt76_set(dev, STANDALONE_MT7927_CBINFRA_RGU_WF_RST,
		 STANDALONE_MT7927_CBINFRA_RGU_WF_RST_WF_SUBSYS);
	usleep_range(1000, 2000);

	standalone_mt76_clear(dev, STANDALONE_MT7927_CBINFRA_RGU_WF_RST,
		   STANDALONE_MT7927_CBINFRA_RGU_WF_RST_WF_SUBSYS);
	usleep_range(5000, 10000);

	standalone_mt76_wr(dev, STANDALONE_MT7927_CBINFRA_MCU_OWN_SET, BIT(0));

	if (!__standalone_mt76_poll_msec(mdev, STANDALONE_MT7927_ROMCODE_INDEX, 0xffff,
			      STANDALONE_MT7927_MCU_IDLE_VALUE, 200)) {
		val = standalone_mt76_rr(dev, STANDALONE_MT7927_ROMCODE_INDEX);
		dev_err(mdev->dev,
			"STANDALONE_MT7927 WFSYS reset timeout (ROMCODE_INDEX=0x%04x)\n",
			val & 0xffff);
		return -ETIMEDOUT;
	}

	standalone_mt7927_sema_status_read(dev, STANDALONE_MT7927_SEMA_OWN_STA_REP);

	return 0;
}

static int standalone_mt792x_wfsys_reset_default(struct standalone_mt792x_dev *dev)
{
	u32 addr = is_connac2(&dev->standalone_mt76) ? 0x18000140 : 0x7c000140;

	standalone_mt76_clear(dev, addr, WFSYS_SW_RST_B);
	msleep(50);
	standalone_mt76_set(dev, addr, WFSYS_SW_RST_B);

	if (!__standalone_mt76_poll_msec(&dev->standalone_mt76, addr, WFSYS_SW_INIT_DONE,
			      WFSYS_SW_INIT_DONE, 500))
		return -ETIMEDOUT;

	return 0;
}

int standalone_mt792x_wfsys_reset(struct standalone_mt792x_dev *dev)
{
	if (is_standalone_mt7927(&dev->standalone_mt76))
		return standalone_mt7927_wfsys_reset(dev);

	return standalone_mt792x_wfsys_reset_default(dev);
}
EXPORT_SYMBOL_GPL(standalone_mt792x_wfsys_reset);


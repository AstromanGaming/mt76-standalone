// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */

#include "standalone_mt76.h"
#include "dma.h"
#include "trace.h"

static u32 standalone_mt76_mmio_rr(struct standalone_mt76_dev *dev, u32 offset)
{
	u32 val;

	val = readl(dev->mmio.regs + offset);
	trace_reg_rr(dev, offset, val);

	return val;
}

static void standalone_mt76_mmio_wr(struct standalone_mt76_dev *dev, u32 offset, u32 val)
{
	trace_reg_wr(dev, offset, val);
	writel(val, dev->mmio.regs + offset);
}

static u32 standalone_mt76_mmio_rmw(struct standalone_mt76_dev *dev, u32 offset, u32 mask, u32 val)
{
	val |= standalone_mt76_mmio_rr(dev, offset) & ~mask;
	standalone_mt76_mmio_wr(dev, offset, val);
	return val;
}

static void standalone_mt76_mmio_write_copy(struct standalone_mt76_dev *dev, u32 offset,
				 const void *data, int len)
{
	int i;

	for (i = 0; i < ALIGN(len, 4); i += 4)
		writel(get_unaligned_le32(data + i),
		       dev->mmio.regs + offset + i);
}

static void standalone_mt76_mmio_read_copy(struct standalone_mt76_dev *dev, u32 offset,
				void *data, int len)
{
	int i;

	for (i = 0; i < ALIGN(len, 4); i += 4)
		put_unaligned_le32(readl(dev->mmio.regs + offset + i),
				   data + i);
}

static int standalone_mt76_mmio_wr_rp(struct standalone_mt76_dev *dev, u32 base,
			   const struct standalone_mt76_reg_pair *data, int len)
{
	while (len > 0) {
		standalone_mt76_mmio_wr(dev, data->reg, data->value);
		data++;
		len--;
	}

	return 0;
}

static int standalone_mt76_mmio_rd_rp(struct standalone_mt76_dev *dev, u32 base,
			   struct standalone_mt76_reg_pair *data, int len)
{
	while (len > 0) {
		data->value = standalone_mt76_mmio_rr(dev, data->reg);
		data++;
		len--;
	}

	return 0;
}

void standalone_mt76_set_irq_mask(struct standalone_mt76_dev *dev, u32 addr,
		       u32 clear, u32 set)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->mmio.irq_lock, flags);
	dev->mmio.irqmask &= ~clear;
	dev->mmio.irqmask |= set;
	if (addr) {
		if (mtk_wed_device_active(&dev->mmio.wed))
			mtk_wed_device_irq_set_mask(&dev->mmio.wed,
						    dev->mmio.irqmask);
		else
			standalone_mt76_mmio_wr(dev, addr, dev->mmio.irqmask);
	}
	spin_unlock_irqrestore(&dev->mmio.irq_lock, flags);
}
EXPORT_SYMBOL_GPL(standalone_mt76_set_irq_mask);

void standalone_mt76_mmio_init(struct standalone_mt76_dev *dev, void __iomem *regs)
{
	static const struct standalone_mt76_bus_ops standalone_mt76_mmio_ops = {
		.rr = standalone_mt76_mmio_rr,
		.rmw = standalone_mt76_mmio_rmw,
		.wr = standalone_mt76_mmio_wr,
		.write_copy = standalone_mt76_mmio_write_copy,
		.read_copy = standalone_mt76_mmio_read_copy,
		.wr_rp = standalone_mt76_mmio_wr_rp,
		.rd_rp = standalone_mt76_mmio_rd_rp,
		.type = STANDALONE_MT76_BUS_MMIO,
	};

	dev->bus = &standalone_mt76_mmio_ops;
	dev->mmio.regs = regs;

	spin_lock_init(&dev->mmio.irq_lock);
}
EXPORT_SYMBOL_GPL(standalone_mt76_mmio_init);

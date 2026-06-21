# SPDX-License-Identifier: BSD-3-Clause-Clear
ccflags-y += -Werror -DCONFIG_STANDALONE_MT76_LEDS
obj-m := standalone-mt76.o
obj-$(CONFIG_STANDALONE_MT76_USB) += standalone-mt76-usb.o
obj-$(CONFIG_STANDALONE_MT76_CONNAC_LIB) += standalone-mt76-connac-lib.o
obj-$(CONFIG_STANDALONE_MT792x_LIB) += standalone-mt792x-lib.o
obj-$(CONFIG_STANDALONE_MT792x_USB) += standalone-mt792x-usb.o

standalone-mt76-y := \
	mmio.o util.o trace.o dma.o mac80211.o debugfs.o eeprom.o \
	tx.o agg-rx.o mcu.o wed.o scan.o channel.o

standalone-mt76-usb-y := usb.o usb_trace.o

CFLAGS_trace.o := -I$(src)
CFLAGS_usb_trace.o := -I$(src)
CFLAGS_standalone_mt792x_trace.o := -I$(src)

standalone-mt76-connac-lib-y := standalone_mt76_connac_mcu.o standalone_mt76_connac_mac.o standalone_mt76_connac3_mac.o

standalone-mt792x-lib-y := standalone_mt792x_core.o standalone_mt792x_mac.o standalone_mt792x_trace.o \
		standalone_mt792x_debugfs.o standalone_mt792x_dma.o
standalone-mt792x-lib-$(CONFIG_ACPI) += standalone_mt792x_acpi_sar.o
standalone-mt792x-usb-y := standalone_mt792x_usb.o

obj-$(CONFIG_STANDALONE_MT7921_COMMON) += mt7921/
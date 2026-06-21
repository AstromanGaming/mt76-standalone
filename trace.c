// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */

#include <linux/module.h>

#ifndef __CHECKER__
#define CREATE_TRACE_POINTS
#include "trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(mac_txdone);
EXPORT_TRACEPOINT_SYMBOL_GPL(dev_irq);

#endif

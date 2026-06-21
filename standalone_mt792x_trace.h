/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 Sam Bélanger <github@astromangaming.ca>
 */

#if !defined(__STANDALONE_MT792X_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __STANDALONE_MT792X_TRACE_H

#include <linux/tracepoint.h>
#include "standalone_mt792x.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM standalone_mt792x

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN	strscpy(__entry->wiphy_name,	\
				wiphy_name(standalone_mt76_hw(dev)->wiphy), MAXNAME)
#define DEV_PR_FMT	"%s"
#define DEV_PR_ARG	__entry->wiphy_name
#define LP_STATE_PR_ARG	__entry->lp_state ? "lp ready" : "lp not ready"

TRACE_EVENT(lp_event,
	TP_PROTO(struct standalone_mt792x_dev *dev, u8 lp_state),

	TP_ARGS(dev, lp_state),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, lp_state)
	),

	TP_fast_assign(
		DEV_ASSIGN;
		__entry->lp_state = lp_state;
	),

	TP_printk(
		DEV_PR_FMT " %s",
		DEV_PR_ARG, LP_STATE_PR_ARG
	)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE standalone_mt792x_trace

#include <trace/define_trace.h>

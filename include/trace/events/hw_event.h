#undef TRACE_SYSTEM
#define TRACE_SYSTEM hw_event

#if !defined(_TRACE_HW_EVENT_MC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HW_EVENT_MC_H

#include <linux/tracepoint.h>
#include <linux/edac.h>

/*
 * Hardware Anomaly Report Mecanism (HARM) events
 *
 * Those events are generated when hardware detected a corrected or
 * uncorrected event, and are meant to replace the current API to report
 * errors defined on both EDAC and MCE subsystems.
 */

DECLARE_EVENT_CLASS(hw_event_class,
	TP_PROTO(const char *type, unsigned int instance),
	TP_ARGS(type, instance),

	TP_STRUCT__entry(
		__string(	type,		type			)
		__field(	unsigned int,	instance		)
	),

	TP_fast_assign(
		__assign_str(type, type);
		__entry->instance = instance;
	),

	TP_printk("Initialized %s#%d\n",
		__get_str(type),
		__entry->instance)
);

/*
 * This event indicates that a hardware collection mechanism is started
 */
DEFINE_EVENT(hw_event_class, hw_event_init,

	TP_PROTO(const char *type, unsigned int instance),

	TP_ARGS(type, instance)
);


/*
 * Memory Controller specific events
 */

/*
 * Default error mechanisms for Memory Controller errors (CE and UE)
 */
TRACE_EVENT(mc_error,

	TP_PROTO(const unsigned int err_type,
		 const unsigned int mc_index,
		 const char *msg,
		 const char *label,
		 const int branch,
		 const int channel,
		 const int dimm,
		 const int csrow,
		 const int cschannel,
		 const char *detail,
		 const char *driver_detail),

	TP_ARGS(err_type, mc_index, msg, label, branch, channel, dimm, csrow,
		cschannel, detail, driver_detail),

	TP_STRUCT__entry(
		__field(	unsigned int,	err_type		)
		__field(	unsigned int,	mc_index		)
		__field(	int,		branch			)
		__field(	int,		channel			)
		__field(	int,		dimm			)
		__field(	int,		csrow			)
		__field(	int,		cschannel		)
		__string(	msg,		msg			)
		__string(	label,		label			)
		__string(	detail,		detail			)
		__string(	driver_detail,	driver_detail		)
	),

	TP_fast_assign(
		__entry->err_type		= err_type;
		__entry->mc_index		= mc_index;
		__entry->branch			= branch;
		__entry->channel		= channel;
		__entry->dimm			= dimm;
		__entry->csrow			= csrow;
		__entry->cschannel		= cschannel;
		__assign_str(msg, msg);
		__assign_str(label, label);
		__assign_str(detail, detail);
		__assign_str(driver_detail, driver_detail);
	),

	TP_printk(HW_ERR "mce#%d: %s error %s on label \"%s\" (location %d.%d.%d.%d.%d %s %s)\n",
		  __entry->mc_index,
		  (__entry->err_type == HW_EVENT_ERR_CORRECTED) ? "Corrected" :
			((__entry->err_type == HW_EVENT_ERR_FATAL) ?
			"Fatal" : "Uncorrected"),
		  __get_str(msg),
		  __get_str(label),
		  __entry->branch, __entry->channel, __entry->dimm,
		  __entry->csrow, __entry->cschannel,
		  __get_str(detail),
		  __get_str(driver_detail))
);

TRACE_EVENT(mc_out_of_range,
	TP_PROTO(struct mem_ctl_info *mci, const char *type, const char *field,
		int invalid_val, int min, int max),

	TP_ARGS(mci, type, field, invalid_val, min, max),

	TP_STRUCT__entry(
		__string(	type,		type			)
		__string(	field,		field			)
		__field(	unsigned int,	mc_index		)
		__field(	int,		invalid_val		)
		__field(	int,		min			)
		__field(	int,		max			)
	),

	TP_fast_assign(
		__assign_str(type, type);
		__assign_str(field, field);
		__entry->mc_index		= mci->mc_idx;
		__entry->invalid_val		= invalid_val;
		__entry->min			= min;
		__entry->max			= max;
	),

	TP_printk(HW_ERR "mce#%d %s: %s=%d is not between %d and %d\n",
		__entry->mc_index,
		__get_str(type),
		__get_str(field),
		__entry->invalid_val,
		__entry->min,
		__entry->max)
);

/*
 * MCE Events placeholder. Please add non-memory events that come from the
 * MCE driver here
 */


#endif /* _TRACE_HW_EVENT_MC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

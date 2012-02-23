#undef TRACE_SYSTEM
#define TRACE_SYSTEM hw_event

#if !defined(_TRACE_HW_EVENT_MC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HW_EVENT_MC_H

#include <linux/tracepoint.h>
#include <linux/edac.h>
#include <linux/ktime.h>

/*
 * Hardware Anomaly Report Mecanism (HARM) events
 *
 * Those events are generated when hardware detected a corrected or
 * uncorrected event, and are meant to replace the current API to report
 * errors defined on both EDAC and MCE subsystems.
 *
 * There are two types of events defined here: arch-independent ones, and
 * x86 arch events. The x86 arch events are based on x86 MCE architecture.
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
 * Hardware-independent Memory Controller specific events
 */

/*
 * Default error mechanisms for Memory Controller errors (CE and UE)
 */
TRACE_EVENT(mc_error,

	TP_PROTO(const unsigned int err_type,
		 const unsigned int mc_index,
		 const char *msg,
		 const char *label,
		 const char *location,
		 const char *detail,
		 const char *driver_detail),

	TP_ARGS(err_type, mc_index, msg, label, location,
		detail, driver_detail),

	TP_STRUCT__entry(
		__field(	unsigned int,	err_type		)
		__field(	unsigned int,	mc_index		)
		__string(	msg,		msg			)
		__string(	label,		label			)
		__string(	detail,		detail			)
		__string(	location,	location		)
		__string(	driver_detail,	driver_detail		)
	),

	TP_fast_assign(
		__entry->err_type		= err_type;
		__entry->mc_index		= mc_index;
		__assign_str(msg, msg);
		__assign_str(label, label);
		__assign_str(location, location);
		__assign_str(detail, detail);
		__assign_str(driver_detail, driver_detail);
	),

	TP_printk(HW_ERR "mce#%d: %s error %s on label \"%s\" (%s %s %s)",
		  __entry->mc_index,
		  (__entry->err_type == HW_EVENT_ERR_CORRECTED) ? "Corrected" :
			((__entry->err_type == HW_EVENT_ERR_FATAL) ?
			"Fatal" : "Uncorrected"),
		  __get_str(msg),
		  __get_str(label),
		  __get_str(location),
		  __get_str(detail),
		  __get_str(driver_detail))
);

#endif /* _TRACE_HW_EVENT_MC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

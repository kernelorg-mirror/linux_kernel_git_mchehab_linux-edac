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

	TP_printk(HW_ERR "mce#%d %s: %s=%d is not between %d and %d",
		__entry->mc_index,
		__get_str(type),
		__get_str(field),
		__entry->invalid_val,
		__entry->min,
		__entry->max)
);

/*
 * X86 arch-specific events
 */

#ifdef CONFIG_X86
#include <asm/mce.h>

/*
 * Generic MCE event
 */
TRACE_EVENT(mce_record,

	TP_PROTO(const struct mce *m),

	TP_ARGS(m),

	TP_STRUCT__entry(
		__field(	u64,		mcgcap		)
		__field(	u64,		mcgstatus	)
		__field(	u64,		status		)
		__field(	u64,		addr		)
		__field(	u64,		misc		)
		__field(	u64,		ip		)
		__field(	u64,		tsc		)
		__field(	u64,		walltime	)
		__field(	u32,		cpu		)
		__field(	u32,		cpuid		)
		__field(	u32,		apicid		)
		__field(	u32,		socketid	)
		__field(	u8,		cs		)
		__field(	u8,		bank		)
		__field(	u8,		cpuvendor	)
	),

	TP_fast_assign(
		__entry->mcgcap		= m->mcgcap;
		__entry->mcgstatus	= m->mcgstatus;
		__entry->status		= m->status;
		__entry->addr		= m->addr;
		__entry->misc		= m->misc;
		__entry->ip		= m->ip;
		__entry->tsc		= m->tsc;
		__entry->walltime	= m->time;
		__entry->cpu		= m->extcpu;
		__entry->cpuid		= m->cpuid;
		__entry->apicid		= m->apicid;
		__entry->socketid	= m->socketid;
		__entry->cs		= m->cs;
		__entry->bank		= m->bank;
		__entry->cpuvendor	= m->cpuvendor;
	),

	TP_printk("CPU: %d, MCGc/s: %llx/%llx, MC%d: %016Lx, ADDR/MISC: %016Lx/%016Lx, RIP: %02x:<%016Lx>, TSC: %llx, PROCESSOR: %u:%x, TIME: %llu, SOCKET: %u, APIC: %x",
		__entry->cpu,
		__entry->mcgcap, __entry->mcgstatus,
		__entry->bank, __entry->status,
		__entry->addr, __entry->misc,
		__entry->cs, __entry->ip,
		__entry->tsc,
		__entry->cpuvendor, __entry->cpuid,
		__entry->walltime,
		__entry->socketid,
		__entry->apicid)
);

/*
 * MCE event for memory-controller errors
 */
TRACE_EVENT(mc_error_mce,

	TP_PROTO(const unsigned int err_type,
		 const unsigned int mc_index,
		 const char *msg,
		 const char *label,
		 const char *location,
		 const char *detail,
		 const char *driver_detail,
		 const struct mce *m),

	TP_ARGS(err_type, mc_index, msg, label, location,
		detail, driver_detail, m),

	TP_STRUCT__entry(
		__field(	unsigned int,	err_type	)
		__field(	unsigned int,	mc_index	)
		__string(	msg,		msg		)
		__string(	label,		label		)
		__string(	detail,		detail		)
		__string(	location,	location	)
		__string(	driver_detail,	driver_detail	)
		__field(	u64,		mcgcap		)
		__field(	u64,		mcgstatus	)
		__field(	u64,		status		)
		__field(	u64,		addr		)
		__field(	u64,		misc		)
		__field(	u64,		ip		)
		__field(	u64,		tsc		)
		__field(	u64,		walltime	)
		__field(	u32,		cpu		)
		__field(	u32,		cpuid		)
		__field(	u32,		apicid		)
		__field(	u32,		socketid	)
		__field(	u8,		cs		)
		__field(	u8,		bank		)
		__field(	u8,		cpuvendor	)
	),

	TP_fast_assign(
		__entry->err_type	= err_type;
		__entry->mc_index	= mc_index;
		__assign_str(msg, msg);
		__assign_str(label, label);
		__assign_str(location, location);
		__assign_str(detail, detail);
		__assign_str(driver_detail, driver_detail);
		__entry->mcgcap		= m->mcgcap;
		__entry->mcgstatus	= m->mcgstatus;
		__entry->status		= m->status;
		__entry->addr		= m->addr;
		__entry->misc		= m->misc;
		__entry->ip		= m->ip;
		__entry->tsc		= m->tsc;
		__entry->walltime	= m->time;
		__entry->cpu		= m->extcpu;
		__entry->cpuid		= m->cpuid;
		__entry->apicid		= m->apicid;
		__entry->socketid	= m->socketid;
		__entry->cs		= m->cs;
		__entry->bank		= m->bank;
		__entry->cpuvendor	= m->cpuvendor;
	),

	TP_printk("mce#%d: %s error %s on label \"%s\" (%s %s CPU: %d, MCGc/s: %llx/%llx, MC%d: %016Lx, ADDR/MISC: %016Lx/%016Lx, RIP: %02x:<%016Lx>, TSC: %llx, PROCESSOR: %u:%x, TIME: %llu, SOCKET: %u, APIC: %x %s)",
		  __entry->mc_index,
		  (__entry->err_type == HW_EVENT_ERR_CORRECTED) ? "Corrected" :
			((__entry->err_type == HW_EVENT_ERR_FATAL) ?
			"Fatal" : "Uncorrected"),
		  __get_str(msg),
		  __get_str(label),
		  __get_str(location),
		  __get_str(detail),
		  __entry->cpu,
		  __entry->mcgcap, __entry->mcgstatus,
		  __entry->bank, __entry->status,
		  __entry->addr, __entry->misc,
		  __entry->cs, __entry->ip,
		  __entry->tsc,
		  __entry->cpuvendor, __entry->cpuid,
		  __entry->walltime,
		  __entry->socketid,
		  __entry->apicid,
		  __get_str(driver_detail))
);

TRACE_EVENT(mc_out_of_range_mce,
	TP_PROTO(struct mem_ctl_info *mci, const char *type, const char *field,
		int invalid_val, int min, int max, const struct mce *m),

	TP_ARGS(mci, type, field, invalid_val, min, max, m),

	TP_STRUCT__entry(
		__string(	type,		type		)
		__string(	field,		field		)
		__field(	unsigned int,	mc_index	)
		__field(	int,		invalid_val	)
		__field(	int,		min		)
		__field(	int,		max		)
		__field(	u64,		mcgcap		)
		__field(	u64,		mcgstatus	)
		__field(	u64,		status		)
		__field(	u64,		addr		)
		__field(	u64,		misc		)
		__field(	u64,		ip		)
		__field(	u64,		tsc		)
		__field(	u64,		walltime	)
		__field(	u32,		cpu		)
		__field(	u32,		cpuid		)
		__field(	u32,		apicid		)
		__field(	u32,		socketid	)
		__field(	u8,		cs		)
		__field(	u8,		bank		)
		__field(	u8,		cpuvendor	)
	),

	TP_fast_assign(
		__assign_str(type, type);
		__assign_str(field, field);
		__entry->mc_index	= mci->mc_idx;
		__entry->invalid_val	= invalid_val;
		__entry->min		= min;
		__entry->max		= max;
		__entry->mcgcap		= m->mcgcap;
		__entry->mcgstatus	= m->mcgstatus;
		__entry->status		= m->status;
		__entry->addr		= m->addr;
		__entry->misc		= m->misc;
		__entry->ip		= m->ip;
		__entry->tsc		= m->tsc;
		__entry->walltime	= m->time;
		__entry->cpu		= m->extcpu;
		__entry->cpuid		= m->cpuid;
		__entry->apicid		= m->apicid;
		__entry->socketid	= m->socketid;
		__entry->cs		= m->cs;
		__entry->bank		= m->bank;
		__entry->cpuvendor	= m->cpuvendor;
	),

	TP_printk(HW_ERR "mce#%d %s: %s=%d is not between %d and %d (CPU: %d, MCGc/s: %llx/%llx, MC%d: %016Lx, ADDR/MISC: %016Lx/%016Lx, RIP: %02x:<%016Lx>, TSC: %llx, PROCESSOR: %u:%x, TIME: %llu, SOCKET: %u, APIC: %x)",
		  __entry->mc_index,
		  __get_str(type),
		  __get_str(field),
		  __entry->invalid_val,
		  __entry->min,
		  __entry->max,
		  __entry->cpu,
		  __entry->mcgcap, __entry->mcgstatus,
		  __entry->bank, __entry->status,
		  __entry->addr, __entry->misc,
		  __entry->cs, __entry->ip,
		  __entry->tsc,
		  __entry->cpuvendor, __entry->cpuid,
		  __entry->walltime,
		  __entry->socketid,
		  __entry->apicid)
);

#endif


#endif /* _TRACE_HW_EVENT_MC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

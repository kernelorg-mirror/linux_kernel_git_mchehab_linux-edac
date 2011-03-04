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
		__field(	const char *,	type			)
		__field(	unsigned int,	instance		)
	),

	TP_fast_assign(
		__entry->type	= type;
		__entry->instance = instance;
	),

	TP_printk("Initialized %s#%d\n",
		__entry->type,
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
TRACE_EVENT(mc_corrected_error,

	TP_PROTO(struct mem_ctl_info *mci,
		unsigned long page_frame_number,
		unsigned long offset_in_page, unsigned long syndrome,
		int row, int channel, const char *msg),

	TP_ARGS(mci, page_frame_number, offset_in_page, syndrome, row,
		channel, msg),

	TP_STRUCT__entry(
		__field(	unsigned int,	mc_index		)
		__field(	unsigned long,	page_frame_number	)
		__field(	unsigned long,	offset_in_page		)
		__field(	u32,		grain			)
		__field(	unsigned long,	syndrome		)
		__field(	int,		row			)
		__field(	int,		channel			)
		__field(	const char *,	label			)
		__field(	const char *,	msg			)
	),

	TP_fast_assign(
		__entry->mc_index		= mci->mc_idx;
		__entry->page_frame_number	= page_frame_number;
		__entry->offset_in_page		= offset_in_page;
		__entry->grain			= mci->csrows[row].grain;
		__entry->syndrome		= syndrome;
		__entry->row			= row;
		__entry->channel		= channel;
		__entry->label			= mci->csrows[row].channels[channel].label;
		__entry->msg			= msg;
	),

	TP_printk(HW_ERR "mce#%d: Corrected error %s on label \"%s\" "
			 "(page 0x%lux, offset 0x%lux, grain %ud, "
			 "syndrome 0x%lux, row %d, channel %d)\n",
		__entry->mc_index,
		__entry->msg,
		__entry->label,
		__entry->page_frame_number,
		__entry->offset_in_page,
		__entry->grain,
		__entry->syndrome,
		__entry->row,
		__entry->channel)
);

TRACE_EVENT(mc_uncorrected_error,

	TP_PROTO(struct mem_ctl_info *mci,
		unsigned long page_frame_number,
		unsigned long offset_in_page,
		int row, const char *msg, const char *label),

	TP_ARGS(mci, page_frame_number, offset_in_page,
		row, msg, label),

	TP_STRUCT__entry(
		__field(	unsigned int,	mc_index		)
		__field(	unsigned long,	page_frame_number	)
		__field(	unsigned long,	offset_in_page		)
		__field(	u32,		grain			)
		__field(	int,		row			)
		__field(	const char *,	msg			)
		__field(	const char *,	label			)
	),

	TP_fast_assign(
		__entry->mc_index		= mci->mc_idx;
		__entry->page_frame_number	= page_frame_number;
		__entry->offset_in_page		= offset_in_page;
		__entry->grain			= mci->csrows[row].grain;
		__entry->row			= row;
		__entry->msg			= msg;
		__entry->label			= label;
	),

	TP_printk(HW_ERR "mce#%d: Uncorrected error %s on label \"%s\""
			 "(page 0x%lux, offset 0x%lux, grain %ud, row %d)\n",
		__entry->mc_index,
		__entry->msg,
		__entry->label,
		__entry->page_frame_number,
		__entry->offset_in_page,
		__entry->grain,
		__entry->row)
);


/*
 * Fully-Buffered memory hardware in general don't provide syndrome/grain/row
 * information for all types of errors. So, we need to either have another
 * trace event or add a bitmapped field to indicate that some info are not
 * provided and use the previously-declared event. It seemed easier and less
 * confusing to create a different event for such cases
 */
TRACE_EVENT(mc_corrected_error_fbd,

	TP_PROTO(struct mem_ctl_info *mci,
		int row, int channel, const char *msg),

	TP_ARGS(mci, row, channel, msg),

	TP_STRUCT__entry(
		__field(	unsigned int,	mc_index		)
		__field(	int,		row			)
		__field(	int,		channel			)
		__field(	const char *,	label			)
		__field(	const char *,	msg			)
	),

	TP_fast_assign(
		__entry->mc_index		= mci->mc_idx;
		__entry->row			= row;
		__entry->channel		= channel;
		__entry->label			= mci->csrows[row].channels[channel].label;
		__entry->msg			= msg;
	),

	TP_printk(HW_ERR "mce#%d: Corrected Error %s on label \"%s\" "
			 "(row %d, channel %d)\n",
		__entry->mc_index,
		__entry->msg,
		__entry->label,
		__entry->row,
		__entry->channel)
);

TRACE_EVENT(mc_uncorrected_error_fbd,

	TP_PROTO(struct mem_ctl_info *mci,
		int row, int channela, int channelb,
		const char *msg, const char *label),

	TP_ARGS(mci, row, channela, channelb, msg, label),

	TP_STRUCT__entry(
		__field(	unsigned int,	mc_index		)
		__field(	int,		row			)
		__field(	int,		channela		)
		__field(	int,		channelb		)
		__field(	const char *,	msg			)
		__field(	const char *,	label			)
	),

	TP_fast_assign(
		__entry->mc_index		= mci->mc_idx;
		__entry->row			= row;
		__entry->channela		= channela;
		__entry->channelb		= channelb;
		__entry->msg			= msg;
		__entry->label			= label;
	),

	TP_printk(HW_ERR "mce#%d: Uncorrected Error %s on label \"%s\" "
			 "(row %d, channels: %d, %d)\n",
		__entry->mc_index,
		__entry->msg,
		__entry->label,
		__entry->row,
		__entry->channela,
		__entry->channelb)
);

/*
 * The Memory controller driver needs to discover the memory topology, in
 * order to associate a hardware error with the memory label. If, for any
 * reason, it receives an error for a channel or row that are not supposed
 * to be there, an error event needs to be generated to indicate:
 *	- that a Corrected or Uncorrected error was received;
 *	- that the driver has a bug and, for that particular hardware, was
 *	  not capable of detecting the hardware architecture
 * If one of such errors is ever received, a bug to the kernel driver must
 * be filled.
 */

TRACE_EVENT(mc_out_of_range,
	TP_PROTO(struct mem_ctl_info *mci, const char *type, const char *field,
		int invalid_val, int min, int max),

	TP_ARGS(mci, type, field, invalid_val, min, max),

	TP_STRUCT__entry(
		__field(	const char *,	type			)
		__field(	const char *,	field			)
		__field(	unsigned int,	mc_index		)
		__field(	int,		invalid_val		)
		__field(	int,		min			)
		__field(	int,		max			)
	),

	TP_fast_assign(
		__entry->type			= type;
		__entry->field			= field;
		__entry->mc_index		= mci->mc_idx;
		__entry->invalid_val		= invalid_val;
		__entry->min			= min;
		__entry->max			= max;
	),

	TP_printk(HW_ERR "mce#%d %s: %s=%d is not between %d and %d\n",
		__entry->mc_index,
		__entry->type,
		__entry->field,
		__entry->invalid_val,
		__entry->min,
		__entry->max)
);

/*
 * On some cases, a corrected or uncorrected error was detected, but it
 * couldn't be properly handled, or because another error overrided the
 * error registers that details the error or because of some internal problem
 * on the driver. Those events bellow are meant for those error types.
 */
TRACE_EVENT(mc_corrected_error_no_info,
	TP_PROTO(struct mem_ctl_info *mci, const char *msg),

	TP_ARGS(mci, msg),

	TP_STRUCT__entry(
		__field(	const char *,	msg			)
		__field(	unsigned int,	mc_index		)
	),

	TP_fast_assign(
		__entry->msg			= msg;
		__entry->mc_index		= mci->mc_idx;
	),

	TP_printk(HW_ERR "mce#%d: Corrected Error: %s\n",
		__entry->mc_index,
		__entry->msg)
);

TRACE_EVENT(mc_uncorrected_error_no_info,
	TP_PROTO(struct mem_ctl_info *mci, const char *msg),

	TP_ARGS(mci, msg),

	TP_STRUCT__entry(
		__field(	const char *,	msg			)
		__field(	unsigned int,	mc_index		)
	),

	TP_fast_assign(
		__entry->msg			= msg;
		__entry->mc_index		= mci->mc_idx;
	),

	TP_printk(HW_ERR "mce#%d: Uncorrected Error: %s\n",
		__entry->mc_index,
		__entry->msg)
);



/*
 * MCE Events placeholder. Please add non-memory events that come from the
 * MCE driver here
 */


#endif /* _TRACE_HW_EVENT_MC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

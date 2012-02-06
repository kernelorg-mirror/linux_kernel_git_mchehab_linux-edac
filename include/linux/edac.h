/*
 * Generic EDAC defs
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2006-2008 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */
#ifndef _LINUX_EDAC_H_
#define _LINUX_EDAC_H_

/*
 * Concepts used at the EDAC subsystem
 *
 * There are several things to be aware of that aren't at all obvious:
 *
 * SOCKETS, SOCKET SETS, BANKS, ROWS, CHIP-SELECT ROWS, CHANNELS, etc..
 *
 * These are some of the many terms that are thrown about that don't always
 * mean what people think they mean (Inconceivable!).  In the interest of
 * creating a common ground for discussion, terms and their definitions
 * will be established.
 *
 * Memory devices:	The individual DRAM chips on a memory stick.  These
 *			devices commonly output 4 and 8 bits each (x4, x8).
 *			Grouping several of these in parallel provides the
 *			number of bits that the memory controller expects:
 *			typically 72 bits, in order to provide 64 bits of ECC
 *			corrected data.
 *
 * Memory Stick:	A printed circuit board that aggregates multiple
 *			memory devices in parallel.  In general, this is the
 *			First replaceable unit (FRU) that the final consumer
 *			cares to replace. It is typically encapsulated as DIMMs
 *
 * Socket:		A physical connector on the motherboard that accepts
 *			a single memory stick.
 *
 * Branch:		The highest hierarchy on a Fully-Buffered DIMM memory
 *			controller. Typically, it contains two channels.
 *			Two channels at the same branch can be used in single
 *			mode or in lockstep mode.
 *			When lockstep is enabled, the cache line is higher,
 *			but it generally brings some performance penalty.
 *			Also, it is generally not possible to point to just one
 *			memory stick when an error occurs, as the error
 *			correction code is calculated using two dimms instead
 *			of one. Due to that, it is capable of correcting more
 *			errors than on single mode.
 *
 * Channel:		A memory controller channel, responsible to communicate
 *			with a group of DIMM's. Each channel has its own
 *			independent control (command) and data bus, and can
 *			be used independently or grouped.
 *
 * Single-channel:	The data accessed by the memory controller is contained
 *			into one dimm only. E. g. if the data is 64 bits-wide,
 *			the data flows to the CPU using one 64 bits parallel
 *			access.
 *			Typically used with SDR, DDR, DDR2 and DDR3 memories.
 *			FB-DIMM and RAMBUS use a different concept for channel,
 *			so this concept doesn't apply there.
 *
 * Double-channel:	The data size accessed by the memory controller is
 *			contained into two dimms accessed at the same time.
 *			E. g. if the DIMM is 64 bits-wide, the data flows to
 *			the CPU using a 128 bits parallel access.
 *			Typically used with SDR, DDR, DDR2 and DDR3 memories.
 *			FB-DIMM and RAMBUS uses a different concept for channel,
 *			so this concept doesn't apply there.
 *
 * Chip-select row:	This is the name of the memory controller signal used
 *			to select the DRAM chips to be used. It may not be
 *			visible by the memory controller, as some memory buffer
 *			chip may be responsible to control it.
 *			On devices where it is visible, it controls the DIMM
 *			(or the DIMM pair, in dual-channel mode) that is
 *			accessed by the memory controller.
 *
 * Single-Ranked stick:	A Single-ranked stick has 1 chip-select row of memory.
 *			Motherboards commonly drive two chip-select pins to
 *			a memory stick. A single-ranked stick, will occupy
 *			only one of those rows. The other will be unused.
 *
 * Double-Ranked stick:	A double-ranked stick has two chip-select rows which
 *			access different sets of memory devices.  The two
 *			rows cannot be accessed concurrently.
 *
 * Double-sided stick:	DEPRECATED TERM, see Double-Ranked stick.
 *			A double-sided stick has two chip-select rows which
 *			access different sets of memory devices.  The two
 *			rows cannot be accessed concurrently.  "Double-sided"
 *			is irrespective of the memory devices being mounted
 *			on both sides of the memory stick.
 *
 * Socket set:		All of the memory sticks that are required for
 *			a single memory access or all of the memory sticks
 *			spanned by a chip-select row.  A single socket set
 *			has two chip-select rows and if double-sided sticks
 *			are used these will occupy those chip-select rows.
 *
 * Bank:		This term is avoided because it is unclear when
 *			needing to distinguish between chip-select rows and
 *			socket sets.
 *
 * Controller pages:
 *
 * Physical pages:
 *
 * Virtual pages:
 *
 *
 * STRUCTURE ORGANIZATION AND CHOICES
 *
 *
 *
 * PS - I enjoyed writing all that about as much as you enjoyed reading it.
 */

#include <linux/atomic.h>
#include <linux/sysdev.h>

#define EDAC_OPSTATE_INVAL	-1
#define EDAC_OPSTATE_POLL	0
#define EDAC_OPSTATE_NMI	1
#define EDAC_OPSTATE_INT	2

extern int edac_op_state;
extern int edac_err_assert;
extern atomic_t edac_handlers;
extern struct sysdev_class edac_class;

extern int edac_handler_set(void);
extern void edac_atomic_assert_error(void);
extern struct sysdev_class *edac_get_sysfs_class(void);
extern void edac_put_sysfs_class(void);

static inline void opstate_init(void)
{
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_NMI:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_POLL;
	}
	return;
}

#define EDAC_MC_LABEL_LEN	31
#define MC_PROC_NAME_MAX_LEN	7

/**
 * enum dev_type - describe the type of memory DRAM chips used at the stick
 * @DEV_UNKNOWN:	Can't be determined, or MC doesn't support detect it
 * @DEV_X1:		1 bit for data
 * @DEV_X2:		2 bits for data
 * @DEV_X4:		4 bits for data
 * @DEV_X8:		8 bits for data
 * @DEV_X16:		16 bits for data
 * @DEV_X32:		32 bits for data
 * @DEV_X64:		64 bits for data
 *
 * Typical values are x4 and x8.
 */
enum dev_type {
	DEV_UNKNOWN = 0,
	DEV_X1,
	DEV_X2,
	DEV_X4,
	DEV_X8,
	DEV_X16,
	DEV_X32,		/* Do these parts exist? */
	DEV_X64			/* Do these parts exist? */
};

#define DEV_FLAG_UNKNOWN	BIT(DEV_UNKNOWN)
#define DEV_FLAG_X1		BIT(DEV_X1)
#define DEV_FLAG_X2		BIT(DEV_X2)
#define DEV_FLAG_X4		BIT(DEV_X4)
#define DEV_FLAG_X8		BIT(DEV_X8)
#define DEV_FLAG_X16		BIT(DEV_X16)
#define DEV_FLAG_X32		BIT(DEV_X32)
#define DEV_FLAG_X64		BIT(DEV_X64)

/**
 * enum hw_event_mc_err_type - type of the detected error
 *
 * @HW_EVENT_ERR_CORRECTED:	Corrected Error - Indicates that an ECC
 * 				corrected error was detected
 * @HW_EVENT_ERR_UNCORRECTED:	Uncorrected Error - Indicates an error that
 *				can't be corrected by ECC, but it is not
 *				factal (maybe it is on an unused memory area,
 *				or the memory controller could recover from
 *				it for example, by re-trying the operation).
 * @HW_EVENT_ERR_FATAL:		Fatal Error - Uncorrected error that could not
 *				be recovered.
 */
enum hw_event_mc_err_type {
	HW_EVENT_ERR_CORRECTED,
	HW_EVENT_ERR_UNCORRECTED,
	HW_EVENT_ERR_FATAL,
};

/**
 * enum hw_event_error_scope - escope of a memory error
 *
 * @HW_EVENT_ERR_MC:		error can be anywhere inside the MC
 * @HW_EVENT_SCOPE_MC_BRANCH:	error can be on any DIMM inside the branch
 * @HW_EVENT_SCOPE_MC_CHANNEL:	error can be on any DIMM inside the MC channel
 * @HW_EVENT_SCOPE_MC_DIMM:	error is on a specific DIMM
 * @HW_EVENT_SCOPE_MC_CSROW:	error can be on any DIMM inside the csrow
 * @HW_EVENT_SCOPE_MC_CSROW_CHANNEL: error is on a CSROW channel
 *
 * Depending on the error detection algorithm, the memory topology and even
 * the MC capabilities, some errors can't be attributed to just one DIMM, but
 * to a group of memory sockets. Depending on where the error occurs, the
 * EDAC core will increment the corresponding error count for that entity,
 * and the upper entities. For example, assuming a system with 1 memory
 * controller 2 branches, 2 MC channels and 4 DIMMS on it, if an error
 * happens at channel 0, the error counts for channel 0, for branch 0 and
 * for the memory controller 0 will be incremented. The DIMM error counts won't
 * be incremented, as, in this example, the driver can't be 100% sure on what
 * memory the error actually occurred.
 *
 * The order here is important, as edac_mc_handle_error() will use it, in order
 * to check what parameters will be used. The smallest number should be
 * the hole memory controller, and the last one should be the more
 * fine-grained detail, e. g.: DIMM.
 */
enum hw_event_error_scope {
	HW_EVENT_SCOPE_MC,
	HW_EVENT_SCOPE_MC_BRANCH,
	HW_EVENT_SCOPE_MC_CHANNEL,
	HW_EVENT_SCOPE_MC_DIMM,
	HW_EVENT_SCOPE_MC_CSROW,
	HW_EVENT_SCOPE_MC_CSROW_CHANNEL,
};

/**
 * enum mem_type - Type of the memory stick
 *
 * @MEM_EMPTY		Empty csrow
 * @MEM_RESERVED:	Reserved csrow type
 * @MEM_UNKNOWN:	Unknown csrow type
 * @MEM_FPM:		Fast page mode
 *			An old asyncronous memory technology, encapsulated as
 *			SIMM, popularly used between 1987-1995.
 * @MEM_EDO:		Extended data out
 *			Asynchronous memory technology used on early Pentium's
 *			(1995-1998).
 * @MEM_BEDO:		Burst Extended data out
 *			EDO memories with performance improvement gains.
 * @MEM_SDR:		Single data rate SDRAM
 *			First types of syncronous ram specified by JEDEC
 *			popular between 1998-2002. The JEDEC standards defined
 *			3 types of sticks: PC66, PC100 and PC133. There were
 *			also some non-official overclocked sticks like PC150
 *			and PC166.
 *			There are 3 pins for chip select: Pins 0 and 2 are
 *			for rank 0; pins 1 and 3 are for rank 1, if the memory
 *			is dual-rank.
 * @MEM_RDR:		Registered single data rate SDRAM
 * @MEM_DDR:		Double data rate SDRAM, used between 2002 and 2005.
 *			The JEDEC standards defined sticks PC1600, PC2100,
 *			PC2700 and PC3200. Non-official overclocked sticks
 *			also exists.
 *			On DDR memories, there are one or two channels. A
 *			single-channel mode means that one x72 ECC dimm is
 *			accessed, in order to provide 64 bits of data. On
 *			dual-channel mode, two dimm's are used simultaneously,
 *			in order to provide a 128 bits of data. The "cschannel"
 *			concept used on EDAC refers to such channel type.
 * @MEM_RDDR:		Registered Double data rate SDRAM
 *			This is a variant of the DDR memories.
 *			A registered memory has a buffer inside it, hiding
 *			part of the memory details to the memory controller.
 * @MEM_RMBS:		Rambus DRAM
 *			Rambus uses a high-speed multi-drop serial bus to
 *			communicate with each RDRAM chip, used on some
 *			machines between 2000-2002 (Pentium III and IV).
 * @MEM_DDR2:		DDR2 RAM, as described at JEDEC JESD79-2F.
 *			Those memories are labed as "PC2-" instead of "PC" to
 *			differenciate from DDR.
 * @MEM_FB_DDR2:	Fully-Buffered DDR2, as described at JEDEC Std No. 205
 *			and JESD206.
 *			A FB-DIMM channel consists of 14 unidirectional signal
 *			pairs (Northbound path) from the memories to the MC
 *			and 10 unidirectional signal pairs (Southbond path)
 *			from the MC to the DIMM's.
 *			those memories are x72 ECC DIMMs. Up to 8 DIMMs can
 *			be connected per channel. When used with 128 bits,
 *			two channels are needed. The grouping of those two
 *			channels is called "branch".
 * @MEM_RDDR2:		Registered DDR2 RAM
 *			This is a variant of the DDR2 memories.
 * @MEM_XDR:		Rambus XDR
 *			It is an evolution of the original RAMBUS memories,
 *			created to compete with DDR2. Weren't used on any
 *			x86 arch, but cell_edac PPC memory controller uses it.
 * @MEM_DDR3:		DDR3 RAM
 *			Those memories are labed as "PC3-" to differenciate
 *			from DDR and DDR2.
 * @MEM_RDDR3:		Registered DDR3 RAM
 *			This is a variant of the DDR3 memories.
 */
enum mem_type {
	MEM_EMPTY = 0,
	MEM_RESERVED,
	MEM_UNKNOWN,
	MEM_FPM,
	MEM_EDO,
	MEM_BEDO,
	MEM_SDR,
	MEM_RDR,
	MEM_DDR,
	MEM_RDDR,
	MEM_RMBS,
	MEM_DDR2,
	MEM_FB_DDR2,
	MEM_RDDR2,
	MEM_XDR,
	MEM_DDR3,
	MEM_RDDR3,
};

#define MEM_FLAG_EMPTY		BIT(MEM_EMPTY)
#define MEM_FLAG_RESERVED	BIT(MEM_RESERVED)
#define MEM_FLAG_UNKNOWN	BIT(MEM_UNKNOWN)
#define MEM_FLAG_FPM		BIT(MEM_FPM)
#define MEM_FLAG_EDO		BIT(MEM_EDO)
#define MEM_FLAG_BEDO		BIT(MEM_BEDO)
#define MEM_FLAG_SDR		BIT(MEM_SDR)
#define MEM_FLAG_RDR		BIT(MEM_RDR)
#define MEM_FLAG_DDR		BIT(MEM_DDR)
#define MEM_FLAG_RDDR		BIT(MEM_RDDR)
#define MEM_FLAG_RMBS		BIT(MEM_RMBS)
#define MEM_FLAG_DDR2           BIT(MEM_DDR2)
#define MEM_FLAG_FB_DDR2        BIT(MEM_FB_DDR2)
#define MEM_FLAG_RDDR2          BIT(MEM_RDDR2)
#define MEM_FLAG_XDR            BIT(MEM_XDR)
#define MEM_FLAG_DDR3		 BIT(MEM_DDR3)
#define MEM_FLAG_RDDR3		 BIT(MEM_RDDR3)

/** enum edac-type - Error Detection and Correction capabilities and mode
 * @EDAC_UNKNOWN:	Unknown if ECC is available
 * @EDAC_NONE:		Doesn't support ECC
 * @EDAC_RESERVED:	Reserved ECC type
 * @EDAC_PARITY:	Detects parity errors
 * @EDAC_EC:		Error Checking - no correction
 * @EDAC_SECDED:	Single bit error correction, Double detection
 * @EDAC_S2ECD2ED:	Chipkill x2 devices - do these exist?
 * @EDAC_S4ECD4ED:	Chipkill x4 devices
 * @EDAC_S8ECD8ED:	Chipkill x8 devices
 * @EDAC_S16ECD16ED:	Chipkill x16 devices
 */
enum edac_type {
	EDAC_UNKNOWN =	0,
	EDAC_NONE,
	EDAC_RESERVED,
	EDAC_PARITY,
	EDAC_EC,
	EDAC_SECDED,
	EDAC_S2ECD2ED,
	EDAC_S4ECD4ED,
	EDAC_S8ECD8ED,
	EDAC_S16ECD16ED,
};

#define EDAC_FLAG_UNKNOWN	BIT(EDAC_UNKNOWN)
#define EDAC_FLAG_NONE		BIT(EDAC_NONE)
#define EDAC_FLAG_PARITY	BIT(EDAC_PARITY)
#define EDAC_FLAG_EC		BIT(EDAC_EC)
#define EDAC_FLAG_SECDED	BIT(EDAC_SECDED)
#define EDAC_FLAG_S2ECD2ED	BIT(EDAC_S2ECD2ED)
#define EDAC_FLAG_S4ECD4ED	BIT(EDAC_S4ECD4ED)
#define EDAC_FLAG_S8ECD8ED	BIT(EDAC_S8ECD8ED)
#define EDAC_FLAG_S16ECD16ED	BIT(EDAC_S16ECD16ED)

/** enum scrub_type - scrubbing capabilities
 * @SCRUB_UNKNOWN		Unknown if scrubber is available
 * @SCRUB_NONE:			No scrubber
 * @SCRUB_SW_PROG:		SW progressive (sequential) scrubbing
 * @SCRUB_SW_SRC:		Software scrub only errors
 * @SCRUB_SW_PROG_SRC:		Progressive software scrub from an error
 * @SCRUB_SW_TUNABLE:		Software scrub frequency is tunable
 * @SCRUB_HW_PROG:		HW progressive (sequential) scrubbing
 * @SCRUB_HW_SRC:		Hardware scrub only errors
 * @SCRUB_HW_PROG_SRC:		Progressive hardware scrub from an error
 * SCRUB_HW_TUNABLE:		Hardware scrub frequency is tunable
 */
enum scrub_type {
	SCRUB_UNKNOWN =	0,
	SCRUB_NONE,
	SCRUB_SW_PROG,
	SCRUB_SW_SRC,
	SCRUB_SW_PROG_SRC,
	SCRUB_SW_TUNABLE,
	SCRUB_HW_PROG,
	SCRUB_HW_SRC,
	SCRUB_HW_PROG_SRC,
	SCRUB_HW_TUNABLE
};

#define SCRUB_FLAG_SW_PROG	BIT(SCRUB_SW_PROG)
#define SCRUB_FLAG_SW_SRC	BIT(SCRUB_SW_SRC)
#define SCRUB_FLAG_SW_PROG_SRC	BIT(SCRUB_SW_PROG_SRC)
#define SCRUB_FLAG_SW_TUN	BIT(SCRUB_SW_SCRUB_TUNABLE)
#define SCRUB_FLAG_HW_PROG	BIT(SCRUB_HW_PROG)
#define SCRUB_FLAG_HW_SRC	BIT(SCRUB_HW_SRC)
#define SCRUB_FLAG_HW_PROG_SRC	BIT(SCRUB_HW_PROG_SRC)
#define SCRUB_FLAG_HW_TUN	BIT(SCRUB_HW_TUNABLE)

/* FIXME - should have notify capabilities: NMI, LOG, PROC, etc */

/* EDAC internal operation states */
#define	OP_ALLOC		0x100
#define OP_RUNNING_POLL		0x201
#define OP_RUNNING_INTERRUPT	0x202
#define OP_RUNNING_POLL_INTR	0x203
#define OP_OFFLINE		0x300

/* FIXME: add the proper per-location error counts */
struct dimm_info {
	char label[EDAC_MC_LABEL_LEN + 1];	/* DIMM label on motherboard */

	/* Memory location data */
	int mc_branch;
	int mc_channel;
	int mc_dimm_number;
	int csrow;
	int cschannel;

	struct kobject kobj;		/* sysfs kobject for this csrow */
	struct mem_ctl_info *mci;	/* the parent */

	u32 grain;		/* granularity of reported error in bytes */
	enum dev_type dtype;	/* memory device type */
	enum mem_type mtype;	/* memory dimm type */
	enum edac_type edac_mode;	/* EDAC mode for this dimm */

	u32 nr_pages;			/* number of pages in csrow */
};

struct csrow_channel_info {
	int chan_idx;		/* channel index */
	struct dimm_info *dimm;
	struct csrow_info *csrow;	/* the parent */
};

struct csrow_info {
	int csrow_idx;			/* the chip-select row */

	/* Used only by edac_mc_find_csrow_by_page() */
	unsigned long first_page;	/* first page number in csrow */
	unsigned long last_page;	/* last page number in csrow */
	unsigned long page_mask;	/* used for interleaving -
					 * 0UL for non intlv */

	struct mem_ctl_info *mci;	/* the parent */

	struct kobject kobj;	/* sysfs kobject for this csrow */

	/* channel information for this csrow */
	u32 nr_channels;
	struct csrow_channel_info *channels;
};

struct mcidev_sysfs_group {
	const char *name;				/* group name */
	const struct mcidev_sysfs_attribute *mcidev_attr; /* group attributes */
};

struct mcidev_sysfs_group_kobj {
	struct list_head list;		/* list for all instances within a mc */

	struct kobject kobj;		/* kobj for the group */

	const struct mcidev_sysfs_group *grp;	/* group description table */
	struct mem_ctl_info *mci;	/* the parent */
};

/* mcidev_sysfs_attribute structure
 *	used for driver sysfs attributes and in mem_ctl_info
 * 	sysfs top level entries
 */
struct mcidev_sysfs_attribute {
	/* It should use either attr or grp */
	struct attribute attr;
	const struct mcidev_sysfs_group *grp;	/* Points to a group of attributes */

	/* Ops for show/store values at the attribute - not used on group */
        ssize_t (*show)(struct mem_ctl_info *,char *);
        ssize_t (*store)(struct mem_ctl_info *, const char *,size_t);
};

/*
 * Error counters for all possible memory arrangements
 */
struct error_counts {
	u32 ce_mc;
	u32 *ce_branch;
	u32 *ce_channel;
	u32 *ce_dimm;
	u32 *ce_csrow;
	u32 *ce_cschannel;
	u32 ue_mc;
	u32 *ue_branch;
	u32 *ue_channel;
	u32 *ue_dimm;
	u32 *ue_csrow;
	u32 *ue_cschannel;
};

/* MEMORY controller information structure
 */
struct mem_ctl_info {
	struct list_head link;	/* for global list of mem_ctl_info structs */

	struct module *owner;	/* Module owner of this control struct */

	unsigned long mtype_cap;	/* memory types supported by mc */
	unsigned long edac_ctl_cap;	/* Mem controller EDAC capabilities */
	unsigned long edac_cap;	/* configuration capabilities - this is
				 * closely related to edac_ctl_cap.  The
				 * difference is that the controller may be
				 * capable of s4ecd4ed which would be listed
				 * in edac_ctl_cap, but if channels aren't
				 * capable of s4ecd4ed then the edac_cap would
				 * not have that capability.
				 */
	unsigned long scrub_cap;	/* chipset scrub capabilities */
	enum scrub_type scrub_mode;	/* current scrub mode */

	/* Translates sdram memory scrub rate given in bytes/sec to the
	   internal representation and configures whatever else needs
	   to be configured.
	 */
	int (*set_sdram_scrub_rate) (struct mem_ctl_info * mci, u32 bw);

	/* Get the current sdram memory scrub rate from the internal
	   representation and converts it to the closest matching
	   bandwidth in bytes/sec.
	 */
	int (*get_sdram_scrub_rate) (struct mem_ctl_info * mci);


	/* pointer to edac checking routine */
	void (*edac_check) (struct mem_ctl_info * mci);

	/*
	 * Remaps memory pages: controller pages to physical pages.
	 * For most MC's, this will be NULL.
	 */
	/* FIXME - why not send the phys page to begin with? */
	unsigned long (*ctl_page_to_phys) (struct mem_ctl_info * mci,
					   unsigned long page);
	int mc_idx;
	struct csrow_info *csrows;

	/* Number of allocated memory location data */
	unsigned num_branch;
	unsigned num_channel;
	unsigned num_dimm;
	unsigned num_csrows;
	unsigned num_cschannel;

	/*
	 * DIMM info. Will eventually remove the entire csrows_info some day
	 */
	unsigned tot_dimms;
	struct dimm_info *dimms;

	/*
	 * FIXME - what about controllers on other busses? - IDs must be
	 * unique.  dev pointer should be sufficiently unique, but
	 * BUS:SLOT.FUNC numbers may not be unique.
	 */
	struct device *dev;
	const char *mod_name;
	const char *mod_ver;
	const char *ctl_name;
	const char *dev_name;
	char proc_name[MC_PROC_NAME_MAX_LEN + 1];
	void *pvt_info;
	unsigned long start_time;	/* mci load start time (in jiffies) */

	/* drivers shouldn't access this struct directly */
	struct error_counts err;
	unsigned ce_noinfo_count, ue_noinfo_count;

	struct completion complete;

	/* edac sysfs device control */
	struct kobject edac_mci_kobj;

	/* list for all grp instances within a mc */
	struct list_head grp_kobj_list;

	/* Additional top controller level attributes, but specified
	 * by the low level driver.
	 *
	 * Set by the low level driver to provide attributes at the
	 * controller level.
	 * An array of structures, NULL terminated
	 *
	 * If attributes are desired, then set to array of attributes
	 * If no attributes are desired, leave NULL
	 */
	const struct mcidev_sysfs_attribute *mc_driver_sysfs_attributes;

	/* work struct for this MC */
	struct delayed_work work;

	/* the internal state of this controller instance */
	int op_state;
};

#endif

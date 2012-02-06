/*
 * edac_mc kernel module
 * (C) 2005, 2006 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Thayne Harbaugh
 * Based on work by Dan Hollis <goemon at anime dot net> and others.
 *	http://www.anime.net/~goemon/linux-ecc/
 *
 * Modified by Dave Peterson and Doug Thompson
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sysdev.h>
#include <linux/ctype.h>
#include <linux/edac.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/edac.h>
#include "edac_core.h"
#include "edac_module.h"

#define CREATE_TRACE_POINTS
#include <trace/events/hw_event.h>

/* lock to memory controller's control array */
static DEFINE_MUTEX(mem_ctls_mutex);
static LIST_HEAD(mc_devices);

#ifdef CONFIG_EDAC_DEBUG

static void edac_mc_dump_channel(struct csrow_channel_info *chan)
{
	debugf4("\tchannel = %p\n", chan);
	debugf4("\tchannel->chan_idx = %d\n", chan->chan_idx);
	debugf4("\tchannel->csrow = %p\n\n", chan->csrow);
	debugf4("\tchannel->dimm = %p\n", chan->dimm);
}

static void edac_mc_dump_dimm(struct dimm_info *dimm)
{
	debugf4("\tdimm = %p\n", dimm);
	debugf4("\tdimm->label = '%s'\n", dimm->label);
	debugf4("\tdimm->nr_pages = 0x%x\n", dimm->nr_pages);
	debugf4("\tdimm location %d.%d.%d.%d.%d\n",
		dimm->mc_branch, dimm->mc_channel,
		dimm->mc_dimm_number,
		dimm->csrow, dimm->cschannel);
	debugf4("\tdimm->grain = %d\n", dimm->grain);
	debugf4("\tdimm->nr_pages = 0x%x\n", dimm->nr_pages);
}

static void edac_mc_dump_csrow(struct csrow_info *csrow)
{
	debugf4("\tcsrow = %p\n", csrow);
	debugf4("\tcsrow->csrow_idx = %d\n", csrow->csrow_idx);
	debugf4("\tcsrow->nr_channels = %d\n", csrow->nr_channels);
	debugf4("\tcsrow->channels = %p\n", csrow->channels);
	debugf4("\tcsrow->mci = %p\n\n", csrow->mci);
	debugf4("\tcsrow->first_page = 0x%lx\n", csrow->first_page);
	debugf4("\tcsrow->last_page = 0x%lx\n", csrow->last_page);
	debugf4("\tcsrow->page_mask = 0x%lx\n", csrow->page_mask);
}

static void edac_mc_dump_mci(struct mem_ctl_info *mci)
{
	debugf3("\tmci = %p\n", mci);
	debugf3("\tmci->mtype_cap = %lx\n", mci->mtype_cap);
	debugf3("\tmci->edac_ctl_cap = %lx\n", mci->edac_ctl_cap);
	debugf3("\tmci->edac_cap = %lx\n", mci->edac_cap);
	debugf4("\tmci->edac_check = %p\n", mci->edac_check);
	debugf3("\tmci->num_csrows = %d, csrows = %p\n",
		mci->num_csrows, mci->csrows);
	debugf3("\tmci->nr_dimms = %d, dimns = %p\n",
		mci->tot_dimms, mci->dimms);
	debugf3("\tdev = %p\n", mci->dev);
	debugf3("\tmod_name:ctl_name = %s:%s\n", mci->mod_name, mci->ctl_name);
	debugf3("\tpvt_info = %p\n\n", mci->pvt_info);
}

#endif				/* CONFIG_EDAC_DEBUG */

/*
 * keep those in sync with the enum mem_type
 */
const char *edac_mem_types[] = {
	"Empty csrow",
	"Reserved csrow type",
	"Unknown csrow type",
	"Fast page mode RAM",
	"Extended data out RAM",
	"Burst Extended data out RAM",
	"Single data rate SDRAM",
	"Registered single data rate SDRAM",
	"Double data rate SDRAM",
	"Registered Double data rate SDRAM",
	"Rambus DRAM",
	"Unbuffered DDR2 RAM",
	"Fully buffered DDR2",
	"Registered DDR2 RAM",
	"Rambus XDR",
	"Unbuffered DDR3 RAM",
	"Registered DDR3 RAM",
};
EXPORT_SYMBOL_GPL(edac_mem_types);

/* 'ptr' points to a possibly unaligned item X such that sizeof(X) is 'size'.
 * Adjust 'ptr' so that its alignment is at least as stringent as what the
 * compiler would provide for X and return the aligned result.
 *
 * If 'size' is a constant, the compiler will optimize this whole function
 * down to either a no-op or the addition of a constant to the value of 'ptr'.
 */
void *edac_align_ptr(void **p, unsigned size, int quant)
{
	unsigned align, r;
	void *ptr = *p;

	*p += size * quant;

	/* Here we assume that the alignment of a "long long" is the most
	 * stringent alignment that the compiler will ever provide by default.
	 * As far as I know, this is a reasonable assumption.
	 */
	if (size > sizeof(long))
		align = sizeof(long long);
	else if (size > sizeof(int))
		align = sizeof(long);
	else if (size > sizeof(short))
		align = sizeof(int);
	else if (size > sizeof(char))
		align = sizeof(short);
	else
		return (char *)ptr;

	r = size % align;

	if (r == 0)
		return (char *)ptr;

	*p += align - r;

	return (void *)(((unsigned long)ptr) + align - r);
}

/**
 * edac_mc_alloc: Allocate and partially fills a struct mem_ctl_info structure
 * @edac_index:		Memory controller number
 * @fill_strategy:	csrow/cschannel filling strategy
 * @num_branch:		Number of memory controller branches
 * @num_channel:	Number of memory controller channels
 * @num_dimm:		Number of dimms per memory controller channel
 * @num_csrows:		Number of CWROWS accessed via the memory controller
 * @num_cschannel:	Number of csrows channels
 * @size_pvt:		size of private storage needed
 *
 * This routine supports 3 modes of DIMM mapping:
 *	1) the ones that accesses DRAM's via some bus interface (FB-DIMM
 * and RAMBUS memory controllers) or that don't have chip select view
 *
 * In this case, a branch is generally a group of 2 channels, used generally
 * in  parallel to provide 128 bits data.
 *
 * In the case of FB-DIMMs, the dimm is addressed via the SPD Address
 * input selection, used by the AMB to select the DIMM. The MC channel
 * corresponds to the Memory controller channel bus used to see a series
 * of FB-DIMM's.
 *
 * num_branch, num_channel and num_dimm should point to the real
 *	parameters of the memory controller.
 *
 * The total number of dimms is num_branch * num_channel * num_dimm
 *
 * According with JEDEC No. 205, up to 8 FB-DIMMs are possible per channel. Of
 * course, controllers may have a lower limit.
 *
 * num_csrows/num_cschannel should point to the emulated parameters.
 * The total number of cschannels (num_csrows * num_cschannel) should be a
 * multiple of the total number dimms, e. g:
 *  factor = (num_csrows * num_cschannel)/(num_branch * num_channel * num_dimm)
 * should be an integer (typically: it is 1 or num_cschannel)
 *
 *	2) The MC uses CSROWS/CS CHANNELS to directly select a DRAM chip.
 * One dimm chip exists on every cs channel, for single-rank memories.
 *	num_branch and num_channel should be 0
 *	num_dimm should be the total number of dimms
 *	num_csrows * num_cschannel should be equal to num_dimm
 *
 *	3)The MC uses CSROWS/CS CHANNELS. One dimm chip exists on every
 * csrow. The cs channel is used to indicate the defective chip(s) inside
 * the memory stick.
 *	num_branch and num_channel should be 0
 *	num_dimm should be the total number of dimms
 *	num_csrows should be equal to num_dimm
 *
 * Everything is kmalloc'ed as one big chunk - more efficient.
 * Only can be used if all structures have the same lifetime - otherwise
 * you have to allocate and initialize your own structures.
 *
 * Use edac_mc_free() to free mc structures allocated by this function.
 *
 * Returns:
 *	NULL allocation failed
 *	struct mem_ctl_info pointer
 */
struct mem_ctl_info *edac_mc_alloc(int edac_index,
				   enum edac_alloc_fill_strategy fill_strategy,
				   unsigned num_branch,
				   unsigned num_channel,
				   unsigned num_dimm,
				   unsigned num_csrows,
				   unsigned num_cschannel,
				   unsigned sz_pvt)
{
	void *ptr;
	struct mem_ctl_info *mci;
	struct csrow_info *csi, *csr;
	struct csrow_channel_info *chi, *chp, *chan;
	struct dimm_info *dimm;
	u32 *ce_branch, *ce_channel, *ce_dimm, *ce_csrow, *ce_cschannel;
	u32 *ue_branch, *ue_channel, *ue_dimm, *ue_csrow, *ue_cschannel;
	void *pvt;
	unsigned size, tot_dimms, count, dimm_div;
	int i;
	int err;
	int mc_branch, mc_channel, mc_dimm_number, csrow, cschannel;
	int row, chn;

	/*
	 * While we expect that non-pertinent values will be filled with
	 * 0, in order to provide a way for this routine to detect if the
	 * EDAC is emulating the old sysfs API, we can't actually accept
	 * 0, as otherwise, a multiply by 0 whould hapen.
	 */
	if (num_branch <= 0)
		num_branch = 1;
	if (num_channel <= 0)
		num_channel = 1;
	if (num_dimm <= 0)
		num_dimm = 1;
	if (num_csrows <= 0)
		num_csrows = 1;
	if (num_cschannel <= 0)
		num_cschannel = 1;

	tot_dimms = num_branch * num_channel * num_dimm;
	dimm_div = (num_csrows * num_cschannel) / tot_dimms;
	if (dimm_div == 0) {
		printk(KERN_ERR "%s: dimm_div is wrong: tot_channels/tot_dimms = %d/%d < 1\n",
			__func__, num_csrows * num_cschannel, tot_dimms);
		dimm_div = 1;
	}
	/* FIXME: change it to debug2() at the final version */

	/* Figure out the offsets of the various items from the start of an mc
	 * structure.  We want the alignment of each item to be at least as
	 * stringent as what the compiler would provide if we could simply
	 * hardcode everything into a single struct.
	 */
	ptr = NULL;
	mci = edac_align_ptr(&ptr, sizeof(*mci), 1);
	csi = edac_align_ptr(&ptr, sizeof(*csi), num_csrows);
	chi = edac_align_ptr(&ptr, sizeof(*chi), num_csrows * num_cschannel);
	dimm = edac_align_ptr(&ptr, sizeof(*dimm), tot_dimms);

	count = num_branch;
	ue_branch = edac_align_ptr(&ptr, sizeof(*ce_branch), count);
	ce_branch = edac_align_ptr(&ptr, sizeof(*ce_branch), count);
	count *= num_channel;
	ue_channel = edac_align_ptr(&ptr, sizeof(*ce_channel), count);
	ce_channel = edac_align_ptr(&ptr, sizeof(*ce_channel), count);
	count *= num_dimm;
	ue_dimm = edac_align_ptr(&ptr, sizeof(*ce_dimm), count * num_dimm);
	ce_dimm = edac_align_ptr(&ptr, sizeof(*ce_dimm), count * num_dimm);

	count = num_csrows;
	ue_csrow = edac_align_ptr(&ptr, sizeof(*ce_dimm), count);
	ce_csrow = edac_align_ptr(&ptr, sizeof(*ce_dimm), count);
	count *= num_cschannel;
	ue_cschannel = edac_align_ptr(&ptr, sizeof(*ce_dimm), count);
	ce_cschannel = edac_align_ptr(&ptr, sizeof(*ce_dimm), count);

	pvt = edac_align_ptr(&ptr, sz_pvt, 1);
	size = ((unsigned long)pvt) + sz_pvt;

	debugf1("%s(): allocating %u bytes for mci data\n", __func__, size);
	mci = kzalloc(size, GFP_KERNEL);
	if (mci == NULL)
		return NULL;

	/* Adjust pointers so they point within the memory we just allocated
	 * rather than an imaginary chunk of memory located at address 0.
	 */
	csi = (struct csrow_info *)(((char *)mci) + ((unsigned long)csi));
	chi = (struct csrow_channel_info *)(((char *)mci) + ((unsigned long)chi));
	dimm = (struct dimm_info *)(((char *)mci) + ((unsigned long)dimm));
	pvt = sz_pvt ? (((char *)mci) + ((unsigned long)pvt)) : NULL;

	/* setup index and various internal pointers */
	mci->mc_idx = edac_index;
	mci->csrows = csi;
	mci->dimms  = dimm;
	mci->pvt_info = pvt;

	mci->tot_dimms = tot_dimms;
	mci->num_branch = num_branch;
	mci->num_channel = num_channel;
	mci->num_dimm = num_dimm;
	mci->num_csrows = num_csrows;
	mci->num_cschannel = num_cschannel;

	/*
	 * Fills the dimm struct
	 */
	mc_branch = (num_branch > 0) ? 0 : -1;
	mc_channel = (num_channel > 0) ? 0 : -1;
	mc_dimm_number = (num_dimm > 0) ? 0 : -1;
	if (!num_channel && !num_branch) {
		csrow = (num_csrows > 0) ? 0 : -1;
		cschannel = (num_cschannel > 0) ? 0 : -1;
	} else {
		csrow = -1;
		cschannel = -1;
	}

	debugf4("%s: initializing %d dimms\n", __func__, tot_dimms);
	for (i = 0; i < tot_dimms; i++) {
		dimm = &mci->dimms[i];

		dimm->mc_branch = mc_branch;
		dimm->mc_channel = mc_channel;
		dimm->mc_dimm_number = mc_dimm_number;
		dimm->csrow = csrow;
		dimm->cschannel = cschannel;

		/*
		 * Increment the location
		 * On csrow-emulated devices, csrow/cschannel should be -1
		 */
		if (!num_channel && !num_branch) {
			if (num_cschannel) {
				cschannel = (cschannel + 1) % num_cschannel;
				if (cschannel)
					continue;
			}
			if (num_csrows) {
				csrow = (csrow + 1) % num_csrows;
				if (csrow)
					continue;
			}
		}
		if (num_dimm) {
			mc_dimm_number = (mc_dimm_number + 1) % num_dimm;
			if (mc_dimm_number)
				continue;
		}
		if (num_channel) {
			mc_channel = (mc_channel + 1) % num_channel;
			if (mc_channel)
				continue;
		}
		if (num_branch) {
			mc_branch = (mc_branch + 1) % num_branch;
			if (mc_branch)
				continue;
		}
	}

	/*
	 * Fills the csrows struct
	 *
	 * NOTE: there are two possible memory arrangements here:
	 *
	 *
	 */
	switch (fill_strategy) {
	case EDAC_ALLOC_FILL_CSROW_CSCHANNEL:
		for (row = 0; row < num_csrows; row++) {
			csr = &csi[row];
			csr->csrow_idx = row;
			csr->mci = mci;
			csr->nr_channels = num_cschannel;
			chp = &chi[row * num_cschannel];
			csr->channels = chp;

			for (chn = 0; chn < num_cschannel; chn++) {
				int dimm_idx = (chn + row * num_cschannel) /
						dimm_div;
				debugf4("%s: csrow(%d,%d) = dimm%d\n",
					__func__, row, chn, dimm_idx);
				chan = &chp[chn];
				chan->chan_idx = chn;
				chan->csrow = csr;
				chan->dimm = &dimm[dimm_idx];
			}
		}
	case EDAC_ALLOC_FILL_MCCHANNEL_IS_CSROW:
		for (row = 0; row < num_csrows; row++) {
			csr = &csi[row];
			csr->csrow_idx = row;
			csr->mci = mci;
			csr->nr_channels = num_cschannel;
			chp = &chi[row * num_cschannel];
			csr->channels = chp;

			for (chn = 0; chn < num_cschannel; chn++) {
				int dimm_idx = (chn * num_cschannel + row) /
						dimm_div;
				debugf4("%s: csrow(%d,%d) = dimm%d\n",
					__func__, row, chn, dimm_idx);
				chan = &chp[chn];
				chan->chan_idx = chn;
				chan->csrow = csr;
				chan->dimm = &dimm[dimm_idx];
			}
		}
	case EDAC_ALLOC_FILL_PRIV:
		break;
	}

	mci->op_state = OP_ALLOC;
	INIT_LIST_HEAD(&mci->grp_kobj_list);

	/*
	 * Initialize the 'root' kobj for the edac_mc controller
	 */
	err = edac_mc_register_sysfs_main_kobj(mci);
	if (err) {
		kfree(mci);
		return NULL;
	}

	/* at this point, the root kobj is valid, and in order to
	 * 'free' the object, then the function:
	 *      edac_mc_unregister_sysfs_main_kobj() must be called
	 * which will perform kobj unregistration and the actual free
	 * will occur during the kobject callback operation
	 */

	trace_hw_event_init("mce", (unsigned)edac_index);

	return mci;
}
EXPORT_SYMBOL_GPL(edac_mc_alloc);

/**
 * edac_mc_free
 *	'Free' a previously allocated 'mci' structure
 * @mci: pointer to a struct mem_ctl_info structure
 */
void edac_mc_free(struct mem_ctl_info *mci)
{
	debugf1("%s()\n", __func__);

	edac_mc_unregister_sysfs_main_kobj(mci);

	/* free the mci instance memory here */
	kfree(mci);
}
EXPORT_SYMBOL_GPL(edac_mc_free);


/**
 * find_mci_by_dev
 *
 *	scan list of controllers looking for the one that manages
 *	the 'dev' device
 * @dev: pointer to a struct device related with the MCI
 */
struct mem_ctl_info *find_mci_by_dev(struct device *dev)
{
	struct mem_ctl_info *mci;
	struct list_head *item;

	debugf3("%s()\n", __func__);

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->dev == dev)
			return mci;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(find_mci_by_dev);

/*
 * handler for EDAC to check if NMI type handler has asserted interrupt
 */
static int edac_mc_assert_error_check_and_clear(void)
{
	int old_state;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		return 1;

	old_state = edac_err_assert;
	edac_err_assert = 0;

	return old_state;
}

/*
 * edac_mc_workq_function
 *	performs the operation scheduled by a workq request
 */
static void edac_mc_workq_function(struct work_struct *work_req)
{
	struct delayed_work *d_work = to_delayed_work(work_req);
	struct mem_ctl_info *mci = to_edac_mem_ctl_work(d_work);

	mutex_lock(&mem_ctls_mutex);

	/* if this control struct has movd to offline state, we are done */
	if (mci->op_state == OP_OFFLINE) {
		mutex_unlock(&mem_ctls_mutex);
		return;
	}

	/* Only poll controllers that are running polled and have a check */
	if (edac_mc_assert_error_check_and_clear() && (mci->edac_check != NULL))
		mci->edac_check(mci);

	mutex_unlock(&mem_ctls_mutex);

	/* Reschedule */
	queue_delayed_work(edac_workqueue, &mci->work,
			msecs_to_jiffies(edac_mc_get_poll_msec()));
}

/*
 * edac_mc_workq_setup
 *	initialize a workq item for this mci
 *	passing in the new delay period in msec
 *
 *	locking model:
 *
 *		called with the mem_ctls_mutex held
 */
static void edac_mc_workq_setup(struct mem_ctl_info *mci, unsigned msec)
{
	debugf0("%s()\n", __func__);

	/* if this instance is not in the POLL state, then simply return */
	if (mci->op_state != OP_RUNNING_POLL)
		return;

	INIT_DELAYED_WORK(&mci->work, edac_mc_workq_function);
	queue_delayed_work(edac_workqueue, &mci->work, msecs_to_jiffies(msec));
}

/*
 * edac_mc_workq_teardown
 *	stop the workq processing on this mci
 *
 *	locking model:
 *
 *		called WITHOUT lock held
 */
static void edac_mc_workq_teardown(struct mem_ctl_info *mci)
{
	int status;

	if (mci->op_state != OP_RUNNING_POLL)
		return;

	status = cancel_delayed_work(&mci->work);
	if (status == 0) {
		debugf0("%s() not canceled, flush the queue\n",
			__func__);

		/* workq instance might be running, wait for it */
		flush_workqueue(edac_workqueue);
	}
}

/*
 * edac_mc_reset_delay_period(unsigned long value)
 *
 *	user space has updated our poll period value, need to
 *	reset our workq delays
 */
void edac_mc_reset_delay_period(int value)
{
	struct mem_ctl_info *mci;
	struct list_head *item;

	mutex_lock(&mem_ctls_mutex);

	/* scan the list and turn off all workq timers, doing so under lock
	 */
	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->op_state == OP_RUNNING_POLL)
			cancel_delayed_work(&mci->work);
	}

	mutex_unlock(&mem_ctls_mutex);


	/* re-walk the list, and reset the poll delay */
	mutex_lock(&mem_ctls_mutex);

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		edac_mc_workq_setup(mci, (unsigned long) value);
	}

	mutex_unlock(&mem_ctls_mutex);
}



/* Return 0 on success, 1 on failure.
 * Before calling this function, caller must
 * assign a unique value to mci->mc_idx.
 *
 *	locking model:
 *
 *		called with the mem_ctls_mutex lock held
 */
static int add_mc_to_global_list(struct mem_ctl_info *mci)
{
	struct list_head *item, *insert_before;
	struct mem_ctl_info *p;

	insert_before = &mc_devices;

	p = find_mci_by_dev(mci->dev);
	if (unlikely(p != NULL))
		goto fail0;

	list_for_each(item, &mc_devices) {
		p = list_entry(item, struct mem_ctl_info, link);

		if (p->mc_idx >= mci->mc_idx) {
			if (unlikely(p->mc_idx == mci->mc_idx))
				goto fail1;

			insert_before = item;
			break;
		}
	}

	list_add_tail_rcu(&mci->link, insert_before);
	atomic_inc(&edac_handlers);
	return 0;

fail0:
	edac_printk(KERN_WARNING, EDAC_MC,
		"%s (%s) %s %s already assigned %d\n", dev_name(p->dev),
		edac_dev_name(mci), p->mod_name, p->ctl_name, p->mc_idx);
	return 1;

fail1:
	edac_printk(KERN_WARNING, EDAC_MC,
		"bug in low-level driver: attempt to assign\n"
		"    duplicate mc_idx %d in %s()\n", p->mc_idx, __func__);
	return 1;
}

static void del_mc_from_global_list(struct mem_ctl_info *mci)
{
	atomic_dec(&edac_handlers);
	list_del_rcu(&mci->link);

	/* these are for safe removal of devices from global list while
	 * NMI handlers may be traversing list
	 */
	synchronize_rcu();
	INIT_LIST_HEAD(&mci->link);
}

/**
 * edac_mc_find: Search for a mem_ctl_info structure whose index is 'idx'.
 *
 * If found, return a pointer to the structure.
 * Else return NULL.
 *
 * Caller must hold mem_ctls_mutex.
 */
struct mem_ctl_info *edac_mc_find(int idx)
{
	struct list_head *item;
	struct mem_ctl_info *mci;

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->mc_idx >= idx) {
			if (mci->mc_idx == idx)
				return mci;

			break;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(edac_mc_find);

/**
 * edac_mc_add_mc: Insert the 'mci' structure into the mci global list and
 *                 create sysfs entries associated with mci structure
 * @mci: pointer to the mci structure to be added to the list
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */

/* FIXME - should a warning be printed if no error detection? correction? */
int edac_mc_add_mc(struct mem_ctl_info *mci)
{
	debugf0("%s()\n", __func__);

#ifdef CONFIG_EDAC_DEBUG
	if (edac_debug_level >= 3)
		edac_mc_dump_mci(mci);

	if (edac_debug_level >= 4) {
		int i;
		for (i = 0; i < mci->num_csrows; i++) {
			int j;
			edac_mc_dump_csrow(&mci->csrows[i]);
			for (j = 0; j < mci->csrows[i].nr_channels; j++)
				edac_mc_dump_channel(&mci->csrows[i].
						channels[j]);
		}
		for (i = 0; i < mci->tot_dimms; i++)
			edac_mc_dump_dimm(&mci->dimms[i]);
	}
#endif
	mutex_lock(&mem_ctls_mutex);

	if (add_mc_to_global_list(mci))
		goto fail0;

	/* set load time so that error rate can be tracked */
	mci->start_time = jiffies;

	if (edac_create_sysfs_mci_device(mci)) {
		edac_mc_printk(mci, KERN_WARNING,
			"failed to create sysfs device\n");
		goto fail1;
	}

	/* If there IS a check routine, then we are running POLLED */
	if (mci->edac_check != NULL) {
		/* This instance is NOW RUNNING */
		mci->op_state = OP_RUNNING_POLL;

		edac_mc_workq_setup(mci, edac_mc_get_poll_msec());
	} else {
		mci->op_state = OP_RUNNING_INTERRUPT;
	}

	/* Report action taken */
	edac_mc_printk(mci, KERN_INFO, "Giving out device to '%s' '%s':"
		" DEV %s\n", mci->mod_name, mci->ctl_name, edac_dev_name(mci));

	mutex_unlock(&mem_ctls_mutex);
	return 0;

fail1:
	del_mc_from_global_list(mci);

fail0:
	mutex_unlock(&mem_ctls_mutex);
	return 1;
}
EXPORT_SYMBOL_GPL(edac_mc_add_mc);

/**
 * edac_mc_del_mc: Remove sysfs entries for specified mci structure and
 *                 remove mci structure from global list
 * @pdev: Pointer to 'struct device' representing mci structure to remove.
 *
 * Return pointer to removed mci structure, or NULL if device not found.
 */
struct mem_ctl_info *edac_mc_del_mc(struct device *dev)
{
	struct mem_ctl_info *mci;

	debugf0("%s()\n", __func__);

	mutex_lock(&mem_ctls_mutex);

	/* find the requested mci struct in the global list */
	mci = find_mci_by_dev(dev);
	if (mci == NULL) {
		mutex_unlock(&mem_ctls_mutex);
		return NULL;
	}

	del_mc_from_global_list(mci);
	mutex_unlock(&mem_ctls_mutex);

	/* flush workq processes */
	edac_mc_workq_teardown(mci);

	/* marking MCI offline */
	mci->op_state = OP_OFFLINE;

	/* remove from sysfs */
	edac_remove_sysfs_mci_device(mci);

	edac_printk(KERN_INFO, EDAC_MC,
		"Removed device %d for %s %s: DEV %s\n", mci->mc_idx,
		mci->mod_name, mci->ctl_name, edac_dev_name(mci));

	return mci;
}
EXPORT_SYMBOL_GPL(edac_mc_del_mc);

static void edac_mc_scrub_block(unsigned long page, unsigned long offset,
				u32 size)
{
	struct page *pg;
	void *virt_addr;
	unsigned long flags = 0;

	debugf3("%s()\n", __func__);

	/* ECC error page was not in our memory. Ignore it. */
	if (!pfn_valid(page))
		return;

	/* Find the actual page structure then map it and fix */
	pg = pfn_to_page(page);

	if (PageHighMem(pg))
		local_irq_save(flags);

	virt_addr = kmap_atomic(pg, KM_BOUNCE_READ);

	/* Perform architecture specific atomic scrub operation */
	atomic_scrub(virt_addr + offset, size);

	/* Unmap and complete */
	kunmap_atomic(virt_addr, KM_BOUNCE_READ);

	if (PageHighMem(pg))
		local_irq_restore(flags);
}

/* FIXME - should return -1 */
int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci, unsigned long page)
{
	struct csrow_info *csrows = mci->csrows;
	int row, i, j, n;

	debugf1("MC%d: %s(): 0x%lx\n", mci->mc_idx, __func__, page);
	row = -1;

	for (i = 0; i < mci->num_csrows; i++) {
		struct csrow_info *csrow = &csrows[i];
		n = 0;
		for (j = 0; j < csrow->nr_channels; j++) {
			struct dimm_info *dimm = csrow->channels[j].dimm;
			n += dimm->nr_pages;
		}
		if (n == 0)
			continue;

		debugf3("MC%d: %s(): first(0x%lx) page(0x%lx) last(0x%lx) "
			"mask(0x%lx)\n", mci->mc_idx, __func__,
			csrow->first_page, page, csrow->last_page,
			csrow->page_mask);

		if ((page >= csrow->first_page) &&
		(page <= csrow->last_page) &&
		((page & csrow->page_mask) ==
		(csrow->first_page & csrow->page_mask))) {
			row = i;
			break;
		}
	}

	if (row == -1)
		edac_mc_printk(mci, KERN_ERR,
			"could not look up page error address %lx\n",
			(unsigned long)page);

	return row;
}
EXPORT_SYMBOL_GPL(edac_mc_find_csrow_by_page);

void edac_increment_ce_error(enum hw_event_error_scope scope,
			     struct mem_ctl_info *mci,
			     int mc_branch,
			     int mc_channel,
			     int mc_dimm_number,
			     int csrow,
			     int cschannel)
{
	int index;

	mci->err.ce_mc++;

	if (scope == HW_EVENT_SCOPE_MC) {
		mci->ce_noinfo_count = 0;
		return;
	}

	index = 0;
	if (mc_branch >= 0) {
		index = mc_branch;
		mci->err.ce_branch[index]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_BRANCH)
		return;
	index *= mci->num_branch;

	if (mc_channel >= 0) {
		index += mc_channel;
		mci->err.ce_channel[index]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_CHANNEL)
		return;
	index *= mci->num_channel;

	if (mc_dimm_number >= 0) {
		index += mc_dimm_number;
		mci->err.ce_dimm[index]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_DIMM)
		return;
	index *= mci->num_dimm;

	if (csrow >= 0) {
		index += csrow;
		mci->err.ce_csrow[csrow]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_CSROW_CHANNEL)
		return;
	index *= mci->num_csrows;

	if (cschannel >= 0) {
		index += cschannel;
		mci->err.ce_cschannel[index]++;
	}
}

void edac_increment_ue_error(enum hw_event_error_scope scope,
			     struct mem_ctl_info *mci,
			     int mc_branch,
			     int mc_channel,
			     int mc_dimm_number,
			     int csrow,
			     int cschannel)
{
	int index;

	mci->err.ue_mc++;

	if (scope == HW_EVENT_SCOPE_MC) {
		mci->ue_noinfo_count = 0;
		return;
	}

	index = 0;
	if (mc_branch >= 0) {
		index = mc_branch;
		mci->err.ue_branch[index]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_BRANCH)
		return;
	index *= mci->num_branch;

	if (mc_channel >= 0) {
		index += mc_channel;
		mci->err.ue_channel[index]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_CHANNEL)
		return;
	index *= mci->num_channel;

	if (mc_dimm_number >= 0) {
		index += mc_dimm_number;
		mci->err.ue_dimm[index]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_DIMM)
		return;
	index *= mci->num_dimm;

	if (csrow >= 0) {
		index += csrow;
		mci->err.ue_csrow[csrow]++;
	}
	if (scope == HW_EVENT_SCOPE_MC_CSROW_CHANNEL)
		return;
	index *= mci->num_csrows;

	if (cschannel >= 0) {
		index += cschannel;
		mci->err.ue_cschannel[index]++;
	}
}

void edac_mc_handle_error(enum hw_event_mc_err_type type,
			  enum hw_event_error_scope scope,
			  struct mem_ctl_info *mci,
			  unsigned long page_frame_number,
			  unsigned long offset_in_page,
			  unsigned long syndrome,
			  int mc_branch,
			  int mc_channel,
			  int mc_dimm_number,
			  int csrow,
			  int cschannel,
			  const char *msg,
			  const char *other_detail)
{
	unsigned long remapped_page;
	/* FIXME: too much for stack. Move it to some pre-alocated area */
	char detail[80 + strlen(other_detail)];
	char label[(EDAC_MC_LABEL_LEN + 2) * mci->tot_dimms], *p;
	char location[80];
	int i;
	u32 grain;

	debugf3("MC%d: %s()\n", mci->mc_idx, __func__);

	/* Check if the event report is consistent */
	if ((scope == HW_EVENT_SCOPE_MC_CSROW_CHANNEL) &&
	    (cschannel >= mci->num_cschannel)) {
		trace_mc_out_of_range(mci, "CE", "cs channel", cschannel,
					0, mci->num_cschannel);
		edac_mc_printk(mci, KERN_ERR,
				"INTERNAL ERROR: cs channel out of range (%d >= %d)\n",
				cschannel, mci->num_cschannel);
		if (type == HW_EVENT_ERR_CORRECTED)
			mci->err.ce_mc++;
		else
			mci->err.ue_mc++;
		return;
	} else {
		cschannel = -1;
	}

	if ((scope <= HW_EVENT_SCOPE_MC_CSROW) &&
	    (csrow >= mci->num_csrows)) {
		trace_mc_out_of_range(mci, "CE", "csrow", csrow,
					0, mci->num_csrows);
		edac_mc_printk(mci, KERN_ERR,
				"INTERNAL ERROR: csrow out of range (%d >= %d)\n",
				csrow, mci->num_csrows);
		if (type == HW_EVENT_ERR_CORRECTED)
			mci->err.ce_mc++;
		else
			mci->err.ue_mc++;
		return;
	} else {
		csrow = -1;
	}

	if ((scope <= HW_EVENT_SCOPE_MC_CSROW) &&
	    (mc_dimm_number >= mci->num_dimm)) {
		trace_mc_out_of_range(mci, "CE", "dimm_number",
					mc_dimm_number, 0, mci->num_dimm);
		edac_mc_printk(mci, KERN_ERR,
				"INTERNAL ERROR: dimm_number out of range (%d >= %d)\n",
				mc_dimm_number, mci->num_dimm);
		if (type == HW_EVENT_ERR_CORRECTED)
			mci->err.ce_mc++;
		else
			mci->err.ue_mc++;
		return;
	} else {
		mc_dimm_number = -1;
	}

	if ((scope <= HW_EVENT_SCOPE_MC_CHANNEL) &&
	    (mc_channel >= mci->num_dimm)) {
		trace_mc_out_of_range(mci, "CE", "mc_channel",
					mc_channel, 0, mci->num_dimm);
		edac_mc_printk(mci, KERN_ERR,
				"INTERNAL ERROR: mc_channel out of range (%d >= %d)\n",
				mc_channel, mci->num_dimm);
		if (type == HW_EVENT_ERR_CORRECTED)
			mci->err.ce_mc++;
		else
			mci->err.ue_mc++;
		return;
	} else {
		mc_channel = -1;
	}

	if ((scope <= HW_EVENT_SCOPE_MC_BRANCH) &&
	    (mc_branch >= mci->num_branch)) {
		trace_mc_out_of_range(mci, "CE", "branch",
					mc_branch, 0, mci->num_branch);
		edac_mc_printk(mci, KERN_ERR,
				"INTERNAL ERROR: mc_branch out of range (%d >= %d)\n",
				mc_branch, mci->num_branch);
		if (type == HW_EVENT_ERR_CORRECTED)
			mci->err.ce_mc++;
		else
			mci->err.ue_mc++;
		return;
	} else {
		mc_branch = -1;
	}

	/*
	 * Get the dimm label/grain that applies to the match criteria.
	 * As the error algorithm may not be able to point to just one memory,
	 * the logic here will get all possible labels that could pottentially
	 * be affected by the error.
	 * On FB-DIMM memory controllers, for uncorrected errors, it is common
	 * to have only the MC channel and the MC dimm (also called as "rank")
	 * but the channel is not known, as the memory is arranged in pairs,
	 * where each memory belongs to a separate channel within the same
	 * branch.
	 * It will also get the max grain, over the error match range
	 */
	grain = 0;
	p = label;
	for (i = 0; i < mci->tot_dimms; i++) {
		struct dimm_info *dimm = &mci->dimms[i];

		if (mc_branch >= 0 && mc_branch != dimm->mc_branch)
			continue;

		if (mc_channel >= 0 && mc_channel != dimm->mc_channel)
			continue;

		if (mc_dimm_number >= 0 &&
		    mc_dimm_number != dimm->mc_dimm_number)
			continue;

		if (csrow >= 0 && csrow != dimm->csrow)
			continue;
		if (cschannel >= 0 && cschannel != dimm->cschannel)
			continue;

		if (dimm->grain > grain)
			grain = dimm->grain;

		strcpy(p, dimm->label);
		p[strlen(p)] = ' ';
		p = p + strlen(p);
	}
	p[strlen(p)] = '\0';

	/* Fill the RAM location data */
	p = location;
	if (mc_branch >= 0)
		p += sprintf(p, "branch %d ", mc_branch);

	if (mc_channel >= 0)
		p += sprintf(p, "channel %d ", mc_channel);

	if (mc_dimm_number >= 0)
		p += sprintf(p, "dimm %d ", mc_dimm_number);

	if (csrow >= 0)
		p += sprintf(p, "csrow %d ", csrow);

	if (cschannel >= 0)
		p += sprintf(p, "cs_channel %d ", cschannel);


	/* Memory type dependent details about the error */
	if (type == HW_EVENT_ERR_CORRECTED)
		snprintf(detail, sizeof(detail),
			"page 0x%lx offset 0x%lx grain %d syndrome 0x%lx\n",
			page_frame_number, offset_in_page,
			grain, syndrome);
	else
		snprintf(detail, sizeof(detail),
			"page 0x%lx offset 0x%lx grain %d\n",
			page_frame_number, offset_in_page, grain);

	trace_mc_error(type, mci->mc_idx, msg, label, mc_branch, mc_channel,
		       mc_dimm_number, csrow, cschannel,
		       detail, other_detail);

	if (type == HW_EVENT_ERR_CORRECTED) {
		if (edac_mc_get_log_ce())
			edac_mc_printk(mci, KERN_WARNING,
				       "CE %s label \"%s\" (location: %d.%d.%d.%d.%d %s %s)\n",
				       msg, label, mc_branch, mc_channel,
				       mc_dimm_number, csrow, cschannel,
				       detail, other_detail);
		edac_increment_ce_error(scope, mci, mc_branch, mc_channel,
					mc_dimm_number, csrow, cschannel);

		if (mci->scrub_mode & SCRUB_SW_SRC) {
			/*
			 * Some MC's can remap memory so that it is still
			 * available at a different address when PCI devices
			 * map into memory.
			 * MC's that can't do this lose the memory where PCI
			 * devices are mapped. This mapping is MC dependent
			 * and so we call back into the MC driver for it to
			 * map the MC page to a physical (CPU) page which can
			 * then be mapped to a virtual page - which can then
			 * be scrubbed.
			 */
			remapped_page = mci->ctl_page_to_phys ?
				mci->ctl_page_to_phys(mci, page_frame_number) :
				page_frame_number;

			edac_mc_scrub_block(remapped_page,
					    offset_in_page, grain);
		}
	} else {
		if (edac_mc_get_log_ue())
			edac_mc_printk(mci, KERN_WARNING,
				"UE %s label \"%s\" (%s %s %s)\n",
				msg, label, location, detail, other_detail);

		if (edac_mc_get_panic_on_ue())
			panic("UE %s label \"%s\" (%s %s %s)\n",
			      msg, label, location, detail, other_detail);

		edac_increment_ue_error(scope, mci, mc_branch, mc_channel,
					mc_dimm_number, csrow, cschannel);
	}
}
EXPORT_SYMBOL_GPL(edac_mc_handle_error);

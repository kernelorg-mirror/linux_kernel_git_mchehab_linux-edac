/*
 * edac_mc kernel module
 * (C) 2005-2007 Linux Networx (http://lnxi.com)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com> www.softwarebitmaker.com
 *
 */

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/bug.h>
#include <linux/pm_runtime.h>

#include "edac_core.h"
#include "edac_module.h"

/* MC EDAC Controls, setable by module parameter, and sysfs */
static int edac_mc_log_ue = 1;
static int edac_mc_log_ce = 1;
static int edac_mc_panic_on_ue;
static int edac_mc_poll_msec = 1000;

/* Getter functions for above */
int edac_mc_get_log_ue(void)
{
	return edac_mc_log_ue;
}

int edac_mc_get_log_ce(void)
{
	return edac_mc_log_ce;
}

int edac_mc_get_panic_on_ue(void)
{
	return edac_mc_panic_on_ue;
}

/* this is temporary */
int edac_mc_get_poll_msec(void)
{
	return edac_mc_poll_msec;
}

static int edac_set_poll_msec(const char *val, struct kernel_param *kp)
{
	long l;
	int ret;

	if (!val)
		return -EINVAL;

	ret = strict_strtol(val, 0, &l);
	if (ret == -EINVAL || ((int)l != l))
		return -EINVAL;
	*((int *)kp->arg) = l;

	/* notify edac_mc engine to reset the poll period */
	edac_mc_reset_delay_period(l);

	return 0;
}

/* Parameter declarations for above */
module_param(edac_mc_panic_on_ue, int, 0644);
MODULE_PARM_DESC(edac_mc_panic_on_ue, "Panic on uncorrected error: 0=off 1=on");
module_param(edac_mc_log_ue, int, 0644);
MODULE_PARM_DESC(edac_mc_log_ue,
		 "Log uncorrectable error to console: 0=off 1=on");
module_param(edac_mc_log_ce, int, 0644);
MODULE_PARM_DESC(edac_mc_log_ce,
		 "Log correctable error to console: 0=off 1=on");
module_param_call(edac_mc_poll_msec, edac_set_poll_msec, param_get_int,
		  &edac_mc_poll_msec, 0644);
MODULE_PARM_DESC(edac_mc_poll_msec, "Polling period in milliseconds");

struct bus_type mci_bus_type = {
	.name		= "mc",
};

/*
 * various constants for Memory Controllers
 */
static const char *mem_types[] = {
	[MEM_EMPTY] = "Empty",
	[MEM_RESERVED] = "Reserved",
	[MEM_UNKNOWN] = "Unknown",
	[MEM_FPM] = "FPM",
	[MEM_EDO] = "EDO",
	[MEM_BEDO] = "BEDO",
	[MEM_SDR] = "Unbuffered-SDR",
	[MEM_RDR] = "Registered-SDR",
	[MEM_DDR] = "Unbuffered-DDR",
	[MEM_RDDR] = "Registered-DDR",
	[MEM_RMBS] = "RMBS",
	[MEM_DDR2] = "Unbuffered-DDR2",
	[MEM_FB_DDR2] = "FullyBuffered-DDR2",
	[MEM_RDDR2] = "Registered-DDR2",
	[MEM_XDR] = "XDR",
	[MEM_DDR3] = "Unbuffered-DDR3",
	[MEM_RDDR3] = "Registered-DDR3"
};

static const char *dev_types[] = {
	[DEV_UNKNOWN] = "Unknown",
	[DEV_X1] = "x1",
	[DEV_X2] = "x2",
	[DEV_X4] = "x4",
	[DEV_X8] = "x8",
	[DEV_X16] = "x16",
	[DEV_X32] = "x32",
	[DEV_X64] = "x64"
};

static const char *edac_caps[] = {
	[EDAC_UNKNOWN] = "Unknown",
	[EDAC_NONE] = "None",
	[EDAC_RESERVED] = "Reserved",
	[EDAC_PARITY] = "PARITY",
	[EDAC_EC] = "EC",
	[EDAC_SECDED] = "SECDED",
	[EDAC_S2ECD2ED] = "S2ECD2ED",
	[EDAC_S4ECD4ED] = "S4ECD4ED",
	[EDAC_S8ECD8ED] = "S8ECD8ED",
	[EDAC_S16ECD16ED] = "S16ECD16ED"
};

/*
 * Per-dimm (or per-rank) devices
 */

#define to_dimm(k) container_of(k, struct dimm_info, dev)

/* show/store functions for DIMM Label attributes */
static ssize_t dimmdev_location_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);
	struct mem_ctl_info *mci = dimm->mci;
	int i;
	char *p = data;

	for (i = 0; i < mci->n_layers; i++) {
		p += sprintf(p, "%s %d ",
			     edac_layer_name[mci->layers[i].type],
			     dimm->location[i]);
	}

	return p - data;
}

static ssize_t dimmdev_label_show(struct device *dev,
				  struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	/* if field has not been initialized, there is nothing to send */
	if (!dimm->label[0])
		return 0;

	return snprintf(data, EDAC_MC_LABEL_LEN, "%s\n", dimm->label);
}

static ssize_t dimmdev_label_store(struct device *dev,
				   struct device_attribute *mattr,
				   const char *data,
				   size_t count)
{
	struct dimm_info *dimm = to_dimm(dev);

	ssize_t max_size = 0;

	max_size = min((ssize_t) count, (ssize_t) EDAC_MC_LABEL_LEN - 1);
	strncpy(dimm->label, data, max_size);
	dimm->label[max_size] = '\0';

	return max_size;
}

static ssize_t dimmdev_size_show(struct device *dev,
				 struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%u\n", PAGES_TO_MiB(dimm->nr_pages));
}

static ssize_t dimmdev_mem_type_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", mem_types[dimm->mtype]);
}

static ssize_t dimmdev_dev_type_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", dev_types[dimm->dtype]);
}

static ssize_t dimmdev_edac_mode_show(struct device *dev,
				      struct device_attribute *mattr,
				      char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", edac_caps[dimm->edac_mode]);
}

/* dimm/rank attribute files */
static DEVICE_ATTR(dimm_label, S_IRUGO | S_IWUSR,
		   dimmdev_label_show, dimmdev_label_store);
static DEVICE_ATTR(dimm_location, S_IRUGO, dimmdev_location_show, NULL);
static DEVICE_ATTR(size, S_IRUGO, dimmdev_size_show, NULL);
static DEVICE_ATTR(dimm_mem_type, S_IRUGO, dimmdev_mem_type_show, NULL);
static DEVICE_ATTR(dimm_dev_type, S_IRUGO, dimmdev_dev_type_show, NULL);
static DEVICE_ATTR(dimm_edac_mode, S_IRUGO, dimmdev_edac_mode_show, NULL);

/* attributes of the dimm<id>/rank<id> object */
static struct attribute *dimm_attrs[] = {
	&dev_attr_dimm_label.attr,
	&dev_attr_dimm_location.attr,
	&dev_attr_size.attr,
	&dev_attr_dimm_mem_type.attr,
	&dev_attr_dimm_dev_type.attr,
	&dev_attr_dimm_edac_mode.attr,
	NULL,
};

static struct attribute_group dimm_attr_grp = {
	.attrs	= dimm_attrs,
};

static const struct attribute_group *dimm_attr_groups[] = {
	&dimm_attr_grp,
	NULL
};

static void dimm_attr_release(struct device *device)
{
}

static struct device_type dimm_attr_type = {
	.groups		= dimm_attr_groups,
	.release	= dimm_attr_release,
};

/* Create a DIMM object under specifed memory controller device */
static int edac_create_dimm_object(struct mem_ctl_info *mci,
				   struct dimm_info *dimm,
				   int index)
{
	int err;
	dimm->mci = mci;

	dimm->dev.type = &dimm_attr_type;
	dimm->dev.bus = &mci_bus_type;
	device_initialize(&dimm->dev);

	dimm->dev.parent = &mci->dev;
	if (mci->mem_is_per_rank)
		dev_set_name(&dimm->dev, "rank%d", index);
	else
		dev_set_name(&dimm->dev, "dimm%d", index);
	dev_set_drvdata(&dimm->dev, dimm);
	pm_runtime_forbid(&mci->dev);

	err =  device_add(&dimm->dev);

	debugf0("%s(): creating rank/dimm device %s\n", __func__,
		dev_name(&dimm->dev));

	return err;
}

/*
 * Memory controller device
 */

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

static ssize_t mci_reset_counters_store(struct device *dev,
				        struct device_attribute *mattr,
					const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int cnt, row, chan, i;
	mci->ue_mc = 0;
	mci->ce_mc = 0;
	mci->ue_noinfo_count = 0;
	mci->ce_noinfo_count = 0;


	for (row = 0; row < mci->num_csrows; row++) {
		struct csrow_info *ri = &mci->csrows[row];

		ri->ue_count = 0;
		ri->ce_count = 0;

		for (chan = 0; chan < ri->nr_channels; chan++)
			ri->channels[chan].ce_count = 0;
	}

	cnt = 1;
	for (i = 0; i < mci->n_layers; i++) {
		cnt *= mci->layers[i].size;
		memset(mci->ce_per_layer[i], 0, cnt * sizeof(u32));
		memset(mci->ue_per_layer[i], 0, cnt * sizeof(u32));
	}

	mci->start_time = jiffies;
	return count;
}

/* Memory scrubbing interface:
 *
 * A MC driver can limit the scrubbing bandwidth based on the CPU type.
 * Therefore, ->set_sdram_scrub_rate should be made to return the actual
 * bandwidth that is accepted or 0 when scrubbing is to be disabled.
 *
 * Negative value still means that an error has occurred while setting
 * the scrub rate.
 */
static ssize_t mci_sdram_scrub_rate_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	unsigned long bandwidth = 0;
	int new_bw = 0;

	if (!mci->set_sdram_scrub_rate)
		return -EINVAL;

	if (strict_strtoul(data, 10, &bandwidth) < 0)
		return -EINVAL;

	new_bw = mci->set_sdram_scrub_rate(mci, bandwidth);
	if (new_bw < 0) {
		edac_printk(KERN_WARNING, EDAC_MC,
			    "Error setting scrub rate to: %lu\n", bandwidth);
		return -EINVAL;
	}

	return count;
}

/*
 * ->get_sdram_scrub_rate() return value semantics same as above.
 */
static ssize_t mci_sdram_scrub_rate_show(struct device *dev,
				         struct device_attribute *mattr,
					 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int bandwidth = 0;

	if (!mci->get_sdram_scrub_rate)
		return -EINVAL;

	bandwidth = mci->get_sdram_scrub_rate(mci);
	if (bandwidth < 0) {
		edac_printk(KERN_DEBUG, EDAC_MC, "Error reading scrub rate\n");
		return bandwidth;
	}

	return sprintf(data, "%d\n", bandwidth);
}

/* default attribute files for the MCI object */
static ssize_t mci_ue_count_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ue_mc);
}

static ssize_t mci_ce_count_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ce_mc);
}

static ssize_t mci_ce_noinfo_show(struct device *dev,
				  struct device_attribute *mattr,
				  char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ce_noinfo_count);
}

static ssize_t mci_ue_noinfo_show(struct device *dev,
				  struct device_attribute *mattr,
				  char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ue_noinfo_count);
}

static ssize_t mci_seconds_show(struct device *dev,
				struct device_attribute *mattr,
				char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%ld\n", (jiffies - mci->start_time) / HZ);
}

static ssize_t mci_ctl_name_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%s\n", mci->ctl_name);
}

static ssize_t mci_size_mb_show(struct device *dev,
				struct device_attribute *mattr,
				char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	int total_pages, csrow_idx, j;

	for (total_pages = csrow_idx = 0; csrow_idx < mci->num_csrows;
	     csrow_idx++) {
		struct csrow_info *csrow = &mci->csrows[csrow_idx];

		for (j = 0; j < csrow->nr_channels; j++) {
			struct dimm_info *dimm = csrow->channels[j].dimm;

			total_pages += dimm->nr_pages;
		}
	}

	return sprintf(data, "%u\n", PAGES_TO_MiB(total_pages));
}

static ssize_t mci_max_location_show(struct device *dev,
				     struct device_attribute *mattr,
				     char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int i;
	char *p = data;

	for (i = 0; i < mci->n_layers; i++) {
		p += sprintf(p, "%s %d ",
			     edac_layer_name[mci->layers[i].type],
			     mci->layers[i].size - 1);
	}

	return p - data;
}

#ifdef CONFIG_EDAC_DEBUG
static ssize_t edac_fake_inject_show(struct device *dev,
				     struct device_attribute *mattr,
				     char *data)
{
	return sprintf(data,
		       "EDAC fake test engine. Writing to this node a value in the form of :\n"
		       "\t0:1:0\n"
		       "will call the EDAC core routine to produce a memory error for the given memory location (0, 1, 0).\n"
		       "The driver's error parsing logic won't be tested. This tool is useful only\n"
		       "if you're testing the EDAC core tracing facility, or if you're needing to test\n"
		       "some userspace application.\n");
}

static ssize_t edac_fake_inject_store(struct device *dev,
				      struct device_attribute *mattr,
				      const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);

	static enum hw_event_mc_err_type type = HW_EVENT_ERR_CORRECTED;
	int err, layer0 = -1, layer1 = -1, layer2 = -1;
	err = sscanf(data, "%i:%i:%i", &layer0, &layer1, &layer2);
	if (err < 0)
		return err;

	printk(KERN_DEBUG
	       "Generating a fake error to %d.%d.%d to test core handling. NOTE: this won't test the driver-specific decoding logic.\n",
	       layer0, layer1, layer2);
	edac_mc_handle_error(type, mci, 0, 0, 0,
			     layer0, layer1, layer2,
			     "FAKE ERROR", "for EDAC testing only", NULL);
	if (++type == HW_EVENT_ERR_FATAL)
		type = HW_EVENT_ERR_CORRECTED;

	return count;
}

DEVICE_ATTR(fake_inject, S_IRUGO, edac_fake_inject_show, edac_fake_inject_store);
#endif

/* default Control file */
DEVICE_ATTR(reset_counters, S_IWUSR, NULL, mci_reset_counters_store);

/* default Attribute files */
DEVICE_ATTR(mc_name, S_IRUGO, mci_ctl_name_show, NULL);
DEVICE_ATTR(size_mb, S_IRUGO, mci_size_mb_show, NULL);
DEVICE_ATTR(seconds_since_reset, S_IRUGO, mci_seconds_show, NULL);
DEVICE_ATTR(ue_noinfo_count, S_IRUGO, mci_ue_noinfo_show, NULL);
DEVICE_ATTR(ce_noinfo_count, S_IRUGO, mci_ce_noinfo_show, NULL);
DEVICE_ATTR(ue_count, S_IRUGO, mci_ue_count_show, NULL);
DEVICE_ATTR(ce_count, S_IRUGO, mci_ce_count_show, NULL);
DEVICE_ATTR(max_location, S_IRUGO, mci_max_location_show, NULL);

/* memory scrubber attribute file */
DEVICE_ATTR(sdram_scrub_rate, S_IRUGO | S_IWUSR, mci_sdram_scrub_rate_show,
	mci_sdram_scrub_rate_store);

static struct attribute *mci_attrs[] = {
	&dev_attr_reset_counters.attr,
	&dev_attr_mc_name.attr,
	&dev_attr_size_mb.attr,
	&dev_attr_seconds_since_reset.attr,
	&dev_attr_ue_noinfo_count.attr,
	&dev_attr_ce_noinfo_count.attr,
	&dev_attr_ue_count.attr,
	&dev_attr_ce_count.attr,
	&dev_attr_sdram_scrub_rate.attr,
	&dev_attr_max_location.attr,
#ifdef CONFIG_EDAC_DEBUG
	&dev_attr_fake_inject.attr,
#endif
	NULL
};

static struct attribute_group mci_attr_grp = {
	.attrs	= mci_attrs,
};

static const struct attribute_group *mci_attr_groups[] = {
	&mci_attr_grp,
	NULL
};

static void mci_attr_release(struct device *device)
{
}

static struct device_type mci_attr_type = {
	.groups		= mci_attr_groups,
	.release	= mci_attr_release,
};

#if 0
/*
 * Per layer error count nodes
 */
static ssize_t errcount_ce_show(struct mem_ctl_info *mci, char *data,
				void *priv)
{
	struct errcount_attribute_data *ead = priv;
	int i, index = 0;

	for (i = 0; i < ead->n_layers; i++) {
		if (i < ead->n_layers - 1)
			index += mci->layers[i + 1].size * ead->pos[i];
		else
			index += ead->pos[i];
	}
	return sprintf(data, "%u\n",
		       mci->ce_per_layer[ead->n_layers - 1][index]);
}

static ssize_t errcount_ue_show(struct mem_ctl_info *mci, char *data,
				void *priv)
{
	struct errcount_attribute_data *ead = priv;
	int i, index = 0;

	for (i = 0; i < ead->n_layers; i++) {
		if (i < ead->n_layers - 1)
			index += mci->layers[i + 1].size * ead->pos[i];
		else
			index += ead->pos[i];
	}
	return sprintf(data, "%u\n",
		       mci->ue_per_layer[ead->n_layers - 1][index]);
}

static bool is_dimms_filled(struct mem_ctl_info *mci, int n_layers,
			  int pos[EDAC_MAX_LAYERS])
{
	static struct dimm_info *dimm;
	int i, count = 1;

	dimm = GET_POS(mci->layers, mci->dimms, mci->n_layers,
		       pos[0], pos[1], pos[2]);
	for (i = n_layers + 1; i < mci->n_layers; i++)
		count *= mci->layers[i].size;

	debugf2("%s: layers: %d, pos: %d:%d:%d, count = %d\n",
		__func__, n_layers, pos[0], pos[1], pos[2], count);
	for (i = 0; i < count; i++) {
		if (dimm->nr_pages)
			return true;
		dimm++;
	}

	return false;
}

static int edac_create_errcount_layer(struct mem_ctl_info *mci,
				      struct mcidev_sysfs_attribute **erc,
				      struct errcount_attribute_data **ercd,
				      const unsigned layer,
				      const int count)
{
	int err, i, j, pos[EDAC_MAX_LAYERS];
	char location[80], *p;

	memset(&pos, 0, sizeof(pos));
	for (i = 0; i < count; i++) {
		/* Only show the nodes if is there any filled DIMM */
		if (is_dimms_filled(mci, layer, pos)) {
			p = location;
			for (j = 0; j <= layer; j++)
				p += sprintf(p, "_%s%d",
					edac_layer_name[mci->layers[j].type],
					pos[j]);

			(*erc)->attr.name = kasprintf(GFP_KERNEL, "ce%s",
						      location);
			debugf2("%s() creating %s\n", __func__,
				(*erc)->attr.name);
			if (!(*erc)->attr.name)
				return -ENOMEM;
			(*erc)->attr.mode = S_IRUGO | S_IWUSR;
			(*erc)->show = errcount_ce_show;
			(*erc)->priv = *ercd;
			(*ercd)->n_layers = layer + 1;
			memcpy((*ercd)->pos, pos, sizeof(pos));
			err = sysfs_create_file(&mci->edac_mci_kobj,
						&(*erc)->attr);
			if (err < 0) {
				printk(KERN_ERR
				       "sysfs_create_file failed: %d\n", err);
				return err;
			}
			(*erc)++;
			(*ercd)++;

			(*erc)->attr.name = kasprintf(GFP_KERNEL, "ue%s",
						      location);
			debugf2("%s() creating %s\n", __func__,
				(*erc)->attr.name);
			if (!(*erc)->attr.name)
				return -ENOMEM;
			(*erc)->attr.mode = S_IRUGO | S_IWUSR;
			(*erc)->show = errcount_ue_show;
			(*erc)->priv = *ercd;
			(*ercd)->n_layers = layer + 1;
			memcpy((*ercd)->pos, pos, sizeof(pos));
			err = sysfs_create_file(&mci->edac_mci_kobj,
						&(*erc)->attr);
			if (err < 0) {
				printk(KERN_ERR
				       "sysfs_create_file failed: %d\n", err);
				return err;
			}
			(*erc)++;
			(*ercd)++;
		}

		/* increment to the next position */
		for (j = layer; j >= 0; j--) {
			pos[j]++;
			if (pos[j] < mci->layers[j].size)
				break;
			pos[j] = 0;
		}
	}

	return 0;
}

static void edac_remove_errcount(struct mem_ctl_info *mci)
{
	struct mcidev_sysfs_attribute *erc = mci->errcount_attr;

	do {
		if (!(erc->attr.name))
			return;
		debugf2("%s() removing %s\n", __func__, erc->attr.name);
		sysfs_remove_file(&mci->edac_mci_kobj, &erc->attr);

		kfree(erc->attr.name);
		erc++;
	} while (1);
	return;
}

static int edac_create_errcount_objects(struct mem_ctl_info *mci)
{
	struct mcidev_sysfs_attribute *erc = mci->errcount_attr;
	struct errcount_attribute_data *ercd = mci->errcount_attr_data;
	int err, i, count;

	count = 1;
	for (i = 0; i < mci->n_layers; i++) {
		count *= mci->layers[i].size;
		err = edac_create_errcount_layer(mci, &erc, &ercd, i, count);
		if (err < 0)
			goto err;
	}
	debugf2("%s: created %d objects\n", __func__, (unsigned)(erc - mci->errcount_attr));
	return 0;
err:
	edac_remove_errcount(mci);
	return err;
}
#endif

static struct device mci_parent;

/*
 * Create a new Memory Controller kobject instance,
 *	mc<id> under the 'mc' directory
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
int edac_create_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i, err;

	debugf0("%s() idx=%d\n", __func__, mci->mc_idx);

	mci->dev.type = &mci_attr_type;
	mci->dev.bus = &mci_bus_type;
	device_initialize(&mci->dev);

	mci->dev.parent = &mci_parent;
	dev_set_name(&mci->dev, "mc%d", mci->mc_idx);
	dev_set_drvdata(&mci->dev, mci);
	pm_runtime_forbid(&mci->dev);

	debugf0("%s(): creating device %s\n", __func__,
		dev_name(&mci->dev));
	err = device_add(&mci->dev);
	if (err < 0)
		return err;

	/*
	 * Create the dimm/rank devices
	 */
	for (i = 0; i < mci->tot_dimms; i++) {
		struct dimm_info *dimm = &mci->dimms[i];
		/* Only expose populated DIMMs */
		if (dimm->nr_pages == 0)
			continue;
#ifdef CONFIG_EDAC_DEBUG
		debugf1("%s creating dimm%d, located at ",
			__func__,i);
		if (edac_debug_level >= 1) {
			int lay;
			for (lay = 0; lay < mci->n_layers; lay++)
				printk(KERN_CONT "%s %d ",
					edac_layer_name[mci->layers[lay].type],
					dimm->location[lay]);
			printk(KERN_CONT "\n");
		}
#endif
		err = edac_create_dimm_object(mci, dimm, i);
		if (err) {
			debugf1("%s() failure: create dimm %d obj\n",
				__func__, i);
			goto fail;
		}
	}
#if 0
	err = edac_create_errcount_objects(mci);
	if (err) {
		debugf1("%s() failure: create error count objects\n",
			__func__);
		goto fail2;
	}
#ifdef CONFIG_EDAC_DEBUG
	mci->errinject_attr.attr.name = "fake_inject";
	mci->errinject_attr.attr.mode = S_IRUGO | S_IWUSR;
	mci->errinject_attr.show = edac_fake_inject_show;
	mci->errinject_attr.store = edac_fake_inject_store;
	err = sysfs_create_file(&mci->edac_mci_kobj, &mci->errinject_attr.attr);
	if (err < 0) {
		printk(KERN_ERR
		       "sysfs_create_file for fake inject failed: %d\n", err);
		mci->errinject_attr.attr.name = NULL;
	}
#endif
#endif
	return 0;

fail:
	put_device(&mci->dev);
	return err;
}

/*
 * remove a Memory Controller instance
 */
void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i;

	debugf0("%s()\n", __func__);

	for (i = 0; i < mci->tot_dimms; i++) {
		struct dimm_info *dimm = &mci->dimms[i];
		if (dimm->nr_pages == 0)
			continue;
		debugf0("%s(): removing device %s\n", __func__,
			dev_name(&dimm->dev));
		put_device(&dimm->dev);
	}

	debugf0("%s(): removing device %s\n", __func__,
		dev_name(&mci->dev));
	put_device(&mci->dev);
}

/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/rc
 */

int __init edac_mc_sysfs_init(void)
{
<<<<<<< HEAD
	int rc;
	struct sysdev_class *edac_class;

	/* get the /sys/devices/system/edac class reference */
	edac_class = edac_get_sysfs_class();
	if (edac_class == NULL) {
		debugf1("%s() no edac_class\n", __func__);
		return -EINVAL;
	}

	/*
	 * FIXME: fake a parent device for the EDAC node
	 *
	 * Unfortunately, I couldn't find any easy way to do it, as sysdev
	 * doesn't use struct device.
	 */
	mci_parent.type = &mci_attr_type;
	mci_parent.bus = &mci_bus_type;
	device_initialize(&mci_parent);
	dev_set_name(&mci_parent, "edac");
	memcpy(&mci_parent.kobj, &edac_class->kset.kobj,
	       sizeof(mci_parent.kobj));

	rc = bus_register(&mci_bus_type);
	if (rc) {
		printk(KERN_ERR "rc_core: unable to register rc class\n");
		return rc;
=======
	int err = -EINVAL;
	struct bus_type *edac_subsys;

	debugf1("%s()\n", __func__);

	/* get the /sys/devices/system/edac subsys reference */
	edac_subsys = edac_get_sysfs_subsys();
	if (edac_subsys == NULL) {
		debugf1("%s() no edac_subsys error=%d\n", __func__, err);
		goto fail_out;
	}

	/* Init the MC's kobject */
	mc_kset = kset_create_and_add("mc", NULL, &edac_subsys->dev_root->kobj);
	if (!mc_kset) {
		err = -ENOMEM;
		debugf1("%s() Failed to register '.../edac/mc'\n", __func__);
		goto fail_kset;
>>>>>>> dcd6c92267155e70a94b3927bce681ce74b80d1f
	}

	debugf1("%s() Registered '.../edac/mc' kobject\n", __func__);

	return 0;
<<<<<<< HEAD
=======

fail_kset:
	edac_put_sysfs_subsys();

fail_out:
	return err;
>>>>>>> dcd6c92267155e70a94b3927bce681ce74b80d1f
}

void __exit edac_mc_sysfs_exit(void)
{
<<<<<<< HEAD
	debugf0("%s() removing mc bus\n", __func__);
	edac_put_sysfs_class();
=======
	kset_unregister(mc_kset);
	edac_put_sysfs_subsys();
}
>>>>>>> dcd6c92267155e70a94b3927bce681ce74b80d1f

	bus_unregister(&mci_bus_type);
}

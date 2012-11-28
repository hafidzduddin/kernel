/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson.
 *
 * License Terms: GNU General Public License v2
 */

#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/mfd/abx500.h>
#include <linux/regulator/ab8500-debug.h>
#include <linux/io.h>
#include <mach/db8500-regs.h> /* U8500_BACKUPRAM1_BASE */
#include <mach/hardware.h>

/* board profile address - to determine if suspend-force is default */
#define BOOT_INFO_BACKUPRAM1 (U8500_BACKUPRAM1_BASE + 0xffc)
#define BOARD_PROFILE_BACKUPRAM1 (0x3)

/* board profile option */
#define OPTION_BOARD_VERSION_V5X 50

/* for error prints */
struct device *dev;
struct platform_device *pdev;

/* setting for suspend force (disabled by default) */
static bool setting_suspend_force;

/*
 * regulator states
 */
enum ab8500_regulator_state_id {
	AB8500_REGULATOR_STATE_INIT,
	AB8500_REGULATOR_STATE_SUSPEND,
	AB8500_REGULATOR_STATE_SUSPEND_CORE,
	AB8500_REGULATOR_STATE_RESUME_CORE,
	AB8500_REGULATOR_STATE_RESUME,
	AB8500_REGULATOR_STATE_CURRENT,
	NUM_REGULATOR_STATE
};

static const char *regulator_state_name[NUM_REGULATOR_STATE] = {
	[AB8500_REGULATOR_STATE_INIT] = "init",
	[AB8500_REGULATOR_STATE_SUSPEND] = "suspend",
	[AB8500_REGULATOR_STATE_SUSPEND_CORE] = "suspend-core",
	[AB8500_REGULATOR_STATE_RESUME_CORE] = "resume-core",
	[AB8500_REGULATOR_STATE_RESUME] = "resume",
	[AB8500_REGULATOR_STATE_CURRENT] = "current",
};

/*
 * regulator register definitions
 */
enum ab8500_register_id {
	AB8500_REGU_NOUSE, /* if not defined */
	AB8500_REGU_REQUEST_CTRL1,
	AB8500_REGU_REQUEST_CTRL2,
	AB8500_REGU_REQUEST_CTRL3,
	AB8500_REGU_REQUEST_CTRL4,
	AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
	AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
	AB8500_REGU_HW_HP_REQ1_VALID1,
	AB8500_REGU_HW_HP_REQ1_VALID2,
	AB8500_REGU_HW_HP_REQ2_VALID1,
	AB8500_REGU_HW_HP_REQ2_VALID2,
	AB8500_REGU_SW_HP_REQ_VALID1,
	AB8500_REGU_SW_HP_REQ_VALID2,
	AB8500_REGU_SYSCLK_REQ1_VALID,
	AB8500_REGU_SYSCLK_REQ2_VALID,
	AB8500_REGU_MISC1,
	AB8500_REGU_OTG_SUPPLY_CTRL,
	AB8500_REGU_VUSB_CTRL,
	AB8500_REGU_VAUDIO_SUPPLY,
	AB8500_REGU_CTRL1_VAMIC,
	AB8500_REGU_ARM_REGU1,
	AB8500_REGU_ARM_REGU2,
	AB8500_REGU_VAPE_REGU,
	AB8500_REGU_VSMPS1_REGU,
	AB8500_REGU_VSMPS2_REGU,
	AB8500_REGU_VSMPS3_REGU,
	AB8500_REGU_VPLL_VANA_REGU,
	AB8500_REGU_VREF_DDR,
	AB8500_REGU_EXT_SUPPLY_REGU,
	AB8500_REGU_VAUX12_REGU,
	AB8500_REGU_VRF1_VAUX3_REGU,
	AB8500_REGU_VARM_SEL1,
	AB8500_REGU_VARM_SEL2,
	AB8500_REGU_VARM_SEL3,
	AB8500_REGU_VAPE_SEL1,
	AB8500_REGU_VAPE_SEL2,
	AB8500_REGU_VAPE_SEL3,
	AB8500_REGU_VBB_SEL1,
	AB8500_REGU_VBB_SEL2,
	AB8500_REGU_VSMPS1_SEL1,
	AB8500_REGU_VSMPS1_SEL2,
	AB8500_REGU_VSMPS1_SEL3,
	AB8500_REGU_VSMPS2_SEL1,
	AB8500_REGU_VSMPS2_SEL2,
	AB8500_REGU_VSMPS2_SEL3,
	AB8500_REGU_VSMPS3_SEL1,
	AB8500_REGU_VSMPS3_SEL2,
	AB8500_REGU_VSMPS3_SEL3,
	AB8500_REGU_VAUX1_SEL,
	AB8500_REGU_VAUX2_SEL,
	AB8500_REGU_VRF1_VAUX3_SEL,
	AB8500_REGU_CTRL_EXT_SUP,
	AB8500_REGU_VMOD_REGU,
	AB8500_REGU_VMOD_SEL1,
	AB8500_REGU_VMOD_SEL2,
	AB8500_REGU_CTRL_DISCH,
	AB8500_REGU_CTRL_DISCH2,
	AB8500_OTHER_SYSCLK_CTRL, /* Other */
	AB8500_OTHER_VSIM_SYSCLK_CTRL, /* Other */
	AB8500_OTHER_SYSULPCLK_CTRL1, /* Other */
	AB8500_OTHER_TVOUT_CTRL, /* Other */
	NUM_AB8500_REGISTER
};

struct ab8500_register {
	const char *name;
	u8 bank;
	u8 addr;
};

static struct ab8500_register
		ab8500_register[NUM_AB8500_REGISTER] = {
	[AB8500_REGU_REQUEST_CTRL1] = {
		.name = "ReguRequestCtrl1",
		.bank = 0x03,
		.addr = 0x03,
	},
	[AB8500_REGU_REQUEST_CTRL2] = {
		.name = "ReguRequestCtrl2",
		.bank = 0x03,
		.addr = 0x04,
	},
	[AB8500_REGU_REQUEST_CTRL3] = {
		.name = "ReguRequestCtrl3",
		.bank = 0x03,
		.addr = 0x05,
	},
	[AB8500_REGU_REQUEST_CTRL4] = {
		.name = "ReguRequestCtrl4",
		.bank = 0x03,
		.addr = 0x06,
	},
	[AB8500_REGU_SYSCLK_REQ1_HP_VALID1] = {
		.name = "ReguSysClkReq1HPValid",
		.bank = 0x03,
		.addr = 0x07,
	},
	[AB8500_REGU_SYSCLK_REQ1_HP_VALID2] = {
		.name = "ReguSysClkReq1HPValid2",
		.bank = 0x03,
		.addr = 0x08,
	},
	[AB8500_REGU_HW_HP_REQ1_VALID1] = {
		.name = "ReguHwHPReq1Valid1",
		.bank = 0x03,
		.addr = 0x09,
	},
	[AB8500_REGU_HW_HP_REQ1_VALID2] = {
		.name = "ReguHwHPReq1Valid2",
		.bank = 0x03,
		.addr = 0x0a,
	},
	[AB8500_REGU_HW_HP_REQ2_VALID1] = {
		.name = "ReguHwHPReq2Valid1",
		.bank = 0x03,
		.addr = 0x0b,
	},
	[AB8500_REGU_HW_HP_REQ2_VALID2] = {
		.name = "ReguHwHPReq2Valid2",
		.bank = 0x03,
		.addr = 0x0c,
	},
	[AB8500_REGU_SW_HP_REQ_VALID1] = {
		.name = "ReguSwHPReqValid1",
		.bank = 0x03,
		.addr = 0x0d,
	},
	[AB8500_REGU_SW_HP_REQ_VALID2] = {
		.name = "ReguSwHPReqValid2",
		.bank = 0x03,
		.addr = 0x0e,
	},
	[AB8500_REGU_SYSCLK_REQ1_VALID] = {
		.name = "ReguSysClkReqValid1",
		.bank = 0x03,
		.addr = 0x0f,
	},
	[AB8500_REGU_SYSCLK_REQ2_VALID] = {
		.name = "ReguSysClkReqValid2",
		.bank = 0x03,
		.addr = 0x10,
	},
	[AB8500_REGU_MISC1] = {
		.name = "ReguMisc1",
		.bank = 0x03,
		.addr = 0x80,
	},
	[AB8500_REGU_OTG_SUPPLY_CTRL] = {
		.name = "OTGSupplyCtrl",
		.bank = 0x03,
		.addr = 0x81,
	},
	[AB8500_REGU_VUSB_CTRL] = {
		.name = "VusbCtrl",
		.bank = 0x03,
		.addr = 0x82,
	},
	[AB8500_REGU_VAUDIO_SUPPLY] = {
		.name = "VaudioSupply",
		.bank = 0x03,
		.addr = 0x83,
	},
	[AB8500_REGU_CTRL1_VAMIC] = {
		.name = "ReguCtrl1VAmic",
		.bank = 0x03,
		.addr = 0x84,
	},
	[AB8500_REGU_ARM_REGU1] = {
		.name = "ArmRegu1",
		.bank = 0x04,
		.addr = 0x00,
	},
	[AB8500_REGU_ARM_REGU2] = {
		.name = "ArmRegu2",
		.bank = 0x04,
		.addr = 0x01,
	},
	[AB8500_REGU_VAPE_REGU] = {
		.name = "VapeRegu",
		.bank = 0x04,
		.addr = 0x02,
	},
	[AB8500_REGU_VSMPS1_REGU] = {
		.name = "Vsmps1Regu",
		.bank = 0x04,
		.addr = 0x03,
	},
	[AB8500_REGU_VSMPS2_REGU] = {
		.name = "Vsmps2Regu",
		.bank = 0x04,
		.addr = 0x04,
	},
	[AB8500_REGU_VSMPS3_REGU] = {
		.name = "Vsmps3Regu",
		.bank = 0x04,
		.addr = 0x05,
	},
	[AB8500_REGU_VPLL_VANA_REGU] = {
		.name = "VpllVanaRegu",
		.bank = 0x04,
		.addr = 0x06,
	},
	[AB8500_REGU_VREF_DDR] = {
		.name = "VrefDDR",
		.bank = 0x04,
		.addr = 0x07,
	},
	[AB8500_REGU_EXT_SUPPLY_REGU] = {
		.name = "ExtSupplyRegu",
		.bank = 0x04,
		.addr = 0x08,
	},
	[AB8500_REGU_VAUX12_REGU] = {
		.name = "Vaux12Regu",
		.bank = 0x04,
		.addr = 0x09,
	},
	[AB8500_REGU_VRF1_VAUX3_REGU] = {
		.name = "VRF1Vaux3Regu",
		.bank = 0x04,
		.addr = 0x0a,
	},
	[AB8500_REGU_VARM_SEL1] = {
		.name = "VarmSel1",
		.bank = 0x04,
		.addr = 0x0b,
	},
	[AB8500_REGU_VARM_SEL2] = {
		.name = "VarmSel2",
		.bank = 0x04,
		.addr = 0x0c,
	},
	[AB8500_REGU_VARM_SEL3] = {
		.name = "VarmSel3",
		.bank = 0x04,
		.addr = 0x0d,
	},
	[AB8500_REGU_VAPE_SEL1] = {
		.name = "VapeSel1",
		.bank = 0x04,
		.addr = 0x0e,
	},
	[AB8500_REGU_VAPE_SEL2] = {
		.name = "VapeSel2",
		.bank = 0x04,
		.addr = 0x0f,
	},
	[AB8500_REGU_VAPE_SEL3] = {
		.name = "VapeSel3",
		.bank = 0x04,
		.addr = 0x10,
	},
	[AB8500_REGU_VBB_SEL1] = {
		.name = "VBBSel1",
		.bank = 0x04,
		.addr = 0x11,
	},
	[AB8500_REGU_VBB_SEL2] = {
		.name = "VBBSel2",
		.bank = 0x04,
		.addr = 0x12,
	},
	[AB8500_REGU_VSMPS1_SEL1] = {
		.name = "Vsmps1Sel1",
		.bank = 0x04,
		.addr = 0x13,
	},
	[AB8500_REGU_VSMPS1_SEL2] = {
		.name = "Vsmps1Sel2",
		.bank = 0x04,
		.addr = 0x14,
	},
	[AB8500_REGU_VSMPS1_SEL3] = {
		.name = "Vsmps1Sel3",
		.bank = 0x04,
		.addr = 0x15,
	},
	[AB8500_REGU_VSMPS2_SEL1] = {
		.name = "Vsmps2Sel1",
		.bank = 0x04,
		.addr = 0x17,
	},
	[AB8500_REGU_VSMPS2_SEL2] = {
		.name = "Vsmps2Sel2",
		.bank = 0x04,
		.addr = 0x18,
	},
	[AB8500_REGU_VSMPS2_SEL3] = {
		.name = "Vsmps2Sel3",
		.bank = 0x04,
		.addr = 0x19,
	},
	[AB8500_REGU_VSMPS3_SEL1] = {
		.name = "Vsmps3Sel1",
		.bank = 0x04,
		.addr = 0x1b,
	},
	[AB8500_REGU_VSMPS3_SEL2] = {
		.name = "Vsmps3Sel2",
		.bank = 0x04,
		.addr = 0x1c,
	},
	[AB8500_REGU_VSMPS3_SEL3] = {
		.name = "Vsmps3Sel3",
		.bank = 0x04,
		.addr = 0x1d,
	},
	[AB8500_REGU_VAUX1_SEL] = {
		.name = "Vaux1Sel",
		.bank = 0x04,
		.addr = 0x1f,
	},
	[AB8500_REGU_VAUX2_SEL] = {
		.name = "Vaux2Sel",
		.bank = 0x04,
		.addr = 0x20,
	},
	[AB8500_REGU_VRF1_VAUX3_SEL] = {
		.name = "VRF1Vaux3Sel",
		.bank = 0x04,
		.addr = 0x21,
	},
	[AB8500_REGU_CTRL_EXT_SUP] = {
		.name = "ReguCtrlExtSup",
		.bank = 0x04,
		.addr = 0x22,
	},
	[AB8500_REGU_VMOD_REGU] = {
		.name = "VmodRegu",
		.bank = 0x04,
		.addr = 0x40,
	},
	[AB8500_REGU_VMOD_SEL1] = {
		.name = "VmodSel1",
		.bank = 0x04,
		.addr = 0x41,
	},
	[AB8500_REGU_VMOD_SEL2] = {
		.name = "VmodSel2",
		.bank = 0x04,
		.addr = 0x42,
	},
	[AB8500_REGU_CTRL_DISCH] = {
		.name = "ReguCtrlDisch",
		.bank = 0x04,
		.addr = 0x43,
	},
	[AB8500_REGU_CTRL_DISCH2] = {
		.name = "ReguCtrlDisch2",
		.bank = 0x04,
		.addr = 0x44,
	},
	/* Outside regulator banks */
	[AB8500_OTHER_SYSCLK_CTRL] = {
		.name = "SysClkCtrl",
		.bank = 0x02,
		.addr = 0x0c,
	},
	[AB8500_OTHER_VSIM_SYSCLK_CTRL] = {
		.name = "VsimSysClkCtrl",
		.bank = 0x02,
		.addr = 0x33,
	},
	[AB8500_OTHER_SYSULPCLK_CTRL1] = {
		.name = "SysUlpClkCtrl1",
		.bank = 0x02,
		.addr = 0x0b,
	},
	[AB8500_OTHER_TVOUT_CTRL] = {
		.name = "TVoutCtrl",
		.bank = 0x06,
		.addr = 0x80,
	},
};

static u8 ab8500_register_state[NUM_REGULATOR_STATE][NUM_AB8500_REGISTER];
static bool ab8500_register_state_saved[NUM_REGULATOR_STATE];
static bool ab8500_register_state_save = true;

static int ab8500_regulator_record_state(int state)
{
	u8 val;
	int i;
	int ret;

	/* check arguments */
	if ((state > (NUM_REGULATOR_STATE - 1)) || (state < 0)) {
		dev_err(dev, "Wrong state specified\n");
		return -EINVAL;
	}

	/* record */
	if (!ab8500_register_state_save)
		goto exit;

	ab8500_register_state_saved[state] = true;

	for (i = 1; i < NUM_AB8500_REGISTER; i++) {
		ret = abx500_get_register_interruptible(dev,
			ab8500_register[i].bank,
			ab8500_register[i].addr,
			&val);
		if (ret < 0) {
			dev_err(dev, "abx500_get_reg fail %d, %d\n",
				ret, __LINE__);
			return -EINVAL;
		}

		ab8500_register_state[state][i] = val;
	}
exit:
	return 0;
}

/*
 * regulator register dump
 */
static int ab8500_regulator_dump_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int state, reg_id, i;
	int err;

	/* record current state */
	ab8500_regulator_record_state(AB8500_REGULATOR_STATE_CURRENT);

	/* print dump header */
	err = seq_printf(s, "ab8500-regulator dump:\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	/* print states */
	for (state = NUM_REGULATOR_STATE - 1; state >= 0; state--) {
		if (ab8500_register_state_saved[state])
			err = seq_printf(s, "%16s saved -------",
				regulator_state_name[state]);
		else
			err = seq_printf(s, "%12s not saved -------",
				regulator_state_name[state]);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i\n", __LINE__);

		for (i = 0; i < NUM_REGULATOR_STATE; i++) {
			if (i < state)
				err = seq_printf(s, "-----");
			else if (i == state)
				err = seq_printf(s, "----+");
			else
				err = seq_printf(s, "    |");
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i\n",
					__LINE__);
		}
		err = seq_printf(s, "\n");
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	}

	/* print labels */
	err = seq_printf(s, "\n                       addr\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);

	/* dump registers */
	for (reg_id = 1; reg_id < NUM_AB8500_REGISTER; reg_id++) {
		err = seq_printf(s, "%22s 0x%02x%02x:",
			ab8500_register[reg_id].name,
			ab8500_register[reg_id].bank,
			ab8500_register[reg_id].addr);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				reg_id, __LINE__);

		for (state = 0; state < NUM_REGULATOR_STATE; state++) {
			err = seq_printf(s, " 0x%02x",
				ab8500_register_state[state][reg_id]);
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i, %i\n",
					reg_id, __LINE__);
		}

		err = seq_printf(s, "\n");
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				reg_id, __LINE__);
	}

	return 0;
}

static int ab8500_regulator_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_regulator_dump_print, inode->i_private);
}

static const struct file_operations ab8500_regulator_dump_fops = {
	.open = ab8500_regulator_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/*
 * regulator status print
 */
enum ab8500_regulator_id {
	AB8500_VARM,
	AB8500_VBBP,
	AB8500_VBBN,
	AB8500_VAPE,
	AB8500_VSMPS1,
	AB8500_VSMPS2,
	AB8500_VSMPS3,
	AB8500_VPLL,
	AB8500_VREFDDR,
	AB8500_VMOD,
	AB8500_VEXTSUPPLY1,
	AB8500_VEXTSUPPLY2,
	AB8500_VEXTSUPPLY3,
	AB8500_VRF1,
	AB8500_VANA,
	AB8500_VAUX1,
	AB8500_VAUX2,
	AB8500_VAUX3,
	AB8500_VINTCORE,
	AB8500_VTVOUT,
	AB8500_VAUDIO,
	AB8500_VANAMIC1,
	AB8500_VANAMIC2,
	AB8500_VDMIC,
	AB8500_VUSB,
	AB8500_VOTG,
	AB8500_VBUSBIS,
	AB8500_NUM_REGULATORS,
};

/*
 * regulator_voltage
 */
struct regulator_volt {
	u8 value;
	int volt;
};

struct regulator_volt_range {
	struct regulator_volt start;
	struct regulator_volt step;
	struct regulator_volt end;
};

/*
 * ab8500_regulator
 * @name
 * @update_regid
 * @update_mask
 * @update_val[4] {off, on, hw, lp}
 * @hw_mode_regid
 * @hw_mode_mask
 * @hw_mode_val[4] {hp/lp, hp/off, hp, hp}
 * @hw_valid_regid[4] {sysclkreq1, hw1, hw2, sw}
 * @hw_valid_mask[4] {sysclkreq1, hw1, hw2, sw}
 * @vsel_sel_regid
 * @vsel_sel_mask
 * @vsel_val[333] {sel1, sel2, sel3, sel3}
 * @vsel_regid
 * @vsel_mask
 * @vsel_range
 * @vsel_range_len
 */
struct ab8500_regulator {
	const char *name;
	int update_regid;
	u8 update_mask;
	u8 update_val[4];
	int hw_mode_regid;
	u8 hw_mode_mask;
	u8 hw_mode_val[4];
	int hw_valid_regid[4];
	u8 hw_valid_mask[4];
	int vsel_sel_regid;
	u8 vsel_sel_mask;
	u8 vsel_sel_val[4];
	int vsel_regid[3];
	u8 vsel_mask[3];
	struct regulator_volt_range const *vsel_range[3];
	int vsel_range_len[3];
};

static const char *update_val_name[] = {
	"off",
	"on ",
	"hw ",
	"lp ",
	" - " /* undefined value */
};

static const char *hw_mode_val_name[] = {
	"hp/lp ",
	"hp/off",
	"hp    ",
	"hp    ",
	"-/-   ", /* undefined value */
};

/* voltage selection */
static const struct regulator_volt_range varm_vape_vmod_vsel[] = {
	{ {0x00,  700000}, {0x01,   12500}, {0x35, 1362500} },
	{ {0x36, 1362500}, {0x01,       0}, {0x3f, 1362500} },
};

static const struct regulator_volt_range vbbp_vsel[] = {
	{ {0x00,       0}, {0x10,  100000}, {0x40,  400000} },
	{ {0x50,  400000}, {0x10,       0}, {0x70,  400000} },
	{ {0x80, -400000}, {0x10,       0}, {0xb0, -400000} },
	{ {0xc0, -400000}, {0x10,  100000}, {0xf0, -100000} },
};

static const struct regulator_volt_range vbbn_vsel[] = {
	{ {0x00,       0}, {0x01, -100000}, {0x04, -400000} },
	{ {0x05, -400000}, {0x01,       0}, {0x07, -400000} },
	{ {0x08,       0}, {0x01,  100000}, {0x0c,  400000} },
	{ {0x0d,  400000}, {0x01,       0}, {0x0f,  400000} },
};

static const struct regulator_volt_range vsmps1_vsel[] = {
	{ {0x00, 1100000}, {0x01,       0}, {0x1f, 1100000} },
	{ {0x20, 1100000}, {0x01,   12500}, {0x30, 1300000} },
	{ {0x31, 1300000}, {0x01,       0}, {0x3f, 1300000} },
};

static const struct regulator_volt_range vsmps2_vsel[] = {
	{ {0x00, 1800000}, {0x01,       0}, {0x38, 1800000} },
	{ {0x39, 1800000}, {0x01,   12500}, {0x7f, 1875000} },
};

static const struct regulator_volt_range vsmps3_vsel[] = {
	{ {0x00,  700000}, {0x01,   12500}, {0x35, 1363500} },
	{ {0x36, 1363500}, {0x01,       0}, {0x7f, 1363500} },
};

static const struct regulator_volt_range vaux1_vaux2_vsel[] = {
	{ {0x00, 1100000}, {0x01,  100000}, {0x04, 1500000} },
	{ {0x05, 1800000}, {0x01,   50000}, {0x07, 1900000} },
	{ {0x08, 2500000}, {0x01,       0}, {0x08, 2500000} },
	{ {0x09, 2650000}, {0x01,   50000}, {0x0c, 2800000} },
	{ {0x0d, 2900000}, {0x01,  100000}, {0x0e, 3000000} },
	{ {0x0f, 3300000}, {0x01,       0}, {0x0f, 3300000} },
};

static const struct regulator_volt_range vaux3_vsel[] = {
	{ {0x00, 1200000}, {0x01,  300000}, {0x03, 2100000} },
	{ {0x04, 2500000}, {0x01,  250000}, {0x05, 2750000} },
	{ {0x06, 2790000}, {0x01,       0}, {0x06, 2790000} },
	{ {0x07, 2910000}, {0x01,       0}, {0x07, 2910000} },
};

static const struct regulator_volt_range vrf1_vsel[] = {
	{ {0x00, 1800000}, {0x10,  200000}, {0x10, 2000000} },
	{ {0x20, 2150000}, {0x10,       0}, {0x20, 2150000} },
	{ {0x30, 2500000}, {0x10,       0}, {0x30, 2500000} },
};

static const struct regulator_volt_range vintcore12_vsel[] = {
	{ {0x00, 1200000}, {0x08,   25000}, {0x30, 1350000} },
	{ {0x38, 1350000}, {0x01,       0}, {0x38, 1350000} },
};

/* regulators */
static struct ab8500_regulator ab8500_regulator[AB8500_NUM_REGULATORS] = {
	[AB8500_VARM] = {
		.name              = "Varm",
		.update_regid      = AB8500_REGU_ARM_REGU1,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x02,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x02,
		.vsel_sel_regid    = AB8500_REGU_ARM_REGU1,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VARM_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = varm_vape_vmod_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(varm_vape_vmod_vsel),
		.vsel_regid[1]     = AB8500_REGU_VARM_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = varm_vape_vmod_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(varm_vape_vmod_vsel),
		.vsel_regid[2]     = AB8500_REGU_VARM_SEL3,
		.vsel_mask[2]      = 0x3f,
		.vsel_range[2]     = varm_vape_vmod_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(varm_vape_vmod_vsel),
	},
	[AB8500_VBBP] = {
		.name              = "Vbbp",
		.update_regid      = AB8500_REGU_ARM_REGU2,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x00},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x04,
		.vsel_sel_regid    = AB8500_REGU_ARM_REGU1,
		.vsel_sel_mask     = 0x10,
		.vsel_sel_val      = {0x00, 0x10, 0x00, 0x00},
		.vsel_regid[0]     = AB8500_REGU_VBB_SEL1,
		.vsel_mask[0]      = 0xf0,
		.vsel_range[0]     = vbbp_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vbbp_vsel),
		.vsel_regid[1]     = AB8500_REGU_VBB_SEL2,
		.vsel_mask[1]      = 0xf0,
		.vsel_range[1]     = vbbp_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vbbp_vsel),
	},
	[AB8500_VBBN] = {
		.name              = "Vbbn",
		.update_regid      = AB8500_REGU_ARM_REGU2,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x00},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x04,
		.vsel_sel_regid    = AB8500_REGU_ARM_REGU1,
		.vsel_sel_mask     = 0x20,
		.vsel_sel_val      = {0x00, 0x20, 0x00, 0x00},
		.vsel_regid[0]     = AB8500_REGU_VBB_SEL1,
		.vsel_mask[0]      = 0x0f,
		.vsel_range[0]     = vbbn_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vbbn_vsel),
		.vsel_regid[1]     = AB8500_REGU_VBB_SEL2,
		.vsel_mask[1]      = 0x0f,
		.vsel_range[1]     = vbbn_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vbbn_vsel),
	},
	[AB8500_VAPE] = {
		.name              = "Vape",
		.update_regid      = AB8500_REGU_VAPE_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0x0c,
		.hw_mode_val       = {0x00, 0x04, 0x08, 0x0c},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x01,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x01,
		.vsel_sel_regid    = AB8500_REGU_VAPE_REGU,
		.vsel_sel_mask     = 0x24,
		.vsel_sel_val      = {0x00, 0x04, 0x20, 0x24},
		.vsel_regid[0]     = AB8500_REGU_VAPE_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = varm_vape_vmod_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(varm_vape_vmod_vsel),
		.vsel_regid[1]     = AB8500_REGU_VAPE_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = varm_vape_vmod_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(varm_vape_vmod_vsel),
		.vsel_regid[2]     = AB8500_REGU_VAPE_SEL3,
		.vsel_mask[2]      = 0x3f,
		.vsel_range[2]     = varm_vape_vmod_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(varm_vape_vmod_vsel),
	},
	[AB8500_VSMPS1] = {
		.name              = "Vsmps1",
		.update_regid      = AB8500_REGU_VSMPS1_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0x30,
		.hw_mode_val       = {0x00, 0x10, 0x20, 0x30},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x01,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x01,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x01,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x04,
		.vsel_sel_regid    = AB8500_REGU_VSMPS1_REGU,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VSMPS1_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = vsmps1_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vsmps1_vsel),
		.vsel_regid[1]     = AB8500_REGU_VSMPS1_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = vsmps1_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vsmps1_vsel),
		.vsel_regid[2]     = AB8500_REGU_VSMPS1_SEL3,
		.vsel_mask[2]      = 0x3f,
		.vsel_range[2]     = vsmps1_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(vsmps1_vsel),
	},
	[AB8500_VSMPS2] = {
		.name              = "Vsmps2",
		.update_regid      = AB8500_REGU_VSMPS2_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x02,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x02,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x02,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x08,
		.vsel_sel_regid    = AB8500_REGU_VSMPS2_REGU,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VSMPS2_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = vsmps2_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vsmps2_vsel),
		.vsel_regid[1]     = AB8500_REGU_VSMPS2_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = vsmps2_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vsmps2_vsel),
		.vsel_regid[2]     = AB8500_REGU_VSMPS2_SEL3,
		.vsel_mask[2]      = 0x3f,
		.vsel_range[2]     = vsmps2_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(vsmps2_vsel),
	},
	[AB8500_VSMPS3] = {
		.name              = "Vsmps3",
		.update_regid      = AB8500_REGU_VSMPS3_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x04,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x04,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x04,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x10,
		.vsel_sel_regid    = AB8500_REGU_VSMPS3_REGU,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VSMPS3_SEL1,
		.vsel_mask[0]      = 0x7f,
		.vsel_range[0]     = vsmps3_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vsmps3_vsel),
		.vsel_regid[1]     = AB8500_REGU_VSMPS3_SEL2,
		.vsel_mask[1]      = 0x7f,
		.vsel_range[1]     = vsmps3_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vsmps3_vsel),
		.vsel_regid[2]     = AB8500_REGU_VSMPS3_SEL3,
		.vsel_mask[2]      = 0x7f,
		.vsel_range[2]     = vsmps3_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(vsmps3_vsel),
	},
	[AB8500_VPLL] = {
		.name              = "Vpll",
		.update_regid      = AB8500_REGU_VPLL_VANA_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0x0c,
		.hw_mode_val       = {0x00, 0x04, 0x08, 0x0c},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x10,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x10,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x10,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x40,
	},
	[AB8500_VREFDDR] = {
		.name              = "VrefDDR",
		.update_regid      = AB8500_REGU_VREF_DDR,
		.update_mask       = 0x01,
		.update_val        = {0x00, 0x01, 0x00, 0x00},
	},
	[AB8500_VMOD] = {
		.name              = "Vmod",
		.update_regid      = AB8500_REGU_VMOD_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_VMOD_REGU,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x08,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x08,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x08,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x20,
		.vsel_sel_regid    = AB8500_REGU_VMOD_REGU,
		.vsel_sel_mask     = 0x04,
		.vsel_sel_val      = {0x00, 0x04, 0x00, 0x00},
		.vsel_regid[0]     = AB8500_REGU_VMOD_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = varm_vape_vmod_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(varm_vape_vmod_vsel),
		.vsel_regid[1]     = AB8500_REGU_VMOD_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = varm_vape_vmod_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(varm_vape_vmod_vsel),
	},
	[AB8500_VEXTSUPPLY1] = {
		.name              = "Vextsupply1",
		.update_regid      = AB8500_REGU_EXT_SUPPLY_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x10,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x01,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x01,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x04,
	},
	[AB8500_VEXTSUPPLY2] = {
		.name              = "VextSupply2",
		.update_regid      = AB8500_REGU_EXT_SUPPLY_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x20,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x02,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x02,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x08,
	},
	[AB8500_VEXTSUPPLY3] = {
		.name              = "VextSupply3",
		.update_regid      = AB8500_REGU_EXT_SUPPLY_REGU,
		.update_mask       = 0x30,
		.update_val        = {0x00, 0x10, 0x20, 0x30},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0x0c,
		.hw_mode_val       = {0x00, 0x04, 0x08, 0x0c},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x40,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x04,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x04,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x10,
	},
	[AB8500_VRF1] = {
		.name              = "Vrf1",
		.update_regid      = AB8500_REGU_VRF1_VAUX3_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VRF1_VAUX3_SEL,
		.vsel_mask[0]      = 0x30,
		.vsel_range[0]     = vrf1_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vrf1_vsel),
	},
	[AB8500_VANA] = {
		.name              = "Vana",
		.update_regid      = AB8500_REGU_VPLL_VANA_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0x30,
		.hw_mode_val       = {0x00, 0x10, 0x20, 0x30},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x08,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x08,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x08,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x20,
	},
	[AB8500_VAUX1] = {
		.name              = "Vaux1",
		.update_regid      = AB8500_REGU_VAUX12_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0x30,
		.hw_mode_val       = {0x00, 0x10, 0x20, 0x30},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x20,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x20,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x20,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x80,
		.vsel_regid[0]     = AB8500_REGU_VAUX1_SEL,
		.vsel_mask[0]      = 0x0f,
		.vsel_range[0]     = vaux1_vaux2_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vaux1_vaux2_vsel),
	},
	[AB8500_VAUX2] = {
		.name              = "Vaux2",
		.update_regid      = AB8500_REGU_VAUX12_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x40,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x40,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x40,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x01,
		.vsel_regid[0]     = AB8500_REGU_VAUX2_SEL,
		.vsel_mask[0]      = 0x0f,
		.vsel_range[0]     = vaux1_vaux2_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vaux1_vaux2_vsel),
	},
	[AB8500_VAUX3] = {
		.name              = "Vaux3",
		.update_regid      = AB8500_REGU_VRF1_VAUX3_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL4,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x80,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x80,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x80,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x02,
		.vsel_regid[0]     = AB8500_REGU_VRF1_VAUX3_SEL,
		.vsel_mask[0]      = 0x07,
		.vsel_range[0]     = vaux3_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vaux3_vsel),
	},
	[AB8500_VINTCORE] = {
		.name              = "VintCore12",
		.update_regid      = AB8500_REGU_MISC1,
		.update_mask       = 0x44,
		.update_val        = {0x00, 0x04, 0x00, 0x44},
		.vsel_regid[0]     = AB8500_REGU_MISC1,
		.vsel_mask[0]      = 0x38,
		.vsel_range[0]     = vintcore12_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vintcore12_vsel),
	},
	[AB8500_VTVOUT] = {
		.name              = "VTVout",
		.update_regid      = AB8500_REGU_MISC1,
		.update_mask       = 0x82,
		.update_val        = {0x00, 0x02, 0x00, 0x82},
	},
	[AB8500_VAUDIO] = {
		.name              = "Vaudio",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x02,
		.update_val        = {0x00, 0x02, 0x00, 0x00},
	},
	[AB8500_VANAMIC1] = {
		.name              = "Vanamic1",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x08,
		.update_val        = {0x00, 0x08, 0x00, 0x00},
	},
	[AB8500_VANAMIC2] = {
		.name              = "Vanamic2",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x10,
		.update_val        = {0x00, 0x10, 0x00, 0x00},
	},
	[AB8500_VDMIC] = {
		.name              = "Vdmic",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x04,
		.update_val        = {0x00, 0x04, 0x00, 0x00},
	},
	[AB8500_VUSB] = {
		.name              = "Vusb",
		.update_regid      = AB8500_REGU_VUSB_CTRL,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x00, 0x03},
	},
	[AB8500_VOTG] = {
		.name              = "VOTG",
		.update_regid      = AB8500_REGU_OTG_SUPPLY_CTRL,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x00, 0x03},
	},
	[AB8500_VBUSBIS] = {
		.name              = "Vbusbis",
		.update_regid      = AB8500_REGU_OTG_SUPPLY_CTRL,
		.update_mask       = 0x08,
		.update_val        = {0x00, 0x08, 0x00, 0x00},
	},
};

static int status_state;

static int _get_voltage(struct regulator_volt_range const *volt_range,
	u8 value, int *volt)
{
	u8 start = volt_range->start.value;
	u8 end = volt_range->end.value;
	u8 step = volt_range->step.value;

	/* Check if witin range */
	if (step == 0) {
		if (value == start) {
			*volt = volt_range->start.volt;
			return 1;
		}
	} else {
		if ((start <= value) && (value <= end)) {
			if ((value - start)%step != 0)
				return -EINVAL; /* invalid setting */
			*volt = volt_range->start.volt
			     + volt_range->step.volt
			     *((value - start)/step);
			return 1;
		}
	}

	return 0;
}

static int get_voltage(struct regulator_volt_range const *volt_range,
	int volt_range_len,
	u8 value)
{
	int volt;
	int i, ret;

	for (i = 0; i < volt_range_len; i++) {
		ret = _get_voltage(&volt_range[i], value, &volt);
		if (ret < 0)
			break; /* invalid setting */
		if (ret == 1)
			return volt; /* successful */
	}

	return -EINVAL;
}

static int ab8500_regulator_status_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int id, regid;
	int i;
	u8 val;
	int err;

	/* record current state */
	ab8500_regulator_record_state(AB8500_REGULATOR_STATE_CURRENT);

	/* check if chosen state is recorded */
	if (!ab8500_register_state_saved[status_state]) {
		seq_printf(s, "ab8500-regulator status is not recorded.\n");
		goto exit;
	}

	/* print dump header */
	err = seq_printf(s, "ab8500-regulator status:\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	/* print state */
	err = seq_printf(s, "%12s\n",
		regulator_state_name[status_state]);
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	/* print labels */
	err = seq_printf(s,
	      "+-----------+----+--------------+-------------------------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "|       name|man |auto          |voltage                  |\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "+-----------+----+--------------+ +-----------------------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "|           |mode|mode  |0|1|2|3| |    1  |    2  |    3  |\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "+-----------+----+------+-+-+-+-+-+-------+-------+-------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);

	/* dump registers */
	for (id = 0; id < AB8500_NUM_REGULATORS; id++) {
		/* print name */
		err = seq_printf(s, "|%11s|",
			ab8500_regulator[id].name);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				id, __LINE__);

		/* print manual mode */
		regid = ab8500_regulator[id].update_regid;
		val = ab8500_register_state[status_state][regid]
		    & ab8500_regulator[id].update_mask;
		for (i = 0; i < 4; i++) {
			if (val == ab8500_regulator[id].update_val[i])
				break;
		}
		err = seq_printf(s, "%4s|",
			update_val_name[i]);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				id, __LINE__);

		/* print auto mode */
		regid = ab8500_regulator[id].hw_mode_regid;
		if (regid) {
			val = ab8500_register_state[status_state][regid]
			    & ab8500_regulator[id].hw_mode_mask;
			for (i = 0; i < 4; i++) {
				if (val == ab8500_regulator[id].hw_mode_val[i])
					break;
			}
			err = seq_printf(s, "%6s|",
				hw_mode_val_name[i]);
		} else {
			err = seq_printf(s, "      |");
		}
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				id, __LINE__);

		/* print valid bits */
		for (i = 0; i < 4; i++) {
			regid = ab8500_regulator[id].hw_valid_regid[i];
			if (regid) {
				val = ab8500_register_state[status_state][regid]
				    & ab8500_regulator[id].hw_valid_mask[i];
				if (val)
					err = seq_printf(s, "1|");
				else
					err = seq_printf(s, "0|");
			} else {
				err = seq_printf(s, " |");
			}
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i, %i\n",
					regid, __LINE__);
		}

		/* print voltage selection */
		regid = ab8500_regulator[id].vsel_sel_regid;
		if (regid) {
			val = ab8500_register_state[status_state][regid]
			    & ab8500_regulator[id].vsel_sel_mask;
			for (i = 0; i < 3; i++) {
				if (val == ab8500_regulator[id].vsel_sel_val[i])
					break;
			}
			if (i < 3)
				seq_printf(s, "%i|", i + 1);
			else
				seq_printf(s, "-|");
		} else {
			seq_printf(s, " |");
		}
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				regid, __LINE__);

		for (i = 0; i < 3; i++) {
			int volt;

			regid = ab8500_regulator[id].vsel_regid[i];
			if (regid) {
				val = ab8500_register_state[status_state][regid]
				    & ab8500_regulator[id].vsel_mask[i];
				volt = get_voltage(
					ab8500_regulator[id].vsel_range[i],
					ab8500_regulator[id].vsel_range_len[i],
					val);
				seq_printf(s, "%7i|", volt);
			} else {
				seq_printf(s, "       |");
			}
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i, %i\n",
					regid, __LINE__);
		}

		err = seq_printf(s, "\n");
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				regid, __LINE__);

	}
	err = seq_printf(s,
	      "+-----------+----+------+-+-+-+-+-+-------+-------+-------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "Note! In HW mode, voltage selection is controlled by HW.\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);


exit:
	return 0;
}

static int ab8500_regulator_status_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;
	unsigned long user_val;
	int err;

	/* copy user data */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/* convert */
	err = strict_strtoul(buf, 0, &user_val);
	if (err)
		return -EINVAL;

	/* set suspend force setting */
	if (user_val > NUM_REGULATOR_STATE) {
		dev_err(dev, "debugfs error input > number of states\n");
		return -EINVAL;
	}

	status_state = user_val;

	return buf_size;
}


static int ab8500_regulator_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_regulator_status_print,
		inode->i_private);
}

static const struct file_operations ab8500_regulator_status_fops = {
	.open = ab8500_regulator_status_open,
	.write = ab8500_regulator_status_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

#ifdef CONFIG_PM

struct ab8500_force_reg {
	char *name;
	u8 bank;
	u8 addr;
	u8 mask;
	u8 val;
	bool restore;
	u8 restore_val;
};

static struct ab8500_force_reg ab8500_force_reg[] = {
	{
		/*
		 * SysClkCtrl
		 * OTP: 0x00, HSI: 0x06, suspend: 0x00/0x07 (value/mask)
		 * [  2] USBClkEna = disable SysClk path to USB block
		 * [  1] TVoutClkEna = disable 27Mhz clock to TVout block
		 * [  0] TVoutPllEna = disable TVout pll
		 *                     (generate 27Mhz from SysClk)
		 */
		.name = "SysClkCtrl",
		.bank = 0x02,
		.addr = 0x0c,
		.mask = 0x07,
		.val  = 0x00,
	},
	{
		/*
		 * ReguSysClkReq1HPValid2
		 * OTP: 0x03, HSI: 0x40, suspend: 0x60/0x70 (value/mask)
		 * [  5] VextSupply2SysClkReq1HPValid = Vext2 set by SysClkReq1
		 */
		.name = "ReguSysClkReq1HPValid2",
		.bank = 0x03,
		.addr = 0x08,
		.mask = 0x20, /* test and compare with 0x7f */
		.val  = 0x20,
	},
	{
		/*
		 * ReguRequestCtrl3
		 * OTP: 0x00, HSI: 0x00, suspend: 0x05/0x0f (value/mask)
		 * [1:0] VExtSupply2RequestCtrl[1:0] = VExt2 set in HP/OFF mode
		 */
		.name = "ReguRequestCtrl3",
		.bank = 0x03,
		.addr = 0x05,
		.mask = 0x03, /* test and compare with 0xff */
		.val  = 0x01,
	},
	{
		/*
		 * VsimSysClkCtrl
		 * OTP: 0x01, HSI: 0x21, suspend: 0x01/0xff (value/mask)
		 * [  7] VsimSysClkReq8Valid = no connection
		 * [  6] VsimSysClkReq7Valid = no connection
		 * [  5] VsimSysClkReq6Valid = no connection
		 * [  4] VsimSysClkReq5Valid = no connection
		 * [  3] VsimSysClkReq4Valid = no connection
		 * [  2] VsimSysClkReq3Valid = no connection
		 * [  1] VsimSysClkReq2Valid = no connection
		 * [  0] VsimSysClkReq1Valid = Vsim set by SysClkReq1
		 */
		.name = "VsimSysClkCtrl",
		.bank = 0x02,
		.addr = 0x33,
		.mask = 0xff,
		.val  = 0x01,
	},
	{
		/*
		 * SysUlpClkCtrl1
		 * OTP: 0x00, HSI: 0x00, suspend: 0x00/0x0f (value/mask)
		 * [  3] 4500SysClkReq = inactive
		 * [  2] UlpClkReq = inactive
		 * [1:0] SysUlpClkIntSel[1:0] = no internal clock switching.
		 *                              Internal clock is SysClk.
		 */
		.name = "SysUlpClkCtrl1",
		.bank = 0x02,
		.addr = 0x0b,
		.mask = 0x0f,
		.val  = 0x00,
	},
	{
		/*
		 * ExtSupplyRegu (HSI: 0x2a on v2-v40?)
		 * OTP: 0x15, HSI: 0x28, suspend: 0x28/0x3f (value/mask)
		 * [5:4] VExtSupply3Regu[1:0] = 10 = Vext3 off
		 * [3:2] VExtSupply2Regu[1:0] = 10 = Vext2 in HW control
		 * [1:0] VExtSupply1Regu[1:0] = 00 = Vext1 off
		 */
		.name = "ExtSupplyRegu",
		.bank = 0x04,
		.addr = 0x08,
		.mask = 0x3f,
		.val  = 0x08,
	},
	{
		/*
		 * TVoutCtrl
		 * OTP: N/A, HSI: N/A, suspend: 0x00/0x03 (value/mask)
		 * [  2]   PlugTvOn = plug/unplug detection disabled
		 * [1:0]   TvoutDacCtrl[1:0] = "0" forced on DAC input (test)
		 */
		.name = "TVoutCtrl",
		.bank = 0x06,
		.addr = 0x80,
		.mask = 0x03,
		.val  = 0x00,
	},
};

void ab8500_regulator_debug_force(void)
{
	int ret, i;

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_SUSPEND);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record suspend state.\n");

	/* check if registers should be forced */
	if (!setting_suspend_force)
		goto exit;

	/*
	 * Optimize href v2_v50_pwr board for ApSleep/ApDeepSleep
	 * power consumption measurements
	 */

	for (i = 0; i < ARRAY_SIZE(ab8500_force_reg); i++) {
		dev_vdbg(&pdev->dev, "Save and set %s: "
			"0x%02x, 0x%02x, 0x%02x, 0x%02x.\n",
			ab8500_force_reg[i].name,
			ab8500_force_reg[i].bank,
			ab8500_force_reg[i].addr,
			ab8500_force_reg[i].mask,
			ab8500_force_reg[i].val);

		/* assume that register should be restored */
		ab8500_force_reg[i].restore = true;

		/* get register value before forcing it */
		ret = abx500_get_register_interruptible(&pdev->dev,
			ab8500_force_reg[i].bank,
			ab8500_force_reg[i].addr,
			&ab8500_force_reg[i].restore_val);
		if (ret < 0) {
			dev_err(dev, "Failed to read %s.\n",
				ab8500_force_reg[i].name);
			ab8500_force_reg[i].restore = false;
			break;
		}

		/* force register value */
		ret = abx500_mask_and_set_register_interruptible(&pdev->dev,
			ab8500_force_reg[i].bank,
			ab8500_force_reg[i].addr,
			ab8500_force_reg[i].mask,
			ab8500_force_reg[i].val);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to write %s.\n",
				ab8500_force_reg[i].name);
			ab8500_force_reg[i].restore = false;
		}
	}

exit:
	/* save state of registers */
	ret = ab8500_regulator_record_state(
		AB8500_REGULATOR_STATE_SUSPEND_CORE);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record suspend state.\n");

	return;
}

void ab8500_regulator_debug_restore(void)
{
	int ret, i;

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_RESUME_CORE);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record resume state.\n");
	for (i = ARRAY_SIZE(ab8500_force_reg) - 1; i >= 0; i--) {
		/* restore register value */
		if (ab8500_force_reg[i].restore) {
			ret = abx500_mask_and_set_register_interruptible(
				 &pdev->dev,
				 ab8500_force_reg[i].bank,
				 ab8500_force_reg[i].addr,
				 ab8500_force_reg[i].mask,
				 ab8500_force_reg[i].restore_val);
			if (ret < 0)
				dev_err(&pdev->dev, "Failed to restore %s.\n",
					ab8500_force_reg[i].name);
			dev_vdbg(&pdev->dev, "Restore %s: "
				"0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
				ab8500_force_reg[i].name,
				ab8500_force_reg[i].bank,
				ab8500_force_reg[i].addr,
				ab8500_force_reg[i].mask,
				ab8500_force_reg[i].restore_val);
		}
	}

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_RESUME);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record resume state.\n");

	return;
}

#endif

static int ab8500_regulator_suspend_force_show(struct seq_file *s, void *p)
{
	/* print suspend standby status */
	if (setting_suspend_force)
		return seq_printf(s, "suspend force enabled\n");
	else
		return seq_printf(s, "no suspend force\n");
}

static int ab8500_regulator_suspend_force_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;
	unsigned long user_val;
	int err;

	/* copy user data */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/* convert */
	err = strict_strtoul(buf, 0, &user_val);
	if (err)
		return -EINVAL;

	/* set suspend force setting */
	if (user_val > 1) {
		dev_err(dev, "debugfs error input > 1\n");
		return -EINVAL;
	}

	if (user_val)
		setting_suspend_force = true;
	else
		setting_suspend_force = false;

	return buf_size;
}

static int ab8500_regulator_suspend_force_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, ab8500_regulator_suspend_force_show,
		inode->i_private);
}

static const struct file_operations ab8500_regulator_suspend_force_fops = {
	.open = ab8500_regulator_suspend_force_open,
	.write = ab8500_regulator_suspend_force_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *ab8500_regulator_dir;
static struct dentry *ab8500_regulator_dump_file;
static struct dentry *ab8500_regulator_status_file;
static struct dentry *ab8500_regulator_suspend_force_file;

static int __devinit ab8500_regulator_debug_probe(struct platform_device *plf)
{
	void __iomem *boot_info_backupram;
	int ret, i;

	/* setup dev pointers */
	dev = &plf->dev;
	pdev = plf;

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_INIT);
	if (ret < 0)
		dev_err(&plf->dev, "Failed to record init state.\n");

	/* remove force of external regulators if AB8500 3.0 and DB8500 v2.2 */
	if ((abx500_get_chip_id(&pdev->dev) >= 0x30) && cpu_is_u8500v22()) {
		/*
		 * find ExtSupplyRegu register (bank 0x04, addr 0x08)
		 * and update value (Vext1 in low-power, Vext2 and Vext3
		 * off).
		 */
		for (i = 0; i < ARRAY_SIZE(ab8500_force_reg); i++) {
			if (ab8500_force_reg[i].bank == 0x04 &&
			    ab8500_force_reg[i].addr == 0x08) {
				ab8500_force_reg[i].val = 0x03;
			}
		}
	}

	/* make suspend-force default if board profile is v5x-power */
	boot_info_backupram = ioremap(BOOT_INFO_BACKUPRAM1, 0x4);

	if (boot_info_backupram) {
		u8 board_profile;
		board_profile = readb(
			boot_info_backupram + BOARD_PROFILE_BACKUPRAM1);
		dev_dbg(dev, "Board profile is 0x%02x\n", board_profile);

		if (board_profile >= OPTION_BOARD_VERSION_V5X)
			setting_suspend_force = true;

		iounmap(boot_info_backupram);
	} else {
		dev_err(dev, "Failed to read backupram.\n");
	}

	/* create directory */
	ab8500_regulator_dir = debugfs_create_dir("ab8500-regulator", NULL);
	if (!ab8500_regulator_dir)
		goto exit_no_debugfs;

	/* create "dump" file */
	ab8500_regulator_dump_file = debugfs_create_file("dump",
		S_IRUGO, ab8500_regulator_dir, &plf->dev,
		&ab8500_regulator_dump_fops);
	if (!ab8500_regulator_dump_file)
		goto exit_destroy_dir;

	/* create "status" file */
	ab8500_regulator_status_file = debugfs_create_file("status",
		S_IRUGO, ab8500_regulator_dir, &plf->dev,
		&ab8500_regulator_status_fops);
	if (!ab8500_regulator_status_file)
		goto exit_destroy_dump_file;

	/*
	 * create "suspend-force-v5x" file. As indicated by the name, this is
	 * only applicable for v2_v5x hardware versions.
	 */
	ab8500_regulator_suspend_force_file = debugfs_create_file(
		"suspend-force-v5x",
		S_IRUGO, ab8500_regulator_dir, &plf->dev,
		&ab8500_regulator_suspend_force_fops);
	if (!ab8500_regulator_suspend_force_file)
		goto exit_destroy_status_file;

	return 0;

exit_destroy_status_file:
	debugfs_remove(ab8500_regulator_status_file);
exit_destroy_dump_file:
	debugfs_remove(ab8500_regulator_dump_file);
exit_destroy_dir:
	debugfs_remove(ab8500_regulator_dir);
exit_no_debugfs:
	dev_err(&plf->dev, "failed to create debugfs entries.\n");
	return -ENOMEM;
}

static int __devexit ab8500_regulator_debug_remove(struct platform_device *plf)
{
	debugfs_remove(ab8500_regulator_suspend_force_file);
	debugfs_remove(ab8500_regulator_status_file);
	debugfs_remove(ab8500_regulator_dump_file);
	debugfs_remove(ab8500_regulator_dir);

	return 0;
}

static struct platform_driver ab8500_regulator_debug_driver = {
	.driver = {
		.name = "ab8500-regulator-debug",
		.owner = THIS_MODULE,
	},
	.probe	= ab8500_regulator_debug_probe,
	.remove	= __devexit_p(ab8500_regulator_debug_remove),
};

static int __init ab8500_regulator_debug_init(void)
{
	int ret;

	ret = platform_driver_register(&ab8500_regulator_debug_driver);
	if (ret)
		pr_err("Failed to register ab8500 regulator: %d\n", ret);

	return ret;
}
subsys_initcall(ab8500_regulator_debug_init);

static void __exit ab8500_regulator_debug_exit(void)
{
	platform_driver_unregister(&ab8500_regulator_debug_driver);
}
module_exit(ab8500_regulator_debug_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bengt Jonsson <bengt.g.jonsson@stericsson.com");
MODULE_DESCRIPTION("AB8500 Regulator Debug");
MODULE_ALIAS("platform:ab8500-regulator-debug");

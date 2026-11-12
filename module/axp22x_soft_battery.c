// SPDX-License-Identifier: GPL-2.0
/*
 * The battery of the uConsole by a model of the device tree, in place of
 * axp20x_battery (docs/plan.md of axp22x-soft-battery).
 *
 * The module binds to the cell battery-power-supply of the MFD axp20x:
 * from the cell it takes the regmap of the PMIC, the IIO channels of
 * axp20x_adc and the supply axp22x-ac. Every value is in the device tree,
 * in a node of its own at the root (compatible "axp22x-soft-battery") and
 * in the simple-battery of its monitored-battery; the module has no
 * default (D106, D111). The level is read from the voltage on the curves
 * of the device tree; the module counts no charge (D90).
 *
 * At the silence of the AXP (all reads -ENXIO) the dynamic properties give
 * -ENODATA (D94); after guard-samples failed samples the module calls
 * hw_protection_trigger(), the one power off it does (D71).
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/iio/consumer.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/mfd/axp20x.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

/* axp20x_battery built in binds first; the blacklist cannot stop it (D36) */
#if IS_BUILTIN(CONFIG_BATTERY_AXP20X)
#error "CONFIG_BATTERY_AXP20X=y: axp20x_battery must be a module (m) or off (n)"
#endif

#define SB_COMPATIBLE		"axp22x-soft-battery"
#define SB_PROP(name)		"axp22x-soft-battery," name

/* the registers of the AXP22x (axp228 datasheet, REG 01H, 31H, 33H) */
#define SB_OP_CHARGING		BIT(6)		/* 0x01: the charger charges */
#define SB_V_OFF_MASK		GENMASK(2, 0)	/* 0x31: 2.6 V + n * 0.1 V */
#define SB_CHRG_VOLT_MASK	GENMASK(6, 5)	/* 0x33: the target voltage */
#define SB_CHRG_CURR_MASK	GENMASK(3, 0)	/* 0x33: 300 mA + n * 150 mA */

#define SB_MEAN_SAMPLES		3		/* the means of 30 s at 10 s */
#define SB_CV_MAX_PAIRS		16		/* charge-cv-table (D110) */
#define SB_GUARD_MS		20000		/* hw_protection_trigger() */

/* bounds of the checks, not settings (D54) */
#define SB_R_MIN_UOHM		10000
#define SB_R_MAX_UOHM		500000
#define SB_END_MAX_UV		3800000

/* the supply of the charger, the AC cell of the same PMIC (axp20x_ac_power.c) */
static char *sb_supplied_from[] = { "axp22x-ac" };

struct sb_cv_pair {
	u32 ua;
	u32 pct;
};

struct sb {
	struct device *dev;
	struct regmap *regmap;
	struct iio_channel *batt_v, *batt_chrg_i, *batt_dischrg_i;
	struct device_node *np;
	struct power_supply *psy;
	struct delayed_work work;

	/* the settings, read once by sb_init() */
	u32 energy_full_uwh;
	u32 end_uv;
	u32 interval_ms;
	u32 full_margin_uv;
	u32 full_current_ua;
	u32 silent_samples;
	u32 guard_samples;
	struct sb_cv_pair cv[SB_CV_MAX_PAIRS];
	int cv_len;
	int full_uv;		/* constant-charge-voltage-max - full-margin */

	/* the state of the work, touched by the work only */
	int v_uv[SB_MEAN_SAMPLES];
	int i_ua[SB_MEAN_SAMPLES];
	int n_mean;
	int pos_mean;
	unsigned int failed;
	int prev_level;		/* -1: none yet */
	bool prev_supplied;
	int prev_status;
	bool at_zero;

	/* what get_property gives: a copy of the last sample, under lock */
	struct mutex lock;
	struct {
		bool valid;
		bool silent;
		int status;
		int capacity;
		int voltage_uv;
		int current_ua;
	} snap;
};

/* --- the device tree --- */

static int sb_read_u32(struct sb *sb, const char *name, u32 *val)
{
	int ret = of_property_read_u32(sb->np, name, val);

	if (ret)
		dev_err(sb->dev, "%s missing\n", name);
	return ret ? -EINVAL : 0;
}

static int sb_refuse(struct sb *sb, const char *name, long long val,
		     const char *why)
{
	dev_err(sb->dev, "%s refused: %lld, %s\n", name, val, why);
	return -EINVAL;
}

static int sb_check_range(struct sb *sb, const char *name, u32 val,
			  u32 min, u32 max)
{
	if (val < min || val > max) {
		dev_err(sb->dev, "%s refused: %u, not in %u..%u\n",
			name, val, min, max);
		return -EINVAL;
	}
	return 0;
}

/* the simple-battery of monitored-battery, as the core read it */
static int sb_check_battery_info(struct sb *sb)
{
	struct power_supply_battery_info *info = sb->psy->battery_info;
	int i, prev;

	if (!info) {
		dev_err(sb->dev, "monitored-battery missing\n");
		return -EINVAL;
	}
	if (info->technology == POWER_SUPPLY_TECHNOLOGY_UNKNOWN) {
		dev_err(sb->dev, "device-chemistry missing\n");
		return -EINVAL;
	}
	if (info->energy_full_design_uwh <= 0)
		return sb_refuse(sb, "energy-full-design-microwatt-hours",
				 info->energy_full_design_uwh, "missing or not positive");
	if (info->voltage_max_design_uv <= 0)
		return sb_refuse(sb, "voltage-max-design-microvolt",
				 info->voltage_max_design_uv, "missing or not positive");

	/* V_OFF: 2.6-3.3 V by 0.1 V */
	if (info->voltage_min_design_uv < 2600000 ||
	    info->voltage_min_design_uv > 3300000 ||
	    info->voltage_min_design_uv % 100000)
		return sb_refuse(sb, "voltage-min-design-microvolt",
				 info->voltage_min_design_uv,
				 "not 2600000..3300000 by 100000");

	/* the target of the charge: the four codes of the AXP22x */
	switch (info->constant_charge_voltage_max_uv) {
	case 4100000:
	case 4200000:
	case 4220000:
	case 4240000:
		break;
	default:
		return sb_refuse(sb, "constant-charge-voltage-max-microvolt",
				 info->constant_charge_voltage_max_uv,
				 "not 4100000, 4200000, 4220000 or 4240000");
	}

	/* the current of the charge: 300-2550 mA by 150 mA */
	if (info->constant_charge_current_max_ua < 300000 ||
	    info->constant_charge_current_max_ua > 2550000 ||
	    (info->constant_charge_current_max_ua - 300000) % 150000)
		return sb_refuse(sb, "constant-charge-current-max-microamp",
				 info->constant_charge_current_max_ua,
				 "not 300000..2550000 by 150000");

	if (info->factory_internal_resistance_uohm < SB_R_MIN_UOHM ||
	    info->factory_internal_resistance_uohm > SB_R_MAX_UOHM)
		return sb_refuse(sb, "factory-internal-resistance-micro-ohms",
				 info->factory_internal_resistance_uohm,
				 "missing or not 10000..500000");

	/* the core read the table only with ocv-capacity-celsius */
	if (!info->ocv_table[0] || info->ocv_table_size[0] < 2) {
		dev_err(sb->dev, "ocv-capacity-table-0 or ocv-capacity-celsius missing\n");
		return -EINVAL;
	}
	prev = INT_MAX;
	for (i = 0; i < info->ocv_table_size[0]; i++) {
		if (info->ocv_table[0][i].ocv >= prev)
			return sb_refuse(sb, "ocv-capacity-table-0",
					 info->ocv_table[0][i].ocv,
					 "the voltage does not fall");
		prev = info->ocv_table[0][i].ocv;
	}
	return 0;
}

static int sb_read_cv_table(struct sb *sb)
{
	const char *name = SB_PROP("charge-cv-table");
	u32 raw[SB_CV_MAX_PAIRS * 2];
	int n, i, ret;

	n = of_property_count_u32_elems(sb->np, name);
	if (n == -EINVAL) {
		sb->cv_len = 0;		/* optional (D110) */
		return 0;
	}
	if (n < 2 || n > SB_CV_MAX_PAIRS * 2 || n % 2)
		return sb_refuse(sb, name, n, "not 1..16 pairs");

	ret = of_property_read_u32_array(sb->np, name, raw, n);
	if (ret)
		return sb_refuse(sb, name, ret, "not read");

	sb->cv_len = n / 2;
	for (i = 0; i < sb->cv_len; i++) {
		sb->cv[i].ua = raw[2 * i];
		sb->cv[i].pct = raw[2 * i + 1];
		if (sb->cv[i].pct > 100)
			return sb_refuse(sb, name, sb->cv[i].pct, "a percent over 100");
		if (i && (sb->cv[i].ua >= sb->cv[i - 1].ua ||
			  sb->cv[i].pct <= sb->cv[i - 1].pct))
			return sb_refuse(sb, name, i,
					 "the current must fall and the percent rise");
	}
	return 0;
}

static int sb_read_settings(struct sb *sb)
{
	struct power_supply_battery_info *info = sb->psy->battery_info;
	int ret;

	ret = sb_read_u32(sb, SB_PROP("energy-full-microwatt-hours"), &sb->energy_full_uwh) ?:
	      sb_read_u32(sb, SB_PROP("end-microvolt"), &sb->end_uv) ?:
	      sb_read_u32(sb, SB_PROP("sample-interval-ms"), &sb->interval_ms) ?:
	      sb_read_u32(sb, SB_PROP("full-margin-microvolt"), &sb->full_margin_uv) ?:
	      sb_read_u32(sb, SB_PROP("full-current-microamp"), &sb->full_current_ua) ?:
	      sb_read_u32(sb, SB_PROP("silent-samples"), &sb->silent_samples) ?:
	      sb_read_u32(sb, SB_PROP("guard-samples"), &sb->guard_samples);
	if (ret)
		return ret;

	ret = sb_check_range(sb, SB_PROP("energy-full-microwatt-hours"),
			     sb->energy_full_uwh, 1, INT_MAX) ?:
	      sb_check_range(sb, SB_PROP("end-microvolt"), sb->end_uv,
			     info->voltage_min_design_uv, SB_END_MAX_UV) ?:
	      sb_check_range(sb, SB_PROP("sample-interval-ms"),
			     sb->interval_ms, 1000, 600000) ?:
	      sb_check_range(sb, SB_PROP("full-margin-microvolt"),
			     sb->full_margin_uv, 1, 500000) ?:
	      sb_check_range(sb, SB_PROP("full-current-microamp"),
			     sb->full_current_ua, 1, 1000000) ?:
	      sb_check_range(sb, SB_PROP("silent-samples"),
			     sb->silent_samples, 1, 1000) ?:
	      sb_check_range(sb, SB_PROP("guard-samples"), sb->guard_samples,
			     sb->silent_samples + 1, 1000);
	if (ret)
		return ret;

	sb->full_uv = info->constant_charge_voltage_max_uv - sb->full_margin_uv;
	return sb_read_cv_table(sb);
}

/* --- the set up of the PMIC (D5, D105, D112) --- */

static int sb_setup_pmic(struct sb *sb, int *n_writes)
{
	struct power_supply_battery_info *info = sb->psy->battery_info;
	const char *name = SB_PROP("reg-writes");
	u32 *w;
	unsigned int volt;
	int n, i, ret;

	n = of_property_count_u32_elems(sb->np, name);
	if (n == -EINVAL) {
		dev_err(sb->dev, "%s missing\n", name);
		return -EINVAL;
	}
	if (n < 3 || n % 3)
		return sb_refuse(sb, name, n, "not triples <reg mask value>");

	w = kmalloc_array(n, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;
	ret = of_property_read_u32_array(sb->np, name, w, n);
	if (ret) {
		sb_refuse(sb, name, ret, "not read");
		goto out;
	}
	for (i = 0; i < n; i += 3) {
		if (w[i] > 0xff || w[i + 1] > 0xff || w[i + 2] > 0xff ||
		    (w[i + 2] & ~w[i + 1])) {
			ret = sb_refuse(sb, name, i / 3,
					"a value over 0xff, or a bit out of the mask");
			goto out;
		}
	}

	/* the codes of 0x33 [6:5] on the AXP22x (axp20x_battery.c) */
	switch (info->constant_charge_voltage_max_uv) {
	case 4100000:
		volt = 0;
		break;
	case 4220000:
		volt = 1;
		break;
	case 4200000:
		volt = 2;
		break;
	default:	/* 4240000, checked */
		volt = 3;
		break;
	}

	ret = regmap_update_bits(sb->regmap, AXP20X_V_OFF, SB_V_OFF_MASK,
				 (info->voltage_min_design_uv - 2600000) / 100000) ?:
	      regmap_update_bits(sb->regmap, AXP20X_CHRG_CTRL1,
				 SB_CHRG_VOLT_MASK | SB_CHRG_CURR_MASK,
				 (volt << 5) |
				 (info->constant_charge_current_max_ua - 300000) / 150000);
	for (i = 0; !ret && i < n; i += 3)
		ret = regmap_update_bits(sb->regmap, w[i], w[i + 1], w[i + 2]);
	if (ret)
		dev_err(sb->dev, "set up of the PMIC failed: %d\n", ret);
	*n_writes = n / 3;
out:
	kfree(w);
	return ret;
}

/*
 * The init of the power supply: after the core read monitored-battery,
 * before the device is added. A bad value fails the registration, so no
 * battery is there with a bad model (D106).
 */
static int sb_init(struct power_supply *psy)
{
	struct sb *sb = power_supply_get_drvdata(psy);
	struct power_supply_battery_info *info;
	int n_writes, ret;

	sb->psy = psy;
	psy->supplied_from = sb_supplied_from;
	psy->num_supplies = ARRAY_SIZE(sb_supplied_from);

	ret = sb_check_battery_info(sb) ?: sb_read_settings(sb) ?:
	      sb_setup_pmic(sb, &n_writes);
	if (ret)
		return ret;

	info = psy->battery_info;
	dev_info(sb->dev, "bound: charge current %d mA, V_OFF %d mV, full at %d mV; "
		 "R %d mOhm, energy_full %u mWh, curve of the charge %s, %d reg-writes\n",
		 info->constant_charge_current_max_ua / 1000,
		 info->voltage_min_design_uv / 1000,
		 info->constant_charge_voltage_max_uv / 1000,
		 info->factory_internal_resistance_uohm / 1000,
		 sb->energy_full_uwh / 1000,
		 sb->cv_len ? "from the device tree" : "none", n_writes);
	return 0;
}

/* --- the level (D90, D91, D102, D110) --- */

static int sb_level_cv(struct sb *sb, int ua)
{
	int i;

	for (i = 0; i + 1 < sb->cv_len; i++) {
		const struct sb_cv_pair *a = &sb->cv[i], *b = &sb->cv[i + 1];

		if (ua > b->ua)
			return a->pct + (int)((s64)(a->ua - ua) * (b->pct - a->pct) /
					      (a->ua - b->ua));
	}
	return sb->cv[sb->cv_len - 1].pct;
}

static int sb_level(struct sb *sb, int status, bool supplied, int v_uv, int i_ua)
{
	struct power_supply_battery_info *info = sb->psy->battery_info;
	int ocv, level;

	if (status == POWER_SUPPLY_STATUS_FULL)
		return 100;

	/* the phase of constant voltage, on the curve of the charge */
	if (supplied && v_uv >= sb->full_uv && sb->cv_len && i_ua <= (int)sb->cv[0].ua)
		return sb_level_cv(sb, max(i_ua, 0));

	/* OCV = V - I * R, I positive on a charge */
	ocv = v_uv - (int)div_s64((s64)i_ua * info->factory_internal_resistance_uohm,
				  1000000);
	level = power_supply_batinfo_ocv2cap(info, ocv, info->ocv_temp[0]);
	level = clamp(level, 0, 100);

	if (!supplied && v_uv <= (int)sb->end_uv)
		level = 0;
	return level;
}

/* --- the work --- */

static void sb_mean_reset(struct sb *sb)
{
	sb->n_mean = 0;
	sb->pos_mean = 0;
	sb->prev_level = -1;
}

static void sb_schedule(struct sb *sb)
{
	queue_delayed_work(system_power_efficient_wq, &sb->work,
			   round_jiffies_relative(msecs_to_jiffies(sb->interval_ms)));
}

static void sb_failed_sample(struct sb *sb, int err)
{
	sb->failed++;

	if (sb->failed == sb->silent_samples) {
		scoped_guard(mutex, &sb->lock)
			sb->snap.silent = true;
		sb_mean_reset(sb);
		dev_warn(sb->dev, "AXP silent: %u samples failed, last %d\n",
			 sb->failed, err);
		power_supply_changed(sb->psy);
	}
	if (sb->failed == sb->guard_samples) {
		dev_emerg(sb->dev, "guard: power off\n");
		hw_protection_trigger("AXP silent", SB_GUARD_MS);
	}
}

static void sb_work(struct work_struct *work)
{
	struct sb *sb = container_of(to_delayed_work(work), struct sb, work);
	int v_mv, chrg_ma, dischrg_ma, rv, rc, rd, v_uv, i_ua, status, level, i;
	unsigned int op;
	bool supplied, changed;
	s64 sum_v = 0, sum_i = 0;

	rv = iio_read_channel_processed(sb->batt_v, &v_mv);
	rc = iio_read_channel_processed(sb->batt_chrg_i, &chrg_ma);
	rd = iio_read_channel_processed(sb->batt_dischrg_i, &dischrg_ma);

	/* the silence: all three reads find no AXP on the bus (D41) */
	if (rv == -ENXIO && rc == -ENXIO && rd == -ENXIO) {
		sb_failed_sample(sb, rv);
		goto out;
	}
	if (rv || rc || rd) {
		dev_err_ratelimited(sb->dev, "ADC read failed: %d\n",
				    rv ?: rc ?: rd);
		goto out;
	}
	if (sb->failed >= sb->silent_samples)
		dev_info(sb->dev, "AXP answers again after %u failed samples\n",
			 sb->failed);
	sb->failed = 0;

	supplied = power_supply_am_i_supplied(sb->psy) > 0;

	/* status (D40): 0x01 only with the charger in and a small current */
	if (!supplied) {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else if (chrg_ma * 1000 >= (int)sb->full_current_ua) {
		status = POWER_SUPPLY_STATUS_CHARGING;
	} else {
		if (regmap_read(sb->regmap, AXP20X_PWR_OP_MODE, &op)) {
			dev_err_ratelimited(sb->dev, "read of 0x01 failed\n");
			goto out;
		}
		if (op & SB_OP_CHARGING)
			status = POWER_SUPPLY_STATUS_CHARGING;
		else if (v_mv * 1000 >= sb->full_uv)
			status = POWER_SUPPLY_STATUS_FULL;
		else
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}

	/* the means of 30 s; the charger in or out starts them again */
	if (supplied != sb->prev_supplied)
		sb_mean_reset(sb);
	sb->v_uv[sb->pos_mean] = v_mv * 1000;
	sb->i_ua[sb->pos_mean] = (chrg_ma - dischrg_ma) * 1000;
	sb->pos_mean = (sb->pos_mean + 1) % SB_MEAN_SAMPLES;
	sb->n_mean = min(sb->n_mean + 1, SB_MEAN_SAMPLES);
	for (i = 0; i < sb->n_mean; i++) {
		sum_v += sb->v_uv[i];
		sum_i += sb->i_ua[i];
	}
	v_uv = div_s64(sum_v, sb->n_mean);
	i_ua = div_s64(sum_i, sb->n_mean);

	/* the level does not rise on a discharge, nor fall on a charge */
	level = sb_level(sb, status, supplied, v_uv, i_ua);
	if (sb->prev_level >= 0)
		level = supplied ? max(level, sb->prev_level) :
				   min(level, sb->prev_level);

	scoped_guard(mutex, &sb->lock) {
		changed = !sb->snap.valid || sb->snap.silent ||
			  sb->snap.status != status || sb->snap.capacity != level;
		sb->snap.valid = true;
		sb->snap.silent = false;
		sb->snap.status = status;
		sb->snap.capacity = level;
		sb->snap.voltage_uv = v_mv * 1000;
		sb->snap.current_ua = (chrg_ma - dischrg_ma) * 1000;
	}

	if (supplied != sb->prev_supplied)
		dev_info(sb->dev, "%s\n", supplied ? "charger in" : "charger out");
	if (status == POWER_SUPPLY_STATUS_FULL && sb->prev_status != status)
		dev_info(sb->dev, "full: %d mV at %d mA\n", v_mv, chrg_ma);
	if (level == 0 && !sb->at_zero)
		dev_info(sb->dev, "level 0 %%: %d mV at %d mA\n",
			 v_uv / 1000, -i_ua / 1000);
	sb->at_zero = level == 0;
	sb->prev_supplied = supplied;
	sb->prev_status = status;
	sb->prev_level = level;

	if (changed)
		power_supply_changed(sb->psy);
out:
	sb_schedule(sb);
}

static void sb_external_power_changed(struct power_supply *psy)
{
	struct sb *sb = power_supply_get_drvdata(psy);

	/* a sample now, not at the end of the period (D39) */
	mod_delayed_work(system_power_efficient_wq, &sb->work, 0);
}

/* --- the properties (D43, D55, D92-D95) --- */

static const enum power_supply_property sb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INTERNAL_RESISTANCE,
	POWER_SUPPLY_PROP_SCOPE,
};

static int sb_get_property(struct power_supply *psy,
			   enum power_supply_property psp,
			   union power_supply_propval *val)
{
	struct sb *sb = power_supply_get_drvdata(psy);
	struct power_supply_battery_info *info = psy->battery_info;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = info->technology;
		return 0;
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = sb->energy_full_uwh;
		return 0;
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = info->energy_full_design_uwh;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = info->voltage_min_design_uv;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = info->voltage_max_design_uv;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = info->constant_charge_current_max_ua;
		return 0;
	case POWER_SUPPLY_PROP_INTERNAL_RESISTANCE:
		val->intval = info->factory_internal_resistance_uohm;
		return 0;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		return 0;
	default:
		break;
	}

	/* the values of the last sample; none before it and after the silence */
	guard(mutex)(&sb->lock);
	if (!sb->snap.valid || sb->snap.silent)
		return -ENODATA;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = sb->snap.status;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = sb->snap.capacity;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = sb->snap.voltage_uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = sb->snap.current_ua;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		val->intval = div_s64((s64)sb->snap.voltage_uv * sb->snap.current_ua,
				      1000000);
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		val->intval = div_u64((u64)sb->energy_full_uwh * sb->snap.capacity, 100);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct power_supply_desc sb_desc = {
	.name = "axp22x-soft-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = sb_props,
	.num_properties = ARRAY_SIZE(sb_props),
	.get_property = sb_get_property,
	.external_power_changed = sb_external_power_changed,
	.init = sb_init,
};

/* --- probe, remove, suspend --- */

static void sb_put_node(void *data)
{
	of_node_put(data);
}

static void sb_stop(void *data)
{
	struct sb *sb = data;

	/* no more runs, and external_power_changed() cannot queue one */
	disable_delayed_work_sync(&sb->work);
	dev_info(sb->dev, "removed\n");
}

static int sb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct power_supply_config cfg = {};
	struct device_node *other;
	struct power_supply *psy;
	struct sb *sb;
	int ret;

	sb = devm_kzalloc(dev, sizeof(*sb), GFP_KERNEL);
	if (!sb)
		return -ENOMEM;
	sb->dev = dev;
	sb->prev_level = -1;
	sb->prev_status = POWER_SUPPLY_STATUS_UNKNOWN;
	ret = devm_mutex_init(dev, &sb->lock);
	if (ret)
		return ret;
	INIT_DELAYED_WORK(&sb->work, sb_work);

	sb->regmap = dev_get_regmap(dev->parent, NULL);
	if (!sb->regmap)
		return dev_err_probe(dev, -ENODEV, "no regmap of the PMIC\n");

	sb->batt_v = devm_iio_channel_get(dev, "batt_v");
	if (IS_ERR(sb->batt_v))
		return dev_err_probe(dev, PTR_ERR(sb->batt_v), "no IIO batt_v\n");
	sb->batt_chrg_i = devm_iio_channel_get(dev, "batt_chrg_i");
	if (IS_ERR(sb->batt_chrg_i))
		return dev_err_probe(dev, PTR_ERR(sb->batt_chrg_i), "no IIO batt_chrg_i\n");
	sb->batt_dischrg_i = devm_iio_channel_get(dev, "batt_dischrg_i");
	if (IS_ERR(sb->batt_dischrg_i))
		return dev_err_probe(dev, PTR_ERR(sb->batt_dischrg_i),
				     "no IIO batt_dischrg_i\n");

	/* the node of the values, by its compatible, not by a path (D111) */
	sb->np = of_find_compatible_node(NULL, NULL, SB_COMPATIBLE);
	if (!sb->np)
		return dev_err_probe(dev, -EINVAL,
				     "no node \"" SB_COMPATIBLE "\": dtoverlay=axp22x-soft-battery missing?\n");
	ret = devm_add_action_or_reset(dev, sb_put_node, sb->np);
	if (ret)
		return ret;
	/* the search drops a reference of its start: give it one of its own */
	other = of_find_compatible_node(of_node_get(sb->np), NULL, SB_COMPATIBLE);
	if (other) {
		of_node_put(other);
		return dev_err_probe(dev, -EINVAL,
				     "more than one node \"" SB_COMPATIBLE "\"\n");
	}

	/* the checks, the set up of the PMIC and the line "bound": sb_init() */
	cfg.fwnode = of_fwnode_handle(sb->np);
	cfg.drv_data = sb;
	psy = devm_power_supply_register(dev, &sb_desc, &cfg);
	if (IS_ERR(psy)) {
		/* external_power_changed() may have queued a run */
		disable_delayed_work_sync(&sb->work);
		return dev_err_probe(dev, PTR_ERR(psy), "no power supply\n");
	}

	/* after the power supply, so it runs before its removal */
	ret = devm_add_action_or_reset(dev, sb_stop, sb);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, sb);
	queue_delayed_work(system_power_efficient_wq, &sb->work, 0);
	return 0;
}

static int sb_suspend(struct device *dev)
{
	struct sb *sb = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&sb->work);
	dev_info(dev, "suspend\n");
	return 0;
}

static int sb_resume(struct device *dev)
{
	struct sb *sb = dev_get_drvdata(dev);

	/* the means and the level start again from the voltage (D33) */
	sb_mean_reset(sb);
	dev_info(dev, "resume\n");
	queue_delayed_work(system_power_efficient_wq, &sb->work, 0);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(sb_pm, sb_suspend, sb_resume);

static const struct of_device_id sb_of_match[] = {
	{ .compatible = "x-powers,axp221-battery-power-supply" },
	{ }
};
MODULE_DEVICE_TABLE(of, sb_of_match);

/*
 * The name is not the one of the node at the root: the platform bus
 * matches a device by the name of the driver too, and the node
 * "axp22x-soft-battery" is a device of its own with no driver.
 */
static struct platform_driver sb_driver = {
	.probe = sb_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = sb_of_match,
		.pm = pm_sleep_ptr(&sb_pm),
	},
};
module_platform_driver(sb_driver);

MODULE_DESCRIPTION("AXP22x battery by a model of the device tree");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_CONSUMER");

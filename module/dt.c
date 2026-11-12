// SPDX-License-Identifier: GPL-2.0
/*
 * The values of the device tree. The module has no default: each value is
 * in the node "axp22x-soft-battery" or in the simple-battery of its
 * monitored-battery, and a value missing or bad fails the probe.
 */

#include <linux/device.h>
#include <linux/limits.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

#include "sb.h"

/* Bounds of the checks, not settings. */
#define SB_R_MIN_UOHM		10000
#define SB_R_MAX_UOHM		500000
#define SB_END_MAX_UV		3800000

static int sb_refuse(struct sb *sb, const char *name, long long val,
		     const char *why)
{
	dev_err(sb->dev, "%s refused: %lld, %s\n", name, val, why);
	return -EINVAL;
}

static int sb_missing(struct sb *sb, const char *name)
{
	dev_err(sb->dev, "%s missing\n", name);
	return -EINVAL;
}

static int sb_read_u32(struct sb *sb, const char *name, u32 *val)
{
	return of_property_read_u32(sb->np, name, val) ? sb_missing(sb, name) : 0;
}

static int sb_check_range(struct sb *sb, const char *name, u32 val,
			  u32 min, u32 max)
{
	if (val >= min && val <= max)
		return 0;
	dev_err(sb->dev, "%s refused: %u, not in %u..%u\n", name, val, min, max);
	return -EINVAL;
}

/*
 * The node of the values, found by its compatible, not by a path: the
 * same overlay serves each device. It must be the one node so.
 */
int sb_find_node(struct sb *sb)
{
	struct device_node *other;

	sb->np = of_find_compatible_node(NULL, NULL, SB_COMPATIBLE);
	if (!sb->np)
		return dev_err_probe(sb->dev, -EINVAL,
				     "no node \"" SB_COMPATIBLE "\": dtoverlay=axp22x-soft-battery missing?\n");

	other = of_find_compatible_node(of_node_get(sb->np), NULL, SB_COMPATIBLE);
	if (!other)
		return 0;
	of_node_put(other);
	return dev_err_probe(sb->dev, -EINVAL,
			     "more than one node \"" SB_COMPATIBLE "\"\n");
}

/* V_OFF of the AXP22x: 2.6-3.3 V by 0.1 V. */
static int sb_check_voltage_min(struct sb *sb, int uv)
{
	if (uv >= 2600000 && uv <= 3300000 && !(uv % 100000))
		return 0;
	return sb_refuse(sb, "voltage-min-design-microvolt", uv,
			 "not 2600000..3300000 by 100000");
}

/* The target of the charge: one of the four codes of the AXP22x. */
static int sb_check_charge_voltage(struct sb *sb, int uv)
{
	switch (uv) {
	case 4100000:
	case 4200000:
	case 4220000:
	case 4240000:
		return 0;
	}
	return sb_refuse(sb, "constant-charge-voltage-max-microvolt", uv,
			 "not 4100000, 4200000, 4220000 or 4240000");
}

/* The current of the charge of the AXP22x: 300-2550 mA by 150 mA. */
static int sb_check_charge_current(struct sb *sb, int ua)
{
	if (ua >= 300000 && ua <= 2550000 && !((ua - 300000) % 150000))
		return 0;
	return sb_refuse(sb, "constant-charge-current-max-microamp", ua,
			 "not 300000..2550000 by 150000");
}

/*
 * The curve of the discharge. The core reads the table only when
 * ocv-capacity-celsius is there; its voltage must fall point by point.
 */
static int sb_check_ocv_table(struct sb *sb, struct power_supply_battery_info *info)
{
	int i, prev = INT_MAX;

	if (!info->ocv_table[0] || info->ocv_table_size[0] < 2)
		return sb_missing(sb, "ocv-capacity-table-0 or ocv-capacity-celsius");

	for (i = 0; i < info->ocv_table_size[0]; i++) {
		if (info->ocv_table[0][i].ocv >= prev)
			return sb_refuse(sb, "ocv-capacity-table-0",
					 info->ocv_table[0][i].ocv,
					 "the voltage does not fall");
		prev = info->ocv_table[0][i].ocv;
	}
	return 0;
}

/* The simple-battery of monitored-battery, as the core read it. */
static int sb_check_battery_info(struct sb *sb)
{
	struct power_supply_battery_info *info = sb->psy->battery_info;
	int ret;

	if (!info)
		return sb_missing(sb, "monitored-battery");
	if (info->technology == POWER_SUPPLY_TECHNOLOGY_UNKNOWN)
		return sb_missing(sb, "device-chemistry");
	if (info->energy_full_design_uwh <= 0)
		return sb_missing(sb, "energy-full-design-microwatt-hours");
	if (info->voltage_max_design_uv <= 0)
		return sb_missing(sb, "voltage-max-design-microvolt");

	ret = sb_check_voltage_min(sb, info->voltage_min_design_uv) ?:
	      sb_check_charge_voltage(sb, info->constant_charge_voltage_max_uv) ?:
	      sb_check_charge_current(sb, info->constant_charge_current_max_ua);
	if (ret)
		return ret;

	if (info->factory_internal_resistance_uohm < SB_R_MIN_UOHM ||
	    info->factory_internal_resistance_uohm > SB_R_MAX_UOHM)
		return sb_refuse(sb, "factory-internal-resistance-micro-ohms",
				 info->factory_internal_resistance_uohm,
				 "missing or not 10000..500000");

	return sb_check_ocv_table(sb, info);
}

/* A pair of the curve of the charge: the current falls, the level rises. */
static int sb_check_cv_pair(struct sb *sb, int i)
{
	const char *name = SB_PROP("charge-cv-table");

	if (sb->cv[i].pct > 100)
		return sb_refuse(sb, name, sb->cv[i].pct, "a percent over 100");
	if (i && (sb->cv[i].ua >= sb->cv[i - 1].ua ||
		  sb->cv[i].pct <= sb->cv[i - 1].pct))
		return sb_refuse(sb, name, i,
				 "the current must fall and the percent rise");
	return 0;
}

/* The curve of the charge is the one value that may be missing: no curve. */
static int sb_read_cv_table(struct sb *sb)
{
	const char *name = SB_PROP("charge-cv-table");
	u32 raw[SB_CV_MAX_PAIRS * 2];
	int n, i, ret;

	n = of_property_count_u32_elems(sb->np, name);
	if (n == -EINVAL)
		return 0;
	if (n < 2 || n > SB_CV_MAX_PAIRS * 2 || n % 2)
		return sb_refuse(sb, name, n, "not 1..16 pairs");

	ret = of_property_read_u32_array(sb->np, name, raw, n);
	if (ret)
		return sb_refuse(sb, name, ret, "not read");

	sb->cv_len = n / 2;
	for (i = 0; i < sb->cv_len; i++) {
		sb->cv[i].ua = raw[2 * i];
		sb->cv[i].pct = raw[2 * i + 1];
		ret = sb_check_cv_pair(sb, i);
		if (ret)
			return ret;
	}
	return 0;
}

/* A write <reg mask value>: each a byte, the value in the mask. */
static int sb_check_reg_write(struct sb *sb, const u32 *w, int i)
{
	if (w[0] <= 0xff && w[1] <= 0xff && w[2] <= 0xff && !(w[2] & ~w[1]))
		return 0;
	return sb_refuse(sb, SB_PROP("reg-writes"), i,
			 "a value over 0xff, or a bit out of the mask");
}

/* The writes of the registers, done in order at each probe. */
static int sb_read_reg_writes(struct sb *sb)
{
	const char *name = SB_PROP("reg-writes");
	int n, i, ret;

	n = of_property_count_u32_elems(sb->np, name);
	if (n == -EINVAL)
		return sb_missing(sb, name);
	if (n < 3 || n % 3)
		return sb_refuse(sb, name, n, "not triples <reg mask value>");

	sb->reg_writes = devm_kmalloc_array(sb->dev, n, sizeof(u32), GFP_KERNEL);
	if (!sb->reg_writes)
		return -ENOMEM;
	ret = of_property_read_u32_array(sb->np, name, sb->reg_writes, n);
	if (ret)
		return sb_refuse(sb, name, ret, "not read");

	sb->n_reg_writes = n / 3;
	for (i = 0; i < sb->n_reg_writes; i++) {
		ret = sb_check_reg_write(sb, &sb->reg_writes[3 * i], i);
		if (ret)
			return ret;
	}
	return 0;
}

static int sb_read_settings(struct sb *sb)
{
	return sb_read_u32(sb, SB_PROP("energy-full-microwatt-hours"), &sb->energy_full_uwh) ?:
	       sb_read_u32(sb, SB_PROP("end-microvolt"), &sb->end_uv) ?:
	       sb_read_u32(sb, SB_PROP("sample-interval-ms"), &sb->interval_ms) ?:
	       sb_read_u32(sb, SB_PROP("full-margin-microvolt"), &sb->full_margin_uv) ?:
	       sb_read_u32(sb, SB_PROP("full-current-microamp"), &sb->full_current_ua) ?:
	       sb_read_u32(sb, SB_PROP("silent-samples"), &sb->silent_samples) ?:
	       sb_read_u32(sb, SB_PROP("guard-samples"), &sb->guard_samples);
}

/* The end may not be under V_OFF; the guard comes after the silence. */
static int sb_check_settings(struct sb *sb)
{
	struct power_supply_battery_info *info = sb->psy->battery_info;

	return sb_check_range(sb, SB_PROP("energy-full-microwatt-hours"),
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
}

/*
 * All the values, each checked. It runs when the core of the power
 * supply has read monitored-battery into psy->battery_info.
 */
int sb_read_dt(struct sb *sb)
{
	int ret;

	ret = sb_check_battery_info(sb) ?: sb_read_settings(sb) ?:
	      sb_check_settings(sb) ?: sb_read_cv_table(sb) ?:
	      sb_read_reg_writes(sb);
	if (ret)
		return ret;

	sb->full_uv = sb->psy->battery_info->constant_charge_voltage_max_uv -
		      sb->full_margin_uv;
	return 0;
}

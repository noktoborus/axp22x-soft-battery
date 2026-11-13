#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Compare the values of the device tree with the registers of the AXP22x
# and with the properties of the module, on the device.
#
# For each value written at the probe: the value of the device tree, the
# value the module must write (rounded as the module rounds it), the value
# in the register, the value the module shows. Then each reg-write: the
# register under its mask against the value.
#
# Usage: check-regs.sh [bus]
#   bus  the number of the I2C bus of the AXP (0x34); found by the
#        driver axp20x-i2c when not given.
#
# Needs i2cget (i2c-tools) and root. The reads are with -f: the driver
# holds the address; a read does not change a register.
#
# For a test: DT, PSY and I2CGET set the root of the device tree, the
# directory of the power supply and the command of the reads.

set -u

DT=${DT:-/proc/device-tree}
PSY=${PSY:-/sys/class/power_supply/axp22x-soft-battery}
I2CGET=${I2CGET:-i2cget}
ADDR=0x34
NODE=$DT/axp22x-soft-battery
CELLS=$DT/axp22x-soft-battery-cells
FAIL=0

die() {
	echo "check-regs: $*" >&2
	exit 2
}

# The bus of the AXP: the client <bus>-0034 of the driver axp20x-i2c.
find_bus() {
	for d in /sys/bus/i2c/drivers/axp20x-i2c/*-0034; do
		[ -e "$d" ] || continue
		d=${d##*/}
		echo "${d%-0034}"
		return 0
	done
	return 1
}

# The cells of a property of the device tree: big-endian u32, in decimal.
dt_u32s() {
	[ -r "$1" ] || return 1
	od -An -v -tu1 "$1" | tr -s ' \n' '\n\n' | grep . | {
		n=0 v=0
		while read -r b; do
			v=$((v * 256 + b))
			n=$((n + 1))
			if [ $n -eq 4 ]; then
				echo $v
				n=0 v=0
			fi
		done
	}
}

dt_u32() {
	v=$(dt_u32s "$1" | head -n 1)
	[ -n "$v" ] && echo "$v"
}

# A register of the AXP, in decimal.
reg() {
	v=$($I2CGET -f -y "$BUS" $ADDR "$1" 2>/dev/null) || return 1
	echo $((v))
}

# A property of the module; "-" when there is none.
psy() {
	cat "$PSY/$1" 2>/dev/null || echo -
}

# A line: name, dt, expected, register, module. "-" in module: not shown.
line() {
	st=OK
	[ "$3" = "$4" ] || st=DIFF
	[ "$5" = - ] || [ "$5" = "$3" ] || st=DIFF
	[ "$st" = OK ] || FAIL=1
	printf '%-30s %10s %10s %10s %10s  %s\n' "$1" "$2" "$3" "$4" "$5" "$st"
}

# V_OFF, 0x31 [2:0]: 2.6-3.3 V by 0.1 V, rounded up.
check_v_off() {
	dt=$(dt_u32 "$CELLS/voltage-min-design-microvolt") || dt=-
	exp=-
	if [ "$dt" != - ] && [ "$dt" -ge 2600000 ] && [ "$dt" -le 3300000 ]; then
		exp=$((2600000 + (dt - 2600000 + 99999) / 100000 * 100000))
	fi
	r=$(reg 0x31) && r=$((2600000 + (r & 7) * 100000)) || r=-
	line "V_OFF (0x31 [2:0])" "$dt" "$exp" "$r" "$(psy voltage_min_design)"
}

# The target of the charge, 0x33 [6:5], rounded down to a code.
check_charge_voltage() {
	dt=$(dt_u32 "$CELLS/constant-charge-voltage-max-microvolt") || dt=-
	exp=-
	if [ "$dt" != - ]; then
		for c in 4240000 4220000 4200000 4100000; do
			if [ "$dt" -ge $c ]; then
				exp=$c
				break
			fi
		done
		[ "$dt" -le 4240000 ] || exp=-
	fi
	r=-
	if v=$(reg 0x33); then
		case $(((v >> 5) & 3)) in
		0) r=4100000 ;;
		1) r=4220000 ;;
		2) r=4200000 ;;
		3) r=4240000 ;;
		esac
	fi
	line "charge voltage (0x33 [6:5])" "$dt" "$exp" "$r" -
}

# The current of the charge, 0x33 [3:0]: 300-2550 mA by 150 mA, rounded down.
check_charge_current() {
	dt=$(dt_u32 "$CELLS/constant-charge-current-max-microamp") || dt=-
	exp=-
	if [ "$dt" != - ] && [ "$dt" -ge 300000 ] && [ "$dt" -le 2550000 ]; then
		exp=$((300000 + (dt - 300000) / 150000 * 150000))
	fi
	r=$(reg 0x33) && r=$((300000 + (r & 15) * 150000)) || r=-
	line "charge current (0x33 [3:0])" "$dt" "$exp" "$r" \
		"$(psy constant_charge_current_max)"
	if [ -e "$PSY/constant_charge_current" ]; then
		line "  constant_charge_current" "$dt" "$exp" "$r" \
			"$(psy constant_charge_current)"
	fi
}

# The writes <reg mask value> of the node, each under its mask.
check_reg_writes() {
	set -- $(dt_u32s "$NODE/axp22x-soft-battery,reg-writes")
	if [ $# -eq 0 ]; then
		echo "reg-writes: none in the device tree"
		FAIL=1
		return
	fi
	while [ $# -ge 3 ]; do
		r=$(printf '0x%02x' "$1")
		name=$(printf 'reg-write %s mask 0x%02x' "$r" "$2")
		want=$(printf '0x%02x' "$3")
		if v=$(reg "$r"); then
			got=$(printf '0x%02x' $((v & $2)))
		else
			got=-
		fi
		line "$name" "$want" "$want" "$got" -
		shift 3
	done
}

[ -d "$NODE" ] || die "no node $NODE: is the overlay axp22x-soft-battery loaded?"
[ -d "$CELLS" ] || die "no node $CELLS"
BUS=${1:-$(find_bus)} || die "no AXP on a bus of axp20x-i2c; give the bus"
reg 0x33 >/dev/null || die "$I2CGET of bus $BUS, $ADDR failed (root? i2c-dev?)"

echo "AXP at bus $BUS, $ADDR"
if [ -d "$PSY" ]; then
	echo "module: $PSY"
else
	echo "module: no $PSY; the column module is -"
fi
echo
printf '%-30s %10s %10s %10s %10s  %s\n' value dt expected register module ""
check_v_off
check_charge_voltage
check_charge_current
check_reg_writes

# The state of the charger: a bit set or not, with a text for each.
bit() {
	v=$(reg "$1") || return
	if [ $((v & (1 << $2))) -ne 0 ]; then
		echo "$1 bit $2: $3"
	else
		echo "$1 bit $2: $4"
	fi
}

echo
bit 0x33 7 "the charger is enabled: it charges when there is power" \
	"the charger is disabled"
bit 0x00 7 "ACIN is there" "no ACIN"
bit 0x00 5 "VBUS is there" "no VBUS"
bit 0x01 6 "the cells are charged now" "the cells are not charged now"
exit $FAIL

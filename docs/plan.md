# Plan

The decisions are in `decisions.md`; how they came, in `discussion.md`.
Each point names its decision (Dnn). Variant B, the module and the daemon
of learning, is made (D61, D80); variant A is kept apart, at the end.

## The parts

| part | what | name | package |
|---|---|---|---|
| the module of the kernel | reads the AXP, works out the level, gives the battery | `axp22x_soft_battery` | `sys-power/axp22x-soft-battery` |
| the program, in Rust | `daemon`: learns; `gen-dts`: a `.dts` of the model; `import`: a run from a log of axp-log | `axp22x-soft-battery` | `sys-power/axp22x-soft-battery-daemon` |
| the device tree | the model and the settings of the module | properties of `battery@0` and of the cell `battery-power-supply` | an overlay made by the user from `gen-dts` |
| the files | the runs | `/var/lib/axp22x-soft-battery/` | made by the daemon |

Names: D62, D64. The module reads its model only from the device tree;
the daemon gives the module nothing (D87).

## The module

### Rules

- It does its job and nothing more (D12); it powers the system off only
  as the guard of the silence (D11, D71).
- It weighs as little as it can on the system: few locks, the interfaces
  of the drivers there are (D37); it is small and easy to read (D38, D88).
- The AXP is no store, its gauge is not used (D8): not E0:E1, not E2:E3,
  not B9.

### Where it binds

- To the cell `battery-power-supply` of the MFD `axp20x`, in place of
  `axp20x_battery`: `of_match_table` with
  `x-powers,axp221-battery-power-supply`; the package blacklists
  `axp20x_battery` (`/etc/modprobe.d/axp22x-soft-battery.conf`, D36). The
  kernel, its drivers and the overlay of the device are not changed.
- The ebuild stops with `CONFIG_BATTERY_AXP20X=y`; the source has `#if
  IS_BUILTIN(CONFIG_BATTERY_AXP20X)` and `#error` (D36).

### probe

1. The IIO channels of `axp20x_adc`: `batt_v`, `batt_chrg_i`,
   `batt_dischrg_i` (`devm_iio_channel_get()`; `-EPROBE_DEFER` while they
   are not there).
2. `power_supply_get_battery_info()` on the cell (it has
   `monitored-battery`): the voltages, the current of the charge, the
   energy of design, `ocv-capacity-table-0`,
   `factory-internal-resistance-micro-ohms`.
3. The set up of the PMIC (D5): the current of the charge (2.1 A), `V_OFF`
   (0x31) from `voltage-min-design`, the voltage of the full charge, the
   five writes of Rex (0x30, 0x32, 0x34, 0x36, 0x90; `discussion.md`, "The
   writes of Rex").
4. The settings and the model from the device tree ("The device tree"
   below); each value checked; a bad one: a line of `err`, its default.
5. The power supply `axp22x-soft-battery`, `supplied_from = { "axp20x-ac" }`
   (D39); no attributes of its own (D87).
6. The charger by `power_supply_am_i_supplied()`; the first run of the
   work.

### The work

- One `delayed_work` on `system_power_efficient_wq`, each
  `sample-interval-ms` (10 s, D70, D72), `round_jiffies_relative`, not
  deferrable; the time by `ktime_get()` (D42).
- Each run: the three reads of IIO; 0x01 by the regmap of the parent only
  while the charger is in and the current of the charge is under
  `full-current-microamp` (D40); then under one mutex the means of 30 s
  (3 samples), the level, `status`, the count of failed samples; then, out
  of the mutex, `power_supply_changed()` if `status` or the integer percent
  changed (D55), and the lines of dmesg.
- The charger in or out: `external_power_changed()` from `axp20x-ac`
  (D39); one read of `online` by that driver at each edge.
- `get_property`: a copy of the last sample under the mutex; no read of the
  bus (D42).
- One mutex of the module; nothing called under it. No count of the
  charge in the module (D89, D90).

### The level

By the voltage always (D90), on two curves (D91):

| state | the level |
|---|---|
| no charger | OCV ≈ V + I × R on the curve of the discharge |
| the charger in, the voltage under `voltage-max-design` − `full-margin-microvolt` | OCV ≈ V − I × R on the curve of the discharge |
| the charger in, the voltage at that or over (the phase of constant voltage) | the current of the charge on the curve of the charge |
| the full state (D40) | 100 % |

- V and I are the means of 30 s; R is `factory-internal-resistance-micro-ohms`
  of `battery@0`, else 87 mOhm.
- On a discharge the level does not rise (D60); on a charge it does not
  fall (D91).
- 0 % when the mean of 30 s of the voltage is `end-microvolt` or under
  (D21, D35).
- The curve of the discharge: `ocv-capacity-table-0`, else the one built in
  (D59: the mean of the three logs). The curve of the charge:
  `axp22x-soft-battery,charge-cv-table`, else none: in the phase of constant
  voltage the level then does not fall, and `Full` sets 100 % (D91).

### status (D40)

- No charger: `Discharging`.
- The charger in, the current of the charge `full-current-microamp` or
  more: `Charging`, no read of 0x01.
- Under it: bit 6 of 0x01 = 1 → `Charging`; bit 6 = 0 and the voltage at
  `voltage-max-design` − `full-margin-microvolt` or over → `Full` (the full
  state); bit 6 = 0 under it → `Not charging`.

### The properties (D43, D55, D92-D95)

`present`, `status`, `technology` (from `device-chemistry`), `capacity`,
`voltage_now`, `current_now` (negative on a discharge), `power_now`
(V × I), `energy_now` (the level × `energy_full`), `energy_full` (the
model of the device tree, else the design), `energy_full_design`
(`battery@0`), `voltage_min_design`, `voltage_max_design`,
`constant_charge_current_max`, `internal_resistance` (the R in use),
`scope` (System).

Not given: `temp` (D92), `charge_*` (D95), `capacity_level` and
`time_to_*` (D55), `health` (D93), `charge_counter` (D90). upower works out
the times, the health and the percent of the display device from the
energy (`discussion.md`, "What upower reads, and what soft-battery gives").

### The silence of the AXP

- A sample fails when all its three reads of IIO give `-ENXIO`; another
  error, or a part of them, is a line `ADC read failed` (rate limited) and
  the sample is skipped (D41).
- `silent-samples` (6) failed samples in a row, about 1 min: the silence.
  From then `status`, `capacity`, `voltage_now`, `current_now`,
  `power_now`, `energy_now` give `-ENODATA` (D93, D94); the values of design
  stay. The work goes on.
- `guard-samples` (12) failed samples, about 2 min:
  `hw_protection_trigger("AXP silent", 20000)` (D71).

### Suspend (D33)

The work is cancelled at suspend; at resume the time of the last sample is
set to now and the work goes on. Not to be tried on the CM5 now
(`uconsole/linux/notes/suspend-cm5.md`).

### dmesg (D15)

One line at an event, never in a loop; the prefix `axp22x_soft_battery
axp20x-battery-power-supply:`.

- `bound: charge current 2100 mA, V_OFF 2900 mV, full at 4200 mV`, and the
  sources: the curve of the discharge (the device tree or built in), the
  curve of the charge (the device tree or none), R, `energy_full` (D76,
  D87).
- `<property> refused: <value>, <why>; the default taken` (`err`).
- `full: 4204 mV at 21 mA`; `charger out`; `level 0 %: 3211 mV at 2450 mA`.
- `AXP silent: 6 samples failed, last -6`; `guard: power off`.
- `ADC read failed: -5` (rate limited); `suspend`; `resume`; `removed`.

### Memory

The curve of the discharge (21 × u16), the curve of the charge (up to 16
pairs of u16), the sums of 30 s, the last sample: under 0.2 KiB. The
default curve is `const`.

## The device tree

| node | property | default | what |
|---|---|---|---|
| `battery@0` | `ocv-capacity-table-0`, `ocv-capacity-celsius` | the curve built in | the curve of the discharge (D59, D75, D86) |
| `battery@0` | `factory-internal-resistance-micro-ohms` | 87000 | R (D73, D75) |
| the cell | `axp22x-soft-battery,energy-full-microwatt-hours` | the design of `battery@0` | `energy_full` (D75, D95) |
| the cell | `axp22x-soft-battery,end-microvolt` | 3200000 | 0 % (D21, D35, D73) |
| the cell | `axp22x-soft-battery,charge-cv-table` | none | the curve of the charge: pairs of µA and percent (D91) |
| the cell | `axp22x-soft-battery,sample-interval-ms` | 10000 | the period of the work (D70, D72) |
| the cell | `axp22x-soft-battery,full-margin-microvolt` | 50000 | the full state (D39) |
| the cell | `axp22x-soft-battery,full-current-microamp` | 100000 | the full state (D39) |
| the cell | `axp22x-soft-battery,silent-samples` | 6 | the silence (D41) |
| the cell | `axp22x-soft-battery,guard-samples` | 12 | the guard (D71) |

Read once at the probe; a change is a new overlay and a boot.

## The program

Rust (D25); one crate, `lexopt`; errors by `Box<dyn std::error::Error>`;
`lto = "fat"`, `codegen-units = 1` (D50, D54).

### `axp22x-soft-battery daemon`

- A service of systemd, enabled by a preset, from the boot; no `Restart=`,
  no rule of udev, no order against upower (D68, D79, D87). Its log is the
  journal (D67).
- Each 10 s it reads `status`, `voltage_now`, `current_now` of
  `/sys/class/power_supply/axp22x-soft-battery/` (D63, D70); once a minute
  `temp1_input` of the hwmon `axp_hwmon` (D85). The directory not there: it
  waits.
- A run of a discharge (D96):
  - the start: `status` was `Full` and is not now (D14); the values at rest
    are those of that last `Full` read;
  - R at 60 s, by the 6 reads after the start (D34); 0 if the step is under
    300 mA;
  - the charge and the energy by trapezoids over `CLOCK_MONOTONIC` (D89);
    each read kept: the charge out, V, I;
  - the charger in during a run: the count goes on, net; `Full` again: the
    run starts again;
  - the end: a read gives `ENODATA` (D94). `V_end`, `I_end`: the means of
    the last 3 good reads; the model of the run: `Q_full`, `E_full`, R,
    `V_end`, `I_end`, the table of 21 points as OCV (V + I × R, the means of
    60 s at each 5 %) (D90), the temperature (D85); the check (D54, D51);
    the file `runs/<seq>.env` or `invalid/<seq>.env` (D45, D52), `fsync`;
    the oldest past 5 in `runs/` to `archive/` (D90); `systemctl poweroff
    --force` (D49).
- A run of a charge (D91): from the charger in to `Full`, the device on;
  each read kept (the charge in, V, I); at `Full` each point gets its
  percent back from 100 % by the charge in and `Q_full`; the points of the
  phase of constant voltage give the curve of the charge (the current →
  the percent).
- What ends a run, with a line in the journal, and the daemon exits (D89,
  D97): a suspend (by `CLOCK_BOOTTIME` − `CLOCK_MONOTONIC`, D66, D69); a
  gap of over 60 s between two reads; the directory of the battery gone; a
  read that fails with another error than `ENODATA`; a current past the
  bounds of the ADC; a stop of the daemon (SIGTERM: a power off by upower
  or by hand): "the run was broken off".
- At each exit, for any cause, the daemon prints to the journal the data
  it gathered: the run so far, cut short or whole (D97, D98).
- The settings of upower are left alone (D97).

### `axp22x-soft-battery gen-dts`

The model of `runs/` as it would be for the module (D81): the mean of the
runs (each value, each point of the tables), `end-microvolt` = the highest
`V_end` + 50 mV (D35, D48), `ocv-capacity-celsius` = the mean temperature
(D86); an overlay `.dts` with `target-path` to `battery@0` and to the cell
(D82), to stdout. The user compiles it (`dtc -@ -I dts -O dtb`) and puts
it into `config.txt` by hand (D32, D87).

### `axp22x-soft-battery import <log>`

A log of axp-log, from the full state to the silence, into a run of
`runs/` (D90). The three logs of `uconsole/hardware/tools/axp-log/dump/`
give a model of the device tree at once, and serve as tests of the
learning.

### The files

`/var/lib/axp22x-soft-battery/`: `runs/` (the last 5 valid runs),
`archive/` (the valid runs past 5), `invalid/` (those that failed the
check) (D45-D48, D90). A file: lines `KEY=value`, `seq` of 6 digits (D52).

## The packages

- The source is a copy in `files/` of each package, made by a script of
  this repository with its sum in the ebuild (D28); `repo-uconsole` of
  `uconsole/linux`.
- `sys-power/axp22x-soft-battery`: `linux-mod-r1`, `virtual/dist-kernel`;
  the module and `/etc/modprobe.d/axp22x-soft-battery.conf` (`blacklist
  axp20x_battery`).
- `sys-power/axp22x-soft-battery-daemon`: `cargo` (the crates in `CRATES`);
  the program, `axp22x-soft-battery.service`, its preset.
- `config.txt` is not changed by a package (D32).
- Put off: the module in the tree of the kernel,
  `plans/axp22x-soft-battery-in-tree.md`.

## The kernel series

0010 leaves the series (D4, D57, D71): its work is the guard of the
module. `kernel/bump.sh --sum` after. In `uconsole/linux`, committed by the
user.

## Steps

1. Done: the writes of Rex in the datasheet. Left: where GPIO0 of the AXP
   goes (the schematic); the full paths of `battery@0` and of
   `battery-power-supply` in `/proc/device-tree`, for `gen-dts`.
2. Done: the scheme of B on the desk of showcase,
   `shots/3-variant-b.scheme`.
3. A log of axp-log of a full charge, the device on, from low to `Full`:
   the curve of the charge built in (D91).
4. The packages: the script of the copy, the two ebuilds; `linux-mod-r1`
   and `cargo` in the cross build of unidev.
5. The module.
6. The program: `import` first (the three logs), then `gen-dts`, then
   `daemon`.
7. The kernel series: 0010 out.
8. A test on the device: the module with no model (the voltage alone); a
   model from the three logs by `import` and `gen-dts`; a full charge and a
   discharge to the silence with the daemon.

## Open questions

- How a run of a charge is kept: a file of its own in `runs/`, or a part of
  the run of the discharge that follows.
- With the defaults of upower (the critical action at 2 %), a run is broken
  off before the silence (D97): whole runs then come only if the user sets
  upower so, or by `import`.
- Q16. The use of a run broken off low (`discussion.md`, "A run broken
  off"), proposed: a run stopped by SIGTERM when the level by the curve is
  10 % or less is kept in `runs/` with `PARTIAL=1`: R and the curve as far as
  it went are its own; `Q_full`, `E_full` = the charge and the energy out /
  (1 − the level at the stop); the points of the table under the stop from
  the curve in use; no `V_end`. `gen-dts` takes it into the means, but
  `end-microvolt` only of the whole runs (to the silence); with none, it
  writes no `end-microvolt` (3.2 V of D21 holds). A run stopped higher is
  printed (D98), not kept.

## Variant A, kept apart

The module counts, learns (80 bins, R at the start, the model of a run)
and works out the level by C'; the program runs twice a cycle: `load` at
boot (the model into `model_*`, `model_commit`, by `IMPORT{program}` of a
rule of udev) and `save` at the silence (by `RUN`). Its own decisions: D3,
D13, D16, D19, D20, D23, D24, D27, D29-D31, D43 (`model_*`, `run_*`,
`learn_*`), D44, D53, D74 (a model written). Not made now (D80); its full
design is in the history of this file, before the commit "Rewrite the plan
for variant B".

# Plan

The decisions are in `decisions.md`; how they came, in `discussion.md`.
Each point below names its decision (Dnn), or says "proposed" and the
number of its question in "Open questions" (Qn).

Read again on 2026-11-12 against all decisions and answers; what did not
agree is listed at the end, "Fixed on the review".

## Two variants

From 2026-11-12 the plan splits into two variants (D61). The design below
is written for A, as decided until then; what B changes is listed here.

- **A: the module and the tools.** The module reads, counts, learns (the
  bins, R at the start, the model of the run), works out the level; the
  program `axp22x-soft-battery` runs twice a cycle: `load` at boot by a
  rule of udev, `save` at the silence by a rule of udev; no service.
- **B: the module and a daemon of learning**, the daemon
  `axp22x-soft-battery` (D62). The module reads, counts
  and works out the level by the model it holds (the default, D58-D60, or
  the one the daemon wrote); it does not learn. The daemon runs for good
  (a service), reads the files of sysfs each second, learns, keeps the
  runs, makes the mean model and writes it at its start and after a run,
  sees the silence and powers off.

Common to both: D1, D2 (who learns differs), D5/D36, D6, D8, D10-D12,
D14, D15 (the lines of learning come from the module in A, from the
daemon in B), D17, D18, D21-D23, D25, D26, D28, D29, D32, D33 (in B the
daemon drops its learning at a suspend), D34 (in B the daemon learns R),
D35, D37-D41, D45-D55, D57-D60.

Only in A: D3, D13 and D30-D31 (the model by a rule of udev at boot), D16
(the learning in the memory of the module), D19 (the uevent of the silence
starts the program), D20 (the module makes the model of the run), D24
(the learning in the module), D27 (no service), D44 (the bins in the
module), `run_*` of D43.

Only in B, proposed (`discussion.md`, "Variant B in full"):

- QB1. The module in B: the set up of the PMIC; the reads of IIO each 1 s;
  `status` (D40); the count of the charge; the level (D23, D35, D58-D60) by
  the model in use; the model (`model_*`, `model_commit`, `model_source`,
  D43); `health` = `Dead` at the silence; suspend: the level by the voltage
  at resume. Out: the bins, R at the start, `run_*`, `learn_*`, the uevent
  of a run. New: `charge_counter` of the class, the net charge counted by
  the module since the probe, µAh, read only.
- QB2, decided (D63). The daemon reads each 1 s `charge_counter`,
  `voltage_now`, `current_now`, `status`, `health` of our battery: the
  copies of the last sample of the module, no read of the bus; the charge
  of the learning is the difference of `charge_counter`; the energy by
  V × I of its own samples. Not the drivers there are (IIO, `axp20x-ac`):
  that would read the bus twice and lose bit 6 of 0x01 (`discussion.md`,
  "The daemon with the least tie to the module").
- QB3, decided (D65, D68): the daemon runs from the boot, a service of
  systemd, enabled; no `Restart=`. Each 1 s it looks at
  `/sys/class/power_supply/axp22x-soft-battery/` (proposed: by a read of
  `model_source`): not there, it waits; there with `model_source` =
  `default` while it has a model, it writes the model (`model_*`,
  `model_commit`): at the first sight, and again after the module was
  loaded again. No rule of udev for it. Proposed: a read each second, not
  inotify (sysfs does not tell of new directories by inotify) and not the
  netlink of uevents (a crate or code of sockets).
- QB4. The learning in the daemon: the start at `status` from `Full` to
  `Discharging`; R by the step at the start (D34); the curve at full
  resolution in its memory (no bins); the end at `health` = `Dead` or the
  reads failing; then the run kept, the mean made, `systemctl poweroff
  --force` (D49). Kept in memory only, as D16; a restart of the daemon
  drops a learning (a line in its log).
- QB5, decided (D66): a growth of `CLOCK_BOOTTIME` − `CLOCK_MONOTONIC`
  drops the learning (D33).
- QB6, decided (D67): stdout and stderr into the journal; the lines of
  the module stay in dmesg.

## The design

### The rules of the module

- The module does its job and nothing more (D12); it never powers off
  (D11); the AXP is no store, its gauge is not used (D8).
- It weighs as little as it can on the running system: as few locks as it
  can, and the interfaces of the drivers there are, not its own (D37).
- It is small and easy to read (D38).

### Where it binds

- The module binds to the cell `battery-power-supply` of the MFD `axp20x`
  in place of `axp20x_battery` (D36). Its `of_match_table` has
  `x-powers,axp221-battery-power-supply`; udev loads it by the alias of
  that device. The package blacklists `axp20x_battery`. No overlay, no
  node of its own; the kernel, its drivers, the device tree and
  `config.txt` are not changed (D32, D36).
- One power supply of type Battery, `axp22x-soft-battery` (D26).
- One module: the set up, the count, the learning and the level (D24).

### What it reads, and how

- The voltage and the currents of the cells: the IIO channels of
  `axp20x_adc`, `batt_v`, `batt_chrg_i`, `batt_dischrg_i` (D36), by
  `devm_iio_channel_get()`; `-EPROBE_DEFER` while they are not there. The
  values change about once a second (the records `fast` of the log of
  08.11): a sample each 1 s loses nothing.
- The values of `battery@0` (the current of the charge, the voltages,
  `charge_full_design`): `power_supply_get_battery_info()` on the node of
  the cell, which has `monitored-battery` (D36).
- The charger in or out (D39): from `axp20x-ac` of `axp20x_ac_power`, by
  its IRQ: our power supply names it in `supplied_from`, and the class
  calls our `external_power_changed()` at each change; no read of 0x00.
- Bit 6 of 0x01 (D40): by the regmap of `axp20x` (a read of the bus;
  0x00-0x01 are volatile), only while the charger is in and the current of
  the charge is under 100 mA. Else the regmap of the parent serves only the
  set up at the probe.
- At each probe the module sets the PMIC up (D5, D36): the current of the
  charge (2.1 A of `battery@0`), `V_OFF` (0x31), the voltage of the full
  charge, and the five writes of Rex of `axp20x_power_probe` (0x30, 0x32,
  0x34, 0x36, 0x90; their meaning to check, step 1).

### The work in time

D42 (`discussion.md`, "Q3: the work in time, in full"). The
scheme: desk `uconsole-soft-battery-work` of showcase,
`shots/1-threads.scheme`.

- One `delayed_work` on `system_power_efficient_wq`, each
  `round_jiffies_relative(HZ)`: about 1 s, at the tick of other timers;
  not deferrable. The time between samples is measured (`ktime_get()`), so
  a late or early run counts right.
- Each run:
  1. the reads, with no lock of ours: three IIO channels; 0x01 when the
     charger is in and the current of the charge under 100 mA (D40);
  2. under our mutex: the count, the state of the learning, the point, R
     at the 60th second, the level; it notes what changed;
  3. out of the mutex: `power_supply_changed()` if `status` or the integer
     percent changed (D55); the lines of dmesg; at the
     silence the uevent (D19), and the work is not queued again (D41);
  4. queue the next run.
- `external_power_changed()` (the work of the class, at an IRQ of
  `axp20x-ac`): `power_supply_am_i_supplied()` (it reads `online` of
  `axp20x-ac`: one read of 0x00 by that driver), then under our mutex the
  charger in or out and the time of the edge. The next run does the rest.
- `get_property` and our attributes: under our mutex a copy of the last
  sample; no read of the bus. `model_commit`: the lists parsed with no
  lock, then under the mutex the swap of the model.
- One lock of ours, a mutex; under it no call to another part of the
  kernel and no print; held for sums and copies, microseconds.
- The locks a run takes, beside ours: for each IIO read the
  `info_exist_lock` of IIO, the lock of the regmap of `axp20x`, the lock of
  the bus I2C (with the lock of the shared SCL of `i2c-gpio`); for 0x01
  the last three. The same as `axp20x_battery` takes for one read of
  sysfs.
- probe: the IIO channels (`-EPROBE_DEFER`), `battery@0`, the set up of the
  PMIC, the power supply (`supplied_from = "axp20x-ac"`, `attr_grp`), the
  charger state by `power_supply_am_i_supplied()`, the first run at once.
- remove: `cancel_delayed_work_sync()`; the rest by `devm`.
- Suspend (D33): the work is cancelled; a learning is dropped. Resume:
  the time of the last sample is set to now; the level starts again by
  the voltage; the work is queued. Not to be tried on the CM5 now
  (`uconsole/linux/notes/suspend-cm5.md`).
- The silence of the AXP (D41): 6 failed samples in a row, a sample at
  most once a second, so about 5 s after the first fail; then the work
  stops. A sample counts as failed only when all its three reads give
  `-ENXIO`; another error is a line `ADC read failed` (rate limited) and
  the sample is skipped; `silent_samples` (D41).

### The marker of the full charge

- The full state (D39, D40): the charger in, the current of the charge
  (IIO) under 100 mA, the voltage at least `voltage-max-design` − 50 mV
  (`full_margin_uv`), bit 6 of 0x01 = 0. The AXP ends a charge at 10-15 %
  of the charge current (210-315 mA at 2.1 A); in the full state the logs
  show 12-50 mA.
- Each sample in the full state starts the learning again, silently. The
  learning starts in fact at the edge of the charger going out
  (`external_power_changed()`, `online` 1 → 0) when the last sample before
  it was in the full state (`discussion.md`, "What starts the learning").
- `status` (D40): no charger → `Discharging`; the charger in, the current
  100 mA or more → `Charging`, no read; under 100 mA, bit 6 = 1 →
  `Charging`; bit 6 = 0 and at the threshold or over → `Full`; bit 6 = 0
  under it → `Not charging`. upower: `Full` → "fully charged"; `Not
  charging` → "pending charge", or "fully charged" at 90 % or more.

### The learning

- It starts at each full charge (D14), by the marker; it is kept in the
  memory of the module only (D16); a reboot, a cut by `V_OFF`, a suspend
  drop it (D17, D33).
- It counts the charge (net, in − out) and the energy; it keeps the
  curve in bins that merge when full (D44).
- R (D34): at the 60th second after the charger goes out: (the voltage
  at rest − the mean voltage of the first 60 s) / (the mean current of
  those 60 s + the current at rest); 0 if that step is under 300 mA.
- The end (D2, D4): the silence of the AXP. `V_end` is the mean voltage of
  the last 30 s, `I_end` the mean current (D35).
- At the end the module makes the model of the run (D20): `Q_full`,
  `E_full`, `R`, `V_end`, `I_end`, the table of 21 points, one each 5 % of
  the charge out, the mean V and I under load at each (D22); puts it in
  `run_*`; sends the uevent (D19).

### The memory of the learning

D44 (`discussion.md`, "Q5: the memory"). One allocation at the probe; none
in the work.

| what | size |
|---|---|
| the curve: 80 bins, the mean V (u16, mV) and I (u16, mA) each | 320 B |
| the models, in use, the draft, the run: 21 points of u16 mV and u16 mA, 5 values | 3 × about 104 B |
| the count, the sums of the bin that is filled now, R at the start, the end | about 90 B |

About 0.75 KiB. The first step is 25 mAh; when the 80 bins are full, each
two next bins become one (the means of the two) and the step doubles; the
count goes on. At the end of any run 40-80 bins are used, each 1.25-2.5 %
of the charge; the 21 points of the table are put on them by a line
between the middles of the bins. No bound of the capacity, no need to
know it before. The interface gives µV and µA; the module keeps mV and
mA.

### The model and the level

- The model (D22, D34, D35): `Q_full`, `E_full`, `R`, `V_end` (here the
  threshold `V_thr`), `I_end`, the table of 21 points under load.
- With no model: `Q_full` = `charge_full_design` of `battery@0`, not
  guessed (D44); the
  table of the run of 24.10 (`hardware/notes/discharge-cm5.md`); the end
  3.2 V (D21), `end_uv`; R 87 mOhm, `r_uohm` (D34). Both parameters of
  the module (D36).
- The level (D23, D35): the count, `charge_now / Q_full`; the full state
  sets 100 %. On a discharge it falls at the speed of the count times
  f = (the level shown) / (left by the voltage), f in 0.5..2; the voltage
  weighs nothing above 30 % and in full at 10 %; V and I are means of
  30 s; "left" runs to the place of `V_thr` on the table moved to the
  current of now: V_t(x) − (I − I_t(x)) × R = `V_thr`. The level never
  rises on a discharge; it is 0 % when the mean of 30 s of the voltage is
  `V_thr` or under. On the charger the count only.
- The start level (a probe, a new model, a resume): by the voltage, the
  means of 30 s, the place on the table moved by R (D22, D34).
- `power_supply_batinfo_ocv2cap()` of the kernel takes a table of OCV; the
  table under load does not fit it; the module looks up its own.

### The interface

D43 (`discussion.md`, "Q4: the interface, in full"). Units of the
power supply class: µV, µA, µAh, µWh, µΩ.

The properties of the class (read by upower and by anyone):
`present`, `status` (D40), `health` (`Good`; `Dead` at the silence),
`technology`, `capacity`, `voltage_now`, `current_now` (negative in a
discharge), `power_now` (V × I of the last sample, the sign of
`current_now`, D55), `charge_full` (`Q_full` of the model in use),
`charge_full_design` (`battery@0`), `charge_now` (the level × `Q_full`),
`energy_full`, `energy_full_design`, `energy_now` (the level × `E_full`),
`internal_resistance` (R of the model in use; a property of the class
since 2025), `voltage_min_design`, `voltage_max_design`,
`constant_charge_current_max`, `scope` (System). No `time_to_empty_now`,
`time_to_full_now`, `capacity_level` (D55): upower works the times out
itself, and reads `capacity_level` only at `capacity` = 0.

Its own attributes (`attr_grp`), one value or one list of one type per
file (`Documentation/filesystems/sysfs.rst`):

| attribute | mode | what |
|---|---|---|
| `model_q_full`, `model_e_full`, `model_r`, `model_v_end`, `model_i_end` | rw | the draft of the model: a number each |
| `model_table_uv`, `model_table_ua` | rw | the draft of the table: a list each, split by spaces, the same length |
| `model_commit` | wo | `1`: the draft is checked and becomes the model in use, at once |
| `model_source` | ro | `default` or `loaded` |
| `run_q_full`, `run_e_full`, `run_r`, `run_v_end`, `run_i_end`, `run_table_uv`, `run_table_ua` | ro | the run as learned (D29); `-ENODATA` until `done` |
| `learn_state` | ro | `idle`, `full`, `learning`, `done` |
| `learn_charge` | ro | the charge counted by the learning that runs, µAh |

- The draft: at the probe it holds the default model; a write changes
  the draft only; a read gives the draft; after a commit the draft is the
  model in use. The model in use is seen by `charge_full`, `energy_full`,
  `internal_resistance` and `model_source`.
- The checks of `model_commit`: the two lists of the same length, 2 to 64
  points; the voltage of the table falls along it; `Q_full` 0.5-50 Ah;
  R 10-500 mΩ; `V_end` from `voltage-min-design` of `battery@0` (`V_OFF`, D51) to 3.8 V. A bad value: `-EINVAL`, one line in
  dmesg, the model in use stays.
- The silence (D19): the module sets `health` to `Dead`, `learn_state` to
  `done` when a run is there, and calls `power_supply_changed()`: the
  uevent `change` of the class carries `POWER_SUPPLY_HEALTH=Dead`. The rule
  of udev matches that key; the program reads `learn_state` and `run_*`.
  No uevent and no keys of our own. After the silence the work stops, so
  there is no second such uevent.
- The keys of the files of runs and of the output of `load`: the names of
  the attributes in capitals (`RUN_Q_FULL`, `MODEL_Q_FULL`); a run becomes
  a model by its prefix.
- The parameters of the module, writable at run time (D24): `end_uv`
  (3200000, D21), `r_uohm` (87000, D34), `full_margin_uv` (50000, D39),
  `full_current_ua` (100000, D39), `r_step_min_ua` (300000, D34),
  `silent_samples` (6, D41), and C' (D23): `mean_s` (30), `f_min_pct` (50),
  `f_max_pct` (200), `weight_from_pct` (30), `weight_full_pct` (10).

### dmesg

D15: the list and the rule (a line at a change of state, not at each
sample) are in `discussion.md`, "dmesg, the list after D33-D36". The
prefix is `axp22x_soft_battery axp20x-battery-power-supply:`. A cycle
gives about six lines.

### User space

- The program `axp22x-soft-battery`, in Rust (D25), package
  `sys-power/axp22x-soft-battery-tools` (D26).
- At the silence: a rule of udev on `POWER_SUPPLY_HEALTH=Dead` (D43) runs
  `axp22x-soft-battery save` right in `RUN` (D27); no service. The sandbox of udevd allows its work
  (`discussion.md`, "The sandbox of udevd").
  - With no run: a forced power off (D19).
  - With a run: it checks it (proposed, Q6), keeps it as
    `/var/lib/axp22x-soft-battery/runs/<seq>.env` (D29, D45), or, if it
    fails the check, `invalid/<seq>.env` (D45); `fsync` of the file and of
    its directory, then a forced power off. No mean here (D30).
  - Its lines go into `/dev/kmsg` (D27).
- At boot: the rule on `add` of the power supply runs `axp22x-soft-battery
  load` by `IMPORT{program}` (D13, D30); `load` reads all runs each time
  (D31), makes the mean model and prints it; `ATTR{}` writes the draft,
  `ATTR{model_commit}="1"` last. No valid run: nothing is printed or
  written.

  ```
  ACTION=="add", SUBSYSTEM=="power_supply", KERNEL=="axp22x-soft-battery", \
    IMPORT{program}="/usr/bin/axp22x-soft-battery load", \
    ENV{MODEL_Q_FULL}=="?*", \
    ATTR{model_q_full}="$env{MODEL_Q_FULL}", ..., \
    ATTR{model_commit}="1"
  ```
- The mean model (D29, D35, D48, D54): `V_end` is the
  highest of the runs in `runs/` (the last 5), + 50 mV; the other values
  and each pair (V, I) of the table: the mean of the same runs.
- `/var` is to be mounted when the rule runs; one root partition gives it.
- The files (D45-D48): `runs/` (the last 5: the mean and the highest
  `V_end`), `archive/` (set aside by age by `load`, or by `reset`),
  `invalid/` (failed the check).
- The check of a run before `runs/` (D54): the checks of `model_commit`
  (D43, D51).
- `seq` (D52); `load --apply` (D53).
- The crates (D50, D54): `lexopt` only; errors by
  `Box<dyn std::error::Error>`; `lto = "fat"`, `codegen-units = 1`.

### The packages

- Two packages (D6, D25); the source a copy in `files/` of each, made by a
  script of this repository, with its sum in the ebuild (D28).
- The module: `sys-power/axp22x-soft-battery` (D26), `linux-mod-r1`,
  `virtual/dist-kernel`; it installs the module and
  `/etc/modprobe.d/axp22x-soft-battery.conf`, `blacklist axp20x_battery`
  (D36). The ebuild stops with `CONFIG_BATTERY_AXP20X=y`
  (`linux_chkconfig_builtin`); the source has `#if
  IS_BUILTIN(CONFIG_BATTERY_AXP20X)` and `#error`.
- The program: `sys-power/axp22x-soft-battery-tools` (D26), `cargo`; the
  program and both rules of udev. Crates and fat LTO (D50): the ebuild
  takes the crates from crates.io by `CRATES` (`cargo_crate_uris`), each
  with its line in `Manifest`; that `lto = "fat"` of `Cargo.toml` holds
  under the `cargo` eclass is to see in the log of the build.
- The guard (D57): `axp22x_guard`, a module of its own in the
  package of the module, loaded by
  `/usr/lib/modules-load.d/axp22x-guard.conf`; each 10 s it reads
  `online` of `axp20x-ac`; 12 errors in a row → `hw_protection_trigger()`;
  `system_freezable_wq`, no code of suspend; about 60 lines.
- Put off: the module in the tree of the kernel,
  `plans/axp22x-soft-battery-in-tree.md`.

### The default curve

D58, D59. With no model the module works out the percent by the voltage
alone: OCV = V + I × R (the means of 30 s, R `r_uohm`), on
`ocv-capacity-table-0` of `battery@0` if the device tree has it, else on
the curve built in. 100 % in the full state (D40); 0 % at `V_thr`
(`end_uv`, D21). On a discharge the level does not rise: the least of what was shown
since the charger went out (D60).

The curve built in: the mean of the three logs to the silence (24.10,
25.10, 08.11), `tools/make-curve.py 87`; the percent is of the charge of
each run to its silence:

| % left | 100 | 95 | 90 | 85 | 80 | 75 | 70 | 65 | 60 | 55 | 50 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| OCV, mV | 4221 | 4119 | 4093 | 4084 | 4075 | 4064 | 4035 | 3988 | 3934 | 3897 | 3868 |

| % left | 45 | 40 | 35 | 30 | 25 | 20 | 15 | 10 | 5 | 0 |
|---|---|---|---|---|---|---|---|---|---|---|
| OCV, mV | 3837 | 3799 | 3760 | 3716 | 3664 | 3608 | 3554 | 3499 | 3437 | 3351 |

```c
/* OCV, mV, at 100, 95, ... 0 % left */
static const u16 default_ocv_mv[21] = {
	4221, 4119, 4093, 4084, 4075, 4064, 4035, 3988, 3934, 3897, 3868,
	3837, 3799, 3760, 3716, 3664, 3608, 3554, 3499, 3437, 3351
};
```

The same as the device tree takes it:

```
ocv-capacity-celsius = <25>;
ocv-capacity-table-0 = <4221000 100>, <4119000 95>, <4093000 90>,
	<4084000 85>, <4075000 80>, <4064000 75>, <4035000 70>,
	<3988000 65>, <3934000 60>, <3897000 55>, <3868000 50>,
	<3837000 45>, <3799000 40>, <3760000 35>, <3716000 30>,
	<3664000 25>, <3608000 20>, <3554000 15>, <3499000 10>,
	<3437000 5>, <3351000 0>;
```

The error of the level by the voltage alone, at each 30 s of each log:
with this curve, the mean −2.4 to +2.2 %, the largest 4.7 %; with the curve
of the two other logs only, the largest 7.2 %. At the bottom the curves of
the runs part (132 mV at 0-5 %): the silence comes at another OCV at
another load; the end of D35 (0 % at `V_thr` under load) sets 0 % there,
not the curve.

## Steps

1. Done: the five writes of Rex, read in the datasheet
   (`discussion.md`, "The writes of Rex"). Left to see: where GPIO0 of the
   AXP goes on the board (the schematic, the net `AGPIO0`).
2. Done: the scheme of showcase is in line with D36, D37, D41, D43.
3. The packages: the script of the copy (D28), the ebuilds; `linux-mod-r1`
   and `cargo` in the cross build of unidev.
4. The module.
5. The program.
6. The kernel series: 0010 out (D4, D57), `bump.sh --sum`.
7. A test on the device: a full charge, a discharge to the silence, the
   save, the power off, the model at the next boot.

## Open questions

- QB1, QB4: variant B, in "Two variants".

## Fixed on the review

What did not agree, 2026-11-12, and how it stands now:

- The event key: `UCONSOLE_BATTERY_EVENT` (a name before D26) in one place,
  `UCONSOLE_SOFT_BATTERY_EVENT` in another; since D43 neither: the key of
  the class, `POWER_SUPPLY_HEALTH=Dead`.
- The uevent was said to carry the whole model (about 500 B) in two
  places; "The interface" has the program read it from sysfs: the uevent
  carries no model.
- The work was said to read the ADC by the regmap; D36 takes it by IIO.
- The step of the points: "each 50 mAh, about 180 points" in one place,
  "`charge_full_design` / 256, 35 mAh" in another. With
  `charge_full_design_uah=3337000` in `config.txt` of the device
  (`uconsole-pi-firmware`), / 256 gives 13 mAh, and 512 points end at
  6.7 Ah, before the end of cells of 9 Ah. Then a fixed 50 mAh; now 80
  bins that merge when full (D44).
- R "from dV/dI at steps" in "The model": D34 takes it from the step at
  the start.
- The run of 3.10 V at 1.9 A was named the run of 2026-11-08; it is the
  run of 2026-10-24 (`hardware/notes/discharge-cm5.md`). D21 says the same
  and is fixed.
- "The values of C' and the step of R are parameters": the step of R is
  gone (D34); the parameters are listed in "The interface".
- dmesg: "four lines a cycle" and "`dev_*` of the device
  `axp22x-soft-battery`" against "six lines" and the device of the cell:
  six lines, the cell.
- The start level: "means over 30-60 s" against 30 s of D23: 30 s.
- "Open questions" still held the table of 21 points (D22), C' (D23) and
  the end (D35): decided, out of the list.
- Steps: the path of `battery-power-supply` and the edge 0x70 → 0x30 on
  the device are no more needed (D36; the marker is a state).
- The scheme of showcase reads the ADC by the AXP and 0x00 each second:
  to be brought in line (step 2).
- Not ours to fix, noted: `config.txt` of the device has
  `charge_full_design_uah=3337000`; the cells gave 9067 mAh. With no model
  the module takes 3337 mAh as `Q_full`, so the level with no model is
  wrong until the first run, or until that line is set right.

# Plan

The decisions are in `decisions.md`; how they came, in `discussion.md`.
Each point below names its decision (Dnn), or says "proposed" and the
number of its question in "Open questions" (Qn).

Read again on 2026-11-12 against all decisions and answers; what did not
agree is listed at the end, "Fixed on the review".

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
- One power supply of type Battery, `uconsole-soft-battery` (D26).
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
  3. out of the mutex: `power_supply_changed()` if `status`, the integer
     percent or `capacity_level` changed; the lines of dmesg; at the
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
  curve: the mean V and I at each step of charge out.
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

Proposed (Q5). One allocation at the probe; none in the work.

| what | size |
|---|---|
| the points of the curve: the mean V (u16, mV) and I (u16, mA) each 50 mAh of charge out | 4 B × 512 = 2 KiB |
| the sums of the point that is filled now | 24 B |
| the count: charge, energy, time | 32 B |
| R at the start: the voltage at rest, the sums of 60 s | 24 B |
| the end: `V_end`, `I_end` (means of 30 s) | 12 B |
| the model in use, and the one written but not committed | 2 × about 200 B |

About 2.6 KiB. The step is a fixed 50 mAh: 512 points hold 25.6 Ah. Past
that the count goes on and no point is added; dmesg says it once.

### The model and the level

- The model (D22, D34, D35): `Q_full`, `E_full`, `R`, `V_end` (here the
  threshold `V_thr`), `I_end`, the table of 21 points under load.
- With no model: `Q_full` = `charge_full_design` of `battery@0`; the
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

Proposed (Q4; `discussion.md`, "Q4: the interface, in full"). Units of the
power supply class: µV, µA, µAh, µWh, µΩ.

The properties of the class (read by upower and by anyone):
`present`, `status` (D40), `health` (`Good`; `Dead` at the silence),
`technology`, `capacity`, `capacity_level`, `voltage_now`, `current_now`
(negative in a discharge), `power_now`, `charge_full` (`Q_full` of the
model in use), `charge_full_design` (`battery@0`), `charge_now`,
`energy_full`, `energy_full_design`, `energy_now`, `internal_resistance`
(R of the model in use; a property of the class since 2025),
`time_to_empty_now`, `time_to_full_now`, `voltage_min_design`,
`voltage_max_design`, `constant_charge_current_max`, `scope` (System).
How three of them are worked out: Q7.

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
  R 10-500 mΩ; `V_end` 2.9-3.8 V. A bad value: `-EINVAL`, one line in
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
prefix is `uconsole_soft_battery axp20x-battery-power-supply:`. A cycle
gives about six lines.

### User space

- The program `uconsole-soft-battery`, in Rust (D25), package
  `sys-power/uconsole-soft-battery-tools` (D26).
- At the silence: a rule of udev on `POWER_SUPPLY_HEALTH=Dead` (Q4) runs
  `uconsole-soft-battery save` right in `RUN` (D27); no service. The sandbox of udevd allows its work
  (`discussion.md`, "The sandbox of udevd").
  - With no run: a forced power off (D19).
  - With a run: it checks it (proposed, Q6), keeps it as
    `/var/lib/uconsole-soft-battery/runs/<seq>.env` (D29) or
    `<seq>.invalid.env`, `fsync` of the file and of `runs/`, then a forced
    power off. No mean here (D30).
  - Its lines go into `/dev/kmsg` (D27).
- At boot: the rule on `add` of the power supply runs `uconsole-soft-battery
  load` by `IMPORT{program}` (D13, D30); `load` reads all runs each time
  (D31), makes the mean model and prints it; `ATTR{}` writes the draft,
  `ATTR{model_commit}="1"` last. No valid run: nothing is printed or
  written.

  ```
  ACTION=="add", SUBSYSTEM=="power_supply", KERNEL=="uconsole-soft-battery", \
    IMPORT{program}="/usr/bin/uconsole-soft-battery load", \
    ENV{MODEL_Q_FULL}=="?*", \
    ATTR{model_q_full}="$env{MODEL_Q_FULL}", ..., \
    ATTR{model_commit}="1"
  ```
- The mean model (D29, D35; the rest proposed, Q6): `V_end` is the
  highest of all valid runs since `reset`, + 50 mV (D35); the other values
  and each pair (V, I) of the table: the mean of the last 5 valid runs.
- `/var` is to be mounted when the rule runs; one root partition gives it.

### The packages

- Two packages (D6, D25); the source a copy in `files/` of each, made by a
  script of this repository, with its sum in the ebuild (D28).
- The module: `sys-power/uconsole-soft-battery` (D26), `linux-mod-r1`,
  `virtual/dist-kernel`; it installs the module and
  `/etc/modprobe.d/uconsole-soft-battery.conf`, `blacklist axp20x_battery`
  (D36). The ebuild stops with `CONFIG_BATTERY_AXP20X=y`
  (`linux_chkconfig_builtin`); the source has `#if
  IS_BUILTIN(CONFIG_BATTERY_AXP20X)` and `#error`.
- The program: `sys-power/uconsole-soft-battery-tools` (D26), `cargo`; the
  program and both rules of udev. Proposed (Q6): no crate, `std` only.
- Put off: the module in the tree of the kernel,
  `plans/uconsole-soft-battery-in-tree.md`.

## Steps

1. Read: the five writes of Rex (0x30, 0x32, 0x34, 0x36, 0x90) in the
   datasheet.
2. Bring the scheme of showcase in line with D36 and D37.
3. The packages: the script of the copy (D28), the ebuilds; `linux-mod-r1`
   and `cargo` in the cross build of unidev.
4. The module.
5. The program.
6. The kernel series: 0010 out (D4), `bump.sh --sum`.
7. A test on the device: a full charge, a discharge to the silence, the
   save, the power off, the model at the next boot.

## Open questions

- Q4. The interface ("The interface"): the properties with `health` and
  `internal_resistance` of the class; the attributes, fewer (no
  `axp_state`, `learn_energy`, `learn_seconds`); the draft and its checks;
  the silence told by `health` = `Dead` in the uevent of the class, no
  uevent of our own; the parameters.
- Q5. The memory: the points each 50 mAh (fixed), 512 of them.
- Q6. The program: the mean of the last 5 valid runs, no limit of age;
  the check of a run (`Q_full` in 30..200 % of the design, V of the table
  falls with the charge out) and `<seq>.invalid.env`; `seq` in the name,
  the date inside; `reset` moves `runs/` to `runs.<seq>/`; `load` by hand
  with a mode that writes the attributes; `systemctl poweroff --force`;
  `std` only.
- Q7. Three properties: `power_now` = V × I (without it upower shows no
  time, "What does patch 0002 do"); `time_to_empty_now` by the mean
  current of 60 s; `capacity_level` `Critical` under 5 %, `Low` under
  15 %, `Full` in the full state, else `Normal`.

## Fixed on the review

What did not agree, 2026-11-12, and how it stands now:

- The event key: `UCONSOLE_BATTERY_EVENT` (a name before D26) in one place,
  `UCONSOLE_SOFT_BATTERY_EVENT` in another: the second.
- The uevent was said to carry the whole model (about 500 B) in two
  places; "The interface" has the program read it from sysfs: the uevent
  carries no model.
- The work was said to read the ADC by the regmap; D36 takes it by IIO.
- The step of the points: "each 50 mAh, about 180 points" in one place,
  "`charge_full_design` / 256, 35 mAh" in another. With
  `charge_full_design_uah=3337000` in `config.txt` of the device
  (`uconsole-pi-firmware`), / 256 gives 13 mAh, and 512 points end at
  6.7 Ah, before the end of cells of 9 Ah. Now a fixed 50 mAh (Q5).
- R "from dV/dI at steps" in "The model": D34 takes it from the step at
  the start.
- The run of 3.10 V at 1.9 A was named the run of 2026-11-08; it is the
  run of 2026-10-24 (`hardware/notes/discharge-cm5.md`). D21 says the same
  and is fixed.
- "The values of C' and the step of R are parameters": the step of R is
  gone (D34); the parameters are listed in "The interface".
- dmesg: "four lines a cycle" and "`dev_*` of the device
  `uconsole-soft-battery`" against "six lines" and the device of the cell:
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

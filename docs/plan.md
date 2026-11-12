# Plan

The decisions are in `decisions.md`; how they came, in `discussion.md`.

## The design as proposed

Not decided where it says "proposed".

### The module

The module binds to the cell `battery-power-supply` of the MFD `axp20x`
in place of `axp20x_battery`, which the package blacklists (D36;
`discussion.md`, "On the drivers of the AXP", "A cell of the MFD"). No
overlay, no node of its own, no change of the device tree, of the
kernel or of `config.txt`.

- One power supply of type Battery, `uconsole-soft-battery`; its
  properties and its own attributes: "The interface" below.
- Its `of_match_table` has `x-powers,axp221-battery-power-supply`, the
  compatible of the cell; udev loads it by the alias of that device. The
  module sets the PMIC up at each probe: the current of the charge (2.1 A,
  `constant-charge-current-max-microamp` of `battery@0`, by
  `power_supply_get_battery_info()` on the node of the cell, which has
  `monitored-battery`), `V_OFF`
  (register 0x31), the voltage of the full charge, and the five writes of
  Rex in `axp20x_power_probe` (0x30, 0x32, 0x34, 0x36, 0x90); the meaning
  of each is to check in the datasheet.
- The data: the ADC by the IIO channels of `axp20x_adc`,
  `devm_iio_channel_get(dev, "batt_v")`, `"batt_chrg_i"`,
  `"batt_dischrg_i"`: the map of that driver names our device
  (`axp20x-battery-power-supply`). `-EPROBE_DEFER` while they are not
  there. The registers 0x00 and 0x01 by `dev_get_regmap(dev->parent)`;
  they are volatile in the regmap: each read goes to the bus.
- The count: every 1 s, the charge and the energy (V × I × dt) by
  trapezoids. The charge is net: in minus out.
- The marker of the full charge. Proposed: the full state, not its
  edge: in the read of each 1 s, bit 7 of 0x00 (ACIN present) is 1, bit 6
  of 0x01 (charging) is 0, the voltage is `voltage-max-design` of
  `battery@0` − 50 mV or more (4.15 V for 4.2 V). Each sample in
  that state starts the learning again, so the learning starts in fact
  at the last full sample before the charger goes out; a probe in that
  state needs nothing more. The three logs that start full show it:
  0x00 = 0xc4, 0x01 = 0x30, 4204-4205 mV, 12-50 mA. No IRQ.
  (`discussion.md`, "The end of the charge", "The start of the
  discharge logs".)
- The end: the AXP does not answer on I2C, 3 reads in a row (proposed;
  `discussion.md`, "The work, the lock, the silence"). Then the work
  stops: the AXP did not answer again in any log.

### The work and the lock

Proposed; the scheme is on the desk `uconsole-soft-battery-work` of
showcase, `shots/1-threads.scheme`.

- One `delayed_work` each 1 s: it reads 0x00, 0x01 and the ADC by the
  regmap of the AXP, with no lock of its own; then, under one mutex, it
  counts (the time between samples by `ktime`, so a late work counts
  right), steps the learning, adds a point, steps the level.
- One mutex for the whole state: the count, the level, the points, the
  sums of R at the start, the model in use, the draft, the run. Under it only the
  sums, microseconds.
- Out of the mutex: the read of I2C (ms on `i2c-gpio`),
  `power_supply_changed()`, `kobject_uevent_env()`.
- `power_supply_changed()` only when `status` changes or the level moves
  by 1 %: it sends a uevent `change` each time, and udev reads its rules.
  The power supply class calls `get_property` from its own work, not from
  ours: the mutex is not taken twice.
- `get_property` and the attributes take the mutex for a copy, and
  return.
- The rule of `axp_silent` matches its own key only, not each uevent
  `change`.
- Suspend (D33), by `DEFINE_SIMPLE_DEV_PM_OPS` (suspend and resume;
  freeze and thaw the same):
  - suspend: the work is cancelled (`cancel_delayed_work_sync`), so no
    read of a sleeping bus counts as the silence; a learning that runs
    is dropped, `learn_state` goes to `idle`, one line in dmesg;
  - resume: the time of the last sample is set to now, so the first
    sample does not count the time of the sleep as charge; the charge
    out in the sleep is not known (the AXP is not a store, D8), so the
    level starts again by the voltage, as at boot; the work starts
    again;
  - after a resume the full state (the marker) starts a learning as
    always.
  - Suspend on the CM5 is not in the kernel of Raspberry Pi yet
    (`uconsole/linux/notes/suspend-cm5.md`): this path cannot be tried on
    the device now.
- States of the learning: `idle` → `learning` at each end of a full
  charge (D14); `learning` counts; a new end of a charge starts it again;
  `learning` → `done` at the silence of the AXP: the module makes the
  model of the curve (D20). A learning broken by a reboot or a cut by
  `V_OFF` is lost; it is a failed learning (D17).
- At the silence of the AXP the module sends one uevent `change` (D19):
  `UCONSOLE_BATTERY_EVENT=axp_silent`, and with a learning done also the
  model in the same keys as the files of runs (below). With no learning the
  module does nothing more (D18). It knows nothing of upower and of who
  powers off (D11, D12, D18).
- While the learning runs, all its data is in the memory of the module
  only (D16). Nothing goes to the disk before `done`.

### The interface

Proposed (`discussion.md`, "The interface of the module"). Units as the
power supply class has them: µV, µA, µAh, µWh; R in µΩ.

The properties of the power supply (the class, read by upower):
`present`, `status` (Charging, Discharging, Full: the full state of the
marker, Not charging), `technology`, `capacity`, `capacity_level`,
`voltage_now`, `current_now` (its sign as the ABI of 7.3 says, to check
in `Documentation/ABI/testing/sysfs-class-power`), `power_now`,
`charge_full` (of the model), `charge_full_design` (device tree),
`charge_now`, `energy_full`, `energy_full_design`, `energy_now`,
`time_to_empty_now`, `time_to_full_now`, `voltage_min_design`,
`voltage_max_design`, `constant_charge_current_max`, `scope` (System).

Its own attributes (`attr_grp` of `power_supply_config`), one value or
one list per file, as sysfs asks:

| attribute | mode | what |
|---|---|---|
| `model_q_full`, `model_e_full`, `model_r`, `model_v_end`, `model_i_end` | rw | the model in use |
| `model_table_uv`, `model_table_ua` | rw | the table: the voltages, the currents, one list each, the same length |
| `model_source` | ro | `default` or `loaded` |
| `model_commit` | wo | `1`: the values written to `model_*` are checked and taken together; a bad one: `-EINVAL` and a line in dmesg; the model in use stays |
| `run_q_full`, `run_e_full`, `run_r`, `run_v_end`, `run_i_end`, `run_table_uv`, `run_table_ua` | ro | the run learned, as it is (D29); there after `done` |
| `learn_state` | ro | `idle`, `full`, `learning`, `done` |
| `learn_charge`, `learn_energy`, `learn_seconds` | ro | the count of the learning that runs |
| `axp_state` | ro | `ok`, `silent` |

`run_r` is R of the step at the start of the run (D34); 0 when the step
was under 300 mA.

The uevent at the silence: `UCONSOLE_SOFT_BATTERY_EVENT=axp_silent`,
`UCONSOLE_SOFT_BATTERY_RUN=1` with a run, `0` with none (D19). The
program reads the values from sysfs.

The files of runs (`runs/<seq>.env`) and the output of `load` are lines
`KEY=value`, the keys the names of the attributes in capitals:
`RUN_Q_FULL=` in a run, `MODEL_Q_FULL=` from `load`. The rule of D13:

```
ACTION=="add", SUBSYSTEM=="power_supply", KERNEL=="uconsole-soft-battery", \
  IMPORT{program}="/usr/bin/uconsole-soft-battery load", \
  ENV{MODEL_Q_FULL}=="?*", \
  ATTR{model_q_full}="$env{MODEL_Q_FULL}", ..., \
  ATTR{model_table_ua}="$env{MODEL_TABLE_UA}", \
  ATTR{model_commit}="1"
```

udev writes `ATTR{}` in the order of the rule, so `model_commit` comes
last. `ENV{MODEL_Q_FULL}=="?*"`: with no valid run, `load` prints nothing
and nothing is written.

The values of C' and the step of R are parameters of the module,
writable at run time: `/sys/module/uconsole_soft_battery/parameters/`.

### The memory of the learning

Proposed. One allocation at the probe; no allocation in the work.

| what | size |
|---|---|
| the points of the curve: the mean V (u16, mV) and I (u16, mA) per step of charge out | 4 B × 512 = 2 KiB |
| the sums of the point that is filled now: V, I, count | 24 B |
| the count: charge and energy (s64 each), time of the start, last sample | 32 B |
| the end: `V_end`, `I_end`, time | 12 B |
| R at the start (D34): the voltage at rest, the sums of V and I of the first 60 s, the count | 24 B |
| the model in use and the one written, not yet committed: 21 points of V and I each, `Q_full`, `E_full`, `R`, `V_end`, `I_end` | about 2 × 200 B |

About 2.6 KiB in all. The step is `charge_full_design` / 256 (35 mAh
for 9 Ah), so 512 points hold up to 2 × the design. Past that the count
goes on and no point is added; dmesg says it once.

The module makes the model at `done` (D20), in integers:

- `R`: made at the 60th second of the run, not at `done` (D34):
  (the voltage at rest − the mean voltage of the first 60 s) / (the mean
  current of those 60 s + the current at rest); 0 if that step is under
  300 mA;
- the table: 21 points, one each 5 % of the charge out, taken from the
  curve between its points; each point is the mean V and the mean I
  under load, as measured (D22). No OCV: the AXP gives the voltage
  under load only (`discussion.md`, "How to get OCV");
- `Q_full`, `E_full`, `V_end`, `I_end` from the count.

The model is about 500 B as text: it goes into the uevent whole (the
environment of a uevent is at most 2048 B, `UEVENT_BUFFER_SIZE`). The
curve stays in the module; user space does not read it.

### Messages of dmesg

Required: the module loaded, the model awaited, the learning started,
the learning done (D15); the whole list below is D15 too. Each is one line, at
an event, never in a loop. `dev_*` of the device `uconsole-soft-battery`.

A line of the learning is written at a change of state, not at each
sample: the module starts the learning again at each sample of the full
state (the marker), silently. So a cycle gives about four lines: full,
started, done or dropped, silent. (`discussion.md`, "dmesg and the full
state".)

The prefix of each line is that of `dev_*`: the name of the driver and
of the device, `uconsole_soft_battery axp20x-battery-power-supply:`
(D36: the device is the cell of the MFD). `dmesg | grep
uconsole_soft_battery` finds them all.

| level | when | text, about |
|---|---|---|
| info | the probe | `bound: charge current 2100 mA, V_OFF 2900 mV, full at 4200 mV` |
| info | the probe | `no model yet: 9067 mAh of battery@0, end 3200 mV, R 87 mOhm; awaiting user space` |
| info | `model_commit` | `model: 8542 mAh, 31.2 Wh, R 78 mOhm, end 3211 mV, 21 points` |
| err | `model_commit` refused | `model refused: model_table_uv has 20 values, model_table_ua 21` |
| info | the start level (probe with a model, a new model, resume) | `start level 54 % by voltage: 3711 mV at 1601 mA` |
| info | the full state begins | `full: 4204 mV at 21 mA; the learning starts when the charger goes out` |
| info | the full state begins while a learning runs | `full: learning dropped, 1234 mAh counted` |
| info | the charger goes out from the full state | `learning started: 4204 mV at rest, 4104 mV at 1395 mA` |
| info | the 60th second of a learning (D34) | `learning: R 77 mOhm by a step of 1853 mA`, or `learning: no R, a step of 210 mA` |
| debug | the full state ends by a new charge, the charger in | `full state left: charging again` |
| warn | the points are full | `learning: 512 points full at 18000 mAh; the curve stops, the count goes on` |
| info | the level reaches 0 % by the voltage (D35) | `level 0 %: 3211 mV at 2450 mA, mean of 30 s` |
| warn | the AXP silent | `AXP silent: 3 reads failed, last -6` |
| notice | the learning done | `learning done: 8204 mAh, 30.1 Wh in 4 h 14 min, end 3161 mV at 2452 mA, R 77 mOhm; level shown 0 %` |
| warn | the AXP silent, no learning | `AXP silent, no learning; level shown 3 %; uevent sent` |
| info | suspend during a learning (D33) | `suspend: learning dropped, 1234 mAh counted` |
| warn | a read failed, not the silence | `ADC read failed: -5` (`dev_warn_ratelimited`) |
| warn | the module removed during a learning | `removed: learning lost, 1234 mAh counted` |

The level shown at the silence is in both lines of the silence: it
measures the error of the end (D35) on each run.

The power supply itself makes the uevents of `add` and of a change of
its values; they are not in dmesg.

### The model

- `Q_full`, `E_full`; the curve of the voltage against the charge out,
  one point every 50 mAh (about 180 points for 9 Ah), turned into percent
  at the end; `R`, the resistance of the cells, from dV/dI at steps of the
  current; `V_end`, `I_end` at the silence (3.10 V at 1.9 A in the run of
  2026-11-08, `hardware/notes/discharge-cm5.md`).
- With no model: `charge_full_design` of the device tree and the curve of
  `hardware/notes/discharge-cm5.md`. The end is 3.2 V under load (D21), a
  parameter of the module, `end_uv` (no node of its own: D36); the curve
  is cut there. With a model: D35.
  (`discussion.md`, "The end by 3.2 V")
- `power_supply_batinfo_ocv2cap()` of the kernel takes a table of OCV;
  the table under load does not fit it. The module looks up its own
  table.

### The level of the charge

- The base is the count: `charge_now / Q_full`. The marker of the full
  charge sets 100 %.
- At a boot in the middle of a discharge the start is by the voltage
  (D22): the means V and I over 30-60 s; the point q of the table
  where V_t(q) − (I − I_t(q)) × R = V. R only moves the curve by the
  difference of the currents, not by the whole current. Nothing of the
  charge is saved over a boot (D16).
- The voltage corrects the count (D23): on a discharge the level falls
  at the speed of the count times f = (the level shown) / (left by the
  voltage), f in 0.5..2; it never rises on a discharge. The voltage
  weighs nothing above 30 % and in full at 10 %; V and I are means over
  30 s. On the charger the count only; the end of the charge sets 100 %.
- The error of the voltage, from the table of `discharge-cm5.md`: the
  voltage moves about 75 mV per 10 % at 60-40 %, about 130 mV per 10 %
  at 20-0 %. An error of 20 mΩ in R at a difference of 0.5 A from the
  learned current is 10 mV: about 1.5 % in the middle, under 1 % near the
  end.

### User space

- A rule of udev on the uevent `axp_silent` runs the program right in
  `RUN` (D27): `RUN+="/usr/bin/uconsole-soft-battery save"`. No service.
  The program writes its lines into `/dev/kmsg`, next to the lines of the
  module in dmesg. At `done` the module puts the run into its attributes `run_*` and sets
  `learn_state` to `done`; the program reads them from sysfs.
- The sandbox of `systemd-udevd.service` on the device lets it: no
  `ProtectSystem`, no `ReadOnlyPaths`; `AF_UNIX` for `systemctl`;
  `@system-service` has `fsync`; all capabilities but the clock
  (`discussion.md`, "The sandbox of udevd"). The program is built for
  aarch64 (`SystemCallArchitectures=native`) and needs no memory both
  writable and executable (`MemoryDenyWriteExecute`).
- The program takes two events (D19):
  - the AXP silent: a forced power off;
  - the AXP silent and a model: it checks the run (proposed: `Q_full`
    in 30..200 % of the design, V of the table falls with the charge
    out), keeps it as `/var/lib/uconsole-soft-battery/runs/<seq>.env`,
    or `<seq>.invalid.env` with a line into `/dev/kmsg`; `fsync` of the
    file and of `runs/`; then a forced power off. No mean here (D30).
- It runs only at that event: no daemon, no write to the disk during the
  learning (D16, D20).
- Proposed: the forced power off is `systemctl poweroff --force`: no stop
  of the services; systemd kills the processes and unmounts the file
  systems.
- The model reaches the module at boot by a rule of udev (D13), on the
  event `add` of the power supply; the program makes the mean there
  (D30):
  the rule is in "The interface". `IMPORT{program}` takes the lines `KEY=value` the program prints;
  `ATTR{}` writes them into the attributes of the device. The module adds
  its attributes to the power supply by `attr_grp` of
  `power_supply_config`. It works for `=m` and `=y`: for a module built
  in the event comes before udev, and the coldplug of
  `systemd-udev-trigger` gives it again. Until the write the module goes
  by the default model (some seconds with `=y`). A new model makes the
  module work out the start level by the voltage again. The names and the
  form of the attributes: "The interface".
- `load` (D30):
  1. lists `runs/` and takes the last N valid runs by `seq`, by the names
     only (`*.invalid.env` are not taken);
  2. reads them, makes the mean and prints it; each boot, no cache
     (D31);
  3. no valid run: prints nothing; the module keeps the default model.
  - A run added or deleted is seen at the next boot by itself: the list
    of `runs/` is read each time.
- The files are read at the event: `/var` is to be mounted then; one root
  partition gives that.

### The packages

Two packages (D6, D25). Their source is a copy in `files/` of each (D28):
a script of this repository (`make export`, say) copies the module
(`src/`, `Kbuild`, the file of modprobe) into
`sys-power/uconsole-soft-battery/files/`, and the program (the crate, the
rules of udev) into `sys-power/uconsole-soft-battery-tools/files/`, and
writes the sum of `files/` into each ebuild. Its `--check` fails when the
sum is old. The version stays until a first release. A tarball of a tag
(`SRC_URI`) when this repository is published.

- The module: `sys-power/uconsole-soft-battery` (D26) in repo-uconsole;
  `linux-mod-r1`, depends on `virtual/dist-kernel`; it installs the
  module and `/etc/modprobe.d/uconsole-soft-battery.conf`: `blacklist
  axp20x_battery` (D36). The package removed, the file goes with it and
  `axp20x_battery` comes back at the next boot.
- The ebuild stops when the kernel has `axp20x_battery` built in
  (`CONFIG_BATTERY_AXP20X=y`): a blacklist cannot stop a driver built in
  (`linux_chkconfig_builtin` of `linux-info`); the source of the module
  has the same check (`#if IS_BUILTIN(CONFIG_BATTERY_AXP20X)`, `#error`).
  (`discussion.md`, "A conflict in Kconfig".)
- `config.txt` is not changed (D32); with no overlay there is no line to
  tell of.
- The program (D25), in Rust: `sys-power/uconsole-soft-battery-tools`
  (D26), by the eclass `cargo`. Proposed: one program
  `uconsole-soft-battery` with commands; `save` is the first (the work at
  the silence). It holds all of user space: the program, the
  rule of udev of the silence, the rule of udev of the model at boot
  (D13). It depends on the package of the module.
- Proposed: the program takes no crate, only `std`: it reads sysfs,
  writes a file, syncs, runs `systemctl`. Then the ebuild has no list of
  crates to fetch (`CRATES`) and no tarball of vendored crates.
- To check: the cross build of Rust for aarch64 in unidev (`cargo`
  eclass, the target of rust).

## Steps

1. On the device:
   - the full path of `battery-power-supply`:
     `find /proc/device-tree -name 'battery-power-supply*'`;
   - the end of a charge by `axp-log`: 0x01 from 0x70 to 0x30, the
     current, the voltage at that moment;
   - the writes of Rex (0x30, 0x32, 0x34, 0x36, 0x90): their meaning in
     the datasheet.
2. The scheme of the states and threads: a `.scheme` on a desk of
   showcase.
3. The packages: the script of the copy into `files/` (D28), the ebuilds, `linux-mod-r1` in the cross build of unidev
   (does it find `/usr/src/linux` of ROOT), the file of modprobe.
4. The module: the set up of the PMIC, the count, the power supply, the
   learning.
5. User space, the package of the program (Rust): the program, the
   rules of udev.
6. The kernel series: 0010 out, `bump.sh --sum`.
7. A test: a full charge, a discharge to the silence of the AXP, the save,
   the power off, the model at the next boot.

## Open questions

- The mean model (D29, D30). Worked out in `discussion.md`, "The mean
  model". Proposed:
  - all runs kept, `runs/<seq>.env`; `seq` a number the program counts up;
    the date is a key inside, not the order (the clock may be wrong);
  - the mean of the last 5 valid runs; N an option of the program. No
    age limit: at boot the clock may be wrong;
  - a plain mean: each value; each point of the table (one per step of
    the run's own `Q_full`) as a pair (V, I); `V_end`, `I_end` as a pair;
  - `reset` moves `runs/` to `runs.<seq>/`;
  - `load` by hand writes the attributes `model_*` at once, after a
    change of the options or of `runs/`.
- The values of the model and of C'. Worked out in `discussion.md`,
  "The values, from two logs". Proposed:
  - the table: 21 points stay (D22). 41 points gain little where the
    voltage weighs (below 50 %): `discussion.md`, "21 or 41 points";
  - C': as D23. The weight 50 → 20 % is taken back: played on the logs
    it changes the level at the silence by 0.5 % at most
    (`discussion.md`, "The weight of the voltage, played on the logs").
- The end follows the load. The AXP goes silent earlier on the curve at
  a heavier load at the end: 7.7 % left at 2.87 A, 5.9 % at 2.45 A, 0 at
  1.94 A, by the curve of 24.10. With a model of one load the level at the
  silence is off by up to 8 %; the count and the voltage agree, so C'
  does not fix it. Decided (D35; `discussion.md`, "The best end", "The
  end, in full"):
  - `V_end` of a run: the mean voltage of the last 30 s before the
    silence, as measured, not moved by I × R;
  - `V_thr` = the highest `V_end` of all valid runs since the last
    `reset`, + 50 mV (D35); the program writes it as `MODEL_V_END`; with no
    run 3.2 V (D21);
  - the module finds on the table, moved to the current of now (R), the
    point where the voltage reaches `V_thr`; C' steers the level to 0 %
    there; the level is 0 % at once when the mean of 30 s of the voltage
    is `V_thr` or under.
- Put off: the module as a patch of the series of the kernel, not a
  package of its own, and what goes with it: `plans/uconsole-soft-battery-in-tree.md`.

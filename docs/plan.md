# Plan

The decisions are in `decisions.md`; how they came, in `discussion.md`.

## The design as proposed

Not decided where it says "proposed".

### The module

- One power supply of type Battery, `uconsole-soft-battery`: `status`,
  `present`, `voltage_now`, `current_now`, `charge_full` (learned),
  `charge_full_design` (device tree), `charge_now`, `capacity`,
  `energy_*`, `time_to_empty`. Its own attributes: `learn_state`,
  `learn_charge`, `learn_energy`.
- The node `battery-power-supply` of the AXP is off (D5). The module sets
  the PMIC up at each probe: the current of the charge (2.1 A,
  `constant-charge-current-max-microamp` of `battery@0`), `V_OFF`
  (register 0x31), the voltage of the full charge, and the five writes of
  Rex in `axp20x_power_probe` (0x30, 0x32, 0x34, 0x36, 0x90); the meaning
  of each is to check in the datasheet.
- The data: the ADC of the AXP read by its regmap: 0x78 voltage, 0x7A
  current of the charge, 0x7C current of the discharge. They are volatile
  in the regmap: each read goes to the bus. The IIO channels of
  `axp22x-adc` are not used: an overlay of its own cannot take a label of
  the overlay `clockworkpi-uconsole-cm5`.
- The AXP is found by its compatible (`of_find_compatible_node`), then its
  I2C client, then `dev_get_regmap()`.
- The count: every 1 s, the charge and the energy (V × I × dt) by
  trapezoids. The charge is net: in minus out.
- The marker of the full charge: the IRQ "charge done" (register 0x49,
  bit 2), or bit 6 of 0x01 (charging) going to 0 with the external power
  on. Which one the AXP228 gives is to check on the device.
- The end: the AXP does not answer on I2C, some reads in a row.
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

### The memory of the learning

Proposed. One allocation at the probe; no allocation in the work.

| what | size |
|---|---|
| the points of the curve: the mean V (u16, mV) and I (u16, mA) per step of charge out | 4 B × 512 = 2 KiB |
| the sums of the point that is filled now: V, I, count | 24 B |
| the count: charge and energy (s64 each), time of the start, last sample | 32 B |
| the end: `V_end`, `I_end`, time | 12 B |
| the resistance `R`: the sum of dV/dI at steps of the current, the count | 16 B |
| the model in use: 21 points of V and I under load, `Q_full`, `E_full`, `R`, `V_end`, `I_end` | about 200 B |

About 2.2 KiB in all. The step is `charge_full_design` / 256 (35 mAh
for 9 Ah), so 512 points hold up to 2 × the design. Past that the count
goes on and no point is added; dmesg says it once.

The module makes the model at `done` (D20), in integers:

- `R`: the mean of dV/dI over pairs of samples 1 s apart where the
  current moved by 200 mA or more (proposed values);
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
the learning done (D15). Proposed: the whole list. Each is one line, at
an event, never in a loop. `dev_*` of the device `uconsole-soft-battery`.

| level | when | text, about |
|---|---|---|
| info | the probe | `loaded: AXP228 at 3-0034; charge current 2100 mA, V_OFF 2900 mV` |
| info | the probe | `no model yet, awaiting user space; default: 9067 mAh of the device tree` |
| info | a model written | `model: 9067 mAh, 33.3 Wh, R 95 mOhm, V_end 3100 mV` |
| err | a model refused | `model refused: <attribute> = <value>, <why>` |
| info | the start level | `start level 54 % by voltage: 3711 mV at 1601 mA` |
| info | the end of a charge | `charge complete: learning started at 4185 mV` |
| info | a new end of a charge during the learning | `charge complete: learning started again, 1234 mAh counted were dropped` |
| warn | the points are full | `learning: 512 points full at 18000 mAh, curve stops, count goes on` |
| warn | the AXP silent | `AXP silent: 6 reads failed, last -6` |
| notice | the learning done | `learning done: 9067 mAh, 33.3 Wh in 5 h 40 min, end 3100 mV at 1907 mA` |
| info | the model made | `model made: R 73 mOhm, 4028..3147 mV at 1436..1907 mA, uevent sent` |
| warn | the AXP silent, no learning | `AXP silent, no learning in progress, uevent sent` |
| warn | a read of the ADC failed (not the silence) | `ADC read failed: -5`, rate limited |
| warn | the module removed during the learning | `removed: learning lost, 1234 mAh counted` |

The power supply itself makes the uevents of `add` and of a change of
its values; they are not in dmesg.

### The model

- `Q_full`, `E_full`; the curve of the voltage against the charge out,
  one point every 50 mAh (about 180 points for 9 Ah), turned into percent
  at the end; `R`, the resistance of the cells, from dV/dI at steps of the
  current; `V_end`, `I_end` at the silence (3.10 V at 1.9 A in the run of
  2026-11-08, `hardware/notes/discharge-cm5.md`).
- With no model: `charge_full_design` of the device tree and the curve of
  `hardware/notes/discharge-cm5.md`. The end is 3.2 V under load (D21), a property of the node of the module in its overlay
  (`uconsole,end-microvolt = <3200000>`); the curve is cut there. With a
  model the end is its `V_end`, lower or higher than 3.2 V.
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
  module in dmesg. At `done` the module puts the new model into its
  attributes `model_*`, the ones the rule of D13 writes at boot, and sets
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
  ```
  ACTION=="add", SUBSYSTEM=="power_supply", KERNEL=="uconsole-soft-battery", \
    IMPORT{program}="/usr/bin/uconsole-soft-battery load", \
    ATTR{model_q_full}="$env{Q_FULL}", ATTR{model_table}="$env{TABLE}", ...
  ```
  `IMPORT{program}` takes the lines `KEY=value` the program prints;
  `ATTR{}` writes them into the attributes of the device. The module adds
  its attributes to the power supply by `attr_grp` of
  `power_supply_config`. It works for `=m` and `=y`: for a module built
  in the event comes before udev, and the coldplug of
  `systemd-udev-trigger` gives it again. Until the write the module goes
  by the default model (some seconds with `=y`). A new model makes the
  module work out the start level by the voltage again.
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
(`src/`, `Kbuild`, the overlay, the `include` file) into
`sys-power/uconsole-soft-battery/files/`, and the program (the crate, the
rules of udev) into `sys-power/uconsole-soft-battery-tools/files/`, and
writes the sum of `files/` into each ebuild. Its `--check` fails when the
sum is old. The version stays until a first release. A tarball of a tag
(`SRC_URI`) when this repository is published.

- The module: `sys-power/uconsole-soft-battery` (D26) in repo-uconsole;
  `linux-mod-r1`, depends on `virtual/dist-kernel`; the overlay
  `uconsole-soft-battery.dtbo` by `dtc -@` into `/boot/overlays`.
- `config.txt` is not changed by any package (D32). `pkg_postinst` of
  the package of the module says by `elog` the line to add by hand, in
  `[pi5]`, after `dtoverlay=clockworkpi-uconsole-cm5`:
  `dtoverlay=uconsole-soft-battery`; and to delete it before the package
  is removed.
- The overlay: `target-path` to the node `battery-power-supply`,
  `status = "disabled"`; a node `compatible = "uconsole,soft-battery"` at the
  root, so that udev loads the module by its alias.
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
   - the end of a charge by `axp-log`: the IRQ 0x49 bit 2 and 0x01 bit 6;
   - the writes of Rex (0x30, 0x32, 0x34, 0x36, 0x90): their meaning in
     the datasheet.
2. The scheme of the states and threads: a `.scheme` on a desk of
   showcase.
3. The packages: the script of the copy into `files/` (D28), the ebuilds, `linux-mod-r1` in the cross build of unidev
   (does it find `/usr/src/linux` of ROOT), the overlay.
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
  - a plain mean: each value; each point of the table (one per 5 % of
    the run's own `Q_full`) as a pair (V, I); `V_end`, `I_end` as a pair;
  - `reset` moves `runs/` to `runs.<seq>/`;
  - `load` by hand writes the attributes `model_*` at once, after a
    change of the options or of `runs/`.
- The values of the model and of C'. Worked out in `discussion.md`,
  "The values, from two logs". Proposed:
  - R: the median of dV/dI over steps of 1 s of 30 mA or more, kept as a
    histogram of 5 mOhm bins (40 × u16); fewer than 10 steps in a run: no
    R learned, the R of the model in use stays; the default R is a
    property of the node of the module, beside the end of D21:
    `uconsole,resistance-micro-ohms = <87000>`;
  - the table: 41 points, one each 2.5 %, in place of 21 (D22): 21
    points miss the curve by up to 1.7 %, 41 by 0.5 %;
  - C': the means of V and I over 30 s, as D23; the weight of the voltage
    from 50 % (0) to 20 % (full), in place of 30 % and 10 %; f in 0.5..2,
    as D23.
- Which register of the AXP228 marks the end of the charge.

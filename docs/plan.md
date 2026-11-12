# Plan

The decisions are in `decisions.md`; how they came, in `discussion.md`.

## The design as proposed

Not decided where it says "proposed".

### The module

- One power supply of type Battery, `uconsole-battery`: `status`,
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
  model in the same keys as `model.env` (below). With no learning the
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
an event, never in a loop. `dev_*` of the device `uconsole-battery`.

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
  at the speed of the count times f = (left by the voltage) / (left by
  the count), f in 0.5..2; it never rises on a discharge. The voltage
  weighs nothing above 30 % and in full at 10 %; V and I are means over
  30 s. On the charger the count only; the end of the charge sets 100 %.
- The error of the voltage, from the table of `discharge-cm5.md`: the
  voltage moves about 75 mV per 10 % at 60-40 %, about 130 mV per 10 %
  at 20-0 %. An error of 20 mΩ in R at a difference of 0.5 A from the
  learned current is 10 mV: about 1.5 % in the middle, under 1 % near the
  end.

### User space

- A rule of udev on the uevent `axp_silent`: `RUN+="/usr/bin/systemctl
  start --no-block uconsole-battery-save.service"`; udev kills a long
  `RUN`. A service started so does not get the keys of the event: the
  program reads the same values from sysfs. At `done` the module puts the
  new model into its attributes `model_*`, the ones the rule of D13
  writes at boot, and sets `learn_state` to `done`.
- The program takes two events (D19):
  - the AXP silent: a forced power off;
  - the AXP silent and a model: it writes the model into the state file
    `/var/lib/uconsole-battery/model.env` (the attributes `model_*`, as
    they are), syncs, then a forced power off.
- It runs only at that event: no daemon, no write to the disk during the
  learning (D16, D20).
- Proposed: the forced power off is `systemctl poweroff --force`: no stop
  of the services; systemd kills the processes and unmounts the file
  systems.
- The model reaches the module at boot by a rule of udev (D13), on the
  event `add` of the power supply:
  ```
  ACTION=="add", SUBSYSTEM=="power_supply", KERNEL=="uconsole-battery", \
    IMPORT{file}="/var/lib/uconsole-battery/model.env", \
    ATTR{model_q_full}="$env{Q_FULL}", ATTR{model_ocv}="$env{OCV}", ...
  ```
  `IMPORT{file}` reads lines `KEY=value`; `ATTR{}` writes them into the
  attributes of the device. The module adds its attributes to the power
  supply by `attr_grp` of `power_supply_config`. The program of the save
  writes `model.env`. It works for `=m` and `=y`: for a module built in
  the event comes before udev, and the coldplug of
  `systemd-udev-trigger` gives it again. Until the write the module goes
  by the default model (some seconds with `=y`). A new model makes the
  module work out the start level by the voltage again.
- The file is read at the event: `/var` is to be mounted then; one root
  partition gives that.

### The package

- Proposed: `sys-power/uconsole-battery` in repo-uconsole;
  `linux-mod-r1`, depends on `virtual/dist-kernel`; the overlay
  `uconsole-battery.dtbo` by `dtc -@` into `/boot/overlays`.
- The overlay: `target-path` to the node `battery-power-supply`,
  `status = "disabled"`; a node `compatible = "uconsole,battery"` at the
  root, so that udev loads the module by its alias.

## Steps

1. On the device:
   - the full path of `battery-power-supply`:
     `find /proc/device-tree -name 'battery-power-supply*'`;
   - the end of a charge by `axp-log`: the IRQ 0x49 bit 2 and 0x01 bit 6;
   - the writes of Rex (0x30, 0x32, 0x34, 0x36, 0x90): their meaning in
     the datasheet.
2. The scheme of the states and threads: a `.scheme` on a desk of
   showcase.
3. The package: the ebuild, `linux-mod-r1` in the cross build of unidev
   (does it find `/usr/src/linux` of ROOT), the overlay.
4. The module: the set up of the PMIC, the count, the power supply, the
   learning.
5. User space: the program that makes the model, the rules of udev, the
   service.
6. The kernel series: 0010 out, `bump.sh --sum`.
7. A test: a full charge, a discharge to the silence of the AXP, the save,
   the power off, the model at the next boot.

## Open questions

- The values of C' (D23): 30 s, f in 0.5..2, 30 %, 10 %; to check by
  the level at the silence, in dmesg.
- One module (count, learning, level) or two. Worked out in
  `discussion.md`, "One module or two". Proposed: one module, one
  platform driver, one work each 1 s; the code in files by part (set
  up, sample and count, learning, level, sysfs); the values of C' are
  parameters writable at run time, so no reload is needed to tune them.
- The program of user space: in the package of the module or in its own.
- The line `dtoverlay=uconsole-battery` of `config.txt`: by
  `uconsole-pi-firmware` (a USE flag), or by hand.
- How the ebuild in repo-uconsole takes the source of this repository
  (`package.self` of `uconsole/linux/AGENTS.md`).
- The capacity depends on the load (9067 mAh at 1.6 A): keep the last run,
  or a mean of some runs; and who makes the mean.
- The values of the model: the step of the current for `R` (200 mA), the
  21 points of the table.
- Which register of the AXP228 marks the end of the charge.

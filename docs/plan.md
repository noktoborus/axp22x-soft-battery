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
- States of the learning: `idle` → `learning` at the marker; `learning`
  counts; a new marker starts it again; `learning` → `done` at the
  silence of the AXP. At `done`: the values in sysfs, one uevent `change`.
  Nothing more (D11, D12).

### The model

- `Q_full`, `E_full`; the curve of the voltage against the charge out,
  one point every 50 mAh (about 180 points for 9 Ah), turned into percent
  at the end; `R`, the resistance of the cells, from dV/dI at steps of the
  current; `V_end`, `I_end` at the silence (3.10 V at 1.9 A in the run of
  2026-11-08, `hardware/notes/discharge-cm5.md`).
- With no model: `charge_full_design` of the device tree and the curve of
  `hardware/notes/discharge-cm5.md`.
- The kernel has `power_supply_batinfo_ocv2cap()` for a table of OCV
  against percent, the form of `ocv-capacity-table-0` of
  `simple-battery`.

### The level of the charge

- The base is the count: `charge_now / Q_full`. The marker of the full
  charge sets 100 %.
- At a boot in the middle of a discharge the start is by the voltage:
  OCV ≈ V + I × R, the mean over 30-60 s, then the table.
- Proposed: the saved charge of a clean power off is taken when it agrees
  with the voltage (within 5 %); else the voltage. The device may have
  charged while it was off.
- The voltage corrects the count slowly, more at a low charge.
- The error of the voltage, from the table of `discharge-cm5.md`: the OCV
  moves about 75 mV per 10 % at 60-40 %, about 130 mV per 10 % at
  20-0 %. An error of 20 mΩ in R at 1.6 A is about 32 mV: about 4 % in the
  middle, 2.5 % near the end.

### User space

- A rule of udev on the uevent: `RUN+="/usr/bin/systemctl start
  --no-block uconsole-battery-save.service"`; udev kills a long `RUN`.
- The service, oneshot: it saves the model into `/var/lib/uconsole-battery/`,
  then `OnSuccess=poweroff.target`.
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
- Proposed: the points of the learning reach the disk by a uevent each
  50 mAh and a rule of udev (D9). A clean power off writes a mark and the
  charge. At boot: no mark and the last point near the end (below 3.3 V)
  closes the learning by that point; a mark means the run was broken, and
  it does not count.
- Proposed: the learning is started by user space (`learn=1`); the same
  program turns off the action of upower at a low charge for the run.
  Else upower powers off before the AXP goes silent.

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
5. User space: the program, the rule of udev, the service, the model in
   modprobe.d.
6. The kernel series: 0010 out, `bump.sh --sum`.
7. A test: a full charge, a discharge to the silence of the AXP, the save,
   the power off, the model at the next boot.

## Open questions

- Is the learning started by hand (`learn=1`), or does it run on each
  cycle?
- 0 %: the end of the learned run (proposed), or the point where
  V − I × R reaches `V_end` at the current load.
- One module (count, learning, level) or two.
- The program of user space: in the package of the module or in its own.
- The line `dtoverlay=uconsole-battery` of `config.txt`: by
  `uconsole-pi-firmware` (a USE flag), or by hand.
- How the ebuild in repo-uconsole takes the source of this repository
  (`package.self` of `uconsole/linux/AGENTS.md`).
- The capacity depends on the load (9067 mAh at 1.6 A): keep the last run,
  or a mean of some runs.
- Which register of the AXP228 marks the end of the charge.

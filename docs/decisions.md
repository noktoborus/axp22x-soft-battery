# Decisions

Each entry: the number, the date, who decided, the decision. A proposal
not answered yet is in `plan.md`, "Open questions".

| # | date | by | decision |
|---|---|---|---|
| D1 | 2026-11-12 | user | A kernel module gives the battery of the system by itself. The scheme of now goes: `axp20x_battery` and the gauge of the AXP. |
| D2 | 2026-11-12 | user | The learning starts at the full charge, by a marker of the AXP. It ends when the AXP goes off: silent on I2C. |
| D3 | 2026-11-12 | user | At the end of the learning the module sends a uevent. udev starts a program of user space: it saves what was learned and runs `poweroff`. |
| D4 | 2026-11-12 | user | The patch 0010 of the kernel series (the watchdog of the silence of the AXP in `axp20x_battery`) goes. The module takes the silence of the AXP as the end of the learning. |
| D5 | 2026-11-12 | user | The node `battery-power-supply` of the AXP is turned off in the device tree (`status = "disabled"`). `mfd-core.c` then makes no device for it, and `axp20x_battery` does not probe. The module sets the PMIC up in its place: the current of the charge, `V_OFF`, the writes of Rex. |
| D6 | 2026-11-12 | user | First step: the kernel module in a package (ebuild) of its own. |
| D7 | 2026-11-12 | user | A module shows the right level of the charge, by the learned model. |
| D8 | 2026-11-12 | user | The AXP is ignored as a store: it forgets what was written into its registers. No state of the module is kept in the AXP (not the buffer 0x04-0x0F, not E0:E1, not E2:E3). |
| D9 | 2026-11-12 | user | Replaced by D17. Was: a learning cut by `V_OFF` is closed by its last point saved on the disk. |
| D10 | 2026-11-12 | user | Assumed: `poweroff` on the CM5 cuts the power of the whole device. |
| D11 | 2026-11-12 | user | The module never powers the system off; only user space decides. |
| D12 | 2026-11-12 | user | No more logic and no workarounds in the kernel module. A guard against a failure of user space, when it is needed, is a separate module. |
| D13 | 2026-11-12 | user | The model reaches the module at boot by a rule of udev on the event `add` of its power supply: `IMPORT{file}` of `/var/lib/uconsole-battery/model.env`, `ATTR{}` into the attributes of the module. Not modprobe.d: it gives no options to a module built in. |
| D14 | 2026-11-12 | user | The learning starts each time a charge ends in full; no start by hand. |
| D15 | 2026-11-12 | user | The module tells its events in dmesg: it is loaded, the model is awaited, the learning started, the learning done, and others (the list in `plan.md`). |
| D16 | 2026-11-12 | user | While the learning runs, it is kept in the memory of the module only; the memory is to be small. At its end a uevent makes user space write the state file. |
| D17 | 2026-11-12 | user | A learning broken by a cut by `V_OFF` or a reboot is a failed learning and is dropped. The next full charge starts a new one. Replaces D9. |
| D18 | 2026-11-12 | user | upower stays. The module knows nothing of upower, nor when and by whom the system is powered off. When the module finds the AXP silent, a learning that runs ends; else the module does nothing more. |
| D19 | 2026-11-12 | user | At the silence of the AXP the module always sends a uevent. The program takes two events: the AXP silent (it forces a power off); the AXP silent with a model (it writes the state file, then forces a power off). |
| D20 | 2026-11-12 | user | The module makes the model of the curve, not user space: the program is not to run all the time or start often to write to the disk. |
| D21 | 2026-11-12 | user | With no model the end (0 %) is 3.2 V under load: the property `uconsole,end-microvolt = <3200000>` of the node of the module in its overlay; the default curve (the run of 2026-11-08) is cut there. With a model the end is its `V_end`, the last sample before the silence of the AXP, lower or higher than 3.2 V. |
| D22 | 2026-11-12 | user | The model keeps a table under load, not a table of OCV: 21 points, one each 5 % of the charge out, each the mean V and the mean I as measured in the learning. A voltage of now is put on it by V_t(q) − (I − I_t(q)) × R. |
| D23 | 2026-11-12 | user | The level is C': the count, its speed scaled by the voltage. On a discharge the level falls at the speed of the count times f = (left by the voltage) / (left by the count), f in 0.5..2, and never rises; the voltage weighs nothing above 30 % and in full at 10 %; the means of V and I over 30 s. On the charger the count only; the end of the charge sets 100 %. The values are a first guess. |

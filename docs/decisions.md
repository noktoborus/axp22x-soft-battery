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
| D9 | 2026-11-12 | user | When the AXP cuts the power by `V_OFF` in place of going silent, the learning is closed by its last saved point. The point is saved on the disk (D8). |
| D10 | 2026-11-12 | user | Assumed: `poweroff` on the CM5 cuts the power of the whole device. |
| D11 | 2026-11-12 | user | The module never powers the system off; only user space decides. |
| D12 | 2026-11-12 | user | No more logic and no workarounds in the kernel module. A guard against a failure of user space, when it is needed, is a separate module. |
| D13 | 2026-11-12 | user | The model reaches the module at boot by a rule of udev on the event `add` of its power supply: `IMPORT{file}` of `/var/lib/uconsole-battery/model.env`, `ATTR{}` into the attributes of the module. Not modprobe.d: it gives no options to a module built in. |

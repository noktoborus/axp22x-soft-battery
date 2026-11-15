# axp22x-soft-battery

The battery of the uConsole with a CM5, counted in software ("soft"),
not by the gauge of the PMIC: a kernel module that shows the level of
the charge by a model of the device tree, and a daemon of user space that
learns the cells from a full charge down to the point where the PMIC
AXP228 goes silent, writes the curves as an overlay, and powers the system
off.

State: the packages and skeletons of the code are made; the module and
the program do no work yet.

## Files

| file | what |
|---|---|
| `AGENTS.md` | the rules of the project |
| `docs/decisions.md` | what is decided, numbered |
| `docs/plan.md` | the design as proposed, the steps, the open questions |
| `docs/discussion.md` | the log of the discussion, by date |
| `module/` | the module, its `Makefile`, the overlay `axp22x-soft-battery.dts`, the file of modprobe.d |
| `daemon/` | the program `axp22x-soft-battery` (Rust), its unit and preset |

## Tools

| file | what |
|---|---|
| `tools/log-r-noise-slope.py` | from a log of `hardware/tools/axp-log`: R by steps of the current, the noise of the voltage by the window of the mean, the slope of the curve, the end |
| `tools/log-r-table.py` | from such a log: R by the size of the step, the error of a table of 11, 21, 41 points |
| `tools/log-table-zones.py` | from such logs: the error of a table of 21 and 41 points by the part of the discharge |
| `tools/sim-level.py` | plays the level C' over a log with a model made of another log: the level at the silence and the largest gap, by the weights of the voltage; what the voltage alone says against the count |
| `tools/sim-end.py` | plays the level with an end by a voltage under load (`V_thr`), a model of one log over another log |
| `tools/sim-end-mean.py` | the same with the mean model of two logs over the third; also the jump to 0 % |
| `tools/sim-start.py` | the start level by the voltage alone (a boot in the middle of a discharge), with R and with no R, the mean model of two logs over the third |
| `tools/make-curve.py` | from logs to the silence: the default curve OCV by the percent left, its spread, the error of the level by the voltage alone; the table for the module and for the device tree |
| `tools/plot-curves.py` | the curves of the logs and their mean as an SVG (`convert -density 144` makes the PNG) |
| `tools/check-regs.sh` | on the device: the values of the device tree against the registers of the AXP (`i2cget`) and the properties of the module; V_OFF, the target and the current of the charge, each reg-write |

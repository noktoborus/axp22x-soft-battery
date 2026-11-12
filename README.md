# uconsole-soft-battery

The battery of the uConsole with a CM5, counted in software ("soft"),
not by the gauge of the PMIC: a kernel module that learns the
cells from a full charge down to the point where the PMIC AXP228 goes
silent, and shows the level of the charge by what it learned. User space
saves the model and powers the system off.

State: the design is in work; no code yet.

## Files

| file | what |
|---|---|
| `AGENTS.md` | the rules of the project |
| `docs/decisions.md` | what is decided, numbered |
| `docs/plan.md` | the design as proposed, the steps, the open questions |
| `docs/discussion.md` | the log of the discussion, by date |

## Related

- `the research of the kernel`: the kernel series (the patch 0010 to
  remove), repo-uconsole, `notes/custom-battery.md`.
- `uconsole/hardware`: `notes/battery-gauge.md`,
  `notes/discharge-cm5.md`, `tools/axp-log`.

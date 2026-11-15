# AGENTS

LANG   this file uses the notation below. keep writing it in this notation.
FORM   RULE <topic> = <value>        hard rule
       RULE <topic> : <predicate>    hard rule, predicate form
       WHY  <reason>                 why the rule exists
       THEN <action>                 what to do when the rule triggers
       EX+  <good example>
       EX-  <bad example>
       REF  <path>                   where to look
       EXC  <exception>              where a rule is broken, and on whose word
READ   RULE is binding. WHY is context, not negotiable. EX is literal.

## Project

RULE product = the battery of the uConsole: a kernel module that shows
     the charge by a model of the device tree, and a daemon of user space
     that learns the cells, keeps the runs, makes the model as a `.dts`,
     and powers the system off at the end of a discharge
RULE device = uConsole with a CM5, the PMIC AXP228
RULE packages = the two packages of the overlay `uconsole`:
     `sys-power/axp22x-soft-battery`, `sys-power/axp22x-soft-battery-daemon`
RULE state : the module and the program are written, not yet tried on
     the device; the design of variant B is in `docs/plan.md`
REF  docs/plan.md

## Layout

RULE layout.tools = tools/, scripts that read logs of the device for the
     design; each listed in README.md, "Tools"
RULE layout.docs = docs/, the documents (below)
RULE layout.module = module/, the source of the package
     `sys-power/axp22x-soft-battery`: the module (`sb.h`, `*.c`), its
     Makefile, the overlay
     `axp22x-soft-battery.dts`, the file of modprobe.d
RULE layout.daemon = daemon/, the source of the package
     `sys-power/axp22x-soft-battery-daemon`: the crate, the unit, the preset
RULE package.copy : the ebuilds take module/ and daemon/ (the files in
     git) as a copy in files/ of the two packages, with its sum; the copy
     is made outside this repository (D131)
THEN after a change of module/ or daemon/, commit; the copy follows
THEN a new crate in Cargo.lock goes into CRATES and the Manifest of the
     ebuild of the daemon

## Documents

RULE docs.decisions = docs/decisions.md, what is decided: one entry per
     decision, a number, the date, who decided
RULE docs.plan = docs/plan.md, the steps of the work and the open
     questions
RULE docs.discussion = docs/discussion.md, the log of the discussion by
     date: the question, the options, the answer
RULE docs.update : an answer of the user goes into discussion.md, and
     into decisions.md when it decides a thing, in the same commit
THEN a changed decision is changed in decisions.md; discussion.md keeps
     how it changed
RULE docs.lang = simplified english: short sentences, common words, one
     idea per sentence

## Kernel module

RULE module.minimal : the module does its job and nothing more: no
     fallback, no retry, no workaround
EXC  the guard of the silence: at 12 failed samples in a row the module
     calls `hw_protection_trigger()` (D71, the word of the user)
RULE module.impact : the module weighs as little as it can on the running
     system: as few locks as it can, the interfaces of the drivers there
     are before a read of its own
EX-  a read of 0x00 each second for the charger, when `axp22x-ac` tells it
     by its IRQ
RULE module.small : the module is small and easy to read
RULE module.files : one file per part of the module, not blocks in one
     file; `sb.h` lists the parts
EX+  dt.c the device tree, pmic.c the registers, level.c the level,
     work.c the samples, props.c the power supply, main.c the driver
RULE module.comments : a comment stands over a function, a struct or a
     file, never in the body of a function; a comment is a short text in
     simplified english, never a number of docs/decisions.md
THEN a part of a body that needs a comment becomes a function with a
     name that says it
EX+  /* No AXP answers on the bus: all three reads end in -ENXIO. */
     static bool sb_axp_silent(const int err[3])
EX-  /* the silence (D41) */ inside sb_work()
RULE module.overlay : each property of `axp22x-soft-battery.dts` has a
     comment in simplified english: what it is, its unit, what values the
     module takes; never a number of docs/decisions.md
RULE module.poweroff : the module does not power the system off; user
     space decides it
EXC  the guard of D71: `hw_protection_trigger()` at 12 failed samples
RULE module.axp : the AXP is not a store and its gauge is not used
WHY  the AXP forgets what was written into its registers
THEN from the AXP the module takes only what it measures and tells now:
     the ADC, the status, the end of the charge; and it sets the PMIC up at
     each probe, by the device tree only
RULE module.values : the module has no default value; each setting, the
     model and each write of a register come from the device tree
THEN a value missing or bad: a line of `err`, the probe fails
THEN the defaults are in the overlay of the package,
     `axp22x-soft-battery.dts`, set there in full

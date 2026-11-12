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

RULE product = the battery of the uConsole: a kernel module that learns
     the cells and shows the charge, and the part of user space that saves
     what it learned and powers the system off
RULE device = uConsole with a CM5, the PMIC AXP228
RULE linux = the research of the kernel: the series of the kernel, the
     packages of repo-uconsole
RULE hardware = uconsole/hardware: the notes on the PMIC, the
     gauge, the discharge, the tool axp-log
THEN a fact from there is cited by its path, not copied here
RULE state : no code yet; the design is closed (`docs/plan.md`); the steps of
     the work are next
REF  docs/plan.md

## Layout

RULE layout.tools = tools/, scripts that read logs of the device for the
     design; each listed in README.md, "Tools"
RULE layout.docs = docs/, the documents (below)

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
RULE docs.lang = simplified english (the rules of the workspace, `text.lang`)

## Kernel module

RULE module.minimal : the module does its job and nothing more: no
     fallback, no retry, no workaround
THEN logic that guards against a failure of user space is a separate
     module, made only when it is needed
EX-  a call of `hw_protection_trigger()` when user space does not power
     off in time
EX-  a uevent sent again every 10 s while the AXP is silent
RULE module.impact : the module weighs as little as it can on the running
     system: as few locks as it can, the interfaces of the drivers there
     are before a read of its own
EX-  a read of 0x00 each second for the charger, when `axp20x-ac` tells it
     by its IRQ
RULE module.small : the module is small and easy to read
RULE module.poweroff : the module never powers the system off; only user
     space decides it
RULE module.axp : the AXP is not a store and its gauge is not used
WHY  the AXP forgets what was written into its registers
THEN from the AXP the module takes only what it measures and tells now:
     the ADC, the status, the end of the charge; and it sets the PMIC up at
     each probe

---
name: plan
description: "Use when the user asks for a plan, roadmap, phased implementation plan, or continuation of an existing plan."
---

# plan

Use this skill only for planning requests. Planning mode must not modify
source code, tests, build files, configuration, or generated artifacts.
Read files and inspect the repository as needed, but keep exploration focused.
Do not begin implementation until the user explicitly asks to execute the plan.

## Create the baseline

1. Choose an ASCII task slug and create `docs/plans/<task-slug>.md`.
2. Record the goal, scope, non-goals, constraints, current evidence, risks,
   assumptions, and unresolved blockers.
3. Divide the work into ordered phases. Each phase must include:
   - objective and expected outcome;
   - files, symbols, or boundaries likely to change;
   - concrete steps;
   - validation checks and exit criteria;
   - dependencies and possible blockers.
4. Mark the document as `Baseline plan` and give it an initial status.
   Do not silently replace the baseline after the user approves or continues it.

## Continue an existing plan

When the user asks to continue, first locate the original plan document and
compare the new request with its goal, scope, phases, dependencies, and exit
criteria. State whether the request is aligned, an amendment, or a conflict.
Keep the original plan intact; append an `Amendments` or `Phase update` section
with the reason, impact, and affected phases. Ask for clarification when the
new request changes the goal or creates an unresolved conflict.

## Complete a phase

At the end of every completed phase, update the plan and response with:

- what was done;
- what remains;
- blockers, failed checks, or unavailable prerequisites;
- validation performed and its result;
- the next phase or decision required.

Do not claim a phase is complete when its exit criteria or required checks are
unmet. Keep plan status and implementation status separate: a documented plan
is not evidence that code changes were made.

# Scope and opportunistic fixes

Fix clearly discovered issues even when they are unrelated to the user's
original change when doing so is safe, useful, and within the repository's
ownership boundaries. Do not leave a known, low-risk defect untouched merely
because it is outside the initial request.

Before expanding scope:

- confirm the issue is real and not caused only by local or generated state;
- prefer the smallest root-cause fix over cleanup or refactoring;
- preserve unrelated user changes and do not modify vendored dependencies;
- keep the fix logically separate from the requested change when practical;
- add or update focused coverage when behavior changes; and
- run validation for the additional fix, or record why validation is blocked.

Do not use this rule to justify speculative redesigns, broad formatting,
unrelated modernization, or changes outside the repository's ownership. Ask
for clarification when the fix is high-risk, changes public API or behavior
substantially, expands into multiple subsystems, or conflicts with the user's
stated scope.

Report opportunistic fixes separately from the requested work, including the
reason they were included, files affected, validation performed, and any
remaining blocker or risk.

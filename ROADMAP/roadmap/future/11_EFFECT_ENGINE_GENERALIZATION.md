# FUTURE 11 - Generalized Effect Engine

Status: BACKLOG

Implement only after the MVP stop gate is explicitly lifted.

Refactor the two proven effects into a shared lifecycle contract without regressing them.

Possible conceptual interface:

```text
Effect
|- update(delta_time)
|- render(context)
|- is_alive()
`- reset()
```

The exact C++ design may differ.

Do not force inheritance if composition or data-oriented structures are cleaner.

The goal is extensibility, not an object-oriented ritual.

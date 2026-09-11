# FUTURE 18 - Application Rules and Exclusions

Status: BACKLOG

Implement only after the MVP stop gate is explicitly lifted.

Potential rules:

```text
Desktop        -> Default
After Effects  -> Minimal
Cinema 4D      -> Bright
OBS            -> Recording
Games          -> Disabled
```

Potential exclusions:

- selected processes;
- fullscreen applications;
- hidden cursor;
- secure desktop;
- optional typing suppression.

Foreground-process detection must be lightweight.

Never inject into target applications.

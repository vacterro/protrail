# FUTURE 10 - Profiles

Status: BACKLOG

Implement only after the MVP stop gate is explicitly lifted.

Add named effect profiles such as:

- Default
- Minimal
- Presentation
- Recording
- Custom

A profile may contain Trail, Click, Render, and later visual-effect settings.

Requirements:

- instant switching;
- persistence;
- duplicate/rename/delete;
- safe fallback if a selected profile is missing;
- tray switching later.

Do not make profile loading mutate global state in partially applied steps.

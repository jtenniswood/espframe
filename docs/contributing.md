---
title: Contributing to Espframe
description: How to propose and test documentation, firmware, and web interface changes for Espframe.
---

# Contributing to Espframe

Thanks for helping improve Espframe. Small, focused changes are easiest to review and validate.

## Before opening a pull request

- Check the [open issues](https://github.com/jtenniswood/espframe/issues) for existing discussions.
- For a larger feature or a change that affects device behavior, open an issue first to agree on the approach.
- Update the documentation when a change affects how people install or use Espframe.
- Keep existing device support, saved settings, and Home Assistant entity names compatible unless a breaking change has been discussed first.

## Prepare and validate your changes

Create a branch from `main`, make the change, and review the complete diff before committing. For repository changes, install the locked dependencies with `npm ci`, then run:

```sh
npm run check:pr
```

For ESPHome YAML changes or C++ changes not covered by host-side checks, compile the affected firmware profile too. Say which checks you ran and whether device testing is still needed in the pull request.

## Open a pull request

Use a clear title and describe the user-visible result. Include screenshots for visible web interface changes. For device behavior changes, list the affected panel revision and the physical checks reviewers should perform.

See [USB log collection](/collect-usb-logs) if you need to attach startup diagnostics to an issue or pull request.

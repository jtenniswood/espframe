# Tech Debt Cleanup Backlog

Review date: 2026-07-07
Implementation status: completed in branch `tech-debt-all`.

## Completed Items

### 1. Backup import can report success even when device saves fail

Status: Done.

What changed:

- Device save helpers now throw on failed POST responses instead of only logging to the console.
- Failed import saves are counted separately from skipped validation fields.
- Backup import now shows a failed-setting summary when a save request fails.
- A browser smoke scenario was added for a failed import save.

### 2. Generic backup import does not validate most restored values before saving

Status: Done.

What changed:

- Runtime backup import validation now covers product-backed select, number, switch, text, and date settings.
- Immich URL, API key, firmware manifest URL, photo ID/label fields, NTP servers, timezone, schedule wake timeout, and rotation now validate before save.
- Firmware text max lengths are exposed through generated web metadata so the web UI and device limits stay aligned.

### 3. Immich fetch flow has repeated slot update and error handling logic

Status: Done.

What changed:

- Repeated fetch-success retry/cooldown resets were extracted into `immich_register_fetch_success`.
- Repeated slot image URL assignment and deferred slot update dispatch were extracted into shared C++ helpers.
- The fetch decision flow and user-visible slideshow behavior were left unchanged.

### 4. Workflow contract validation is too large and string-fragile

Status: Done.

What changed:

- Pull request template and device-testing checks were split into `scripts/product_contract/workflow_pr_template.py`.
- `scripts/product_contract/workflows.py` now delegates that focused concern to the new module.

### 5. Web bundle generation relies on broad text replacement

Status: Done.

What changed:

- Web bundle generation now requires each placeholder to appear exactly once.
- Generation now fails if any `__ESPFRAME_...__` placeholder is left unreplaced.

### 6. Editable web CSS is minified, making UI changes harder than necessary

Status: Done.

What changed:

- `docs/webserver/src/style.css` was expanded into readable source CSS.
- Generated public CSS and generated web app assets were refreshed from that source.

### 7. Product metadata owns card order, but card content is still mostly handwritten

Status: Done as a conservative first cleanup.

What changed:

- Text input max-length behavior now reads product metadata where available.
- Firmware manifest URL and date fields now use metadata-backed limits instead of only local constants.
- Larger card generation was deliberately avoided to keep the UI unchanged.

### 8. Compatibility aliasing for metadata date format is hidden in runtime state code

Status: Done.

What changed:

- Legacy date-taken format normalization moved into `docs/webserver/src/compat.js`.
- Compatibility tests now directly cover old and current date-format values.

### 9. Public docs sidebar includes internal phase documents

Status: Done.

What changed:

- Phase documents moved out of the main Project sidebar list.
- They now live in a collapsed Engineering Notes section.

### 10. Static HTML snippets are mixed with DOM construction

Status: Done for low-risk cases.

What changed:

- Plain text button labels now use `textContent`.
- Empty-container clears now use `replaceChildren()`.
- Static SVG snippets were left as HTML strings because they are static icon markup.

### 11. Historical product metadata status is still a required project field

Status: Done.

What changed:

- `phase_1_status_note` was removed from product metadata.
- The phase-specific contract requirement was removed from project metadata validation.

## Verification

Run before completion:

- `npm run check:generated`
- `npm run check:product`
- `npm run check:backup`
- `npm run check:compat`
- `npm run check:firmware-fields`
- `npm run test:web-modules`
- `npm run test:web-compat`
- `npm run test:helpers`
- `npm run test:workflow-contract`

Browser smoke tests were attempted, but Chrome exited without output in this environment. The static checks and targeted module tests cover the changed source paths.

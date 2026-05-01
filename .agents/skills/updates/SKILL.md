---
name: updates
description: >-
  Summarize user-facing changes made to this project over the last 7 days. Use
  when the user says "/updates", asks for recent updates, weekly updates,
  release-style highlights, or wants a concise feature summary ordered by
  importance.
---

# Recent Updates

Summarize the project's user-facing changes from the last 7 days in plain
language. Focus on what a user can see, configure, install, fix, or benefit
from, not internal implementation details.

## Workflow

1. Check the current branch and update remote refs without changing branches:

   ```bash
   git status --short --branch
   git fetch origin --prune
   ```

2. Review commits from the last 7 days:

   ```bash
   git log --since="7 days ago" --date=short --pretty=format:"%h %ad %s%n%b"
   ```

3. Inspect changed files when commit subjects are not enough:

   ```bash
   git log --since="7 days ago" --name-status --oneline
   git diff --stat "HEAD@{7 days ago}" HEAD
   ```

   If reflog history is unavailable, use the commit list from step 2 and inspect
   individual commits with `git show --stat <sha>` or `git show <sha>`.

4. Group related commits into user-facing items. Prefer these categories:

   - New features or visible behavior changes
   - Device, firmware, installation, or configuration improvements
   - Bug fixes users would notice
   - Documentation updates only when they help users install, configure, or use
     the project

5. Order items by importance:

   - Direct user impact and breadth of audience first
   - Larger features before small fixes
   - Fixes for broken workflows before polish
   - Recent changes only break ties

## Output

Use one sentence per item in this exact style:

```text
Feature: description of feature.
```

Keep the wording approachable for a technical but non-development-oriented
reader. Avoid commit hashes, file names, PR numbers, and implementation jargon
unless the user asks for them.

If there are no clear user-facing changes, say that directly in one sentence
and briefly mention that the recent work appears to be internal maintenance.

# Repository Governance

Changes to `main` must go through pull requests. The protected branch rule for
`main` requires a pull request before merging, applies to administrators, blocks
force pushes and branch deletion, and requires review conversations to be
resolved before merge.

The protected branch rule should also require the `Validate PR Gate` status
check from the `PR Validation` workflow before a pull request can merge. Full
firmware builds stay available as the manual `Compile Firmware` job in the same
workflow when a branch needs device-test artifacts.

Pull requests are merged with squash merge only. The squash commit message uses
the pull request title and description, so the pull request description should
explain what changes when the pull request is merged.

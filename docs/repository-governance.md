# Repository Governance

Changes to `main` must go through pull requests. The protected branch rule for
`main` requires a pull request before merging, applies to administrators, blocks
force pushes and branch deletion, and requires review conversations to be
resolved before merge.

The protected branch rule should also require the `Validate PR Gate` and
`Compile Firmware` status checks from the `PR Validation` workflow before a pull
request can merge. Those checks run for every pull request, so documentation-only
and maintenance changes still get a clear merge gate instead of silently skipping
CI.

Pull requests are merged with squash merge only. The squash commit message uses
the pull request title and description, so the pull request description should
explain what changes when the pull request is merged.

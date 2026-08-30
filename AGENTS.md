# Contribution workflow

When a task changes tracked project files, deliver the work through a GitHub
pull request unless the user explicitly asks for a different workflow.

- Never commit directly to `main`.
- Create or use a focused non-default branch for the task.
- Keep unrelated local changes out of the branch, commits, and pull request.
- Run the relevant validation, then commit and push the completed changes.
- Open a pull request against the repository's default branch and include its
  URL in the final response.
- Leave the pull request open for review; do not merge it unless the user
  explicitly asks.
- If authentication, permissions, missing remotes, or failing required checks
  prevent creating the pull request, report the blocker clearly and preserve
  the prepared local work.

Read-only tasks, reviews, investigations, and tasks that do not change tracked
files do not require an empty pull request.

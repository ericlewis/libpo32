Use Conventional Commits for any commit messages, PR titles, squash-merge
titles, or one-line change summaries you suggest in this repository.

Use:
`<type>[optional scope]: <summary>`

Rules:
- Use imperative mood: `Add feature`, not `Added feature`.
- Use a scope when it adds clarity.
- Keep the subject concise and specific.
- Keep it under 50 characters when practical.
- Use one of these types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `chore`, `ci`, `revert`.
- A body is optional and may be used for additional context.
- A footer is optional and may be used for issue references or breaking changes.
- Use `BREAKING CHANGE:` in the footer, or `!` before the colon, for breaking changes.
- Use `*` bullets in the body when listing multiple points.
- When a title field expects a single line, output only the Conventional Commit subject line.

Hard requirements for title generation:
- The first token of the title must be a valid Conventional Commit type.
- Do not start a title with plain-English verbs like `Add`, `Update`, `Fix`, `Remove`, `Improve`, or `Refactor` unless they appear after a valid `type:` or `type(scope):` prefix.
- If you are generating a commit or PR title from a diff, choose the type first, then write the summary.

Useful heuristics:
- changes under `.github/`, `scripts/`, or CI/workflow setup usually map to `ci`
- changes limited to docs usually map to `docs`
- changes limited to tests or test harnesses usually map to `test`
- bug fixes in build or automation behavior usually map to `fix(ci)` or `fix(build)`

Examples:
- `feat(pattern): add starter groove presets`
- `fix(protocol): correct lane-chunked pattern encoding`
- `docs(readme): clarify PO-32 transfer workflow`
- `docs: update README usage notes`
- `fix(ci): invoke commit checker with sh`
- `test(cmake): add smoke tests for examples`

Do not suggest looser or plain-English titles like:
- `Add CI workflows, git hooks, and formatting scripts`
- `Run commit checker with sh; use LLVM action`
- `Add CMake smoke tests and enable mtdrum test`

Prefer:
- `fix(ci): ...`
- `docs(readme): ...`
- `ci(actions): ...`
- `test(cmake): ...`

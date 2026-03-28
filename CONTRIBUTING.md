# Contributing

This repo keeps the contribution path intentionally small:

- format C sources with `clang-format`
- keep the core checks green
- use Conventional Commits so release notes and SemVer changes stay easy to read

## Local Setup

From the repo root:

```sh
./scripts/install-git-hooks.sh
```

That configures:

- `core.hooksPath` to use the repo-managed hooks in `.githooks/`
- `commit.template` to use `.gitmessage`

After that:

- the `pre-commit` hook formats staged `.c` and `.h` files with `clang-format`
- the `commit-msg` hook validates commit subjects against Conventional Commits
- the `pre-push` hook runs `./scripts/ci-static-analysis.sh`

## Build And Test

Run the same core verification path used in CI:

```sh
./scripts/check-version-sync.sh
./scripts/ci-verify.sh core
```

Additional local checks:

```sh
./scripts/ci-format.sh
./scripts/ci-static-analysis.sh
./scripts/ci-coverage.sh
```

`ci-coverage.sh` requires `gcovr` on `PATH`.

## Commit Messages

Commit subjects and PR titles should follow Conventional Commits.

Use this format:

```text
<type>[optional scope]: <summary>
```

Rules:

- Use Conventional Commits: `type(scope): description` or `type: description`.
- Use imperative mood: `Add feature`, not `Added feature`.
- Use a scope when it adds clarity, for example `fix(ci): ...` or `docs(readme): ...`.
- Keep the subject line concise and specific. Keep it under 50 characters when practical.
- Use one of these types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `chore`, `ci`, `revert`.
- A body is optional and may be used for additional context.
- A footer is optional and may be used for issue references or breaking changes.
- Use `BREAKING CHANGE:` in the footer, or `!` before the colon, for breaking changes.
- Use `*` bullets in the body when listing multiple points.
- When a title field expects a single line, output only the Conventional Commit subject line.

Examples:

```text
feat(pattern): add starter groove presets
fix(protocol): correct lane-chunked pattern encoding
docs(readme): clarify PO-32 transfer workflow
docs: update README usage notes
fix(ci): invoke commit checker with sh
```

Allowed types:

| Type | Meaning |
| --- | --- |
| `feat` | User-facing feature or API addition |
| `fix` | Bug fix |
| `docs` | Documentation-only change |
| `style` | Formatting-only change |
| `refactor` | Internal restructuring without behavior change |
| `perf` | Performance improvement |
| `test` | Test-only change |
| `build` | Build system or dependency change |
| `ci` | CI or automation change |
| `chore` | Repo maintenance that does not fit another type |
| `revert` | Revert commit |

SemVer guidance:

| Commit shape | Release impact |
| --- | --- |
| `feat(scope): ...` or `feat: ...` | Minor release |
| `fix(scope): ...`, `fix: ...`, `perf(scope): ...`, or `perf: ...` | Patch release |
| `feat(scope)!: ...`, `feat!: ...`, `fix(scope)!: ...`, etc. | Major release |

If a change is breaking, use `!` in the subject and explain the break clearly in
the body.

Prefer a scope when it adds clarity, for example:

```text
fix(ci): invoke commit checker with sh
```

But an unscoped subject is still valid when the scope does not add much:

```text
docs: update README usage notes
```

## Pull Requests

Keep pull requests narrowly scoped.

Before opening one, make sure:

- the worktree is formatted
- the relevant verification scripts pass
- docs are updated when public behavior or API changes
- the commit history uses Conventional Commits
- the PR title also follows Conventional Commits

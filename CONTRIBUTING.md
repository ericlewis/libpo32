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

Commit subjects must follow:

```text
<type>(<scope>)?: <summary>
```

Examples:

```text
feat(pattern): add starter groove presets
fix(protocol): correct lane-chunked pattern encoding
docs(readme): clarify PO-32 transfer workflow
chore(ci): add static-analysis workflow
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
| `feat: ...` | Minor release |
| `fix: ...` or `perf: ...` | Patch release |
| `feat!: ...`, `fix!: ...`, etc. | Major release |

If a change is breaking, use `!` in the subject and explain the break clearly in
the body.

## Pull Requests

Keep pull requests narrowly scoped.

Before opening one, make sure:

- the worktree is formatted
- the relevant verification scripts pass
- docs are updated when public behavior or API changes
- the commit history uses Conventional Commits

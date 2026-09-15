# Building and releasing AyuGram

The application keeps AyuGram's name, icons, feature defaults and platform
identifiers. Repository ownership is not application branding. A development
fork must identify where its binaries come from; it must not claim to be an
official upstream release.

## Build checks

`Source checks` runs on pushes and pull requests without production credentials.
It runs the unit tests in `Telegram/build`, checks preparation syntax and lints
both the workflows and the composite actions, but does not compile the C++
application. `actionlint` globs `.github/workflows` only and resolves
`uses: ./...` from the repository root, so the check first stages a tree
holding every checkout layout the workflows name, and fails when one of those
paths has no action behind it. An unresolvable local `uses:` is not an
`actionlint` error but silence, and a mistyped `with:` key is then not an error
either but an empty string three hours into a build. `.github/lint_actions.py`
covers what `actionlint` still does not read: the shell inside the composite
actions and the step keys the runner requires and accepts there.

`Release` runs only by an explicit manual dispatch. It validates the version
before starting the expensive jobs, then builds universal macOS and Windows x64
packages plus the full recursive source archive. Each macOS architecture builds
in its own job because one cold build of both exceeds the six-hour job limit.
Dependencies and compiler results use separate caches. The workflow creates the
tag and public GitHub Release only after every build and package check succeeds.

The default `all` component is the only mode that publishes a release. Select a
single component only to diagnose its release build without repeating the other
expensive jobs.

Both platform jobs report into the run summary: time per CMake target and the
slowest translation units from `.ninja_log`, compiler cache statistics, and on
macOS the swap used during compilation. Read that report, not a stopwatch, when
a build gets slower. A compiler cache hit rate near zero explains an otherwise
inexplicable three-hour job, and swap above a few hundred megabytes means three
parallel compilers do not fit in the runner's memory.

## Releasing a new Telegram version

The order is merge, push, warm, release. Skipping the warm step is the mistake
that costs hours: `Release` only reads caches, so a branch nobody has warmed
compiles from nothing, and every retry after a late packaging failure pays the
same price again.

1. Branch from the default branch and merge the upstream tag:
   `git switch -c codex/ayu-<version> origin/dev --no-track`. Resolve the
   conflicts in the AyuGram-patched sources. If upstream moved the `lib_ui` or
   `lib_tl` pins, merge and push those repositories first.
2. Set `VERSION` in `.github/workflows/release.yml`. It is the only place the
   version is written; the branch name check derives from it.
3. Push the branch and let `Source checks` go green before anything expensive
   starts.
4. Warm it:
   `gh workflow run "Cache warm" --ref dev -f ref=codex/ayu-<version>`.
   `--ref dev` is the default branch, where the workflow has to run for its
   caches to be readable from anywhere; `-f ref=` names the branch to build.
   Without `-f` the newest release branch is picked, which is the new one
   anyway.
5. When the warm run is green, dispatch `Release` on the new branch.

How long step 4 takes depends on what upstream shipped, and the gap is wide
enough to plan around. The dependency cache key hashes
`Telegram/build/prepare/**` and `Telegram/build/qt_version.py`. Neither changed
across the six version bumps from 7.1.4 to 7.2.5, so all of them reused a
dependency tree that costs two hours per platform to build, and the last four
touched exactly one source file, the one carrying the version number. 7.2.6
changed both files, because it replaced `rlottie` with `tlottie` and
preparation started building a Rust library; the 7.2.5 to 7.2.7 range also
rewrote 154 of the 2883 sources. Read those two paths in the upstream diff
before promising anyone a fast release.

Merging is the slow half now, and none of this speeds it up: the conflicts in
AyuGram-patched sources and the separate `lib_ui` and `lib_tl` merges are hand
work.

## Caches

Cold, a release costs about two hours per platform to prepare the dependencies
and another two and a half to compile. Warm, the same work is minutes. Whether a
platform job takes nine minutes or three hours is decided entirely by whether
the caches below were reachable from the release branch.

`Release` and `Cache warm` drive identical work through one set of composite
actions in `.github/actions`, two per platform: one prepares the dependencies,
one compiles. A single recipe is the point. A cache saved under a different key
or from a different directory layout is useless to the other workflow, and the
divergence is silent.

Two GitHub rules make a release branch start cold. A cache that nothing has read
for seven days is evicted, and a run reaches only caches from its own branch or
from the default branch, while a release branch is renamed every version.
`Cache warm` answers both. Scheduled runs always use the default branch, so the
caches it writes are readable from every branch, and it runs three times a week
so one failed run cannot open a seven-day gap. It warms the newest
`codex/ayu-<major>.<minor>.<patch>` branch; dispatch it with a `ref` to warm an
older one. Anything that is not a release branch of this repository is refused,
because warming hands a branch the application credentials and write access to
the caches every other branch reads.

`cache-warm.yml` and `cache-prune.yml` have to exist on the default branch,
which is `dev`. GitHub starts scheduled runs, and offers `Run workflow`, only
from the default branch's copy of a workflow file, and the warming workflow
refuses to start anywhere else. Everything else a warm run needs, the composite
actions and the sources, comes from the checkout of the branch being warmed, so
`dev` has to carry only the workflow files. Which branch is the default must
not change again: it moved at 7.2.5, which scoped every warmed cache to a
branch that was no longer the default and left the next release cold. Moving
the `dev` tip is not the same thing and is fine, because caches are scoped to
the ref name. GitHub also disables scheduled workflows in a repository with no
activity for sixty days, which would stop the warming silently.

Ownership is split so the budget stays predictable: `Cache warm` writes every
cache, compiler and dependency alike, and `Release` only reads them. Both
`save-cache` inputs default to `false` for that reason. Without the split each
release would add another six gigabytes under its own branch, push the
repository past the ten gigabyte limit, and start evicting the entries being
warmed. `Cache prune` runs twice around the builds for the same reason. Before
them it frees the space the uploads are about to need; after them it drops the
entry each upload superseded, because a compiler cache key ends in the commit,
so a warm run on a new commit leaves two entries per family behind. Without the
second call that peak, roughly double the compiler caches, would stand until
the next scheduled run two days later. Pruning deletes only keys this
repository owns, and only those that are off the default branch or superseded
by a newer entry of the same family; caches belonging to other workflows are
left alone. The last job prints usage against the limit.

The compiler cache key is `ccache-<platform>-<architecture>-release-<commit>`,
restored by prefix, and deliberately carries no toolchain fingerprint. `ccache`
already hashes the compiler through `compiler_check = content`, so a runner
image bump costs a few misses inside a restored directory instead of discarding
the directory. Dependency cache keys do carry a fingerprint, because a library
built by another toolchain is unusable rather than merely stale. That
fingerprint covers Xcode and the macOS SDK and nothing else: a build driver
like CMake cannot change what the libraries were linked against, and including
it would throw away a two-hour tree on every runner image refresh.

Caching anything here at all requires `sloppiness = pch_defines,time_macros`,
because the large targets use precompiled headers. The price is `__DATE__`: the
build date in the About tooltip is the date the object was first compiled, so a
release reports the date of the warm run its objects came from. On Windows the
cache also runs in depend mode, where a miss costs nothing beyond the compile;
without it every miss adds a full MSVC preprocessing pass over the Qt headers,
and the first warm run, which is all misses, is exactly the one that has to fit
inside the job limit.

Set the repository variable `TDESKTOP_API_ID` and secret `TDESKTOP_API_HASH` to
credentials obtained for the application through
[Telegram](https://core.telegram.org/api/obtaining_api_id). Both workflows
validate them before any expensive job starts, because `configure` fails on
empty credentials only after the dependencies are built.

See [macOS](building-mac.md), [Windows](building-win.md) and
[Linux](building-linux.md) for the platform build commands. Windows and Linux
still require full build and runtime verification on this branch. A successful
macOS build does not establish compatibility on those platforms.

## Public release requirements

Technical maintenance releases use the following contract:

- Build the exact source commit for every advertised platform and architecture,
  and retain the workflow URL as release evidence.
- Use application API credentials registered for this distribution. Test-only
  credentials and another application's credentials are not a release setup.
- Do not bundle external translation snapshots. AyuGram translations are fetched
  by the application and cached locally.
- Verify the macOS ad-hoc signature and Windows Authenticode status, and state
  clearly that technical packages are not publisher-signed or notarized.
- Establish an authenticated update channel with its publisher, signing keys,
  version policy and supported platform IDs. Verify a real upgrade and rejection
  of altered or wrong-channel packages before enabling automatic installation.
- Publish checksums, exact source and pinned submodules with the binaries.
  Update the download instructions only when those binaries exist.

Technical release artifacts disable automatic updates explicitly. The source
updater is retained, but must not be pointed at an unrelated publisher or
enabled merely to expose an update button. There is no signed update channel in
this fork.

## Profiles

The normal macOS identity is `one.ayugram.AyuGramDesktop`, with the upstream
`AyuGram Desktop` Application Support directory. Development tests that need
isolation should use `-workdir` with a separate directory. Personal directory
names do not belong in the product defaults.

Close the application and back up its current profile before replacing a build.
Do not overwrite an existing profile automatically, silently merge accounts or
run two clients with copied authorization. Earlier experimental builds using a
different directory need an explicit local migration; this source change does
not perform one.

## Contributing upstream

The Telegram 7.2.8 update builds on
[AyuGramDesktop #460](https://github.com/AyuGram/AyuGramDesktop/pull/460).
Preserve its authorship and merge ancestry. The earlier macOS workflow proposal
[#427](https://github.com/AyuGram/AyuGramDesktop/pull/427) is related work.

`lib_ui` and `lib_tl` still point to the development repositories containing
the pinned commits. These are source dependencies, not application branding.
Upstream integration needs the corresponding submodule changes accepted before
the main repository pins them and restores the upstream URLs. Keep these URLs
absolute: a contributor must be able to fork only the main repository and still
clone its dependencies. Do not point at upstream before it contains the commits.

Keep language fixes independently reviewable from the Telegram version merge.
Start an integration PR as a draft until the advertised platform checks pass.
An open PR or a gap between releases is not evidence that upstream is abandoned.

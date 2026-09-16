# Windows 11 build and release

The Windows package is built as a 64-bit Release binary with Visual Studio and
the Windows 11 SDK `10.0.26100.0`. The application manifest enables Per-Monitor
V2 DPI handling with a Per-Monitor fallback and long-path-aware file access.
Windows 10 compatibility is retained.

## Release artifacts

Each Windows release publishes two packages:

- `AyuGram-7.2.8-windows-x64-setup.exe` installs AyuGram for the current user.
- `AyuGram-7.2.8-windows-x64-portable.zip` keeps its profile inside the extracted
  directory through `TelegramForcePortable`.

The release also contains the full source archive with pinned submodules and
`SHA256SUMS.txt`. Windows binaries are technical builds without an Authenticode
publisher signature, so verify the checksum before use.

## Automation

The `Windows 11 release` workflow checks the source version, runs the build
configuration tests, restores the shared dependency and compiler caches, builds
on the workspace drive, verifies the x64 PE and embedded manifest, smoke-tests
both packages, and publishes the exact successful commit.

The automatic release trigger is deliberately limited to
`release/windows-7.2.8-win11.1`. The workflow can also be started manually from
that branch. Application credentials must be configured in the repository as
`TDESKTOP_API_ID` and `TDESKTOP_API_HASH`.

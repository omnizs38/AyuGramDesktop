## Build instructions for Linux using Docker

### Prepare folder

Choose a folder for the future build, for example **/home/user/TBuild**. It will be named ***BuildPath*** in the rest of this document. All commands will be launched from Terminal.

### Clone source code and prepare libraries

Install [poetry](https://python-poetry.org), [docker](https://www.docker.com/) and [docker-buildx](https://docs.docker.com/reference/cli/docker/buildx/), go to ***BuildPath*** and run

    git clone --recursive https://github.com/AyuGram/AyuGramDesktop.git tdesktop
    ./tdesktop/Telegram/build/prepare/linux.sh

### Building the project

Register application credentials through [Telegram](https://core.telegram.org/api/obtaining_api_id)
and export `TDESKTOP_API_ID` and `TDESKTOP_API_HASH` in the build shell.
For preview binaries, also pass `-D DESKTOP_APP_DISABLE_AUTOUPDATE=ON` until
the distribution meets the [release requirements](releasing.md).

Go to ***BuildPath*/tdesktop** and run

    docker run --rm -it \
        -u $(id -u) \
        -v "$PWD:/usr/src/tdesktop" \
        ghcr.io/telegramdesktop/tdesktop/centos_env:latest \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D "TDESKTOP_API_ID=$TDESKTOP_API_ID" \
        -D "TDESKTOP_API_HASH=$TDESKTOP_API_HASH"

Or, to create a debug build, run

    docker run --rm -it \
        -u $(id -u) \
        -v "$PWD:/usr/src/tdesktop" \
        -e CONFIG=Debug \
        ghcr.io/telegramdesktop/tdesktop/centos_env:latest \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D "TDESKTOP_API_ID=$TDESKTOP_API_ID" \
        -D "TDESKTOP_API_HASH=$TDESKTOP_API_HASH"

The built files will be in the `out` directory. A single-config Linux Release
build places the application at `out/bin/AyuGram`.

You can use `strip` command to reduce binary size.

### Linux x64 CI package check

The manually dispatched `Linux x64 package` workflow performs the repository's
diagnostic Linux build and packaging check on `ubuntu-24.04`. Keeping the
multi-hour job manual lets normal pull requests use the fast source, lint and
packaging smoke checks instead. Start the full check on the branch to verify:

    gh workflow run "Linux x64 package" --ref <branch>
    gh run list --workflow linux.yml --limit 1
    gh run watch <run-id> --exit-status

The workflow follows the Docker build above with recursive submodules and the
published `centos_env` image. It reads the public build credentials already
listed in `docs/api_credentials.md`; it does not read repository secrets. The
result is a diagnostic package, not a public release: automatic updates and
crash reporting are disabled, no tag or GitHub Release is created, and the
artifact expires after three days.

After compilation, the workflow checks all of the following before uploading:

- the exact source revision and 7.2.8 version metadata;
- an x86-64 ELF executable using the standard x86-64 loader;
- no missing `ldd` dependencies or dependencies resolved from the build tree;
- no build-tree RPATH/RUNPATH and no GLIBC requirement newer than 2.28;
- a stripped `AyuGram-7.2.8-linux-x64.tar.xz` with the expected contents;
- successful extraction and re-validation of the packaged executable;
- a verified SHA-256 entry in `SHA256SUMS.txt`.

Download a completed run with the immutable artifact name shown by the run:

    gh run download <run-id> --name linux-x64-<commit-sha>
    sha256sum --check SHA256SUMS.txt

To repeat only the package/runtime checks after a local Release build, from the
repository root run:

    bash .github/scripts/package_linux.sh \
        out/bin/AyuGram \
        "$(awk '$1 == "AppVersionStr" { print $2 }' Telegram/build/version)" \
        "$(git rev-parse HEAD)" \
        artifacts \
        local-centos-env
    (cd artifacts && sha256sum --check SHA256SUMS.txt)

Set `LINUX_CI_MAX_GLIBC` only when intentionally testing a different runtime
baseline; CI keeps the Rocky Linux 8-compatible maximum at 2.28.

### Visual Studio Code integration

Ensure you've followed the instruction up to the [**Clone source code and prepare libraries**](#clone-source-code-and-prepare-libraries) step at least.

Open the repository in Visual Studio Code, install the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote-containers) extension and add the following to `.vscode/settings.json` (using [your **api_id** and **api_hash**](#obtain-your-api-credentials)):

    {
        "cmake.configureSettings": {
            "TDESKTOP_API_ID": "YOUR_API_ID",
            "TDESKTOP_API_HASH": "YOUR_API_HASH"
        }
    }

After that, choose **Reopen in Container** via the menu triggered by the green button in bottom left corner and you're done.

![Quick actions Status bar item](https://code.visualstudio.com/assets/docs/devcontainers/containers/remote-dev-status-bar.png)

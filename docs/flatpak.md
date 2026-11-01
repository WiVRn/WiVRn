# Flatpak maintenance

The manifest template is `flatpak/io.github.wivrn.wivrn.yml.in`. All remote sources are archives pinned with a sha256, downloaded from the project's official host: a release tarball, or an archive of the official repository. Do not use third-party mirrors or `type: git` sources. Get a sha256 with `curl -L <url> | sha256sum`.

| When | Do |
|---|---|
| Updating monado | Update `monado-rev`. Nothing else. |
| Updating xrizer | Update the commit in its archive URL and its sha256, run `tools/gen_flatpak_manifest.py`, commit `flatpak/xrizer-gen-src.json` and `flatpak/xrizer-gen-src.commit`. Requires the `toml` and `aiohttp` python modules. |
| Updating OpenComposite | Also update the `libs/openxr-sdk`, `libs/glm` and `libs/libunwind` archives to its submodule commits: `curl "https://gitlab.com/api/v4/projects/znixian%2FOpenOVR/repository/tree?path=libs&ref=<commit>"` (entries of type `commit`). |
| Updating crosstool-ng | Update `.tarball-version` to `git describe` of the new commit, without the `crosstool-ng-` prefix. |
| Updating the KDE runtime | Update the `flatpak-github-actions:kde-<version>` container tag in [Build.yml](../.github/workflows/Build.yml) to match `runtime-version`. The next CI build is a full rebuild (~40 min). |
| Updating any other dependency | Update its URL and sha256. |
| CI needs a full rebuild | `gh cache list --key flatpak- --json id --jq '.[].id' \| xargs -n1 gh cache delete` |

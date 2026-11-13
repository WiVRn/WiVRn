# Goggles Link

Goggles Link is the VR streaming software of [Goggles OS](https://github.com/jamoijyt/gogglesos):
the PC side that GogglesXR runs, and the app on the headset. It is a fork of
[WiVRn](https://github.com/WiVRn/WiVRn) (GPL-3.0), kept close to it so WiVRn's improvements can
be merged in.

## What differs from WiVRn

Kept small on purpose. Every change is listed here.

| Change | Where |
|---|---|
| The headset app has its own application ID (`org.gogglesos.link`) and name, so it installs beside WiVRn's app | `build.gradle` (`wivrn_app_id`), set by the workflow |
| Builds for Goggles OS: a server archive for `/opt/goggles-link` (no Flatpak) with xrizer included, and the headset APK | `.github/workflows/goggles-link.yml` |
| WiVRn's own build and store-release workflows are removed (they publish WiVRn, not this fork) | `.github/workflows/` |

Names inside the software (the `wivrn-server` program, its D-Bus name, its config folder) are
WiVRn's for now, so the two can't run at the same time, and a stock WiVRn app of the same
version can still connect.

## Builds

Every push builds both parts; a tag `gl-<version>` publishes a release that Goggles OS pins by
checksum. See the comments at the top of `.github/workflows/goggles-link.yml`, including the
signing key the headset app needs.

## Merging a new WiVRn release

```bash
git remote add upstream https://github.com/WiVRn/WiVRn
```

```bash
git fetch upstream --tags
```

```bash
git merge <release tag>
```

Then check the table above still holds, and that `XRIZER_REV` in the workflow matches
`flatpak/io.github.wivrn.wivrn.yml.in`.

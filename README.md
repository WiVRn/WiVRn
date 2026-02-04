<h1 align="center"> WiVRn </h1>

<div align="center">
  
[![License: GPL v3](images/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0) ![CI](https://github.com/WiVRn/WiVRn/workflows/Build/badge.svg) ![Format](https://github.com/WiVRn/WiVRn/workflows/Format/badge.svg)
  
</div>
<p align="center"><img src="images/wivrn.svg" width="180"></p>
<h3 align="center">Fully FOSS PCVR streamer</h3>

# About

WiVRn is an application that wirelessly streams a virtual reality game to a standalone VR headset from a <b>Linux</b> computer.

WiVRn support a wide range of VR devices:

| Headset | Supported | Notes |
|:--------:|:--------:|:--------:|
| Quest 1 | ✓ |  |
| Quest 2 | ✓ |  |
| Quest 3 | ✓ |  |
| Quest 3s | ✓ |  |
| Quest Pro | ✓ |  |
| Pico Neo 3 | ✓ |  |
| Pico 4 | ✓ |  |
| HTC Vive Focus 3 | ✓ | Laggy | 
| HTC Vive XR Elite | ✓ | Laggy |
| Samsung Galaxy XR | ✓ |  |
| Other Android VR | ? | Cannot know |
| Play for Dream | ✖ | https://github.com/WiVRn/WiVRn/issues/465 |
| Non-Android VR | ✖ | Not Android |
| Non-VR Android | ✖ | VR required |

<sup>A Linux client does exist, only for debugging. It has no audio or hardware decoding.</sup>

# Getting started


## PC Server/Dashboard

We recommend using the flatpak package from Flathub:

[![Flathub](https://flathub.org/api/badge)](https://flathub.org/apps/io.github.wivrn.wivrn)

Alternatively, packages are available from:
- [Arch User Repositoy](https://aur.archlinux.org/packages/wivrn-dashboard)
- [Fedora](https://packages.fedoraproject.org/pkgs/wivrn/wivrn/)
- [Gentoo Guru](https://gitweb.gentoo.org/repo/proj/guru.git/tree/media-libs/wivrn)


## Headset Client/App

Follow the wizard in the PC dashboard to install the client on your VR headset.

It should either lead you to the [Meta Store](https://www.meta.com/experiences/7959676140827574/) (for Meta Quest headsets) or to download the correct APK (for other headsets).

> [!WARNING]
> The VR client and PC server need to be on the same version of WiVRn

> [!TIP]
> If the headset fails to connect to the computer, see [troubleshooting](#troubleshooting).



# Usage

## Running

### Prerequisites
Avahi must be running:
```bash
systemctl enable --now avahi-daemon
```

- If a firewall is installed, open port 5353/UDP for Avahi and ports 9757/UDP+TCP for WiVRn.
- For example, if using UFW run `ufw allow 5353/udp` and `ufw allow 9757`.

### Running
- On your computer, run "WiVRn server" application, or `wivrn-dashboard` from the command line. It will show the setup wizard the first time you launch it.
- On your headset, run WiVRn from the App Library. If you are using a Meta Quest and you have installed it from an APK instead of the Meta Store, it should be in the "unknown sources" section.
- You should now see your computer in the list: click connect, the screen will show "Connection ready. Start a VR application on <i>\[your computer's name\]</i>".

You can now stream an OpenXR application from your computer to your headset. For Steam games, you may also need to set the launch options to be able to use WiVRn. The server/dashboard will tell you how to do this if required.
- Right-click on the game you want to play in VR in Steam and click "Properties".
- In the "General" tab, enter the launch options that the WiVRn server/dashboard gave you inside the "Launch Options" setting.

You can set an application to be started automatically when your headset is connected, in the dashboard settings or [manually](docs/configuration.md#application).

### Application list
When the headset is connected and no XR application is running, it will show an application launcher. Applications in that list are sourced from:
- Steam games that are flagged as VR. Steam may need to be restarted for the list to be updated when new games are installed.
- .desktop files that contain `X-WiVRn-VR` in the `Categories` section. Files are searched in [standard locations](https://specifications.freedesktop.org/desktop-entry/latest/file-naming.html#desktop-file-id) which usually include `~/.local/share/applications` and `/usr/share/applications/`.

### OpenVR and Steam games

The flatpak includes [OpenComposite](https://gitlab.com/znixian/OpenOVR/) and [xrizer](https://github.com/Supreeeme/xrizer/), used to translate the OpenVR API to OpenXR. see [SteamVR](docs/steamvr.md) for details.

If using Wine/Proton, it will probe for OpenVR at startup, This means even for OpenXR applications, OpenComposite or xrizer is required.

When you start the server through flatpak, it automatically configures the current OpenVR to use xrizer.

### Steam Flatpak
If you're using the Steam Flatpak, you'll need to grant read only access to the following paths:

```bash
flatpak override \
  --filesystem=xdg-run/wivrn:ro \
  --filesystem=xdg-data/flatpak/app/io.github.wivrn.wivrn:ro \
  --filesystem=/var/lib/flatpak/app/io.github.wivrn.wivrn:ro \
  --filesystem=xdg-config/openxr:ro \
  --filesystem=xdg-config/openvr:ro \
  com.valvesoftware.Steam
```

When using a user installation of flatpak Steam, use `override --user` instead of `override`.

### Audio
When the headset is connected, WiVRn will create a virtual output device simply named "WiVRn. You must manually set this audio output to enabled/default. Please note that in `pavucontrol` it will appear as a virtual device.

To enable microphone, you first have to enable it on the settings tab on the VR headset (and give permission when prompted). It should appear as a virtual input device named "WiVRn(microphone)", and needs to be assigned as the input device (same way as output device).


# Building

See [building](docs/building.md) for building the [dashboard](docs/building.md#dashboard), [server (PC)](docs/building.md#server-pc), and [client (headset)](docs/building.md#client-headset)


# Configuration
Configuration can be done from the dashboard.

See [configuration](docs/configuration.md) for editing the configuration manually.

# Troubleshooting
<details><summary>My computer is not seen by the headset</summary>

If the server list is empty in the headset app:
- Make sure your computer is connected on the same network as your headset
- Check that avahi is running with `systemctl status avahi-daemon`, if it is not, enable it with `systemctl enable --now avahi-daemon`
- If you have a firewall, check that port 5353 (UDP) is open</details>

<details><summary>My headset does not connect to my computer</summary>
  
- If you have a firewall, check that port 9757 (UDP and TCP) is open
- The server and client must be the same version.</details>

<details><summary>How do I use a wired connection?</summary>

- Make sure the WiVRn Server is installed and running on your computer
- Make sure you have the WiVRn app installed on your headset
- After starting the "WiVRn Server" on your computer and ensuring your device is connected to your PC via cable, run the following in your terminal (Note: using `adb` on some devices may require developer mode to be enabled):
   - ```bash
      adb reverse tcp:9757 tcp:9757
      adb shell am start -a android.intent.action.VIEW -d "wivrn+tcp://localhost" org.meumeu.wivrn
      ```
   - Depending on your install type, you may need to replace `org.meumeu.wivrn` (Meta Store install) with:
      - `org.meumeu.wivrn.github` for [releases](https://github.com/WiVRn/WiVRn/releases) on Github
      - `org.meumeu.wivrn.github.nighly` for Github nightlies (wirvn-apk [repository](https://github.com/WiVRn/WiVRn-APK/releases))
      - `org.meumeu.wivrn.github.testing` for Github CI builds
      - `org.meumeu.wivrn.local` for developer builds
- You can now continue the pairing process as documented in the running section.</details>

<details><summary>How do I see server logs when using the dashboard?</summary>

- Click Troubleshoot > Open server logs, or
- Navigate to `${XDG_STATE_HOME}/wivrn/wivrn-dashboard` (with fallback to `${HOME}/.local/state` for `${XDG_STATE_HOME}`, or
- For flatpak, navigate to `${HOME}/.var/app/io.github.wivrn.wivrn/.local/state/wivrn/wivrn-dashboard`.</details>

<details><summary>I have high motion latency, black borders following my view, hear corrupted audio or see a corrupted, pixelated image</summary>

- When connecting through USB, make sure the headset isn't connected through WiFi (switch off WiFi)
- Reset the settings using the button at the bottom of the settings tab
- Try switching to software encoding
- Decrease the bitrate
- Decrease the resolution in the WiVRn app
- Connect through USB or use a better WiFi router.

Note: WiVRn isn't properly optimized for NVIDIA GPUs due to the lack of developers with NVIDIA hardware. Motion latency may be significantly worse at rendering resolutions higher than default.</details>

# Contributing

## Translations

See [translating](docs/translating.md) for procedure.


# Credits
WiVRn uses the following software:
- [ambientCG](https://ambientcg.com/)
- [Avahi](https://www.avahi.org/)
- [Boost.Locale](https://github.com/boostorg/locale)
- [Boost.PFR](https://github.com/boostorg/pfr)
- [CLI11](https://github.com/CLIUtils/CLI11)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [fastgltf](https://github.com/spnda/fastgltf)
- [ffmpeg](https://ffmpeg.org/) optional, for hardware encoding on AMD/Intel
- [FreeType](https://freetype.org/)
- [glm](http://glm.g-truc.net/)
- [HarfBuzz](https://harfbuzz.github.io/)
- [librsvg](https://wiki.gnome.org/Projects/LibRsvg)
- [Monado](https://monado.freedesktop.org/)
- [nvenc](https://developer.nvidia.com/nvidia-video-codec-sdk) optional, for hardware encoding on NVIDIA
- [qCoro](https://qcoro.dev/)
- [Qt 6](https://www.qt.io/) optional, for the dashboard
- [spdlog](https://github.com/gabime/spdlog)
- [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [WebXR input profiles](https://www.npmjs.com/package/@webxr-input-profiles/motion-controllers)
- [x264](https://www.videolan.org/developers/x264.html) optional, for software encoding

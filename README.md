# WiVRn

[![License: GPL v3](images/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0) ![CI](https://github.com/WiVRn/WiVRn/workflows/Build/badge.svg) ![Format](https://github.com/WiVRn/WiVRn/workflows/Format/badge.svg)

<p align="center"><img src="images/wivrn.svg" width="180"></p>

WiVRn wirelessly connects a standalone VR headset to a Linux computer. You can then play PCVR games on the headset while processing is done on the computer.

It supports a wide range of headsets such as Quest 1 / 2 / Pro / 3 / 3S, Pico Neo 3, Pico 4, HTC Vive Focus 3, HTC Vive XR elite and most other Android based headsets.

# Getting started


## Server and dashboard

We recommend using the flatpak package from Flathub:

[![Flathub](https://flathub.org/api/badge)](https://flathub.org/apps/io.github.wivrn.wivrn)

Alternatively, packages are available:
- [AUR for Arch](https://aur.archlinux.org/packages/wivrn-dashboard)
- [Fedora](https://packages.fedoraproject.org/pkgs/wivrn/wivrn/)
- [Guru for Gentoo](https://gitweb.gentoo.org/repo/proj/guru.git/tree/media-libs/wivrn).


## Headset app

Follow the wizard in the dashboard to install the client app on the headset: it will either lead you to the [Meta Store](https://www.meta.com/experiences/7959676140827574/) or download the correct APK.

⚠️ You will need to have a compatible version: if the headset fails to connect to your computer, see [troubleshooting](#troubleshooting).



# Usage

## Running

### Prerequisites
Avahi must be running:
```bash
systemctl enable --now avahi-daemon
```

- If a firewall is installed, open port 5353/UDP for avahi and ports 9757/UDP+TCP for WiVRn itself.
- In the case of ufw, use `ufw allow 5353/udp` and `ufw allow 9757`.

### Running
- On your computer, run "WiVRn server" application, or `wivrn-dashboard`  from the command line, it will show the connection wizard the first time you launch it.
- On your headset, run WiVRn from the App Library. If you are using a Quest and you have installed it from an APK instead of the Meta Store, it will be in the "unknown sources" section.
- You should now see your computer in the list: click connect, the screen will show "Connection ready. Start a VR application on **your computer's name**".

You can now start an OpenXR application on your computer. For Steam games, you may also need to set the launch options to be able to use WiVRn. If nothing related to Steam is displayed in the dashboard or wivrn-server output, then your system does not require it.
- Right-click on the game you want to play in VR in Steam and click "Properties".
- In the "General" tab, set the launch options to the value given in the dashboard.

You can set an application to be started automatically when you connect your headset in the dashboard settings or [manually](docs/configuration.md#application)

### OpenVR and Steam games

The flatpak also includes [OpenComposite](https://gitlab.com/znixian/OpenOVR/), used to translate the OpenVR API used by SteamVR to OpenXR used by WiVRn, see [SteamVR](docs/steamvr.md) for details.

If using Wine/Proton, it will probe for OpenVR at startup, so even for OpenXR applications, OpenComposite is required.

When you start the server through flatpak, it will automatically configure the current OpenVR to use OpenComposite.

### Steam Flatpak

If you're using the Steam Flatpak, you'll need to grant read only access to the following paths:

```bash
flatpak override --user \
  --filesystem=xdg-run/wivrn:ro \
  --filesystem=xdg-data/flatpak/app/io.github.wivrn.wivrn:ro \
  --filesystem=xdg-config/openxr:ro \
  --filesystem=xdg-config/openvr:ro \
  com.valvesoftware.Steam
```

Then create a symlink for the OpenXR configuration file (the directory `~/.var/app/com.valvesoftware.Steam/.config/openxr` will need to be created if it doesn't already exist):

```bash
ln -s ~/.config/openxr/1 ~/.var/app/com.valvesoftware.Steam/.config/openxr/1
```

### Audio
When the headset is connected, WiVRn will create a virtual output device named WiVRn. It is not selected as default and you should either assign the application to the device when it is running, or mark it as default. To do so you can use `pavucontrol` or your desktop environment's configuration panel. Please note that in `pavucontrol` it will appear as a virtual device.

For microphone, you first have to enable it on the settings tabs on the headset (and give permission when prompted). It will then appear as a virtual input device named WiVRn(microphone) and also needs to be assigned like for output device.


# Building

See [building](docs/building.md) for building the [dashboard](docs/building.md#dashboard), [server (PC)](docs/building.md#server-pc), and [client (headset)](docs/building.md#client-headset)


# Configuration
Configuration can be done from the dashboard.

See [configuration](docs/configuration.md) for editing the configuration manually.

# Troubleshooting

## My computer is not seen by the headset

If the server list is empty in the headset app:
- Make sure your computer is connected on the same network as your headset
- Check that avahi is running with `systemctl status avahi-daemon`, if it is not, enable it with `systemctl enable --now avahi-daemon`
- If you have a firewall, check that port 5353 (UDP) is open

## My headset does not connect to my computer
- If you have a firewall, check that port 9757 (UDP and TCP) is open
- The server and client must be compatible.

## How do I use a wired connection?

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
- You can now continue the pairing process as documented in the running section.

## How do I see server logs when using the dashboard?

- Click Troubleshoot > Open server logs, or
- Navigate to `${XDG_STATE_HOME}/wivrn/wivrn-dashboard` (with fallback to `${HOME}/.local/state` for `${XDG_STATE_HOME}`, or
- For flatpak, navigate to `${HOME}/.var/app/io.github.wivrn.wivrn/.local/state/wivrn/wivrn-dashboard`.

## I have high motion latency, black borders following my view, hear corrupted audio or see a corrupted, pixelated image

- When connecting through USB, make sure the headset isn't connected through WiFi (switch off WiFi)
- Reset the settings using the button at the bottom of the settings tab
- Try switching to software encoding
- Decrease the bitrate
- Decrease the resolution in the WiVRn app
- Connect through USB or use a better WiFi router.

Note: WiVRn isn't properly optimized for NVIDIA GPUs due to the lack of developers with NVIDIA hardware. Motion latency may be significantly worse at rendering resolutions higher than default.

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

# Notes for OpenVR and Steam

WiVRn only provides an OpenXR runtime. For games that use OpenVR, [xrizer](https://github.com/Supreeeme/xrizer) or [OpenComposite](https://gitlab.com/znixian/OpenOVR/) should be used to translate the APIs.

If using Steam, games will be sandboxed by pressure vessel, which does not passthrough OpenXR by default.
- Files under `/usr` are mapped into `/run/host/usr`. If xrizer/OpenComposite is installed there, make sure `VR_OVERRIDE` or `~/.config/openvr/openvrpaths.vrpath` uses `/run/host/usr` prefix.
- `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1` must be set to passthrough the WiVRn OpenXR manifest, library locations, and socket location.

By default, WiVRn attempts to automatically find xrizer and OpenComposite in a number of places:
- When WiVRn is shipped as a flatpak, it uses a provided copy of xrizer by default.
- WiVRn attempts to autodetect xrizer and OpenComposite in a number of places (see `OVR_COMPAT_SEARCH_PATH` in `CMakeLists.txt`).
- The `openvr-compat-path` configuration may be used for an unexpected install location.

If it is found, WiVRn adjusts the `openvrpaths.vrpath` file, and includes whitelisting the xrizer/OpenComposite path and setting `VR_OVERRIDE` in the environment variables.

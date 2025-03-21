# Notes for OpenVR and Steam

WiVRn only provides an OpenXR runtime. For games that use OpenVR, [OpenComposite](https://gitlab.com/znixian/OpenOVR/) should be used to translate the APIs.

If using Steam, games will be sandboxed by pressure vessel, which is not OpenXR aware yet.
- Files under `/usr` are mapped into `/run/host/usr`, if OpenComposite or WiVRn are installed there, make sure that `XR_RUNTIME_JSON` uses `/run/host/usr` prefix and that `VR_OVERRIDE` or `~/.config/openvr/openvrpaths.vrpath` also uses this configuration.
- WiVRn uses a socket to communicate between the application and the server process, this socket needs to be whitelisted as `PRESSURE_VESSEL_FILESYSTEMS_RW=$XDG_RUNTIME_DIR/wivrn/comp_ipc`.

By default, WiVRn attempts to automatically find OpenComposite in a number of places:
- When WiVRn is shipped as a flatpak, it uses a provided copy of OpenComposite by default.
- WiVRn attempts to autodetect OpenComposite in a number of places (see `OVR_COMPAT_SEARCH_PATH` in `CMakeLists.txt`).
- The `openvr-compat-path` configuration may be used for an unexpected install location.

If it is found, WiVRn adjusts the `openvrpaths.vrpath` file, and includes whitelisting the OpenComposite path and setting `VR_OVERRIDE` in the environment variables.

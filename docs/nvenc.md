# Notes for NVIDIA NVENC encoder

### Introduction
NVIDIA GPUs use performance states (P-states), which influence how the driver manages GPU core and memory clocks. P0 is generally the highest-performance state, while P8 is typically one of the lowest-power states. 

WiVRn's NVIDIA NVENC encoder implementation relies on CUDA, which may cause NVIDIA drivers to keep the GPU in the P2 performance state instead of allowing it to enter P0.
This behavior is associated with CUDA workloads and may be used by NVIDIA drivers to improve stability.

NVIDIA drivers support an application profile setting named `CudaNoStablePerfLimit`, users have reported that it resolves the issue. [open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules/issues/333#issuecomment-3669477571)

Some distributions already ship application profiles that exclude specific applications from these performance limits. You can see examples of how they apply this in [Pop!_OS](https://github.com/pop-os/nvidia-graphics-drivers/blob/master/debian/application-profiles/cuda-no-stable-perf-limit) or [CachyOS](https://github.com/CachyOS/CachyOS-PKGBUILDS/blob/master/nvidia/nvidia-utils/cuda-no-stable-perf-limit).

### Identifying the issue

To check whether your system is affected, monitor the GPU P-State using `watch nvidia-smi -q -d PERFORMANCE` while playing a game with the NVIDIA NVENC encoder enabled. If the GPU remains in P2 under load while using NVENC, but reaches P0 with the Vulkan encoder or after applying this profile, the system is likely affected.

### Workaround instructions
> [!NOTE] 
> If your GPU already enters P0 while using the NVIDIA encoder, this workaround is unlikely to provide any benefit.

1. Create the NVIDIA configuration directory

    The application profiles configuration file is located at:

    `$HOME/.nv/nvidia-application-profiles-rc`

    If the `.nv` directory does not already exist, create it:

    `mkdir -p "$HOME/.nv"`

2. Create the configuration file

    Open `$HOME/.nv/nvidia-application-profiles-rc` in your preferred text editor and add the following contents:
```
{
    "profiles": [
        {
            "name": "WiVRnServer",
            "settings": [
                {
                  "key"   : "0x166c5e",
                  "value" : 0
                },
                {
                  "key"   : "GLVRRAllowed",
                  "value" : false
                },
                {
                  "key"   : "VKDirectGSYNCAllowed",
                  "value" : false
                },
                {
                  "key"   : "VKDirectGSYNCCompatibleAllowed",
                  "value" : 0
                },
                {
                  "key"   : "GLShowGraphicsOSD",
                  "value" : false
                }
            ]
        }
    ],
    "rules": [
        { "pattern" : { "feature" : "procname", "matches" : "wivrn-server" }, "profile" : "WiVRnServer" }
    ]
}
```
3. Save the file

    Save the file and `restart wivrn-server`. The profile should be applied automatically by the NVIDIA driver.

| Settings key | Reference name | Value | Description |
| --- | --- | :---: | --- |
| `0x166c5e` | CUDA - Force P2 State | 0 | Controls forcing of the CUDA P-State. When set to 0, prevents automatic forcing of a specific P-State. |
| `GLVRRAllowed` | G-SYNC VRR | false | Enables VRR support for applications on G-SYNC compatible monitors. Set to false to disable this feature. |
| `VKDirectGSYNCAllowed` | Vulkan Direct G-SYNC | false | Enables VRR support for vulkan direct-to-display applications on G-SYNC compatible monitors. Set to false to disable this feature. |
| `VKDirectGSYNCCompatibleAllowed` | Vulkan Direct Compatible G-SYNC | 0 | Allows VRR on validated or unvalidated G-SYNC compatible monitors for Vulkan Direct-to-Display applications. Set to value 0 to disable this feature. |
| `GLShowGraphicsOSD` | Graphics API OSD | false | Control whether the graphics API visual indicator is rendered on top of OpenGL and Vulkan applications. Set to false to disable this feature. |

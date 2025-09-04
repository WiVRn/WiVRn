# Configurable items

Configuration is done on server side.
Files are read from
- `/usr/share/wivrn/config.json` (where `/usr` is selected at configure time with `CMAKE_INSTALL_PREFIX`)
- `/etc/wivrn/config.json`
- `$XDG_CONFIG_HOME/wivrn/config.json` or if `$XDG_CONFIG_HOME` is not set, `$HOME/.config/wivrn/config.json`.

Files later in the list replace top-level values from previous ones.

All elements are optional and have default values.

## `scale`
Default value: `0.5`, `0.35` if headset supports eye tracking

Controls the size of the video stream, either a number between 0 and 1 or a pair of numbers between 0 and 1. If two numbers are provided the first one is horizontal scale and the second vertical.
Scaling is applied in a foveated fashion: the center has a 1:1 ratio and the rest is scaled so that the total number of pixels matches the desired scale.

### Examples:
```json
{
	"scale": 0.5
}
```
The x and y resolution of the streamed video are half the size on the headset, reduces the required bandwidth and encoding/decoding time by about 4.

```json
{
	"scale": [0.75, 0.5]
}
```
Scales x by a 0.75 factor, and y by a 0.5 factor.

## `bitrate`
Default value: `50000000` (50Mb/s)

Bitrate of the video, in bit/s. Split among decoders based on size and codecs.

## `bit-depth`
Default value: `8` (bits)

Bit depth of the video. 8-bit is supported by all encoders. 10-bit is supported by `vaapi` encoders using `h265` or `av1`.

## `encoders`
A list of encoders to use.

Default value: one encoder per eye

WiVRn has the ability to split the video in blocks that are processed independently, this may use resources more effectively and reduce latency.
All the provided encoders are put into groups, groups are executed concurrently and items within a group are processed sequentially.

### `encoder`
Default value: `nvenc` if Nvidia GPU and compiled with nvenc, `vaapi` for all other GPU when compiled with ffmpeg, else `x264`.

Identifier of the encoder, one of
* `x264`: software encoding
* `nvenc`: Nvidia hardware encoding
* `vaapi`: AMD/Intel hardware encoding
* `vulkan`: experimental, for any GPU that supports vulkan video encode

### `codec`
Default value: first supported by both headset and encoder of `av1`, `h264`, `h265`.

One of `h264`, `h265` or `av1`.
Not all encoders support every codec, `x264` and `vulkan` only support `h264`.

### `width`, `height`, `offset_x`, `offset_y` (advanced)
Default values: full image (`width` = 1, `height` = 1, `offset_x` = 0, `offset_y` = 0)

Specifies the portion of the video to encode: all values are in 0, 1 range. Left eye image ranges from x 0 to x 0.5 and y 0 to 1, Rigth eye is x from 0.5 to 1 and y 0 to 1.

### `group` (very advanced)
Default value: One value for each encoder type (nvenc, vaapi, vulkan, x264).

Identifier (number) of the encoder group. Encoders with the same identifier are executed sequentially, in the order they are defined in the configuration. Encoders with different identifiers are executed concurrently.
Default setting will have all encoders of a given type execute sequentially, and different types in parallel.

### Examples
1. Simple encoder
```json
{
	"bitrate": 50000000,
	"encoders": [
		{
			"encoder": "vaapi",
			"codec": "h265"
		}
	]
}
```
Creates a single encoder, using vaapi hardware encoding, h265 video codec (HEVC) and 50Mb/s bitrate.

2. Hardware + software encoder
```json
{
	"bitrate": 50000000,
	"encoders": [
		{
			"encoder": "vaapi",
			"codec": "h265",
			"width": 0.5,
			"height": 1,
			"offset_x": 0,
			"offset_y": 0
		},
		{
			"encoder": "x264",
			"codec": "h264",
			"width": 0.5,
			"height": 1,
			"offset_x": 0.5,
			"offset_y": 0
		}
	]
}
```
Creates a hardware encoder for left eye, and a software encoder for right eye.

3. 2 Hardware encoders
```json
{
	"bitrate": 50000000,
	"encoders": [
		{
			"encoder": "vaapi",
			"codec": "h265",
			"width": 0.5,
			"height": 1,
			"offset_x": 0,
			"offset_y": 0,
			"group": 0
		},
		{
			"encoder": "vaapi",
			"codec": "h264",
			"width": 0.5,
			"height": 1,
			"offset_x": 0.5,
			"offset_y": 0,
			"group": 0
		}
	]
}
```
Creates two hardware encoders, one for left eye and one for right eye, executed sequentially as they have the same `group`.
This allows the left eye image to be encoded faster than the full image would be, so network transfer starts earlier, and decoding starts earlier. While the total encoding, transfer and decoding time remain the same or are longer, this can reduce the latency.

### `device`, only for vaapi
Default value: unset

Manually specify the device for encoding, can be used to offload encode to an iGPU. Device shall be in the form "/dev/dri/renderD128".


### `options` (very advanced), only for vaapi
Default value: unset

Json object of additional options to pass directly to ffmpeg `avcodec_open2`'s `option` parameter.

## `encoder-passthrough`

The single encoder used for passthrough (transparency), contains the same elements as the other encoders, except for width/height and offsets.

## `application`
Default value: unset

An application to start when connection with the headset is established, can be a string or an array of strings if parameters need to be provided.

### Example
```json
{
	"application": ["steam", "steam://launch/275850/VR"]
}
```
Launch No Man's Sky in VR mode on Steam when connection with headset is established.

## `tcp-only`
Default value: `false`

Only use TCP for communications with the client, this may have increased latency.
If `false` or unset, WiVRn will use both TCP and UDP.

### Example
```json
{
	"tcp-only": true
}
```

## `publish-service`
Default value: `avahi`

How to publish the service over the network, `avahi` or null.

If set to null, service will not be published and address has to be entered manually on the headset.

## `openvr-compat-path`
Default value: unset

Provides the path to the directory of an OpenVR compatibility tool (such as OpenComposite).

If unset, WiVRn will autodetect the path of such a tool as usual (see [the SteamVR guide](./steamvr.md)).

If set to an null, WiVRn will not manage the OpenVR configuration.

## `debug-gui`
Default value: `false`

Only available when built with `WIVRN_FEATURE_DEBUG_GUI`.

Enables the Monado debug gui.

## `use-steamvr-lh`
Default value: `false`

Only available when built with `WIVRN_FEATURE_STEAMVR_LIGHTHOUSE`

Enables the driver to load SteamVR Lighthouse devices.

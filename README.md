# libcamera for the OX05B1S RGB-IR camera

This is a Raspberry Pi `libcamera` fork with support for the OmniVision
OX05B1S RGB-IR sensor and the DW9714 voice coil motor (VCM).

It adds:

- RGB-IR to standard Bayer conversion in the PiSP pipeline
- NIR controls (`NirMode`, `NirRed`, `NirGreen`, `NirBlue`)
- OX05B1S sensor properties, tuning file and camera helper
- Autofocus support through the DW9714 VCM

The sensor runs at 2592x1944, up to 60 fps over 4 MIPI lanes.

## Requirements

- Raspberry Pi 5 with the OX05B1S module on CAM0
- Raspberry Pi OS kernel 6.18 (the tested kernel is `6.18.50+rpt-rpi-2712`)
- The OX05B1S kernel driver and device-tree overlay

Install the kernel driver first by following the instructions in the kernel
driver repository:

[wavesvision/kmod-ox05b1s-basic](https://github.com/wavesvision/kmod-ox05b1s-basic)

That procedure installs the sensor driver, builds the overlay and configures
the camera. Reboot after completing it.

For autofocus, also install the DW9714 VCM driver and enable the `vcm` overlay
parameter, following the instructions in:

[wavesvision/kmod-dw9714](https://github.com/wavesvision/kmod-dw9714)

Without the DW9714 driver, streaming and still capture work, but the lens is
not exposed to libcamera and no `AfMode` or `LensPosition` controls are
available.

## 1. Build and install libcamera

Install the build dependencies:

```sh
sudo apt install build-essential git meson ninja-build pkg-config python3-ply \
    libgnutls28-dev libssl-dev libevent-dev libudev-dev \
    libsdl2-dev libdrm-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good
```

The first configure downloads the `libpisp` and `libyaml` wrap dependencies, so
network access is required.

Build and install to `/usr/local`:

```sh
meson setup build . \
    -Dpipelines=rpi/pisp \
    -Dipas=rpi/pisp \
    -Dbuildtype=release \
    -Dgstreamer=enabled \
    -Dpycamera=disabled \
    -Dcam=enabled
ninja -C build
sudo meson install -C build
```

The GStreamer plugin built here exposes `draft-nir-mode`, `draft-nir-red`,
`draft-nir-green` and `draft-nir-blue`; the distribution `libcamerasrc` plugin
does not. Make sure the `/usr/local` plugin is used:

```sh
export LD_LIBRARY_PATH=/usr/local/lib/aarch64-linux-gnu
export GST_PLUGIN_PATH=/usr/local/lib/aarch64-linux-gnu/gstreamer-1.0
gst-inspect-1.0 libcamerasrc
```

## 2. Start and stop a stream

List the camera and verify the sensor was detected:

```sh
cam --list
cam --list-properties
```

`cam --list-properties` should report `Model = ox05b1s`,
`PixelArraySize = 2592x1944` and `ColorFilterArrangement = RGGB`.

Start streaming capture:

```sh
cam -c 1 --capture \
    --script=utils/ox05b1s/nir-day.yaml \
    --stream=width=2592,height=1944
```

The example script runs at 60 fps nominal and falls back to approximately
15 fps in low light. The command runs until stopped and prints a frame rate
line for every frame. Press `Ctrl-C` to stop the stream.

If a monitor is connected directly to the Raspberry Pi, use the DRM preview:

```sh
cam -c 1 --capture --display \
    --script=utils/ox05b1s/nir-day.yaml \
    --stream=width=2592,height=1944
```

Press `Ctrl-C` to stop the preview.

### Over SSH with X11

`--display` uses DRM/KMS and will not work through SSH. Use the SDL preview
instead, which opens an X11 window forwarded to your local machine:

```sh
ssh -X -C user@raspberrypi
SDL_VIDEODRIVER=x11 cam -c 1 --capture --sdl \
    --script=utils/ox05b1s/nir-day.yaml \
    --stream=width=2592,height=1944
```

Press `Ctrl-C` in the SSH terminal to stop. If the window does not open, check
that X11 forwarding is enabled on the server (`X11Forwarding yes` in
`/etc/ssh/sshd_config`) and try `ssh -Y -C` instead. If the window opens but
stays black, force software rendering:

```sh
SDL_RENDER_DRIVER=software cam -c 1 --capture --sdl \
    --script=utils/ox05b1s/nir-day.yaml \
    --stream=width=2592,height=1944
```

Full-resolution X11 forwarding is bandwidth-heavy; wired Ethernet or `ssh -C`
compression gives the smoothest preview.

## 3. Take a still image

Capture one full-resolution image:

```sh
cam -c 1 --capture=1 --file=/tmp/ox05b1s.ppm \
    --script=utils/ox05b1s/nir-day.yaml \
    --stream=role=still,width=2592,height=1944,pixelformat=RGB888
```

The image is written to `/tmp/ox05b1s.ppm`. Use `pixelformat=RGB888` with the
PPM writer; the default still format is YUV420.

## 4. Autofocus

The `af-auto-day.yaml` script enables continuous autofocus with the daylight
NIR profile:

```sh
cam -c 1 --capture=100 --metadata \
    --script=utils/ox05b1s/af-auto-day.yaml \
    --stream=width=2592,height=1944
```

For low light, use `af-auto-night.yaml` in the same way.

Autofocus requires the DW9714 VCM driver and the `vcm` overlay parameter. See
[wavesvision/kmod-dw9714](https://github.com/wavesvision/kmod-dw9714) for the
installation procedure. Without it, the camera still streams but these AF
scripts have no lens to control.

The metadata prints `AfState`, `FocusFoM` and `LensPosition`. For a continuous
stream, use `--capture` without a count and stop with `Ctrl-C`.

Check that the VCM is detected:

```sh
media-ctl -p -d /dev/media0
```

Look for an entity named `dw9714 10-000c` with a `Lens` function. If it is not
in `media0`, try `media1`.

## 5. Measure actual FPS

`cam` prints the instantaneous rate for each frame. For an average, capture a
fixed number of frames and time it:

```sh
time cam -c 1 --capture=600 \
    --script=utils/ox05b1s/nir-day.yaml \
    --stream=width=2592,height=1944
```

Average FPS is `600 / elapsed_seconds`.

All example scripts set:

```yaml
FrameDurationLimits: [16666, 66666]
```

This gives 60 fps nominal. In low light the AGC extends the exposure time and
the frame rate falls back to approximately 15 fps, so the image stays exposed
correctly.

For a strict 60 fps measurement, lock the frame duration to 16.666 ms instead:

```yaml
FrameDurationLimits: [16666, 16666]
```

The image will be darker and noisier in low light because exposure cannot
exceed about 16 ms.

## NIR modes

All examples above use the daylight profile
(`utils/ox05b1s/nir-day.yaml`), which subtracts the estimated infrared
component from the colour channels.

For low light, use the night profile, which adds the infrared component:

```sh
cam -c 1 --capture=100 --file=/tmp/nir_#.ppm \
    --script=utils/ox05b1s/nir-night.yaml \
    --stream=width=2592,height=1944
```

To disable NIR compensation (the RGB-IR to Bayer conversion still runs), use
`utils/ox05b1s/nir-off.yaml` in the same way.

## Troubleshooting

- `cam --list` shows no camera: check that the kernel driver installation and
  overlay setup described in the kernel driver repository were completed.
  Reboot after changing the overlay.
- Check the modules and overlays:

  ```sh
  lsmod | grep -E 'ox05b1s|dw9714'
  dmesg | grep -Ei 'ox05|dw9714'
  ```

- Check the sensor line length and pixel rate:

  ```sh
  v4l2-ctl -d /dev/v4l-subdev6 --list-ctrls
  ```

  Use the `ox05b1s` subdevice from `v4l2-ctl --list-devices`; the index may
  vary between boots. Expected values include `horizontal_blanking = 352`,
  `pixel_rate = 384000000` and `link_frequency = 450000000`.

- Always 30 fps: check that a capture script with
  `FrameDurationLimits: [16666, 66666]` is being loaded.
- Frame rate drops to about 15 fps: this is the low-light fallback. Use
  `FrameDurationLimits: [16666, 16666]` if you need a strict 60 fps lock.
- Autofocus does not move: verify that the `dw9714` module is loaded and that
  `media-ctl -p -d /dev/media0` shows the lens entity. See the kernel driver
  repository for VCM setup.

## Notes

- Use `/usr/local/bin/cam`; the distribution `rpicam-apps` uses the system
  libcamera and does not expose the NIR controls.
- Requesting a raw stream returns the sensor's native RGB-IR data and disables
  NIR processing.
- Horizontal, vertical and transposed transforms are not supported on RGB-IR
  sensors.
- The unit cell size, NIR coefficients, LUX/AWB and AF calibration in the
  tuning file are provisional. Colour correction and lens shading are omitted
  until calibrated for this module. See `Documentation/ox05b1s.rst` for
  details.

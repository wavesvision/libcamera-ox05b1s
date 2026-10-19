OX05B1S RGB-IR sensor integration
=================================

This document describes the Raspberry Pi PiSP support for the OmniVision
OX05B1S RGB-IR sensor, including the software RGB-IR to Bayer conversion, the
near-infrared (NIR) controls and the DW9714 voice coil motor (VCM).

Sensor and kernel requirements
------------------------------

The OX05B1S driver must expose ``V4L2_CID_PIXEL_RATE``, ``V4L2_CID_HBLANK`` and
``V4L2_CID_VBLANK``. The out-of-tree driver used for development originally
omitted horizontal blanking, which prevents libcamera from retrieving the sensor
line length. The driver changes required by this integration are:

* Expose a read-only ``V4L2_CID_HBLANK`` control. For the 4-lane 450 MHz mode
  the line length is 2944 pixels (HTS register ``0x170`` in units of 8 pixels),
  so horizontal blanking is ``2944 - 2592 = 352``.
* Report the correct pixel rate. All modes run at 384 MHz. The original driver
  reported 240 MHz for the 4-lane modes, which made the IPA compute incorrect
  exposure and frame duration limits.

The DW9714 VCM driver is not part of the standard Raspberry Pi kernel image.
The ``kmod-dw9714`` DKMS package provides the upstream driver, which matches the
``dongwoon,dw9714`` compatible string used by the camera overlay.

Kernel modules and overlay
~~~~~~~~~~~~~~~~~~~~~~~~~~

Install both DKMS packages and enable the VCM in ``/boot/firmware/config.txt``::

    dtoverlay=ox05b1s,cam0,4lane,vcm

After a reboot the media pipeline must contain a lens entity::

    media-ctl -p -d /dev/media0

Look for an entity named ``dw9714 10-000c`` with subtype ``Lens``. Verify the
focus control::

    v4l2-ctl -d /dev/v4l-subdev7 --list-ctrls

The ``focus_absolute`` control must have a range of 0 to 1023.

Building libcamera
------------------

Build the Raspberry Pi PiSP pipeline handler and IPA against the sensor::

    meson setup build . \
        -Dpipelines=rpi/pisp \
        -Dipas=rpi/pisp \
        -Dbuildtype=release \
        -Dgstreamer=disabled \
        -Dpycamera=disabled
    ninja -C build
    sudo meson install -C build

The sensor tuning file ``src/ipa/rpi/pisp/data/ox05b1s.json`` is installed as
``/usr/local/share/libcamera/ipa/rpi/pisp/ox05b1s.json`` by default.

NIR controls
------------

The following draft controls are advertised for RGB-IR sensors:

``NirMode``
    ``NirModeOff`` (0), ``NirModeDay`` (1) or ``NirModeNight`` (2). The default
    is taken from the ``pipeline_handler.nir.mode`` value in the tuning file and
    is ``NirModeDay``.

    The sensor's RGB-IR samples are always converted to a standard BGGR Bayer
    frame before the ISP. ``NirModeOff`` disables only the infrared
    compensation, not the RGB-IR to Bayer conversion.

``NirRed``, ``NirGreen``, ``NirBlue``
    Per-channel infrared coefficients in the range 0 to 10. Day mode subtracts
    ``coefficient / 10`` of the estimated infrared component from the channel;
    night mode adds it. The tuning file provides the defaults for each mode.

Per-request values override the defaults. Values persist between requests until
changed, so an application may set the mode once and update coefficients later.
The controls are consumed by the pipeline handler before the ISP runs; the IPA
only advertises them.

The NIR processing is disabled when the application requests a raw stream,
because the raw stream shares the CFE output buffer with the ISP input. A
warning is logged in that case.

Validation
----------

Unit test
~~~~~~~~~

The RGB-IR converter is tested against a direct port of the ``rgbir-fast`` mode
of the ``bayerir2bayer`` GStreamer element::

    meson test -C build rgbir

The test covers ``NirModeOff``, day mode, night mode and the maximum
coefficients, and checks the NEON implementation bit-for-bit against the scalar
reference.

Capture tests
~~~~~~~~~~~~~

The ``cam`` application can set the NIR mode with a capture script. Example
scripts are provided under ``utils/ox05b1s/``::

    cam -c 1 --capture=5 \
        --file=/tmp/ox_nir_#.ppm \
        --script=utils/ox05b1s/nir-day.yaml \
        --stream=width=2592,height=1944

For low light, replace ``nir-day.yaml`` with ``nir-night.yaml``. To disable
NIR compensation, use ``nir-off.yaml``.

All example scripts set::

    FrameDurationLimits: [16666, 66666]

This gives 60 fps nominal, with a low-light fallback to approximately 15 fps
when the AGC extends the exposure time. For a strict 60 fps capture, set both
values to 16666 instead.

Raw streams can be captured with::

    cam -c 1 --capture=3 \
        --file=/tmp/ox_raw_#.bin \
        --script=utils/ox05b1s/nir-day.yaml \
        --stream=role=raw,width=2592,height=1944

The raw stream contains the sensor's native, compressed RGB-IR data and has no
NIR processing. The NIR controls in the script are ignored for raw streams; the
script is used only for the frame duration limits.

Autofocus can be exercised with ``utils/ox05b1s/af-auto.yaml`` and
``--metadata``; the metadata reports ``AfState``, ``FocusFoM`` and
``LensPosition``.

Performance
-----------

The converter processes 2592x1944 16-bit samples in place. The AArch64 NEON
implementation takes about 6 ms per frame on a Raspberry Pi 5, including the
buffer copy used by the benchmark, which leaves enough headroom for 60 fps.
The conversion runs between ``dmabufSyncStart()`` and ``dmabufSyncEnd()`` on
the CFE output buffer.

Known limitations
-----------------

* The conversion assumes the sensor's native BGGR order. Horizontal, vertical
  and transposed transforms are rejected for RGB-IR sensors. The current
  driver programs ISP register ``0x5000`` with ``0x7e`` (the NXP reference
  uses ``0x6e``); if the module's colour filter phase is found to differ, the
  conversion and the reported Bayer order must be revisited.
* Raw and processed streams can be requested together, but NIR processing is
  disabled and the processed output receives the unconverted RGB-IR data.
* The unit cell size (2200 nm), the NIR coefficients and the ``rpi.lux``,
  ``rpi.awb``, ``rpi.ccm`` and ``rpi.af`` calibration data are provisional and
  must be replaced with measurements from the final module. Lens shading
  correction (``rpi.alsc``) is intentionally omitted until tables for the
  final lens are available.
* The AF map in ``rpi.af`` assumes a linear VCM position range of 0 to 1023.
  The DW9714 driver moves the lens in steps of 16 with approximately 1 ms per
  step, so the AF speed parameters may need tuning for the final lens.

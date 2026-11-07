# ALC4082 Linux capture support

This project documents and implements the vendor initialization missing from
Linux for the rear microphone on an ASRock ALC4082 USB audio device
(`26ce:0a08`). Without it, the microphone is detected and streams samples, but
speech is roughly 30 dB too quiet.

The behavior was recovered independently for interoperability by comparing a
Windows USB capture with Linux, tracing the corresponding Windows driver code,
and running controlled register and widget-command experiments.

## Current result

The experimental kernel patch:

1. activates the rear-microphone widget with vendor request `0x6c`, command
   `0x0f`; and
2. scopes the behavior to USB ID `26ce:0a08`, capture endpoint `0x83`.

After a USB reset under the stock module, the command increased a fixed 1 kHz
acoustic signal from `-69.3` to `-44.5 dBFS`. Applying the separate
five-register transaction afterward produced `-44.7 dBFS`, no additional
benefit. That transaction is therefore documented for completeness but
excluded from the minimal kernel fix.

The reduced command-only module was then loaded before a fresh USB
re-enumeration. Its first rear-capture prepare produced `-43.9 dBFS`; after 100
open/prepare/close cycles it produced `-43.7 dBFS`, with no kernel USB/audio
warnings. Runtime resume measured `-44.0 dBFS`, system resume measured
`-44.7 dBFS`, and a normal PipeWire capture measured `-43.6 dBFS`.

The patch is in
[`patches/0001-ALSA-usb-audio-enable-ASRock-ALC4082-rear-capture.patch`](patches/0001-ALSA-usb-audio-enable-ASRock-ALC4082-rear-capture.patch).

This repository provides a patch for the Linux USB audio module. The Arch
package downloads Linux source and applies the patch. The repository does not
contain a separate kernel driver.

## CachyOS package

The Arch package in [`packaging/arch`](packaging/arch) installs the patched
`snd-usb-audio` source for DKMS. DKMS builds the module for each installed
CachyOS kernel with matching headers. A kernel update starts a new build through
the Arch DKMS hook. DKMS saves the stock module for restoration on removal.

Install the package once:

```sh
sudo pacman -Syu linux-cachyos linux-cachyos-headers dkms
cd packaging/arch
makepkg -sf
sudo pacman -U snd-usb-audio-alc4082-cachyos-7.2.7-1-x86_64.pkg.tar.zst
dkms status snd-usb-audio-alc4082
modinfo -n snd_usb_audio
```

Reboot to load the new module. The DKMS source is based on Linux 7.2.7.
Future kernel changes can require a source update if the build fails.
The package replaces the complete `snd-usb-audio` module with this source
version. Review upstream USB audio fixes after each kernel update before you
continue to use the override.

## Status

This is an upstream-oriented, device-scoped candidate, not yet an accepted
Linux fix. Remaining work includes:

- regression testing the other known motherboard that shares USB ID
  `26ce:0a08`;
- identifying whether the codec's documented analog microphone preamp is
  independently programmable; and
- deciding whether any proven hardware gain belongs in an ALSA mixer control.

The Windows `0/+10/+20/+30 dB` Microphone Boost slider was tested separately.
It produced no USB transaction while capture was active, and the matching
driver package installs a host-side Realtek capture APO containing a software
`SoundBooster`. It is therefore not evidence of a codec analog-gain register.
The rear-widget activation and that Windows software control must not be
conflated.

## Probe tool

Build the read/write probe with:

```sh
make
```

Examples:

```sh
./alc4082-reg read 0x0319 0x0303 0x050a 0x031a 0x0510
./alc4082-reg activate-rear-mic
./alc4082-reg enable-rear-registers --omit 0x050a
./alc4082-reg enable-rear-capture
./alc4082-reg disable-rear-registers
```

The USB device node must be accessible to the current user. The tool does not
detach the kernel driver and does not claim an audio interface.

The `--omit` form and the exact Windows disable branch exist for controlled
ablation tests. They change undocumented device state and should only be used
with the test matrix in
[`docs/upstream-readiness.md`](docs/upstream-readiness.md), followed by the
known-good full enable sequence.

With a fixed acoustic tone playing continuously, run the hot-state register
ablation as follows:

```sh
./tools/run-register-ablation.sh
```

The harness keeps one ALSA stream open so an already loaded experimental quirk
cannot reapply the full transaction between cases. It stores raw results under
`results/`, records the exact register state for each segment, and restores the
full known-good sequence even if interrupted. This test complements, but does
not replace, cold-power tests.

After resetting the USB device with the installed stock module loaded, measure
the untouched-to-enabled transition with:

```sh
TEST_MODE=cold-transition ./tools/run-register-ablation.sh
```

This mode refuses to run if the loaded module's source version differs from the
installed module.

## Evidence and reproduction

See [`docs/reverse-engineering.md`](docs/reverse-engineering.md) for the
protocol, driver trace, confidence levels, and measurements. Raw captures and
the proprietary driver are intentionally excluded; see
[`captures/README.md`](captures/README.md) for capture instructions.

## License

The project code and documents use GPL-2.0-only. See [LICENSE](LICENSE).
Downloaded Linux source files keep their own SPDX license statements.

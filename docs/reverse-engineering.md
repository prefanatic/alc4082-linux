# Reverse-engineering findings

## Test hardware

- Motherboard: ASRock X870 Steel Legend WiFi
- USB audio identity: `26ce:0a08 Generic USB Audio`
- USB device revision: `0.02`
- Codec family: Realtek ALC4082
- Rear capture endpoint: interface 1, endpoint `0x83`
- Test kernel: CachyOS `7.1.6-1-cachyos`
- Motherboard BIOS during final testing: ASRock/AMI `4.43`, 2026-06-24

The USB Audio Class topology exposes rear and front microphone feature units,
including mute and `-17.25` through `+12 dB` digital/ADC volume. It does not
advertise the codec's documented `0/+10/+20/+30 dB` analog microphone boost.

## Driver provenance

The analyzed Windows driver was Realtek USB Audio `6.4.0.2395`, dated
2024-07-10. Its SHA-256 is:

```text
1712dd0580950bdd729c47bdaffad2c9611e82c100b8f43a66c39d7b617e9f7c
```

The driver itself is proprietary and is not redistributed here.

## Hardware scope

USB ID `26ce:0a08` is shared by at least the ASRock X870 Steel Legend WiFi and
ASRock Z790 PG-ITX/TB4. ASRock identifies both boards as using ALC4082 audio,
and the current ALSA UCM configuration lists both boards under the same USB
identity. The Linux patch therefore scopes on the USB identity and endpoint,
not on X870-specific DMI data.

The Windows driver's hardware table provides stronger protocol-specific
evidence. Its exact `26ce:0a08` entry selects internal model class `0xb4` and
product variant `0x80`. The common configuration path for class `0xb4` sets
codec-family value `0x4080` and the feature bit checked by the five-register
routine. Variant `0x80` selects register `0x0510` with bit `0x02`; it does not
take the alternate `0x0511` branch. The routine is consequently reached from
the exact VID/PID without consulting DMI.

This proves that the captured branch belongs to the complete Windows match for
`26ce:0a08`, rather than being an incidental X870-only call. It does not prove
that a different USB ID or internal product variant uses the same transaction.

## Vendor protocol

The Windows driver uses device-recipient vendor control transfers:

| Operation | bmRequestType | bRequest | wValue | wIndex | Data |
|---|---:|---:|---:|---:|---|
| Read byte register | `0xc0` | `0x25` | `0` | register | one byte IN |
| Write byte register | `0x40` | `0x24` | `0` | register | one byte OUT |
| Widget command | `0x40` | `0x6c` | `0` | `1` | one byte OUT |

The generic Windows register helper performs:

```text
new_value = set_bits | (old_value & preserve_mask)
```

## Rear microphone activation

Widget command `0x0f` activates the rear microphone path. This was established
with two fresh driver binds and a continuously spoken signal. In the replicated
test, the command changed RMS from `-70.69 dBFS` to `-44.24 dBFS`, a 26.45 dB
increase. Repeated `0x0f` and `0x10` commands did not produce comparable further
changes.

The static driver path gives the command a narrower behavioral name. The
driver emits `0x0f` from `CWidget::AssignDeviceType`; its
`CCommon::updateMicSelection` path maps selection value `1` to device type
`0x0f` and mode `2` before calling that method. Command `0x10` is emitted for a
different stored widget pointer. This supports describing `0x0f` as rear
microphone assignment/activation, not as an analog gain setting.

The Windows initialization capture contained `0x0f, 0x10, 0x0f, 0x0f, 0x10`
because it initialized multiple widgets. Replaying that whole sequence works,
but endpoint `0x83` requires only the first `0x0f` command.

Confidence: high, based on replicated dynamic tests and static call-site
analysis. The internal Realtek symbolic widget name is not present in the
public datasheet.

## Separate five-register transaction

After rear-widget activation, Windows also applies these read-modify-write
updates:

| Order | Register | Preserve | Set | Observed Linux → Windows value |
|---:|---:|---:|---:|---:|
| 1 | `0x0319` | `0x3f` | `0x40` | `0x14` → `0x54` |
| 2 | `0x0303` | `0xf0` | `0x05` | `0x2a` → `0x25` |
| 3 | `0x050a` | `0xfd` | `0x02` | `0x0a` → `0x0a` |
| 4 | `0x031a` | `0x00` | `0x20` | `0x00` → `0x20` |
| 5 | `0x0510` | `0xfd` | `0x02` | `0x00` → `0x02` |

Static cross-references show this boolean routine is called with enable at
Windows PnP start and when a type-1 widget enters private power state `1`. It is
called with disable during teardown, explicit power states `3` and `4`, and a
power-IRP shutdown path. The exact request protocol, masks, ordering, and
lifecycle role are proven. Public symbolic names for the individual registers
are unavailable.

Fixed-tone testing now shows that this routine is separate from the missing
rear-microphone assignment: command `0x0f` restores the signal before these
writes occur, and the writes add no measurable level. The upstream microphone
fix therefore excludes the entire register transaction.

The false branch is not simply the reverse list. It uses this order:

| Order | Register | Preserve | Set |
|---:|---:|---:|---:|
| 1 | `0x0319` | `0x3f` | `0x00` |
| 2 | `0x0303` | `0xf0` | `0x00` |
| 3 | `0x050a` | `0xfd` | `0x02` |
| 4 | `0x0510` | `0xfd` | `0x00` |
| 5 | `0x031a` | `0x00` | `0x00` |

The driver selects register `0x0511` instead of `0x0510` for three internal
codec variants (`0x8e` through `0x90`) and uses bit `0x08` instead of `0x02`.
The exact `26ce:0a08` hardware-table entry selects variant `0x80`, which takes
the captured `0x0510` branch. This is evidence for keeping the Linux quirk
restricted to that USB identity rather than generalizing it to all
ALC4080-family devices.

### What the individual registers mean

An exhaustive immediate-value cross-reference in the analyzed driver found:

- `0x0319` and `0x0303` only in this five-register boolean routine;
- `0x031a` only in the paired enable/disable helper, apart from an unrelated
  trace constant; and
- `0x050a` and `0x0510`/`0x0511` both here and in a separate routine operating
  on a configurable register pair in the `0x0504`-`0x0512` range. That second
  routine manipulates other bits and polls status as part of device-state or
  jack-state initialization.

The public ALC4080-family datasheet contains none of these private addresses.
It documents selectable analog microphone boost at a functional level, but
does not publish the USB-controller register map. Therefore the defensible
description is that these writes form the vendor's capture-path state
transition. Names such as “preamp gain,” “mux,” or “jack detect” for individual
bits would be speculation. Controlled ablation can establish functional
necessity, but cannot by itself recover Realtek's private symbolic names.

### Hot-state register ablation

A fixed 1 kHz acoustic signal was played through an independent Anker USB
headphone output with the rear microphone held inside the headphones. One ALSA
capture stream remained open while the exact Windows disable branch was
applied before each case. This prevented the loaded experimental module from
reapplying the full enable transaction between cases.

The measured 1 kHz band was:

| Case | Tone mean (dBFS) |
|---|---:|
| Full transaction, before | `-43.9` |
| Widget `0x0f`, registers disabled | `-44.0` |
| Omit `0x0319` | `-44.2` |
| Omit `0x0303` | `-44.1` |
| Omit `0x050a` | `-43.9` |
| Omit `0x031a` | `-44.0` |
| Omit `0x0510` | `-44.0` |
| Full transaction, after | `-43.9` |

The `0.3 dB` total spread is experimental noise, not evidence of a gain
change. None of the five writes has a live signal-level effect after the
widget has already been activated. This supersedes earlier sequential speech
measurements that appeared to associate a level increase with `0x0319`; those
measurements did not use a fixed source and cannot establish causality.

The reset-state experiment removed that limitation. Under the stock module,
the untouched tone measured `-69.3 dBFS`; command `0x0f` changed it to
`-44.5 dBFS`; applying all five writes afterward measured `-44.7 dBFS`.
Together, the hot-state and reset-state results establish that the command is
the microphone fix and the register transaction is not.

## Excluded traffic

Register `0x051b` is written `0x29`, then `0x15`, during codec detection. An
isolated dynamic test produced no level change (`-70.62` versus `-70.78 dBFS`
RMS), so it is intentionally excluded from the fix.

The broad five-command `0x6c` replay has also been narrowed to the single rear
widget command `0x0f`.

## Kernel integration

Source audit shows that a fresh endpoint open sets both `need_prepare` and
interface `need_setup`; suspend does the same. Consequently the current hook is
reached on first prepare, after close/reopen, and on the first userspace prepare
after suspend or reset-resume. It is not called for every ALSA trigger.

The candidate uses `snd_usb_select_mode_quirk()` during endpoint preparation.
This path may sleep and already returns an error, so a negative or short vendor
transfer aborts PCM prepare and leaves the endpoint marked for retry. The hook
runs under the device mutex and a runtime-PM/disconnect reference, while the
streaming interface is at alternate setting zero, before clock/rate setup and
final interface selection. Command `0x0f` was accepted by the live device at
alternate setting zero.

The reduced module was loaded before reauthorizing the reset USB device. On
the first ALSA prepare it recovered the fixed tone to `-43.9 dBFS`, while all
five private registers remained at their reset values. One hundred short rear
open/prepare/close cycles completed without a USB/audio warning; the post-test
tone measured `-43.7 dBFS`. Twenty front-capture opens and all four playback
PCMs also completed without warnings. A PipeWire capture through the ordinary
desktop route measured `-43.6 dBFS`.

Ordinary non-target endpoint testing changed register `0x0319` from `0x54` to
`0x4c` without affecting the fix. This is further evidence that the separate
register routine describes broader device state and should not be copied into
the rear-widget quirk.

With USB runtime power policy set to `auto`, the composite device entered
`runtime_status=suspended`. The next rear capture resumed it and measured the
fixed tone at `-44.0 dBFS`, with no kernel USB/audio warning. This confirms the
prepare hook reapplies the command correctly after runtime resume.

A whole-system deep suspend caused the host to reset USB device `1-10` during
resume. The first rear capture afterward measured `-44.7 dBFS` with no new
USB/audio warning. A final concurrent test ran rear capture (`-44.8 dBFS`),
front capture, and a motherboard playback PCM together without warnings.

The patch is restricted to:

- USB ID `26ce:0a08`;
- capture direction; and
- endpoint `0x83`.

## Windows Microphone Boost

The public ALC4080-family datasheet documents analog microphone boost values of
`0`, `+10`, `+20`, and `+30 dB`. Windows exposes a second `Microphone Boost`
slider with exactly those values in addition to its ordinary microphone volume
slider.

A host `usbmon1` capture was taken while endpoint `0x83` streamed continuously
and only the boost slider was changed in this order:

```text
+20 dB -> 0 dB -> +10 dB -> +20 dB -> +30 dB -> +20 dB
```

No USB control, bulk, or interrupt transaction occurred for any transition.
After stream setup, the device emitted only isochronous capture data on endpoint
`0x83`. This rules out a live device-register implementation of the slider.

The matching ASRock extension INF associates the `26ce:0a08` capture endpoints
with Realtek capture Audio Processing Objects (APOs), including volume and
inverse-volume mode effects. The installed `RtkMicUUn64.dll`:

- registers for Windows `APO_NOTIFICATION_TYPE_MICROPHONE_BOOST` changes;
- contains `CRtkRecVolMFX` and `CRtkRecInvVolMFX` capture effects; and
- constructs a two-channel, 48 kHz DSP identified as
  `SoundBooster EMQF MB ver. 6.09c (2017.03.22)`.

Together, the dynamic bus trace and static package evidence show with high
confidence that this Windows slider is host-side software gain. A Linux
equivalent belongs in PipeWire, EasyEffects, or an ALSA `softvol`, not in this
kernel hardware quirk.

The Realtek Audio Control application also exposes a `Microphone Boost`
stepper, alongside Noise Suppression and Beam Forming toggles. A second,
separate capture repeated the same boost sequence through that application.
After enumeration, all 66,567 device packets were isochronous endpoint `0x83`
submissions/completions; there were zero control, vendor, bulk, or interrupt
transfers. The Realtek application is therefore another frontend for the same
host-side boost property, not a separate codec control.

Capture metadata:

```text
filename: analog-gain-session.pcapng (excluded from Git)
duration: 178.004601 seconds
packets: 66974
sha256: fae4e35e1c0763d243251064b942549fe8180aa4eb9ffc4d7dca282bd8381c40
```

Realtek Audio Control capture metadata:

```text
filename: realtek-gain-session.pcapng (excluded from Git)
duration: 135.156289 seconds
packets: 67004
sha256: 9812f34f160e8ef533a7dd8c638287ae7a19d093a399d18008caf76354d7bbfb
```

## Codec analog gain: still open

Rear-widget activation recovers about 27–30 dB, but that does not prove command
`0x0f` is the adjustable boost control. It is currently classified as path
activation that may cause firmware to apply a configured/default analog gain.

The actual codec preamp remains undocumented and is not exposed in the USB Audio
Class topology. Only after a distinct hardware encoding is identified and
replicated should Linux expose an ALSA `Mic Boost` control.

This preamp question is separate from acceptance of a device-specific path
enable quirk. Upstream needs observable semantics and a justified lifecycle;
it does not require invented symbolic names for private registers.

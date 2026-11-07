# Capture workflow

Raw captures are deliberately excluded from Git. They may contain unrelated USB
traffic, recorded voice data, or proprietary driver payloads.

For a Windows USBPcap capture intended to identify analog microphone gain:

1. Pass the complete ALC4082 USB device to Windows.
2. Start USBPcap on the controller containing the codec.
3. Leave the microphone streaming continuously.
4. Hold each boost setting for at least five seconds in this order:
   `0 dB`, `+10 dB`, `+20 dB`, `+30 dB`, `0 dB`.
5. Create a timing marker at every transition, for example by briefly toggling
   capture mute or writing down the wall-clock time.
6. Stop capture without unplugging the device.

Useful Wireshark filters:

```text
usb.transfer_type == 2 &&
(usb.bmRequestType == 0x40 || usb.bmRequestType == 0xc0)
```

```text
usb.transfer_type == 2 &&
(usb.bmRequestType == 0x21 || usb.bmRequestType == 0xa1)
```

Example field extraction:

```sh
tshark -r capture.pcap \
  -Y 'usb.transfer_type == 2' \
  -T fields \
  -e frame.number -e frame.time_relative \
  -e usb.bmRequestType -e usb.setup.bRequest \
  -e usb.setup.wValue -e usb.setup.wIndex \
  -e usb.setup.wLength -e usb.data_fragment
```

## 2026-08-22 result

The sequence above was captured on the host with the rear microphone streaming.
Changing Windows `Microphone Boost` produced no USB transaction. Static analysis
of the device's Windows package shows that the capture endpoint is associated
with Realtek volume APOs and a software `SoundBooster` DSP. See
[`../docs/reverse-engineering.md`](../docs/reverse-engineering.md) for the
evidence and conclusion.

The same sequence was then repeated through the `Microphone Boost` stepper in
Realtek Audio Control. That second test also produced no non-isochronous device
traffic after enumeration, confirming that the application mirrors the same
host-side boost property.

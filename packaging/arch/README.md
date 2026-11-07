# Arch/CachyOS package

This package installs the patched Linux 7.2.7 USB audio source under
`/usr/src/snd-usb-audio-alc4082-7.2.7`. DKMS builds `snd-usb-audio.ko` for
each installed CachyOS kernel with matching headers. It installs the result
under that kernel's `updates/dkms` directory. DKMS saves the stock module and
restores it when you remove the DKMS package.

Install the kernel, its headers, and DKMS. Then build and install this package:

```sh
sudo pacman -Syu linux-cachyos linux-cachyos-headers dkms
makepkg -sf
sudo pacman -U snd-usb-audio-alc4082-cachyos-7.2.7-1-x86_64.pkg.tar.zst
```

The Arch DKMS hook builds the module during package installation and after
future kernel updates. Check the result before you reboot:

```sh
dkms status snd-usb-audio-alc4082
modinfo -n snd_usb_audio
```

The module path must include `updates/dkms` for the new kernel. Reboot to
load the override. The package replaces the earlier package with the same name
and removes its fixed-version module.

DKMS removes the manual build step, but it does not keep the bundled USB audio
source current. A later kernel can change an API or fix a defect in this source.
Check the DKMS build result after each kernel update. Update the source snapshot
and patch when the build fails or when upstream changes require a new snapshot.
An accepted upstream kernel fix removes the need for this package.

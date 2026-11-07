# Upstream readiness

This checklist distinguishes facts needed to justify the kernel quirk from
questions that are interesting but do not block it. A patch is ready to send
only when every **required** row is either proven or explicitly resolved by an
upstream maintainer.

## Acceptance matrix

| Question | Required | Current evidence | Status / next evidence |
|---|---:|---|---|
| Is the affected hardware identified narrowly enough? | yes | USB ID `26ce:0a08` is shared by the ASRock X870 Steel Legend WiFi and Z790 PG-ITX/TB4, both sold as ALC4082 boards. The Windows hardware table maps this exact ID to class `0xb4`, variant `0x80`, without a DMI condition. The patch also checks rear-capture endpoint `0x83`. | **Strongly proven for the USB identity.** Test the second known board if available; do not add other IDs without equivalent evidence. |
| Is the vendor transport understood? | yes | Windows traffic and the driver agree on widget request `0x6c`, device recipient, index `1`, one-byte command `0x0f`. | **Proven.** Preserve a sanitized transfer transcript and hashes of excluded captures. |
| Is widget command `0x0f` necessary and sufficient? | yes | Under the stock module after USB reset, it raised a fixed 1 kHz signal from `-69.3` to `-44.5 dBFS`. The subsequent five-register transaction measured `-44.7 dBFS`. An earlier fresh-bind test measured a 26.45 dB recovery. | **Proven for the reported symptom.** |
| Are the five register updates part of this fix? | yes | Hot-state exact-disable/omission cases were within `0.3 dB`. In the reset-state stock-module test, `0x0f` restored 24.8 dB before the transaction; the transaction added no signal. | **Resolved: no.** Exclude all five writes from the kernel patch. Their separate vendor power/state role remains documented. |
| Is the command safe to repeat? | yes | It is a device-type assignment in the Windows driver. The final module completed 100 rear open/prepare/close cycles without USB/audio warnings; tone level changed only `0.2 dB`. | **Proven for repeated stream use.** |
| Is the command run at the right Linux lifecycle point? | yes | The candidate uses `snd_usb_select_mode_quirk()` during endpoint preparation. It fixed the first prepare after USB reauthorization, recovered `-44.0 dBFS` after verified runtime suspend, and recovered `-44.7 dBFS` after deep system suspend reset the USB device. | **Proven locally.** USB reset/rebind, close/reopen, runtime suspend, and system suspend pass. |
| Can a control-transfer failure reach userspace? | yes | The candidate uses an error-returning preparation hook. Negative and short transfers fail prepare, and `need_prepare` remains set so a later prepare retries the whole idempotent transaction. | **Proven by source audit; fault injection remains desirable.** |
| Are concurrent control operations serialized? | yes | Endpoint preparation calls the hook while holding `chip->mutex` and a runtime-PM/disconnect reference. Standard mixer controls use different UAC requests. | **Proven for in-kernel callers.** The quirk is one synchronous control transfer under the device mutex. |
| Could the quirk alter another capture/playback path? | yes | It is scoped to capture endpoint `0x83`, and static analysis identifies `0x0f` as assignment of that rear-mic widget. Twenty front-capture opens, all four playback PCMs, normal PipeWire capture, and simultaneous rear/front capture plus playback passed. | **Proven locally.** |
| Does stream close require an inverse command? | yes | No inverse widget command was observed. Windows treats `0x0f` as device-type assignment, while its separate register routine owns the enable/disable power transition. | **Resolved for the candidate:** do not invent an inverse command or import the unrelated register disable branch. Confirm through lifecycle tests. |
| Does the patch apply and build on the submission base? | yes | It applies to Linux 7.2.7 and builds against CachyOS 7.2.7-1 headers with Clang 22. The command-only candidate passed live tests on an earlier kernel. Strict checkpatch reports only the absent `Signed-off-by`. | **Proven for Linux 7.2.7.** Refresh the patch and repeat the build before upstream submission. |
| Is the commit message independently reviewable? | yes | Current message states the symptom, exact hardware scope, measured effect, command, placement, retry behavior, and completed regression tests. Detailed provenance and capture hashes belong in the cover letter or supporting report rather than the permanent commit message. | **Structurally ready.** Update after remaining lifecycle tests; add the author's `Signed-off-by` only when approved for submission. |
| Is the interoperability provenance suitable for publication? | yes | Behavior was independently recovered from bus traffic, black-box experiments, and static analysis. Proprietary binaries and raw captures are excluded. | **Partly documented.** Keep only factual protocol descriptions and hashes; do not publish vendor code or copied decompilation. |

## Non-blocking questions

These questions should not be represented as facts in the kernel patch, but
their unresolved status does not make the device-specific interoperability
quirk invalid:

- The five-register routine's private bit names and alternative orderings. The
  entire routine is now outside the kernel fix.
- Whether the public ALC4080-family pin preamp is fixed by firmware or has a
  separately reachable runtime control on this USB implementation.
- Whether Windows' host-side `Microphone Boost` APO should have a Linux
  userspace equivalent.
- Whether different ASRock or non-ASRock ALC4082 USB IDs share this sequence.

For upstream review, observable semantics are sufficient: which hardware is
affected, which state transition is missing, how the vendor implementation
performs that transition, why the Linux placement is safe, and what regression
testing was completed. Inventing names for private registers would reduce the
quality of the submission.

## Required test matrix

Each signal-level comparison should use the same microphone placement, input
level, sample format, mixer settings, duration, and RMS calculation. A hardware
loopback or fixed tone source is preferable to speech.

| Initial state | Action | Result | Status |
|---|---|---|---|
| USB reset, stock module | Capture untouched, send `0x0f`, then apply registers | `-69.3` → `-44.5` → `-44.7 dBFS` | **complete** |
| Prior initialized state | Exact disable and five omission cases | All tone levels within `0.3 dB` | **complete** |
| USB reset, command-only module | First rear prepare | `-43.9 dBFS`; registers unchanged | **complete** |
| Enabled state | 100 rear open/prepare/close cycles | `-43.7 dBFS` afterward; no warnings | **complete** |
| Enabled state | Runtime autosuspend/resume | Verified suspend; resumed capture measured `-44.0 dBFS` | **complete** |
| Enabled state | System suspend/resume | Deep resume reset USB; next capture measured `-44.7 dBFS` | **complete** |
| Candidate loaded | Authorized USB reset/rebind | First prepare restored capture | **complete** |
| Enabled state | Front capture and every playback PCM | 20 front opens and four playback PCMs passed | **complete** |
| Normal desktop | PipeWire rear capture | `-43.6 dBFS`; expected sources/sinks present | **complete** |
| Enabled state | Simultaneous rear/front capture and playback | Rear tone `-44.8 dBFS`; no warnings | **complete** |
| Transfer failure or disconnect | Prepare rear capture | Source audit returns errno and preserves retry | **source-proven** |

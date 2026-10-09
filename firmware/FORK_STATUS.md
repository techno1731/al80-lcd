# Fork status

What this fork changes over upstream v36, and what has been proven. "Built" means it compiles and
its hardware-free tests pass. Nothing below is verified on a keyboard until the last column says so.

| Area | Change | Built | On hardware |
|---|---|---|---|
| Modes | Mac and Windows/Linux modes switched with Fn+A / Fn+S, each with its own USB identity (Apple in Mac mode, factory otherwise) | yes | no |
| Modes | Stock Fn layer restored in Windows/Linux mode: media row, GUI lock, factory reset (hold), side bar brightness | yes | no |
| Flashing | Bootloader entry from a held key (function key + Right Shift, 3 s) and, in development builds, over raw HID | yes | raw HID entry confirmed: build 3 flashed with no hands on the keyboard (9 Oct 2026); held key not yet tried |
| Boot | Stored settings from another firmware are reset once on first boot | yes | yes, 9 Oct 2026 |
| Hubs | Enumerates and flashes through a monitor KVM hub (BenQ RD28UG) | yes | yes, build 2 on 9 Oct 2026 |
| Mac | Fn/Globe key reported over USB (Apple IDs, AppleVendor Top Case usage) | yes | yes: macOS honours it, Fn+Left moves to line start (owner confirmed on build 7, 9 Oct 2026) |
| Mac | Fn is only reported when a host-visible key is pressed with it, so Fn + local keys is not a Globe tap | yes | a lone tap acts as the Globe key (confirmed); Fn + local keys not triggering it is not yet confirmed |
| Mac | Function row mapped in firmware on every connection; Fn navigation substituted over radio | yes | function row confirmed on 2.4G; over USB macOS did not map it by itself, so build 7 does it in firmware (not yet confirmed) |
| Mac | Dictation, Do Not Disturb and Spotlight keycodes (CUSTOM 32-34) | yes | no |
| Radio | Link status read as the vendor does (0 = up); upstream had it inverted | yes | yes: module reported link up on 2.4G, 9 Oct 2026 |
| Radio | 2.4G dongle: typing works with the cable in, selected by key (owner confirmed 9 Oct 2026) | yes | yes, including with the cable out and from a cold start on the switch (9 Oct 2026); media keys not yet confirmed |
| Radio | Bluetooth: pairs and connects from the switch position plus the slot key (owner confirmed on build 3, 9 Oct 2026) | yes | yes; typing, media keys and knob over Bluetooth not yet itemised |
| Radio | Media and system keys sent to the module (`55 03 <id> <usage>`) | yes | function row confirmed on 2.4G (9 Oct 2026); the knob did nothing on build 2, fixed in build 3 by holding each step, not yet flashed |
| Radio | Mouse reports sent to the module | yes | no |
| Radio | Lock LED state from the module drives Caps Lock indication | yes | no |
| Radio | Mode switch read on C14 (BT) and C15 (2.4G) at a host-less boot | yes | C15 goes low on the dongle position (confirmed); BT position and host-less boot not yet tested |
| Radio | Raw HID (LCD, clock, diagnostics) keeps flowing over USB while typing goes by radio | yes | no (works in USB mode) |
| Battery | Cell measured against the internal reference; charging and full detected from plug pin | yes | reads 4140 mV, 99%, charging on USB (9 Oct 2026); discharge not yet observed |
| LCD | Homepage shows the real connection type, OS type and battery state | yes | no |
| LCD | Fn+7 makes the keyboard alternate the home page and the GIF page by itself (20 s / 10 s) | yes | no |
| LCD tool | `tooling/al80_screen.py`: GIF and picture upload, view switch, clock, under either USB identity | yes | upload of an 8-frame GIF fully acknowledged (9 Oct 2026); picture not yet seen by eye |
| LCD | Several GIFs uploaded as one medley; the key backlight takes each scene's dominant colour in step with it (raw HID 0x50 stores the scene list) | yes | no |
| Lighting | Managed solid lighting: white on the home page, the scene colour on the GIF page; right Command + 6 turns LCD and all lights off | yes | no |
| Power | Lights and LCD off when the USB host sleeps, the 2.4G host sleeps, or after 5 idle minutes on battery | yes | no |

## Known gaps

- 2.4G with the cable out was dead on upstream. The switch read above is the first fix to try; the
  cause is not yet confirmed.
- The battery reading jumps between charging and discharging (97% on USB, 23% on battery a minute later). Build 3 rate-limits the shown level and logs raw samples (0x4F) so the curve can be checked against real use.
- The MCU does not enter stop mode, so battery life will still trail the stock firmware.
- Reports are sent whether or not the module says the link is up (`AL80_WL_OPTIMISTIC`), until the
  corrected link status is confirmed on hardware.

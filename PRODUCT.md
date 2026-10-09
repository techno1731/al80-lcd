# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

Recorded as `web` because the schema has no embedded value. The real surface is a 96x160 pixel
colour LCD on a keyboard, RGB565, viewed at arm's length while typing.

## Users

One owner, a developer who works on two Macs through a BenQ RD28UG monitor KVM and plans to add a
Linux mini PC. The keyboard is used wired and wirelessly (Bluetooth and the 2.4G dongle), and both
Macs run Karabiner from an earlier keyboard. Macros are planned.

## Product Purpose

Make the YUNZII AL80 behave like a Magic Keyboard on macOS while keeping everything the stock
firmware could do on every host, then turn its screen into a glanceable command screen. Success:
no capability lost against stock, switchable Mac and Windows/Linux modes, and a screen that shows
the keyboard's state with some life in it.

## Operating Context

- The screen is a separate module with its own closed firmware. The keyboard can only send it
  commands: status values for its built-in home page, still pictures, stored GIFs and view switches.
- Animation exists only in stored GIFs: a 96x64 strip on the built-in home page (42 frames), a
  full-screen GIF page (160 frames) and a boot animation (64 frames), each at one global frame rate.
- A still picture is stored by the module when pushed, so frequent redraws have a wear cost that
  has not been measured.
- The keyboard must keep working through the monitor's KVM hub, by cable and by dongle.

## Capabilities and Constraints

Readouts the owner wants: time, date, battery level, charging state, connection type (cable, 2.4G,
Bluetooth) with the right symbol, Bluetooth slot, Mac or Windows mode, Caps Lock, typing stats.
Open to other interesting readouts.

Wanted: an animated element like the eyes on the stock design, and a home page of our own design.

Undecided: whether the main screen is the built-in home page with our animation, a custom still
page redrawn on change, or a rotation of both. The owner asked whether a fully custom animated
home page is possible; replacing the screen module's firmware has not been investigated.

## Evidence on Hand

- `screen/tide-glow.gif`: the eyes the owner chose on 9 Oct 2026 from four rendered directions (Tide Glow: eyes made of drifting motes of blue light). On the home page strip. The earlier amber `lookout.gif` was rejected.
- `tooling/al80_screen.py`: uploads GIFs and pictures; an 8-frame GIF was confirmed animating on
  the device by the owner.
- No photo of the built-in home page yet, so its arrangement is unknown to the design.

## Product Principles

1. Add capabilities, never remove them.
2. Behaviour lives in the keyboard, so it works on any host and connection.
3. Say what is verified on hardware and what is only built.
4. The screen is read in a glance: state first, decoration second.

# iii-neotrellis

A fork of [iii](https://codeberg.org/tehn/iii) by tehn and Ezra Buchla, used as the iii layer of the [neotrellis-monome](https://github.com/jonwaterschoot/neotrellis-monome) firmware. For the original iii, issues and contributions, go to [tehn's repo](https://codeberg.org/tehn/iii).


## Changes from iii v1.1.2

The `combined/pr10-11-12` branch adds three pull requests by carlosedp to tehn's iii:

- [#10](https://codeberg.org/tehn/iii/pulls/10): running out of memory is a Lua error instead of a reboot, plus memory-safety fixes.
- [#11](https://codeberg.org/tehn/iii/pulls/11): scripts load and upload through flash in small pieces, so scripts over 32 KB work.
- [#12](https://codeberg.org/tehn/iii/pulls/12): a 4 KB serial buffer that drains when full, so downloads of large scripts and boot messages get through.

Other branches:

- `main`: tehn's iii v1.1.2, unchanged.
- `fix/streaming-upload` and `fix/25kb-streaming`: my earlier upload fixes, replaced by #11.
- `device_reset`: tehn's branch, already part of v1.1.2.


## About iii

A lua interface for rp2040-based devices, creating a USB-serial REPL, USB-MIDI interface, and mechanism for managing a small onboard file system. Lua scripts can be written and stored to flash, and the runtime can be interacted with in realtime.

Designed primarily for [monome](https://monome.org) devices but is easily usable on any rp2040.

tehn's iii is a sub-repository shared by several monome devices:

- [grid one](https://codeberg.org/tehn/iii-grid-one)
- [grid zero](https://codeberg.org/tehn/iii-grid-zero)
- [grid 2022](https://codeberg.org/tehn/iii-grid-2022)
- [arc](https://codeberg.org/tehn/iii-arc)

To experiment with iii on a Pico devboard, see [blink](https://codeberg.org/tehn/iii-blink).


## Discussion

For questions about this fork or the NeoTrellis firmware, open an [issue on neotrellis-monome](https://github.com/jonwaterschoot/neotrellis-monome/issues). For iii itself, see [tehn's repo](https://codeberg.org/tehn/iii).


## Credits

iii is a [monome](https://monome.org) initiative. Designed and coded primarily by [tehn](https://nnnnnnnn.co) and [Ezra Buchla](https://ezrabuchla.com). The changes in this fork are carlosedp's pull requests listed above.


## License

GPLv3, as iii.

tehn's original readme asks anyone using this code to get in touch, to see what gets made.

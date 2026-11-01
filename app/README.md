# Phone-side integration

The pack is a USB device; the phone is the host and runs the user interface.
No driver is needed. Protocol: `docs/06-usb-interface-and-protocol.md`;
reference implementation `tools/mrp.py`.

## Android (primary target)

Requirements: Android 8+, USB host mode (every phone with USB-C has it).

1. **Discovery**: declare `android.hardware.usb.host` and a
   `USB_DEVICE_ATTACHED` intent filter with `device_filter.xml` listing the
   VID/PID so the app launches when the pack is plugged in.
2. **Permission**: `UsbManager.requestPermission()` once; Android remembers
   it per device serial when the user ticks the box.
3. **Open**: `openDevice()`, find interface 0 (class 0xFF), `claimInterface`,
   locate the bulk IN and OUT endpoints.
4. **I/O threads**: one reader thread calling `bulkTransfer(epIn, 4096,
   timeout)` in a loop feeding the MRP decoder; one writer thread draining a
   queue. Use `UsbRequest` with `queue()` for lower latency if needed.
5. **Audio out (RX)**: `AudioTrack` 16 kHz, mono, PCM16, `MODE_STREAM`,
   `PERFORMANCE_MODE_LOW_LATENCY`, usage `VOICE_COMMUNICATION` or `MEDIA`;
   keep a 2-3 frame jitter buffer; play through the phone speaker (or BT
   headset if the user prefers, routing is the app's choice).
6. **Audio in (TX)**: `AudioRecord` 16 kHz mono PCM16, source
   `VOICE_COMMUNICATION` (phone AEC/NS are harmless for half-duplex); on PTT
   press send `PTT(1, ttl=2000)`, then one `AUDIO_TX` every 20 ms; release
   sends `PTT(0)`. Audio frames count as keep-alive, so no extra timer is
   needed while talking; send `KEEPALIVE` every 500 ms if audio is paused.
7. **Status**: parse `STATUS` at 1 Hz: battery and charging of the pack, PD
   role (show "pack is charging the phone" / "phone is powering the pack"),
   RSSI bar, squelch indicator, PTT inhibit reasons, TX time-out countdown.
8. **Foreground service** with a notification while connected so the link
   survives screen-off; acquire a partial wake lock while receiving.
9. **Settings** map directly to `SET_CHANNEL`, `SET_AUDIO`, `SET_POWER`.
   Default: channel 1, no tone, squelch 4, low power, sourcing enabled,
   reserve 20 %.
10. **Charging UX**: when the pack becomes the source the phone shows its
    normal charging indicator; the app only displays the pack's view.

Edge cases:

- Phone refuses `DR_Swap` (rare): the pack stays DFP and the app never sees
  it; the pack's LED blinks a code and the pack still charges the phone.
  Re-plugging usually helps (the other Try.* outcome).
- User enables "reverse wireless/wired charging" on the phone: the pack
  rejects the swap while above reserve (docs/03).
- The app must not keep PTT keyed when backgrounded or when the screen
  locks; release on `onPause`.

## iOS

No vendor USB access for third-party apps. Use the UAC profile: the pack
enumerates as a USB headset (16 kHz mono in/out); the user talks with the
pack's own PTT button and channel memory, iOS routes call/voice-memo audio
to the headset if the user picks it. Power sharing works the same (PD is
below the OS). A future MFi path could expose MRP.

## Desktop / bench

`python3 tools/mrp.py --usb status --follow` (pyusb) or over the debug UART
with `--serial`. WinUSB binds automatically through the MS OS 2.0 descriptor;
on Linux add a udev rule for the VID/PID.

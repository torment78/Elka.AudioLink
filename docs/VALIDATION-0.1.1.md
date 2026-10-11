# AudioLink 0.1.1 preview validation

Validated locally on Windows x64, 11 October 2026, using Visual Studio 2026/v145.

## Receiver update

- Closing the window leaves the tray icon and receiver running. A test TLS sender
  continued delivering more than 100 additional VBAN packets after window close.
- The tray menu reopens settings, refreshes/reconnects reception, and shuts down.
  Refresh resumed VBAN output; shutdown removed the icon and released the TCP port.
- The stereo sample-peak meter measured **30 updates in 500 ms**, approximately
  60 Hz. Brief peaks survive subsequent quiet blocks before the next display frame.
- Left/right levels, dBFS conversion, immediate attack, release, disconnect clearing,
  minimize/restore, and pausing display refresh while hidden were checked.
- The receiver window and tray menu were rendered and inspected.

## Regression checks

All four CTest integration checks passed: receiver tray/meter, transport/local VBAN,
OBS monitoring mix, and loading the actual OBS plugin.

The receiver retains the existing pairing and settings format. Protocol version 1
is unchanged; the 0.1.0 OBS sender remains compatible.

These checks used isolated settings, a local TLS sender, and a test VBAN endpoint.
They do not establish audible reception on the other PC or resolve its previously
reported VoiceMeeter incoming-stream selection issue. No live VoiceMeeter settings,
installed OBS files, network adapters, or firewall rules were changed.

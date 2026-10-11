# Receiver 0.2.0 preview validation

Validated on Windows x64 on 11 October 2026, with Visual Studio 2026/v145.

- All six CTest suites passed: callback routing, receiver UI/tray/meter, TLS/local
  VBAN, OBS monitor mix, actual OBS plugin, and published 0.1.1 plugin with Callback.
- Callback tests cover every available destination in Standard, Banana and Potato,
  multiple destinations, stereo polarity, preservation of existing and B/surround
  channels, conflicting owners, failed start/release, edition changes, disconnect
  flushing, rate conversion in both 44.1/48 kHz directions and switching to VBAN.
- The UI test checks eight/five strips, five/three A buses, arbitrary multi-select,
  clearing selections after edition changes and leaving output unchanged until Apply.
- The unchanged published 0.1.1 OBS DLL also ran under OBS 32.2.1 and delivered
  **432 matching callback blocks** through the new receiver, with **zero VBAN sends**.
- A read-only probe through the installed native API detected **Potato 3.1.3.0**.
  It did not register an audio callback or change VoiceMeeter settings.
- Isolated receiver installation and portable extraction matched every staged
  payload file by SHA-256. The installed receiver rendered using only its packaged
  dependencies. The isolated test uninstall removed its executable.
- The receiver's VBAN and Callback layouts were rendered and inspected. The installer
  is **unsigned**, as disclosed in the release notes. Previous 0.1.1 assets retain
  their original hashes.

The callback engine in the automated audio tests is a test double of the documented
VoiceMeeter ABI. These checks do not establish audible playback through a real
VoiceMeeter callback or the other PC's final device routing. Live listening and a
long-running network session remain target-PC acceptance checks. No installed OBS
plugin, live VoiceMeeter routing, adapter or firewall rule was changed.

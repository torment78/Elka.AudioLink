# AudioLink 0.2.1 preview validation

Built on Windows x64 with Visual Studio 2026/v145 on 11 October 2026.

All seven CTest suites pass: callback routing, receiver UI/tray/meter, TLS/local
VBAN, OBS monitor mix, actual OBS plugin with VBAN, actual OBS plugin with Callback,
and the unchanged published 0.1.1 plugin with Callback.

The new audio checks measure -6 dB attenuation, saved -12 dB startup gain, live mute,
unity restoration and gradual gain transitions. They verify that the return alone
changes, that other bus channels and existing audio pass through unchanged, and
that the bus fader has no effect on input strips. TLS tests exercise one A5 return
at 44.1/48 kHz, including the receiver's live gain setter and output-mode ACKs.

UI checks cover mutually exclusive destinations, clearing old multi-selection
settings, immediate level persistence, hiding the fader for input strips, available
Banana/Potato channels and leaving destinations unchanged until Apply. The callback
panel is rendered and inspected. The receiver retains its tray and meter checks.

`tools/Test-ReceiverPackage.ps1` verifies both installers and the combined ZIP. It
compares every payload file by SHA-256, launches the receiver with packaged
dependencies, uninstalls the isolated receiver, and checks that the sender installer
and uninstaller preserve unrelated OBS fixture files. No real OBS installation is
changed by that test. Installers remain unsigned, as disclosed in the release notes.

Callback audio tests use a simulated VoiceMeeter engine. They do not establish
audible playback through the target PC's live VoiceMeeter callback or device routing.
Listening and extended network use remain target-PC acceptance checks.

# AudioLink 0.1.0 validation — 2026-10-11

Built Windows x64 with Visual Studio 2026 / v145 (MSVC 19.51), Windows SDK
10.0.26100.0. No compiler errors in the final build.

## Automated results

All three CTest integration tests passed:

| Test | Evidence |
| --- | --- |
| Transport and local VBAN | Real encrypted TCP, correct/wrong certificate pins and tokens, protected identity persistence, discovery, stream framing validation, bit-preserving PCM24, loopback source address, consecutive local sequence numbers, acknowledgements, reconnect and idle cleanup |
| OBS monitor mix | Real libobs 31.1.1 sources, both monitoring modes, fader gain, Monitor Off, silence, dynamic source lifetime and shutdown |
| Actual OBS plugin | Loads the distributed DLL in an isolated OBS 31.1.1 host, operates the Tools dialog, saves settings, and transfers monitored samples through TLS into a local VBAN sink |

An additional actual-DLL test passed against the official cached OBS 32.2.1
runtime with Qt 6.11.1. The final installer-extracted plugin delivered 865 packets
matching the expected 0.2 sample level from a 0.4 source with a 0.5 fader. The
receiver reported 893 received packets and 882 local sends during that run.

The OBS 32 test uses normalized module paths, matching OBS's real module loader.
TLS uses linked Mbed TLS and does not depend on Qt's optional TLS plugins.

## Packaging checks

- Both Inno Setup installers completed successfully in isolated repository folders.
- `/ISOLATED=1` suppressed application registration and shortcuts during testing.
- The extracted plugin DLL and receiver EXE matched the tested build's SHA-256 hashes.
- The extracted receiver launched with only Windows system directories in PATH,
  generated an isolated identity, rendered its window, and exited successfully.
- The extracted plugin passed the OBS 32 monitored-audio integration test.
- Sender and receiver previews were rendered and inspected. Both panels scroll on
  smaller screens so setup and diagnostics remain reachable.
- Final distributable checksums are in `dist/SHA256SUMS.txt`.

## Still requires the target PC

The other operator was unavailable. No claim is made that their original receive
failure is fixed. The local sink proves valid VBAN packet generation, not
VoiceMeeter reception. Remaining checks: a real two-PC connection, discovery and
manual fallback on that network, matching incoming VoiceMeeter stream settings,
meter activity, listening, reconnect after a network interruption, and extended
operation under normal audio/network load.

The installed OBS plugin, VoiceMeeter configuration, firewall and network adapters
were not changed. The existing VBAN Stream repository was not edited.

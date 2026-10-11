# Development

## Build

Use Visual Studio 2026, MSVC v145, Windows SDK 10.0.26100.0, x64. Open this repository
as a CMake folder in Visual Studio and select `windows-x64`, or use `tools/Build.ps1`.

The SDK is cached in `.deps` and is not committed. Run `tools/Prepare-Sdk.ps1` to
copy the already-built OBS 31.1.1 SDK and pinned `2025-07-11` dependencies from the
local VBAN Stream checkout. `-SourceSdk` accepts another prepared SDK directory.
The copy is independent: no build or runtime binary refers back to that checkout.
For a fresh machine, prepare an equivalent OBS SDK using the official OBS plugin
template and the pinned versions listed below before invoking this script.

Pinned dependencies: OBS 31.1.1, obs-deps 2025-07-11, Qt 6.8.3 from the matching OBS
Qt dependency package. `tests/frontend-stub.hpp` uses the pinned private frontend
interface only inside isolated test hosts; the distributed plugin uses public APIs.

```powershell
./tools/Prepare-Sdk.ps1
./tools/Build.ps1
./tools/Test.ps1
./tools/Package.ps1
./tools/Test-ReceiverPackage.ps1
```

## Architecture

The OBS capture callback copies samples and scalar metadata to preallocated SPSC
rings. It performs no network, disk, UI, blocking wait, or allocation work.
The reused monitor worker resamples and aligns source timelines, follows monitoring
state, source gain, filters and the pinned OBS mute semantics, and emits 128-frame
stereo float blocks. Its tunnel sink writes another bounded SPSC ring.

A separate Qt network thread converts to PCM24 VBAN packets and frames them inside
TLS/TCP. The receiver validates those packets, buffers them, rewrites the local
stream name and packet counter, and paces them to `127.0.0.1`. One receiver accepts
one sender at a time. Callback mode instead converts PCM to VoiceMeeter's rate on
the worker and fans the stereo return out to selected local strips or A buses.

The protocol has a 4-byte big-endian length, a one-byte type, and a bounded payload
(maximum 4096 bytes). Types: hello, ready, audio, ack, error. JSON hello carries
version 1 and the pairing token inside TLS; audio carries one validated stereo
128-frame PCM24 VBAN packet. ACK distinguishes received audio from local UDP sends.
TCP fragmentation and coalescing are handled independently of audio boundaries.

The receiver holds no more than the selected jitter buffer plus 100 ms of packets;
overflow discards the oldest packets to the target depth. Sender application and
TLS write queues are bounded. A two-second missing audio/ACK deadline terminates
the session and discards its queue. TCP kernel buffers and retransmission still
add delay; no fixed maximum end-to-end latency is promised.

Loopback output uses a precise timer and small bounded bursts. A bounded pacing
adjustment follows average queue depth to limit sender/receiver clock drift.
The target PC still needs a listening and extended-load check.

## VoiceMeeter callback output

The ABI and channel maps follow the official
[SDK revision 02a1abf](https://github.com/vburel2018/Voicemeeter-SDK/tree/02a1abf15358ddb33e588cd31c43576a065f81ac).
The worker loads the installed x64 Remote DLL by absolute path, logs in and checks
the running edition. No routing setters or VoiceMeeter launch calls are used.
Input insert mode (1) processes pre-strip audio; output insert mode (2) processes
pre-master buses. Only one category is registered. B channels pass through.

The realtime callback copies every existing channel, then adds stereo to the first
two channels of selected destinations. It performs no allocation, Qt calls,
logging, networking, blocking lock or wait. A preallocated SPSC float ring separates
it from the worker. Generation-based flushes avoid cross-thread cursor writes.
Disconnects, rate changes and overflow re-prime with a short fade. The ring has a
fixed 262,144-frame capacity; the consumer trims to target when latency exceeds
target plus 100 ms. Target depth is at least two engine blocks.

SpeexDSP resamples on the worker, including equal-rate streams for independent
device clocks. A bounded 0.2% rate correction follows queue depth every half-second
of input. API polling is worker-only. Engine changes request an explicit Refresh;
changed editions require detection and re-selection. Busy registrations are never
stopped or unregistered. Failed owned unregister retains the disabled pass-through
context and DLL, preventing a callback into freed memory.

Protocol version 1 is unchanged. Older OBS senders remain compatible; their status
still mentions VBAN, so the receiver shows its real mode and callback frame count.
Callback mode opens no local VBAN socket. Discovery UDP remains independent.
`tools/Package.ps1` now produces only the 0.2.0 receiver installer and portable ZIP.
OBS users keep the published 0.1.1 plugin. Older release artifacts are preserved.

## Pairing

The receiver creates a random RSA-2048 identity and 192-bit random token. A pairing
code combines the SHA-256 certificate fingerprint and token. The sender pins that
certificate before sending the token or audio. Self-signed and hostname errors are
allowed only for the exact pinned certificate; other validation failures remain
fatal. A wrong token is rejected. There is no discovery-based automatic trust.
Windows DPAPI protects saved credentials for the current user. Certificate/key
creation and TLS 1.2 or newer use statically linked Mbed TLS 3.6.4. Qt Network
provides the underlying TCP socket; no Qt TLS backend is required. This avoids
depending on private Qt plugin compatibility across OBS versions.

Discovery is a small versioned nonce-bearing broadcast request on UDP 45992 with a
unicast JSON reply. Results are size-limited, expire, and never replace the pinned
identity. It is not mDNS and does not install a service. No WAN relay is provided.

## Validation

`voicemeeter-callback-routing` verifies every strip/A destination in all editions,
multi-selection, stereo polarity, preservation of existing/surround/B channels,
conflicting owners, failed start/release, changed editions, flushes, real TLS with
legacy framing, 44.1/48 kHz conversion in both directions, no local UDP in Callback,
acknowledgements and switching back to VBAN. It simulates the API boundary without
launching or changing live VoiceMeeter. `callback-tests --probe` only queries the
installed API's running edition/version. The UI test also checks edition-dependent
options and unapplied tab edits. Live callback listening remains unverified.

When `ELKA_LEGACY_PLUGIN` points to a published 0.1.1 DLL, CTest also runs that actual
old plugin through the new callback receiver. `tools/Test-Obs32.ps1` accepts
`-PluginPath` and `-Callback` to run the same check under a supplied OBS 32 runtime.
See [0.2.0 validation](VALIDATION-0.2.0.md) for the recorded results and limits.

`receiver-tray-and-meter` exercises the real receiver window on the Windows Qt
platform with isolated settings and a TLS sender. It checks continuing VBAN audio
after closing to the tray, reconnect from the tray, reopen/minimize behavior,
shutdown releasing the listener, stereo peak capture and dBFS ballistics, and a
measured display cadence near 60 Hz. Its window is not shown on the desktop.

The receiver uses QSystemTrayIcon for the Windows notification area and a
16,666,667 ns QChronoTimer for the visible meter. A separate 250 ms status timer
updates text and the tray tooltip. The network worker accumulates per-channel
peaks across every decoded block until consumed by the display; disconnect/stop
clears pending peaks. These operations are outside OBS capture callbacks.
The notification-area behavior follows the
[Qt system tray API](https://doc.qt.io/qt-6/qsystemtrayicon.html).

`transport-and-local-vban` covers fragmented/coalesced/invalid frames, protected
identity persistence, LAN discovery, certificate pinning, token rejection, actual
TLS transfer, PCM preservation, local source address, packet sequence, receiver
acknowledgements, reconnect, and idle cleanup.

`obs-monitor-mix` runs real libobs capture with both monitoring modes, Monitor Off,
gain, dynamic source creation/removal, silence, and clean shutdown.

`actual-obs-plugin` loads the actual plugin DLL, opens its Tools panel, applies
settings, and routes a real monitored OBS source through TLS to a local VBAN sink.
`plugin-smoke32` uses the OBS 32 frontend interface when that optional SDK is present.

None of these tests modifies the installed OBS plugin, VoiceMeeter settings,
firewall, adapters, or the remote PC. User acceptance still requires hearing the
return and checking the real VoiceMeeter meter on that PC.

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
one sender at a time. Multiple monitor-return destinations are outside this preview.

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

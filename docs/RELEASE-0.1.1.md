Elka AudioLink sends the OBS monitor/headphone mix to another Windows PC over an
encrypted TCP connection. Its small receiver delivers the audio as local VBAN to
VoiceMeeter. This is the first public preview for testing.

## Download the part for each PC

- **OBS PC:** `elka-audiolink-obs-0.1.1-windows-x64-setup.exe`
- **VoiceMeeter PC:** `elka-audiolink-receiver-0.1.1-windows-x64-setup.exe`
- **Manual installation:** `elka-audiolink-0.1.1-windows-x64-portable.zip` contains both parts.
- `SHA256SUMS-v0.1.1.txt` contains checksums for these three downloads.

Windows 10/11 x64 is required. The sending PC needs OBS 31.1.1 or later; OBS is not
needed on the receiving PC. Install VoiceMeeter separately. These preview
installers are unsigned. The Source code archives are for developers.

## Start here

**[Full setup guide](https://github.com/torment78/Elka.AudioLink#readme)** ·
**[First test and troubleshooting](https://github.com/torment78/Elka.AudioLink/blob/main/docs/TESTING.md)**

1. Open the receiver on the VoiceMeeter PC and copy its pairing code privately to
   the OBS operator.
2. Open **Tools → Elka AudioLink** in OBS. Discover the receiver or enter its LAN
   address and TCP port `45991`, paste the code, and connect.
3. Enable monitoring for the OBS sources to send.
4. In VoiceMeeter, select the detected `AudioLink` incoming stream, switch it on,
   and choose its destination input strip. Defaults: sender `127.0.0.1`, UDP `6980`.
5. Route that strip to your listening output and check both its meter and the sound.

## Included

- Encrypted TCP audio with receiver pairing, reconnect, LAN discovery, and manual addressing.
- Stereo PCM24 audio from the OBS monitor mix.
- Close-to-tray reception with **Open settings**, **Refresh / reconnect**, and **Shut down**.
- Responsive stereo dBFS meter at approximately 60 Hz while visible, with peak hold.
- Receiver counters for incoming packets, local VBAN sends, UDP errors, and buffering.

Users of the local 0.1.0 preview can update only the receiver for the tray and meter
changes. Exit the old receiver before installing; pairing and settings are retained.
The 0.1.0 OBS sender remains compatible.

## Test status and limits

The local operator reports that it is working. Four automated integration tests
passed, covering transport, OBS monitoring, the actual plugin, and tray/meter
lifecycle. The plugin was also exercised with OBS 32.2.1, and the receiver installer
was checked in an isolated folder. Further testing on other PCs is welcome.

Windows networking and firewall rules still apply. Discovery is local-subnet only;
there is no internet relay or automatic NAT traversal. TCP retransmissions can add
latency. Successful local VBAN sends do not by themselves confirm VoiceMeeter
playback. See the test guide to check the complete route.

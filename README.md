# Elka AudioLink

Send the OBS headphone / monitor mix to another Windows PC over an encrypted TCP
connection. A small receiver turns it into a local VBAN stream for VoiceMeeter.

**0.1.0 preview · Windows x64 · OBS 31.1.1 or later**

AudioLink uses the same source monitoring choices as VBAN Stream's return:
**Monitor Only** and **Monitor and Output** contribute; **Monitor Off** does not.
Source filters and fader gains are included. Stereo PCM 24-bit is preserved at the
OBS sample rate. There is no lossy codec, virtual cable, or virtual network adapter.

## On the receiving PC

1. Install **Elka AudioLink Receiver** and open it. OBS is not required on this PC.
2. Leave the TCP port at **45991** and the receive buffer at **40 ms** initially.
3. In VoiceMeeter, turn VBAN on and enable an incoming stream:
   - Stream name: **AudioLink** (or the name chosen in the receiver).
   - Sender IP: **127.0.0.1**.
   - UDP port: **6980** (or the receiver's local VBAN port).
4. Choose the desired VoiceMeeter destination strip.
5. Click **Copy pairing code**. Send that code privately to the OBS operator or
   copy it directly if you operate both PCs. It grants permission to send audio.
6. Keep the helper running. Closing it stops reception.

The receiver deliberately sends to loopback only. This avoids the external UDP
receive path. Whether VoiceMeeter accepts that local stream on the problem PC
still needs testing when its operator returns.

## On the OBS PC

1. Close OBS. Install **Elka AudioLink OBS Plugin** into the OBS root folder
   containing `bin/64bit/obs64.exe`, then reopen OBS.
2. Open **Tools → Elka AudioLink**.
3. Select a nearby receiver and click **Use selected receiver**, or enter its
   IP address / hostname and TCP port manually.
4. Paste the receiver's pairing code, then click **Connect and save**.
5. In OBS **Advanced Audio Properties**, select **Monitor Only** or
   **Monitor and Output** for each source to include. A headphone / monitoring
   button in another OBS interface works when it changes that same OBS setting.
6. Optionally enable **Connect automatically when OBS opens** and save.

Pairing is remembered for your Windows user. The sender reconnects after network
interruptions. Discovery can locate the same paired receiver after a LAN address
change; a discovered name alone never authorizes an audio connection.

## Discovery and connectivity

- Audio uses **TCP 45991** by default, protected by TLS.
- Optional discovery uses small **UDP 45992** broadcast queries and replies. Audio
  does not travel in these discovery packets. No Bonjour or mDNS service is needed.
- Discovery works within a broadcast subnet. Across a reachable VPN, VLAN, or
  routed network, use manual addressing when broadcasts do not cross the router.
- Windows Firewall must allow the receiving helper's selected TCP port. If Windows
  asks, allow it on the trusted network you intend to use. Discovery additionally
  needs UDP 45992. The installers do not disable or modify the firewall.
- There is no hosted relay, NAT traversal service, or automatic internet port
  forwarding. A directly reachable address is required.

## Reading the status

The sender distinguishes connecting, authenticated, and streaming with receiver
acknowledgements. The helper shows received audio level, received packet count,
local VBAN sends, UDP errors, buffer depth, discarded audio, and underruns.

**Local VBAN sent means Windows accepted a local UDP packet. It does not confirm
VoiceMeeter received or played it.** Check the VoiceMeeter input meter and listen.

Start with the default buffers. Increase the receive buffer if short network
interruptions cause underruns; increase the OBS monitor buffer if source callbacks
arrive late. More buffering adds delay. TCP can stall while retransmitting lost
data. AudioLink drops excess queued audio and reconnects stalled sessions instead
of allowing an unlimited backlog.

In OBS 32.2 and later on Windows, AudioLink follows the return engine's monitoring
mute behavior: the normal program mute may leave monitored audio audible. Use
**Monitor Off** to remove a source reliably. In OBS 31.1.1, program mute is honored.

Do not route the received return back into the monitored OBS mix; that creates an
audio feedback loop.

## Files and privacy

Receiver settings live under `%LOCALAPPDATA%/ElkaSoft/Elka AudioLink`.
Sender settings live in OBS's `plugin_config/elka-audiolink` folder. Pairing secrets
and the receiver private key are protected with Windows DPAPI for the current user.
No accounts, cloud service, recordings, or telemetry are used.

This is a preview. Local integration tests can prove the transport and plugin
behavior, but cannot prove that it fixes another PC's VoiceMeeter receive problem.

Build and engineering details: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
License and reused components: [NOTICE.md](NOTICE.md).

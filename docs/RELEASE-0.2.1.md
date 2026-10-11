# AudioLink 0.2.1 preview — one channel, bus return fader, both installers

The release contains the **Receiver installer**, **OBS sender installer**, and a
**combined portable ZIP** with both apps in separate folders. Use the receiver on
the VoiceMeeter PC and the OBS sender on the OBS PC. OBS is not required on the receiver PC.

## Changes

- Callback now says **Pick one channel only** and uses radio buttons. Choose one
  input strip OR one A bus. Potato offers In 1–In 8 / A1–A5; Banana In 1–In 5 / A1–A3.
- **Bus return level** adjusts only AudioLink's return, from 0 dB down to mute.
  Changes apply live, with a smooth transition and no reconnect. Other bus audio
  remains unchanged. The setting is saved and retained by tray Refresh.
- The matching OBS sender reports Callback output and its processed frame count
  instead of displaying VBAN-only status for that mode.
- Existing OBS senders still work. Pairing and the network protocol are unchanged.

## Updating

Choose **Shut down** in the receiver tray menu before installing. Close OBS before
running the sender installer and select the OBS root folder containing `bin/64bit/obs64.exe`.
Pairing and VBAN settings are retained. If several callback destinations were saved
in 0.2.0, select one channel again and press **Start / apply**. The bus fader works
immediately; changes to the destination or output mode still require Start / apply.

The installers are unsigned. SHA-256 checksums are included. Automated checks cover
single-channel routing, return-only gain and mute, settings migration, transport,
OBS sender compatibility, and isolated installer payloads. Live callback listening
and final device routing still need confirmation on the target PC.

[Full setup](https://github.com/torment78/Elka.AudioLink/blob/v0.2.1/README.md)
· [Callback and fader guide](https://github.com/torment78/Elka.AudioLink/blob/v0.2.1/docs/CALLBACK.md)

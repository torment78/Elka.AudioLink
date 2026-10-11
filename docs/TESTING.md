# First test and troubleshooting

For direct output in receiver 0.2.0, start with the [Callback guide](CALLBACK.md).
The VBAN checks below remain available through the VBAN tab.

Use the [receiver 0.2.0](https://github.com/torment78/Elka.AudioLink/releases/tag/v0.2.0)
and [OBS plugin 0.1.1](https://github.com/torment78/Elka.AudioLink/releases/tag/v0.1.1) downloads
and follow the [setup guide](../README.md) first. The OBS PC needs the plugin; the
VoiceMeeter PC needs the receiver. Both sides use the same receiver pairing code.

## A short first test

1. Start VoiceMeeter and AudioLink Receiver on the receiving PC. Keep its window
   open for this test and copy its pairing code privately to the OBS operator.
2. In OBS, open **Tools → Elka AudioLink**, select the receiver or enter its LAN
   IP/hostname, paste the pairing code, and click **Connect and save**.
3. Play a known audio source. Set that source to **Monitor Only** or **Monitor and
   Output** in OBS Advanced Audio Properties. Start at a comfortable listening
   level and avoid sending the return back into the same monitored mix.
4. Check that the receiver is streaming, its packet counts increase, and its left
   and right meters move. A silent channel in the source can leave one meter flat.
5. In VoiceMeeter's VBAN window, right-click **Stream Name** or **IP Address From**
   on an incoming row and select the detected **AudioLink** stream. Turn the row
   on. The default sender IP is **127.0.0.1**, and the VBAN UDP port is **6980**.
6. Select the destination input strip. Confirm its meter moves, then route it to
   your configured listening output. Confirm that you can hear the audio clearly.
7. Close the receiver window. Audio should continue. Click its system tray icon to
   reopen it; the meter should resume updating.
8. From the tray menu, choose **Refresh / reconnect**. Expect a brief interruption
   and automatic reconnection from OBS. Finally, **Shut down** should stop audio
   and remove the receiver's tray icon.

## Identify the point where audio stops

| What you see | What to check |
| --- | --- |
| Receiver is not listed in discovery | Enter its LAN IP or hostname and TCP port manually. Discovery broadcasts stay within a subnet. |
| OBS cannot connect or keeps reconnecting | Keep the receiver open, check TCP port 45991 on both sides, and check that Windows Firewall allows the receiver on the intended network. Re-copy the pairing code if the receiver identity changed. |
| Connection is established but the receiver meters stay flat | Check the source has audio and its monitoring mode is enabled in OBS. **Monitor Off** contributes no audio. Check source gain and filters. |
| Receiver meters move but VoiceMeeter only says a stream was detected | Select the detected stream in an incoming row, enable the row, and match the stream name, sender IP, and VBAN port exactly. The receiver's meter is before VoiceMeeter. |
| VoiceMeeter's destination strip meter moves but you hear nothing | Check the strip's mute/fader, its A/B routing, and the selected VoiceMeeter output device. |
| Dropouts or increasing buffer underruns | Start with a stable wired LAN if available. Increase the receiver buffer modestly from 40 ms and test again; extra buffering adds delay. |
| Receiver disappeared after closing its window | Look under Windows' hidden tray icons, or launch it again to restore settings. It continues receiving until **Shut down**. |

## Address and port reference

| Setting | Default / meaning |
| --- | --- |
| OBS receiver address | The **other PC's LAN IP or hostname**, for example `192.168.1.50`. |
| AudioLink TCP port | `45991`, matching in sender and receiver. |
| Discovery UDP port | `45992`; optional for manual connections. |
| VoiceMeeter incoming sender IP | `127.0.0.1`, because AudioLink Receiver is on this same PC. |
| Local VBAN UDP port | `6980`, matching in receiver and VoiceMeeter. |
| VBAN stream name | `AudioLink`, or the exact name configured in the receiver. |

Use the full address `127.0.0.1`. The TCP transport port and local VBAN UDP port
serve different purposes and should not be substituted for each other.

## Useful feedback

If something fails, include the OBS and VoiceMeeter versions, AudioLink version,
wired/Wi-Fi connection, the receiver state, whether its meters move, and which
VoiceMeeter input receives the stream. Note UDP errors and underruns, and whether
the failure happens before or after closing to the tray or reconnecting.

Screenshots of the receiver status and VoiceMeeter incoming row are useful. Keep
pairing codes and `identity.bin` private. Report reproducible problems through
[GitHub Issues](https://github.com/torment78/Elka.AudioLink/issues).

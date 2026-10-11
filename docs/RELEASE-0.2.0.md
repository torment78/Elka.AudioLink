# Receiver 0.2.0 preview — direct VoiceMeeter output

This update is for the **Windows receiver only**. Keep your existing AudioLink OBS
plugin, pairing code and TCP port. OBS plugins 0.1.0 and 0.1.1 remain compatible.

- New **VBAN / Callback** output tabs.
- Callback detects the running VoiceMeeter edition. Choose **Input strips** or
  **Output buses (A)**, then tick one, several or all destinations.
- Potato: In 1–In 8 / A1–A5. Banana: In 1–In 5 / A1–A3.
- Standard: In 1–In 3 / its shared A bus. B buses are excluded.
- Return mixes with existing audio. Different OBS/VoiceMeeter rates are converted.
- Close-to-tray, tray refresh, saved pairing and the 60 Hz meter remain.

Choose **Shut down** in the old receiver's tray menu before installing. Run the
setup EXE, or extract the portable ZIP and keep its receiver folder together.
Pairing and saved VBAN settings are retained. Start VoiceMeeter, choose Callback,
Detect, select destinations and press Start / apply. Check VoiceMeeter's meters
and headphones/speakers. After an engine restart use tray Refresh / reconnect.

Selecting the tab alone does not apply it. An occupied callback is reported with
its owner; AudioLink does not take it over. Switch back to VBAN with Start / apply.

The installer is unsigned. SHA-256 checksums accompany the downloads. Automated
tests cover simulated callbacks, actual TLS transport, rate conversion, all channel
mappings, preservation, mode switching, UI/tray behavior and OBS/VBAN regressions.
Live callback listening on the target VoiceMeeter PC still needs confirmation.

Setup and troubleshooting: [Callback guide](https://github.com/torment78/Elka.AudioLink/blob/v0.2.0/docs/CALLBACK.md).
New OBS installations can use the [existing 0.1.1 plugin](https://github.com/torment78/Elka.AudioLink/releases/tag/v0.1.1).

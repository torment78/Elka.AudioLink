# Direct VoiceMeeter output

Receiver 0.2.0 adds a **Callback** output tab alongside **VBAN**. This changes only
the Windows receiver. Keep the existing OBS plugin, TCP port and pairing code.

## Setup

1. Start VoiceMeeter, then open AudioLink Receiver and select **Callback**.
2. Click **Detect** if needed. The running edition and version appear above the controls.
3. Choose **Input strips** or **Output buses (A)**, then tick the destinations.
   **Select all** selects the current category; **Clear** removes its selections.
4. Press **Start / apply**. Selecting tabs or ticking boxes alone does not change
   the running output. Only one category and one output mode run at a time.
5. Play a monitored OBS source. Check the receiver's **CALLBACK** active-mode label,
   moving received-audio meter and increasing callback frame count. Then check
   VoiceMeeter's meters and listen on the intended headphones or speakers.

| Running edition | Input strips | Direct output buses |
| --- | --- | --- |
| Banana | In 1–In 5 | A1–A3 |
| Potato | In 1–In 8 | A1–A5 |
| Standard | In 1–In 3 | A (shared by A1 / A2) |

Input numbers follow VoiceMeeter's mixer order, including its virtual input strips.
The same stereo return is **added** to every selected destination. Existing audio
is preserved. Input strips keep their normal faders, mute and routing. Direct bus
audio enters before the bus master processing and requires that bus's physical
output to be configured. B buses are not direct destinations; a selected input
strip can still route to a B bus through VoiceMeeter's own controls.

The return uses the first stereo pair of each destination. Other surround channels
are preserved. Selecting multiple strips routed to the same bus adds multiple
copies of the return and can make it louder. Start with one destination to verify it.

AudioLink converts the OBS sample rate to VoiceMeeter's rate automatically. Start
with a **40 ms** receive buffer. Close the window to keep receiving in the tray.
The incoming meter remains a receive-level meter, not VoiceMeeter's output level.

## If there is no audio

- **VoiceMeeter not detected:** start it, then click Detect. AudioLink does not launch
  or reconfigure VoiceMeeter. If unavailable at receiver startup, open settings and
  apply again after VoiceMeeter starts.
- **Callback already in use:** the message names the owning application. Release
  the chosen input/output callback there, or use VBAN. AudioLink does not take it over.
- **Waiting for callbacks:** check VoiceMeeter's audio engine and A1 device configuration.
- **Audio engine changed:** use **Start / apply** or tray **Refresh / reconnect**.
- **Changed from Banana to Potato or back:** click Detect, reselect destinations and apply.
  Previous channel selections are cleared because the channel maps differ.
- **Frame counter increases, but no sound:** check the selected strip/bus mute,
  fader, routing, and physical output. The counter proves callback processing only.
- **Clicks or underruns:** increase the receive buffer and check the network.
  Callback removes the local UDP audio handoff; normal TCP networking still applies.

To return to the previous behavior, choose the VBAN tab and press Start / apply.
The saved VBAN stream name and port remain. Switching modes briefly reconnects OBS.
Older OBS plugin status text still says VBAN; the receiver shows the actual mode.

This is a preview. Automated tests cover the callback layout, mixed stereo audio,
sample-rate conversion and real encrypted transport using a simulated VoiceMeeter
engine. A live listening check on the target machine is still required.

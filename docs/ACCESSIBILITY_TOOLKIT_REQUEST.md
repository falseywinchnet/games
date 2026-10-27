# Request to GUI.Forms: spoken, keyboard-complete play for blind users

PlaySuite is adding a mode for blind players. It starts with `--visual-explain-mode` or a settings checkbox. In that mode every menu, popup and game is fully playable from the keyboard. The game speaks what is on screen, and short sounds carry position and urgency. Most of the work is game-specific and stays in PlaySuite. The parts below are reusable by any GUI.Forms application, so we are asking for them in the toolkit.

These requests were checked against `c90eacf`. Much of the foundation already exists, so this asks for extensions, not a new system:

- The semantic tree: `SemanticDescriptor`, `semantic_virtual_children`, `perform_semantic_action`.
- Tab traversal: `tab_stop`, `move_focus`, focus scopes.
- The focus-visible cue.
- The macOS, Windows and Linux accessibility bridges.

## 1. Speech service

There is no way for an application to speak today. We need an owner-held service:

```cpp
gf::Speech speech;                         // one per application
speech.say("Seven of hearts", gf::Speech::polite);
speech.say("Granny is close", gf::Speech::urgent);   // jumps the queue
speech.interrupt();                        // stop now, clear the queue
speech.repeat_last();
speech.set_rate(1.0 .. 4.0);               // multiple of the platform default
gf::on(speech.finished(), owner, &Owner::spoke);
```

**Queue rules**
- `polite` waits its turn. `assertive` replaces whatever polite speech is playing. `urgent` interrupts anything.
- A new `say` with the same channel key replaces the queued one. Example: moving the cursor quickly speaks only where it lands.
- Rate matters. Experienced blind users listen at 300+ words per minute, so the rate is a user setting that persists across sessions.

**Backends**
- macOS: AVSpeechSynthesizer.
- Windows: SAPI 5 (`ISpVoice`), which is present on Windows 10 without extra packages.
- Linux: speech-dispatcher via `libspeechd`, loaded at run time so its absence degrades instead of failing to start.
- A headless backend that records every utterance with its priority. Tests need it (see 6).

**Route choice.** When a screen reader is running, many users prefer hearing their own voice and rate. `Speech` should therefore offer a second route: an announcement through the platform bridge. That means `NSAccessibilityAnnouncementRequestedNotification` on macOS, `UiaRaiseNotificationEvent` on Windows, and an AT-SPI announcement on Linux. The application picks a route; the API stays the same.

## 2. Assistive-technology detection

Blind users can't find a settings checkbox. The application must be able to offer the mode out loud on first launch, so we need:

```cpp
bool host.assistive_technology_active();   // VoiceOver, Narrator/NVDA/JAWS, Orca
gf::on(host.assistive_technology_changed(), owner, &Owner::changed);
```

Possible sources on each platform:
- macOS: `NSWorkspace.voiceOverEnabled` and its key-value notification.
- Windows: `SPI_GETSCREENREADER` plus `WM_SETTINGCHANGE`.
- Linux: the AT-SPI bus `IsEnabled` / `ScreenReaderEnabled` property.

## 3. Arrow-key movement inside a container

Tab already moves between tab stops. Blind and keyboard users also need arrows to move within a group, as in a toolbar or radio group: the shelf's game boxes, the capsule's icons, a row of settings. We ask for a container option:

```cpp
container.set_arrow_navigation(gf::ArrowNavigation::linear);   // Left/Right or Up/Down in order
container.set_arrow_navigation(gf::ArrowNavigation::grid);     // nearest neighbour in the pressed direction
```

The group behaves as one Tab stop and remembers its last focused child. Arrows wrap or stop as set by an option. This is the WinForms/UIA convention, so platform screen readers already expect it.

## 4. A cursor over virtual items and plain text

Games draw cards, nodes and pieces that are not controls, and help pages hold text that is not focusable. `semantic_virtual_children` already describes such items. What is missing is moving to them and reading them:

- **Focus on a virtual child.**
  - `Window` tracks a focused virtual id under a control: `focus_virtual(control, stable_id)` and `focused_virtual()`.
  - The bridges report it as the accessibility focus, so VoiceOver's cursor follows too.
  - The control receives keys while a virtual child is focused and decides how arrows move between its items. A card game moves between piles; Untangle moves between nodes.
- **A reading cursor.**
  - `gf::ReadingCursor` walks the semantic tree in reading order, inside the active focus scope.
  - It covers static text and descriptions as well as focusable items.
  - Operations: `next`, `previous`, `first`, `into` (first child), `out` (parent), and `current()` returning the node.
  - Help uses it directly: Down steps through each string, and Left/Right collapse and expand via the existing `expand`/`collapse` actions.
  - Inside a popup it stops at the popup's edge.
- **Grid position on a node.** Add optional `row`, `column`, `row_count` and `column_count` fields to `SemanticNode`. Games can then say "column three of seven" without each one inventing a convention. These map to the UIA GridItem and AX row/column index properties.

## 5. Keyboard parity for every stock control

Every interactive stock control should be fully operable from the keyboard, with a test proving it:

- Space and Enter activate buttons and check boxes.
- Arrows and Page Up/Down move sliders.
- Left/Right collapse and expand expandable sections and tree items.
- Arrows move within menus, list boxes and combo boxes; Escape closes popups.
- Every popup and dialog opens a focus scope with a sensible starting focus and returns focus when it closes.

Please list any control that does not meet this, so we know what to avoid until it does.

## 6. Testing

- The headless host records utterances (see 1) and exposes the semantic snapshot (already present).
- A test can then press keys and assert both where focus landed and what was spoken. PlaySuite will add a contract test for each game, requiring that its explain key describes everything visible. The kit check will require that test of new games.

## 7. Audio: per-voice pan and pitch

Speech is too slow for real-time play. Switchbox needs a tone for each light, and Mowing needs Granny's distance and direction as sound. Short sounds need a stereo position and a pitch, set when they start and adjustable while they play:

```cpp
mixer.play(clip, {.gain = 0.8, .pan = -0.4, .rate = 1.25});
voice.set_pan(0.2);
```

`pan` runs from -1 to 1 using a constant-power law. `rate` is a resampling rate, so it changes pitch. If the audio engine already supports this through a path we missed, please point us at it.

## What stays in PlaySuite

- The mode switch and the first-launch prompt.
- The keys shared by all games: E explains everything, a key describes what's around the selection, plus keys for repeat and stop.
- The words each game uses, its cursors and sounds, the Mowing narrator, and Eggy's and the parrots' lines.
- The note that Rock Stack waits for a common haptic standard.

## Order of need

1. Speech (1) and detection (2): nothing can be heard without them.
2. Arrow movement (3) and keyboard parity (5): the shell's menu, capsule, settings and help.
3. Virtual focus and the reading cursor (4): the card games and help.
4. Pan and pitch (7): Switchbox and Mowing, which come last on our side anyway.

Please reply with each item's status: present, planned, or a reason it belongs elsewhere.

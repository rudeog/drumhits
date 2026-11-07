# Drum Sampler – Design Document

## 1. Overview

A drum sampler written in C++ using JUCE. It ships as VST3 and standalone on Windows, Linux and macOS. AU comes from JUCE for macOS at little cost. The C++ standard is whatever the targeted JUCE version supports. Non-UI code (engine and controller) is covered by unit tests. It is drum-specific: sounds are always played one-shot, never looped.

### 1.0 Terminology

- **Choke**: the choke group value (1–8, or 0 for none) that a drum triggers when it plays.
- **Choked by**: the set of choke group values that cause a drum to be choked.

### 1.1 Layers

| Layer | Language | Thread | Role |
|---|---|---|---|
| Engine | Pure C++ | Audio thread | Plays sounds. Lock-free. |
| Controller | Pure C++ | UI thread | Owns waves and patch state. Configures the engine through a queue. |
| UI | C++ / JUCE | UI thread | Talks only to the controller, never the engine. |

### 1.2 Threading

- There are two threads: the audio thread and the UI thread.
- The engine must be lock-free.
- Controller → engine communication goes through a thread-safe (lock-free) message queue.
- Host/hardware MIDI does not go through the queue. It is handed to the engine directly on the audio thread (see 2.4).
- Engine → controller communication is limited to wave reference counts (see 2.5), plus the last received MIDI note for MIDI learn (see 2.4).
- All engine state is touched only from the audio thread. Engine methods, including note handling, must never be called from the UI thread.

### 1.2.1 Control message queue

- One-way, lock-free, single-producer/single-consumer (SPSC) ring buffer. The UI thread writes and the audio thread reads. It can be built on `juce::AbstractFifo` or a hand-written ring buffer with atomic indices.
- **Single writer.** All pushes come from the UI thread. If the host can call the controller from another thread (e.g. state restore), the controller must marshal onto the UI thread or serialize pushes with a mutex that the audio thread never takes.
- **Messages** are fixed-size plain structs (tagged union/variant) with no heap allocation and no strings. Waves travel as pointers under the in-flight reference rule (2.5).
- **Capacity** is fixed at construction and sized for a full patch load (16 drums, up to 256 hits, several messages each). A few thousand slots is the starting point.
- **Full queue.** `push` returns false and never blocks the audio thread. Configuration messages are never silently dropped, and the controller retries (e.g. on the next timer tick). Audition notes may be dropped. A dropped message that carries a wave must release its in-flight reference.
- **Ordering.** Order is preserved, and the engine drains the queue completely at the start of each block (2.4). Draining is bounded and allocation-free, and messages are consumed in place.
- **Patch-load atomicity.** Initially accepted as non-atomic: the audio thread may run between messages and briefly play a partially configured kit (see open items).

### 1.3 Limits

| Item | Limit |
|---|---|
| Drums | 16 (fixed; drums cannot be created or deleted) |
| Hits per drum | 16 |
| Stereo outputs | 4 |
| Global polyphony | 64 hard limit |
| Choke groups | 8 (values 1–8; 0 = none) |
| Waves | No limit |
| MIDI velocity | 0–127 |

---

## 2. Engine

### 2.1 Object hierarchy

- **Engine**: main container.
- **Output**: the engine has four stereo outputs, individually enabled by the host.
- **Drum**: one triggerable instrument (fixed set of 16). Holds 0 or more hits.
- **Hit**: one sound within a drum. References a single wave and belongs to a single drum.
- **Wave**: a reference-counted, immutable block of sample data. It is created and owned by the controller, and the engine only holds references. Many hits may reference the same wave.

### 2.2 Properties

**Engine**
- Drums (16)

**Wave** (created by the controller, used by the engine)
- Atomic reference count
- Reference to raw sample data (immutable once created)

**Drum**
- Hits (up to 16 slots)
- Output assignment (dropdown, 1–4)
- Play mode: `regular` or `random round robin` (RRR)
- Note assignment: a single MIDI note. Multiple drums may share a note.
- Choke: the choke group value this drum triggers when it plays, 0–8 (0 = does not choke anything)
- Choked by: the set of choke group values that choke this drum (multiple)
- Max polyphony: 0 = no max (default)
- Volume (dB)
- Pan

**Hit**
- Name (string, a controller-level property; see 3.1)
- Envelope: fade-in length (offset from start) and fade-out length (offset from end), measured in wave samples (unpitched). The fade-in ramps from start to start + fade-in, and the fade-out ramps from end − fade-out to end. Because they are offsets, moving start or end moves the fades with it. Constraint: fade-in + fade-out ≤ end − start. Pitch doesn't affect the stored values, and the audible fade time is the length divided by the playback rate. The UI may display them as unpitched time in the wave (ms). If a change to start or end makes the fades invalid, they are clamped: fade-in is limited to the span first, then fade-out to the remainder.
- Volume (dB)
- Pitch: semitones + cents, combined into one playback-rate calculation (affects playback rate only)
- Associated wave
- Start position and end position in the wave
- Velocity range to respond to (min–max, 0–127)
- Velocity sensitivity (integer percent, 0–100, default 100): how much the note velocity scales the hit's volume. The hit's gain is multiplied by (1 − s) + s · velocity / 127, where s is the sensitivity as a fraction. At 100% the hit volume is fully scaled by velocity (velocity 127 is unchanged, velocity 64 is about −6 dB, and velocity 0 is silent). At 0% velocity does not change the volume.

### 2.3 Messages to the engine

| Message | Parameters |
|---|---|
| Note on/off (audition only) | MIDI note, velocity. Sent by the controller for UI auditioning (clicking a drum in the grid sends the drum's assigned note, if any). Host MIDI bypasses the queue (see 2.4). Note-off is currently ignored. |
| All notes off (panic) | none. The engine immediately marks every active cursor (drums and preview) for fast-fade deletion, so all sound stops within the fast fade length. Sent by the UI Panic button. Like configuration messages, it is never dropped: the controller retries it if the queue is full. |
| Set drum properties | drum slot, output, play mode, note, choke, choked by, volume, pan, max poly |
| Create hit | drum slot, hit slot |
| Clear hit | drum slot, hit slot |
| Set hit properties | drum slot, hit slot, wave object, envelope, volume, pitch, start pos, end pos, velocity range, velocity sensitivity |
| Play portion of wave (preview) | wave object, start, end, fade-in length, fade-out length (fades measured from start and from end, 0 = none; output defaults to 1; may become configurable). Used by the Wave Pool play button. Like audition notes it may be dropped when the queue is full, and the controller then releases the in-flight wave reference. |
| Audition hit | drum slot, hit slot, velocity. Plays the selected hit with its properties and drum volume, pan and output; bypasses note mapping, velocity selection, round robin, choke and per-drum polyphony. May be dropped when the queue is full. |

Wave Pool preview is independent of drums and hits from the engine's perspective. Hit audition voices are isolated from choke and per-drum polyphony, but share the global voice limit and respond to panic.

The plugin exposes four stereo output buses: output 1 is always enabled, and outputs 2–4 are independently enabled by the host. Each drum can be assigned to any output; a drum routed to a disabled host bus is silent until that bus is enabled.

### 2.4 Runtime behavior

**Per-block processing order.** Everything below runs on the audio thread, called from JUCE `processBlock` (plugin) or the audio callback (standalone):
1. Drain the control message queue completely, so configuration is current before any note is handled.
2. Process the block's MIDI buffer. Each event has a sample offset within the block.
3. Render audio. The block is split at MIDI event offsets, so a note starts at its exact sample offset instead of at the block boundary. Cursors that start mid-block begin rendering from their offset.

Notes arrive from two sources, which both feed the same note-handling function:
- Host or hardware MIDI, passed directly by the audio callback.
- Audition notes from the controller, delivered through the queue. They are handled when the queue is drained, so they start at the beginning of the block.

Only note-on is acted on. Note-off and other MIDI events (CC, pitch bend, all-notes-off) are ignored for now. The only all-notes-off is the controller's panic message (2.3), which is handled when the queue is drained.

**MIDI learn.** The engine records the last received note (and that a note arrived) in a lock-free atomic that the UI thread polls. This is the one engine → controller path beyond wave reference counts.

**Real-time safety.** Note handling is a hot path. It must not allocate, lock or log. Cursors and the candidate collection have fixed capacity.

**Playback cursors.** The engine keeps a set of playback cursors. Each holds:
- a reference to its wave object (reference counted) and its position in the wave,
- its end position,
- envelope, volume, pan, pitch and output,
- enough drum/hit info (e.g. its choke value) to keep working if the originating drum or hit is later modified or cleared.

A cursor keeps playing from its original wave even if the hit is repointed to a new wave, until the cursor finishes.

**Note handling**
1. Find all drums assigned to the note.
2. For each drum, choose the hits to start:
   - `regular`: all hits whose velocity range contains the incoming velocity.
   - `RRR`: of the hits in range, pick one at random from those not yet played. When all have played, clear the played flags.
   - Hits with no valid wave are silently skipped.
3. Gather the triggered hits into a candidate collection.
4. Choke: for each triggered drum with a non-zero choke value, mark any active cursor whose drum is choked by that value for fast-fade deletion.
5. Per-drum max poly: if a drum's max poly is reached (and not 0), mark its oldest cursors for fast-fade deletion. This behaves the same as choking.
6. Global limit: if the 64-voice limit is reached, no new hits are triggered. Voices fading out from a choke still count toward the limit.
7. Add the candidates to the cursors.

A drum can choke its own group (its choke is N and it is choked by N). Setting its max poly to 1 achieves the same effect.

**Random round robin details**

Each drum has one played flag per hit. On every note, RRR considers only the hits whose velocity range contains the incoming velocity (and that have a valid wave). It only reads and clears the played flags of those hits. Hits outside the velocity range are never picked, and their flags are left alone.

Per note:
1. Build the eligible set (in velocity range, valid).
2. If every eligible hit is already flagged as played, clear the flags of the eligible hits only.
3. Pick one at random from the eligible hits not flagged, and flag it.

Consequences:
- A hit is not repeated within its layer until all the layer's hits have played. With two hits in a layer this is strict alternation. At the restart the same hit can play twice in a row (e.g. A, B, A).
- Non-overlapping velocity layers cycle independently. Playing notes in one layer never advances or resets another layer's cycle.
- If velocity ranges overlap, a hit shared by two ranges has a single flag. A play from one velocity therefore counts as played for the other.

Example: a drum in RRR mode with four hits. A and B have range 0–99, C and D have range 100–127.

| Note | Eligible | What happens |
|---|---|---|
| vel 50 | A, B | Picks one at random, say A, and flags it. |
| vel 120 | C, D | Picks one at random, say D, and flags it. A and B are untouched. |
| vel 50 | A, B | A is flagged, so B plays. Both are now flagged. |
| vel 50 | A, B | Both flagged, so A and B's flags are cleared. One is picked at random. D stays flagged. |
| vel 120 | C, D | D is flagged, so C plays. Both are now flagged. |

**Rendering**
- Per block, advance each cursor and compute samples.
- Cursors marked for deletion get a fast fade. When it completes the cursor is removed.
- The fast fade length is a code constant, defaulting to the smallest value that avoids a click.

**Audio quality rules**
- Waves in memory are always converted to the host sample rate at load time.
- On a host sample-rate change, the message thread reloads each wave from its ordered paths at the new rate and rescales hit positions and fades. A successful fallback becomes primary. A wave that cannot be reloaded becomes missing, retains its paths and scaled hit settings, and can be resolved through Search Missing. Shorter files are padded as necessary to preserve hit ranges.
- During a sample-rate change, output is muted and incoming MIDI is discarded until conversion and queued configuration are ready; existing voices are cleared. Reload failures are reported, including when the editor is next opened.
- All waves are stereo internally. Mono files are loaded as stereo with the channel duplicated. Files with more than two channels are not supported (initially).
- Volume is in dB (standard).
- Pitch changes use 4-point cubic (Catmull-Rom) interpolation during playback. Offline sample-rate conversion uses a band-limited Blackman-windowed sinc filter, including anti-alias filtering for downsampling.
- Envelope fades are linear.
- Pan uses a constant-power law with −3 dB at center, applied uniformly to every wave (all waves are stereo internally). With pan p in [−1, 1] and θ = (p + 1)·π/4, the left channel is multiplied by cos θ and the right by sin θ. Center is about −3 dB on both channels, hard left is unity on left and silent on right, and hard right is the reverse. Stereo material at center is therefore 3 dB below file level, which users can compensate for with volume. The law may become configurable later.
- Velocity selects hits (via the hit's velocity range) and scales each hit's volume by the hit's velocity sensitivity (see 2.2). The scaling is linear in amplitude and is applied on top of the hit and drum volumes.

### 2.5 Wave lifetime and reference counting

The mechanics are not final. Current thinking:
- The controller creates a wave with a reference count of 1.
- When a cursor starts playing a wave it atomically increments the count. When the cursor finishes or dies it decrements it.
- To delete or replace a wave, the controller decrements its own count and the wave is flagged for deletion. The controller has a garbage-collect method that frees flagged waves whose reference count has reached 0. It is called periodically (every 10 seconds, set by a code constant) from the UI thread (e.g. a JUCE timer), never from the audio thread.
- The processor's message-thread timer also retries pending configuration messages every 50 ms, including when the editor is closed.
- Trimming: the controller makes a trimmed copy as a new wave object and sends it to the engine for the affected hits. Active cursors keep using the original until done.
- The engine may only increment or decrement the count of waves it was handed (via a hit's wave or a preview message). It never looks up or acquires other waves.
- In-flight messages: when the controller sends a message carrying a wave object (set hit properties, preview), it first takes an extra reference on behalf of the message. The engine releases that reference once it has processed the message (taking its own references as needed, or dropping the message's reference if the message is discarded). This prevents the garbage collector from freeing a wave that is still queued.
- Waves are never modified in place, so the engine can read them without locking.
- The engine must not allocate or free memory on the audio thread.

### 2.6 Slots and robustness

- Drums are addressed by slot (0–15) and hits by slot within a drum (0–15). A slot of −1 means unused or invalid.
- Waves are referenced by object (see 2.5).
- The engine must tolerate references to invalid slots. It may log, but must not fail.
- A hit with no wave simply does not play when triggered.
- Deleting a hit only clears its slot, which can be reused later. There is no compaction in the engine.
---

## 3. Controller

The controller is C++ and may depend on the JUCE core module (e.g. for `ValueTree` and file handling), but has no UI dependency. It may be called from the UI thread. It sends messages to the engine to configure and operate it.

### 3.1 State

- It keeps a copy of engine state (possibly in a more convenient structure), plus data the engine doesn't need:
  - wave names, locations (multiple locations per wave)
  - hit names (defaults to the wave's display name, i.e. the filename or a custom name; can be overridden),
  - drum names.
- The controller's wave list is separate from the engine's. Each controller wave entry references a physical file on disk or is flagged as missing.
  The file on disk is never modified. The in-memory data is treated as static.
- Loading the same file twice shows a message that the wave already exists.
- If a wave cannot be loaded, it is flagged as missing

### 3.2 Wave handling

- Initial format support is WAV. Other formats may come later.
- The engine has no concept of paths. The controller loads waves, converts them to the host sample rate, wraps them in a wave object and uses that object when setting a hit's wave.
- The controller records the paths of the waves. There may be multiple paths per wave, with the first path primary and later paths serving as fallbacks.

### 3.3 Operations

- load patch / serialize (save) patch
- load wave, add/remove wave
- add/remove hit on a drum (drums themselves are fixed)
- duplicate drum: copy all hits and settings from one drum to another empty drum (no hits). The note assignment is copied, and the name is copied with " copy" appended. The target's existing settings are overwritten.
- send note data (auditioning)
- read wave data (for UI rendering)
- get/set any engine, drum or hit property
- duplicate hits
- copy hits to other drums

Every operation that writes files prompts for a path. The default is the DAW project directory if available, otherwise a sensible fallback.

### 3.4 Paths

- Wave paths in the loaded controller are absolute, with the first path as primary.
- A wave may have multiple paths. If the first path fails to load, subsequent paths are tried; a path that loads successfully becomes primary, and the other paths remain as fallbacks.
- When loading a patch file, if relative paths are found, they are relative to the patch directory. Once the patch is loaded they are changed to absolute
  in the loaded controller. This means if the patch is resaved to the same location, unless the user elects to consolidate, they will be saved with absolute
  paths.
- Replacing a wave file in the Wave Pool loads the new audio and updates every hit that references that wave, preserving the wave identity and hit sample positions. If the wave was missing, the new path becomes primary and the prior paths are retained as fallbacks. If it was not missing, the prior paths are discarded. If the replacement is shorter than any hit's start or end position, silence is appended through the highest referenced position.

### 3.5 Patch format

A patch contains enough data to restore controller state and wave references, but never contains wave data. Audio availability depends on the referenced files being present.

**File layout**
1. Header: magic identifier and an integer format version.
2. The serialized tree (e.g. `ValueTree::writeToStream`).

**Tree contents** (names indicative)
- Patch properties: sample rate.
- Waves: one child per controller wave entry, with:
  - a stable wave id that hits reference,
  - display name,
  - the path, along with fallback paths
- Drums: 16 children with output, play mode, note, choke, choked by, volume, pan, max poly and name.
- Hits: children of a drum, in user display order, each with name, wave id, start, end, fade-in and fade-out lengths (samples, offsets from start and end), volume (dB), pitch (semitones + cents), velocity range and velocity sensitivity. Engine slot numbers are not stored. The controller reassigns slots on load.

**Loading**
- Read the header and reject unknown (newer) versions with an error.
- Older versions are migrated on load by upgrading the tree before it is applied.
- The controller loads waves, resolves relative paths (in the case of loading from a patch file) and sends the messages that configure the engine.
- Missing file waves load as missing (see 4.1 Missing-wave dialog) and keep their hit data. Hits that reference them cannot be played or previewed, and their wave display and associated controls are unavailable.

### 3.6 Serialize (Save) Patch Action
This differs from when the user saves a daw project with an instance of the plugin.
The user selects a directory and filename and is given a few yes/no options:
- purge unused (this does the same as the purge unused action)
- consolidate waves into patch subdirectory (this is described below)
Saving is not allowed if any wave files are missing.
A patch file is created or updated with references to the waves. If consolidate was selected, a sibling subdirectory is created in the directory containing the patch file, named from the patch filename without its extension plus `_dh_data`. For example, saving `/my/dir/patchfile.dhpatch` creates `/my/dir/patchfile_dh_data/`. The waves are copied to this directory as-is without modification and referenced from there.
Existing files in the directory (if it pre-existed) are overwritten if they bear the same name. If two waves in the patch would end up with the same name, one
is renamed to avoid collision (numeric suffix which comes before the file extension)
The file references in this case are stored as relative paths in the patch. They are relative to the directory where the patch is saved.
Once a patch is loaded into the controller, these paths are replaced with their absolute path. This means that if the daw project is saved, its saved with
absolute paths.
When consolidating waves, since we are saving the relative paths, we will eliminate any extra fallback paths that were associated with waves.
When saving a patch, we first do any wave copy operation (if consolidating); if purging, only used waves are copied and included in the saved patch. Next we write the patch file.
If copying or writing the patch fails, the save fails and the loaded controller is not changed. Files copied before a failure are left in the destination directory, including any files overwritten during consolidation.
Only after the patch file is written successfully is the loaded controller updated to reflect the new paths (absolute in this case) and, if requested, purged of unused waves.
Here is an example scenario
1. User adds waves to an instance. (The waves have absolute paths in the controller)
2. User saves the daw project (we write the absolute path for each wave)
3. User loads the project on a different OS. The paths are missing. User resolves the missing paths to their location on the new OS. (new paths become primary; original paths are retained as fallbacks)
4. User saves the project and loads it again on the original OS. (the first path is now invalid, it falls back to the second path and loads)
5. User does a save patch action (all paths are preserved as is)
6. User does a save patch action with consolidate (waves are copied, each wave in the patch file now has a single relative path to the new wave file. The controller in memory now points to these new files but keeps an absolute path to each. if user did purge then unused waves are also purged from the instance in memory)


### 3.7 Purge Unused
This action removes unused waves

---

## 4. User Interface

The UI uses JUCE and sits above the controller. It has three tabs: Main, Waves and Settings. A **Panic** button sits at the top right of the whole interface, in the tab bar area and visible on every tab. It sends the all-notes-off message (2.3), which stops all playing cursors.

### 4.1 Main tab

**Top: drum grid** (8 wide × 2 high)
- Click a drum to make it active and audition it: the UI sends the drum's assigned MIDI note (nothing is sent if unassigned). Selecting a different drum selects its first hit, if it has one. Clicking the already active drum keeps the current hit selection. The active drum may have no hits.
- Four visual states: inactive/no hits, inactive/has hits, active/no hits, active/has hits. This could be a border color plus a fill color.

**Middle: drum properties**
- Output assignment
- Drum name (editable)
- Play mode
- Note assignment (with MIDI learn)
- Choke selector (0–8)
- Choked by: a button opening a checkbox list of the choke groups. Each value identifies the drums that trigger that group.
- Max poly (0 = no max)
- Right-clicking a drum opens its drum actions menu. Currently:
  - **Duplicate drum** submenu: lists the valid targets, i.e. all other drums with 0 hits (the source drum is never listed, even if it has no hits). The item is disabled if there are no targets. Choosing a target copies the hits and settings (see 3.3), including when the source has no hits, in which case only settings are copied. The new name has " copy" appended.
  - **Reset to empty** removes the drum's hits, clears its custom name, and restores default settings and its configured default MIDI note.

**Bottom left: hit list**
- Shows hits in user order. A new hit is appended at the end of the displayed list even if it fills an earlier empty engine slot. Hits can be reordered with Move up / Move down (see below).
- Multi-select is allowed. With multiple hits selected, the hit details area is disabled.
- Double-clicking a hit selects it and auditions it as with the hit details play button.
- Buttons:
  - **Add...**: opens a menu with **Add file(s)** and **Add wave**. Add file(s) opens a multi-file dialog; each file is loaded as a wave (an already loaded file reuses its wave) and added as a hit at the end of this drum's list. Add wave opens a submenu of all loaded waves and adds the selected wave as a hit. If there are more files than free hit slots, the extra files are skipped and a "maximum hits reached" message is shown. Files that fail to load are reported.
  - **...** or right-clicking a hit: opens actions applied to the selected hits, including **Delete**, **Copy to...**, and the lesser-used hit actions below. Right-clicking an unselected hit selects it before opening the menu; right-clicking a selected hit preserves the current multi-selection. Delete removes the selected hits. Afterwards the hit before the first deleted one is selected, or if there is none, the next hit. Copy to... lists only other drums with fewer than 16 used hit slots; choosing one appends copies of the selected hits with their properties and names. If the target runs out of room, a "maximum hits reached" message is shown.
    - **Select wave**: allows changing the selected hits to a different available wave. The range resets to the whole new wave; hit names and other properties are preserved, with fades clamped if necessary. Selecting the current wave leaves the hit unchanged.
    - **Normalize**: sets the hit volume so the portion of the wave between the hit's start and end peaks at 0 dB (limited to the volume range of −60 to +12 dB). Only the peak is used, and velocity, pan and fades are ignored. Each selected hit is normalized individually.
    - **Duplicate**: copies each selected hit in the same drum and appends the copy to the list. The copy's name gets " (n)" appended, where n is one more than the highest number used by a hit of this drum with the same base name (an unnumbered name counts as 1, and an existing " (n)" suffix is replaced rather than stacked). For example "Snare" becomes "Snare (2)", then "Snare (3)". Disabled when the drum already has 16 hits.
    - **Move up** / **Move down**: moves the selected hits one place towards the start or end of the list, keeping their relative order and the selection. Disabled when the selection already fills the top or bottom of the list. Only the display order changes, and the engine slots are unaffected.
    - **Vel split**: divides the velocity range 0–127 evenly between all the hits of the drum (not only the selected ones). The first hit in the list gets the highest band and the last gets the lowest, so for three hits the bands are 85–127, 42–84 and 0–41. Disabled with fewer than two hits.
    - **Jump to wave**: switches to the Wave Pool tab, selects the hit's wave in the list, and selects the portion of the wave between the hit's start and end in the wave view. Enabled when exactly one hit is selected.
- Wave files can be dropped on a drum to add hits. Hits can be dragged onto another drum to copy them.

**Bottom right: hit details**
- Fields in this order: **Name**, **Volume** (dB), **Pitch** (semitones and cents sliders), **Velocity range**, **Velocity sensitivity** (0–100 %, default 100). The velocity range is a single two-knob slider (min and max) with the current min and max values shown at either end.
- The fields are disabled unless exactly one hit is selected. Changes are applied to the hit immediately.
- The selected hit's wave name and primary path are shown above the waveform, including for missing waves. Long values can be read in their tooltips.
- Preview: the green play button of the wave display (see 4.4) auditions the selected hit with its range, fades, pitch, volume and velocity sensitivity, and the drum's volume, pan and output. It uses the drum grid's audition velocity (currently 100) and is isolated from choke and per-drum polyphony.
- Wave display with editable start, end and fades (see 4.2).
- If the selected hit references a missing wave, its wave display and associated controls are not shown, and it cannot be previewed or played. The hit's other data remains intact.

**Missing-wave dialog**
- A modal warning appears when one or more wave files can't be loaded.
- **Search Missing** opens the Wave Pool's Search Missing workflow; **OK** dismisses the dialog.

### 4.2 Hit wave editing (in the hit details)

There is no separate wave popup. The start, end and fades of a hit are edited directly in the wave display at the bottom of the hit details area (shared control, see 4.4). It shows the whole wave and is disabled unless exactly one hit is selected.

- A movable vertical cursor (click to place it).
- Start and end are vertical lines with draggable flags: **i** (start) at the bottom pointing right, **o** (end) at the bottom pointing left.
- Fade-in and fade-out have only a flag (no line), **f**, at the top, pointing right and left respectively. They are displayed at start + fade-in and end − fade-out, with a ramp drawn from start to the fade-in flag and from the fade-out flag to end. Dragging a flag edits the offset.
- Dragging a start/end line (or its flag) moves it, and superimposed start/end lines are dragged together. Fade flags are dragged only by their own flag. Moving start or end keeps the fade lengths, so the fade flags move with them.
- Constraints: start < end, and the fade flags lie between start and end with fade-in ≤ fade-out.
- The **i** and **o** buttons next to the zoom buttons set the start and end from the cursor position.
- Changes are applied to the hit when a drag finishes, or when **i** / **o** is used.

### 4.3 Wave Pool tab

**Top: wave list**
- Columns: name, path, usage count, and a missing-file indicator. Clicking a column header sorts the list, preserving the selected waves.
- Multiple waves can be selected. Right-clicking a wave opens the same actions menu as **...**, selecting it first if necessary.
- Double-clicking a wave selects it and plays the wave-view selection, or the whole wave if no portion is selected, as with the play button.
- Wave files can be dropped onto the Wave Pool to load them, using the same duplicate and error handling as **Add...**.
- Buttons:
  - **...** opens wave actions:
    - **Delete.** Deletes all selected waves. If any are in use, confirmation is shown; if confirmed, the hits using them are also deleted.
    - **Rename.** Available for one selected wave. The name is shown in hits. It defaults to the filename. Hit names that still match the old default follow the rename; custom hit names are preserved.
    - **Choose new file.** Available for one selected wave, including a missing wave.
    - **Usage.** Available for one selected wave. Lists its hits grouped by drum; selecting a hit switches to Main and selects it. If unused, the menu says so.
    - **To drum.** Adds every selected wave as a whole-wave hit to the selected drum. Disabled if no waves are selected or any selected wave is missing; drums without enough room for every selected wave are disabled.
  - **Search missing.** Searches all missing waves regardless of selection. Select a directory. It and its subdirectories are searched for filename matches, and the user gets a checked list of found files to resolve. If several files share a filename, the first one found is used. The user confirms the list and can later point the wave at a different file with **Choose new file**.

**Bottom: wave view**
- Uses the shared wave control and shows the whole wave by default.
- For a missing wave, the wave view and its associated controls are unavailable.
- Drag in either direction to select a start/end, and drag either edge to adjust. It doesn't show the hit markers from 4.2.
- Setting only a start (with **i**) selects to the end of the wave, and setting only an end (with **o**) selects from the start of the wave. A hit added without a selection (or with only a start) therefore ends at the end of the wave.
- The green **play** button plays the selection, or the whole wave if nothing is selected, which is the same portion **Add to drum** would add. It sends the preview message (2.3) with no fades.
- The **To drum...** button beside play opens a menu of drums (with names if named). It adds the selection as a hit, or the whole wave if nothing is selected. This differs from the list's **To drum** menu, which always adds whole waves.
- With multiple waves selected in the list, the wave view is grayed out.

### 4.4 Shared wave control

- Zoom from the whole wave out to one pixel per sample in.
- Horizontal scroll.
- A green **play** arrow at the left of the bottom bar. The control only reports the click; each usage decides what to play (see 4.1 and 4.3).
- Configurable overlays (cursor, markers, selection) per usage.

### 4.5 Settings tab

- **Load patch** (dialog), followed by a boxed **Save patch** action (always a dialog)
  with **Purge unused waves** and **Consolidate waves into patch folder** options.
- **Global preferences**: default drum notes for new patches.

All path prompts default to the DAW project directory, or a sensible fallback.

### 4.6 Other interactions

- Drag a wave file from the host (e.g. Reaper) onto a drum to create a hit. This is available where the host/OS supports it.
- Drag a hit onto another drum to copy it.
- Wave actions: create hit in drum X, delete, see usage
- Hit actions: duplicate, delete, copy to other drum, normalize.
- Common user actions, covered above:
  - load/unload wave (if referenced, offer to remove the hits using it)
  - create a drum + hit from a wave section
  - duplicate drum: copy all hits and settings from one drum to another empty drum. "Empty" means no hits. It is reached from the drum actions (…) button (4.1), which offers only empty drums as targets and is disabled when none exist. Drums themselves are fixed (16) and cannot be created or deleted.
  - purge unused waves
  - save/load patch
  - rename waves - allows changing the name of a wave (not its filename). the name of a wave defaults to the file only portion of the filename.
- Undo/redo is wanted but deferred to the future.

---

## 5. Open items

Unresolved and deferred:

1. **Queue follow-ups**: whether patch loads need atomic application (begin/end batch markers), and whether any non-UI thread can write to the controller. Also fixed-capacity structures for cursors and candidates (sizes).
2. Which parameters are host-automatable. (MIDI learn is covered in 2.4.)
6. **Logging policy**: engine logging must not happen on the audio thread. Research JUCE mechanisms for audio-thread-safe logging.

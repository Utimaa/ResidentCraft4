# Native event landing handoff

Minecraft retains its source feet and epoch transform while host input is
paused. Previously, the first same-epoch guest motion after a native cutscene
could therefore overwrite a scripted native player placement with the old
source feet. `Re4CraftClient.beginFrame` deliberately retains this transform on
pause; its ordinary GuestAuthority branch resumes source physics without
following the new native pose.

`NativeScriptHandoff` now arms only after actual F8 movement eligibility. An
observed native `STA_EVENT` cycle retains native movement. Once the event ends
and native gameplay is playable, the bridge captures the same actor's actual
landing and sends the existing HostAuthority / NativeTraversal synchronization.
Minecraft must acknowledge both its integrated-server feet and its actual
exported client feet before a subsequent ordinary ready frame can restore
movement. The anchor remains unchanged, so imported terrain and saved world
coordinates remain consistent.

The cycle requires the same native global, player allocation, GUID, room and
epoch. A disconnect, mode change, death or changed identity retires ownership.
Native actions are bounded to 180 seconds; landing acknowledgement to two
seconds and displacement to the existing 32-block transfer bound. Failure
restores native control. An exact native window already in progress retains its separate
30-second handoff and takes priority. A window callback nested inside script
ownership runs natively under that script hold. Ordinary pause/focus changes and native
displacements do not start a scripted handoff.

## U-3 native evidence

The owned retail UHD1.1.0 executable SHA256 is
`19aed4af0ab06a748ff8744d45ac5580fcd6be6b6b7e944b1ab8822a00c8ee4a`.
Room0x50f initialization at0x7B61C0 registers actual area event3 and callback
0x7B5700 through0x7B633B–0x7B6348. The original room-save flag0x10000000
continues to govern activation.

The callback calls event-begin0x6C1470 at0x7B5972. It sets `STA_EVENT` at
0x6C1531 before the first explicit coroutine yield0x7B5984. Native player
placements at0x7B59DD,0x7B59ED,0x7B5BF3 and0x7B5C03 occur inside this event
interval. Cleanup0x7B549E calls0x6C1680, which clears `STA_EVENT` at0x6C1736.
Thus existing cutscene eligibility already protects the placements while the
event runs; the corrected gap is the return to guest movement afterward.

The actual AEV actor pass0x6BB180 reads absolute player position and calls the
original geometry predicate0x6B7DB0. Original-byte fixture checks use the real
AEV3 polygon from owned r50f.udas.lfs, actual shape copy/dispatch/quadrilateral
instructions, and actual production F8 scheduler writes. Normal source feet
enter the original trigger with unchanged old position, zero speed and the
existing temporary collision flags. There is no evidence justifying a change
to player+0x110, speed+0x104, collision flags, or the player hide bit.

`tools/probe-native-u3.py` generates the private fixture header and static proof
under ignored build/. It consumes the separately decoded original AEV file;
neither original instructions nor room data are public source assets.
`native/build-u3-tests.cmd` passed2023 original-byte x86 checks.
`native/build-script-tests.cmd` passed3045 scripted-event policy checks and5064
existing window handoff checks. These are component results. Live U-3 intro,
spawn and post-intro combat remain to be checked in the actual campaign room.

# RE4Craft 0.2.0-alpha.3

Minecraft Java 26.3 gameplay inside original Resident Evil 4 UHD, using a real background Minecraft client. Download the **Windows ZIP**, extract it, run **Setup.cmd** with RE4 closed, then **Play.cmd**. Load a campaign save and press **F8**. Install and open owned Minecraft **26.3** once through the official Launcher or Prism Launcher before setup. Full steps are in [INSTALL.md](INSTALL.md).

This update adds:

- **Prism support:** select its data root or 26.3 instance and reuse the existing shared client, libraries and assets. RE4 and Minecraft may be on different drives.
- **Startup fixes and logs:** restore the JVM stack-shadow option specified by the pinned Minecraft 26.3 launch metadata, correct the Windows argument-file encoding for supported non-ASCII paths, reject incompatible paths clearly, and retain Java stdout/stderr and exit codes even before Minecraft creates its log.
- **Native combat corrections:** send boss/special-enemy hits through original damage consumers while preserving weak points, phases and protection rules. Armed mines/tripwires retain their original damage and detonation logic.
- **Moving platforms:** update actual RE4 collision transforms and carry grounded Minecraft riders through translation and rotation, releasing carry on jumps, lost support or stale data.
- **Block entities:** export original models, textures and animations such as chests, heads, banners and moving blocks. Sign glyphs and specialized portal shader effects remain unsupported.
- **Scripted landings:** retain native event placements and require the actual Minecraft server/client landing acknowledgement before restoring guest movement. Existing native-window handoff keeps priority.

Campaign crafting/loot progression, existing worlds and inventories, AZERTY ZQSD movement and **X** inventory are preserved. Install both bridge components together through Setup.

**Experimental pre-release:** source/component checks and isolated Minecraft block-entity, moving-platform and landing-receipt scenes pass. Combined RE4 campaign behavior remains to be verified. The Windows encoding defect was reproduced and fixed, but the exact cause of the background-startup report in issue #2 is unproven. Intermittent native JVM access violations remain unresolved, including failures before a world opens. See [validation details](docs/RELEASE_VALIDATION.md) and [startup troubleshooting](docs/LAUNCHER.md). No sustained 60 FPS guarantee is made.

Requirements: Windows, owned Minecraft Java 26.3 and original Steam RE4 UHD x86, tested against 1.1.0. Game assets, account files and saves are not included. Halo and BeamNG development remain on hold; ULTRAKILL is cancelled.

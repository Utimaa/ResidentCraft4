# RE4Craft 0.2.0-alpha.2

Minecraft 26.3 gameplay in original Resident Evil 4 UHD, with an automatically started background Minecraft client.

Download the **Windows ZIP**, extract it, and run **Setup.cmd**, then **Play.cmd**. Install Minecraft Java **26.3** in the official Launcher and open it once before setup. In RE4, load a campaign save and press **F8**. Full steps and controls are in **INSTALL.md** inside the ZIP.

This update extends crate targeting to the original larger collision meshes and adds campaign loot progression: wood, stone, iron, diamonds, then netherite upgrade and enchanting supplies in chapters 5 and 6. Collect and craft your equipment using actual Minecraft recipes; the loot includes smithing templates, enchanted books and experience for an enchanted netherite set by the end of the campaign. Existing inventory and armor are preserved. Ammo, food and building materials continue throughout.

Background launch, native-window handoff and the starter bow remain. Actual Minecraft physics, animation, skin/armor, hands, HUD, inventory, blocks and combat remain supplied by Minecraft. AZERTY ZQSD movement and X inventory are retained. Install both parts of this update together using Setup.cmd.

Requirements: Windows, owned Minecraft Java 26.3, and original Steam RE4 UHD x86, tested against 1.1.0. The source engine still uses CPU/GPU resources while running in the background. Game assets and saves are not included.

**Experimental pre-release:** the final hidden client passed actual recipe, anvil-preview, saved-book, loot-rotation and XP terrain/pickup checks, followed by successful save/reload. All seven crate variants passed the native component fixtures. Larger-crate and window behavior still needs combined RE4 gameplay verification. One private startup attempt hit an unresolved JVM access violation before a successful retry and reload; see **docs/RELEASE_VALIDATION.md**. No sustained 60 FPS guarantee is made.

Halo and BeamNG development are on hold; ULTRAKILL is cancelled. This package focuses on Minecraft. See **CHANGELOG.md** and **docs/RELEASE_VALIDATION.md** for details and current limits.

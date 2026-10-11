# Changelog

## 0.2.0-alpha.2

- Include the original collision meshes of larger wooden crates in Steve's targeting bounds, alongside their active native damage parts.
- Reward originally empty ordinary crates after native destruction completes, while retaining keys, treasures and scripted drops.
- Extend chapter loot through netherite upgrade materials and enchanting supplies in chapter 5, with more supplies in chapter 6-1.
- Drop the materials, smithing templates, enchanted books and experience needed to craft and upgrade equipment through the campaign, preserving existing inventories and equipment.
- Use real Minecraft enchanted-book components and retain ammunition, food and building supplies in ordinary drops.
- Let experience orbs collide with RE4 terrain so enchanting experience can be collected in the mirror world.
- Keep background launch, AZERTY movement, X inventory and existing world selection.
- Begin newly created campaign worlds with wooden tools, a shield, bow, arrows and basic supplies; leave existing saves and equipment intact.
- Report background-client crashes while RE4 is running and allow Play to restart Minecraft with RE4 already open.
- Write JVM crash reports into the private Minecraft instance for startup diagnosis.

## 0.2.0-alpha.1

- Add an automatically launched background Minecraft 26.3 client with a hidden SDL3 window.
- Retain actual Minecraft movement, animation, skin/armor/hands, HUD, inventory and combat.
- Add Setup, Play, StopMinecraft and Uninstall wrappers using the user's installed Minecraft files and Java 25 runtime.
- Preserve private worlds and custom settings; verify release dependencies and owned source processes.
- Synchronize native window landings and observe the actual cooperative coroutine completion so Minecraft mode can resume automatically.
- Correct zero-based campaign chapter progression so useful drops begin in chapter 1-1.
- Give a starter bow once without replacing existing inventory contents.
- Preserve existing hotbar and equipment on reload; apply the starter kit only to newly created private worlds.
- Automatically prepare a checksum-verified portable patched Java runtime when the installed runtime is too old.
- Expand wooden-box targeting to validated original physical bounds and retain original native damage/destruction.
- Keep AZERTY movement and X inventory. Halo/BeamNG development is on hold; ULTRAKILL is cancelled.

Packaged background launches, graceful save/shutdown and inventory reload passed. Final box/window gameplay verification in RE4 is pending. Completed checks and limits are recorded in `docs/RELEASE_VALIDATION.md`.

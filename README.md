# RE4Craft

Minecraft Java 26.3 gameplay inside original Resident Evil 4 UHD, with Minecraft running automatically in the background.

The mod sends Minecraft's actual player meshes, textures, hands, HUD and inventory to RE4. Minecraft supplies movement, physics, blocks and combat; RE4 supplies terrain, native enemies and campaign scripts. Useful item drops progress with the campaign. The bridge preserves AZERTY controls and uses **X** for inventory.

**Experimental release: 0.2.0-alpha.3.** This update adds Prism installation selection, fixes Windows launch-path encoding, and improves native boss/mine interactions, moving platforms, block-entity rendering and scripted landings. Source and component checks pass; the new engine scenes and combined RE4 campaign behavior still need verification. Intermittent native JVM crashes remain unresolved. See [validation details](docs/RELEASE_VALIDATION.md).

## Install

Requirements: Windows, original **Resident Evil 4 UHD** on Steam, and owned **Minecraft Java 26.3**. The supported host is the original 32-bit UHD build tested as 1.1.0.

1. Install Minecraft **26.3** in the official Launcher or Prism Launcher and open it once, then close it.
2. Extract the Windows release ZIP and run **Setup.cmd** with RE4 closed.
3. Run **Play.cmd**, load your RE4 campaign save and press **F8**.

Minecraft starts with a hidden window and keeps running as a real background game process. The release reads your locally installed game files and uses a separate private world. Your original mods, worlds and account files are preserved. Prism users can select their launcher data root or 26.3 instance; RE4 and Minecraft may be on different drives. See [installation selection and startup logs](docs/LAUNCHER.md). If your Java runtime is older than 25.0.4, Setup downloads a checksum-verified portable Temurin runtime into the release folder; it does not replace your system Java.

See the [simple install guide](INSTALL.md) for controls, uninstall and troubleshooting. Java/Fabric build tools are not required to use the release. Keep the extracted release folder in place.

## Campaign loot

Ordinary enemies and breakable crates drop Minecraft supplies appropriate to the current campaign chapter. Collect the materials, craft equipment, upgrade it at a smithing table and enchant it. Existing inventory and equipment are preserved.

| Campaign chapter | Crafting progression |
| --- | --- |
| 1 | Wood |
| 2 | Stone |
| 3 | Iron |
| 4 | Diamond |
| 5 | Netherite upgrade materials, enchanted books and XP |
| 6-1 | More upgrade and enchanting supplies |

Loot includes the smithing and enchanting supplies needed to work toward enchanted netherite by the end of the campaign. Ammo, food and building materials remain available throughout. Campaign loot conversion leaves progression items and bonus modes under RE4's rules.

New private worlds start with wooden tools, a shield, a bow, arrows and basic supplies. Existing worlds keep their current inventory and equipment when updating.

## Source layout

- `native/`: original RE4 UHD x86 ASI, rendering, terrain, input and native combat integration.
- `minecraft/`: Fabric 26.3 client, actual simulation and geometry/HUD export.
- `protocol/`: local bridge wire format.
- `release/`: setup, background launcher, graceful shutdown and uninstall scripts.
- `tools/`: build, dependency and installation helpers.
- `tests/`: independent protocol/native component fixtures.
- `licenses/`: third-party notices.

This release focuses on Minecraft. Halo and BeamNG development are on hold; ULTRAKILL is cancelled. Native compatibility code is retained, but those guest adapters are not supplied in this release. Historical private worlds and game captures are excluded from the repository.

## Build from source

Install a Java **25 JDK** and Visual Studio 2022 C++ x86 build tools with the Windows SDK. Then run:

```powershell
.\tools\build-release.ps1 -JavaHome 'C:\Path\To\Your\JDK25'
```

The native build writes `build/native/RE4Craft.asi`. The Fabric build writes its versioned mod JAR under `minecraft/build/libs`. Builds do not install or launch either game. Optional retail instruction fixtures require the user's own supported RE4 executable and stay private.

For a verified release build, prepare official cached Fabric launch dependencies with `tools/prepare-release-dependencies.ps1`, then run `tools/package-release.py` using Python 3. The packager creates an explicit source folder and Windows ZIP, checks hashes and excludes game files, private data and unrelated guest experiments.

## Credits and license

Inspired by [SkyCraft v0.1.0](https://github.com/chasmlol/SkyCraft/tree/v0.1.0); the adapted Minecraft exporter retains its MIT notice. Native research references [re4_tweaks](https://github.com/nipkownix/re4_tweaks). The bridge uses Fabric, MinHook and Ultimate ASI Loader. Complete notices are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and `licenses/`.

AI-assisted development used OpenAI Codex and Universal Modder playbooks. Mod code is MIT licensed, with separately attributed third-party code under its original licenses. Minecraft and Resident Evil 4 game files belong to their respective owners and are not included. This is an unofficial project.

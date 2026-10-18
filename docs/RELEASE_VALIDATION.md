# 0.2.0-alpha.3 validation

Experimental prerelease for original RE4 UHD x86 and Minecraft Java 26.3. Build/component results below are separate from running-client and combined campaign verification.

| Check | Current result |
| --- | --- |
| Integrated Minecraft source | 112 Java tests and client compilation passed |
| Native specialized combat | 132,192 checks passed |
| Original ordinary crates | 1,253 checks passed across seven variants |
| Original U-3 area-event trigger | 2,023 x86 checks passed |
| Scripted-event landing policy | 3,045 checks passed |
| Existing native-window handoff | 5,064 policy, 12,048 hook/ABI and 14 memory checks passed |
| Launcher policy/ownership | 76 checks passed in Windows PowerShell 5.1 and PowerShell 7 |
| Real Java bootstrap | 15 full checks in PowerShell 5.1; 9 bootstrap checks in PowerShell 7 passed |
| Actual chest/head/model source scene | 16 assertions passed across 12 original block-entity model renderers; chest animation/removal checked |
| Actual moving-surface source scene | 74 assertions passed: stationary player/server/cow/zombie, translation/rotation, jump, disembark, removal and250ms stale hold/resume |
| Actual source event-landing receipt | 7 checks passed: pause, server/client acknowledgement and ordinary movement resumption |
| Finished x86 ASI transport | Production self-test, fragmented loopback exchange and 6 independent Python checks passed |

Combat fixtures cover original specialized damage routing, mine/tripwire consumers and high-damage Gigante weak-point overflow across all positive signed16 health values; they do not establish every boss fight or phase in the loaded campaign. U-3 checks execute original trigger geometry/instructions and production movement writes. Scripted-event checks retain the native landing and require server/client acknowledgement; live U-3 intro, spawn and post-intro combat remain pending.

Moving-platform source tracks original collision identities and changed transforms, using actual Minecraft movement for a supported rider. Block-entity source exports original models/textures/animations through existing mesh transport and preserves its bounds. The disposable source scene now passes16 assertions for12 original model renderers after restoring the official JVM launch option. The first moving-platform scene exposed stationary NoAI mobs skipping the movement hook. Carry now also runs through actual entity base ticks; the same retained scene passes74 assertions with player/server/cow/zombie translation and rotation, jumping, disembarking, support removal and the deliberate 250 ms stale hold/resume. A separate actual client/integrated-server native-landing receipt scene passed 7 checks. These use controlled host packets, rather than a native RE4 campaign. Native RE4 carriage, block-entity presentation and sustained combined-game frame rate remain unverified. Sign glyphs and specialized portal shader effects are outside the implemented rendering scope.

Launcher probes used real Java with a classpath and game directory containing spaces and supported non-ASCII characters. They verified early JVM-option errors/exit codes, direct logs after the supervisor exits, and a Prism layout populated from owned client/libraries/assets-index files, without a `versions` directory. Pinned metadata, modified files and missing asset objects were checked. These are bootstrap/component checks, not a live Prism account or combined-game test.

The UTF-8 argument file previously became incorrect paths under the Windows Java launcher's active ANSI code page. That defect was reproduced and corrected; unsupported characters now fail explicitly. [Issue #2's native log](https://github.com/Utimaa/ResidentCraft4/issues/2) shows no Minecraft connection but contains no Java failure establishing that reporter's exact cause. Different RE4/Minecraft drives do not alter the generated Minecraft arguments.

The launcher omitted the unconditional `-XX:StackShadowPages=32` option required by pinned Minecraft 26.3 metadata. The failing JVM actually used 8; the corrected real bootstrap verifies 32. The retained disposable entity scene then passed and saved normally, and a second source session completed the landing receipt. Earlier native crashes included resource-worker failures before world entry and after fixtures were placed; a full private dump identifies an interface-resolution path, without proving an independent VM/source defect. Successful corrected runs do not establish that every intermittent native JVM failure is resolved. New direct logs and crash-report routing remain available.

Earlier alpha.2 disposable-client evidence remains applicable to the retained loot behavior: real recipes/anvil previews, XP terrain/pickup, book rotations and successful save/reload were checked. The sixteen-crate late-chapter budget supplied crafting/upgrading/enchanting materials; production drops grant no finished equipment. Full GUI anvil transactions, larger-crate/window gameplay and full-campaign completion remain separate live checks.

Original game installations and the production world are preserved. Private saves, dumps, captures, game binaries/assets and extracted retail instructions stay outside public packages. Halo/BeamNG remain on hold; ULTRAKILL is cancelled.

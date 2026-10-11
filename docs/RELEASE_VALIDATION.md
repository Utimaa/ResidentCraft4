# 0.2.0-alpha.2 validation

This is an experimental development prerelease for original RE4 UHD x86 and Minecraft Java 26.3.

Native checks passed: 1,253 actual x86 crate checks across all seven ordinary container variants, including transformed body/SAT targeting, the original hit consumer/destruction/broken-state scripts, native bookkeeping, nearer walls, empty/hazard breaks, protected contents and duplicate exclusion. The chapter policy passed 67,863 checks covering zero-based chapters 0-18 and bonus-mode exclusions. The final ASI passed its production protocol and fragmented loopback tests; six independent Python wire checks passed.

The final Minecraft source passed 105 Java unit tests and client compilation. Its revised loot contains materials and usable enchanted books; production drops do not grant finished equipment. The background launcher passed 62 policy/ownership checks and six script syntax checks.

Two successful hidden-client launches used the final mod JAR and pinned portable Temurin 25.0.4.1 runtime in a disposable private save. The seed passed 1,136 actual integrated-server checks; reload passed 103. The tests paid for real Minecraft crafting and smithing recipes from a sixteen-crate late-chapter resource budget, then checked actual anvil previews and costs. The budget supplied at least 1,536 XP from bottles at their minimum yield, against 1,380 XP for seven fresh combined-book operations. These are recipe/output/cost checks; the tests did not perform seven full GUI pickup transactions or complete the campaign.

Actual bottle impact, orb terrain contact and vanilla XP pickup passed 146 seed and 215 reload checks. The bottle created real experience orbs, which rested for five consecutive ticks on imported terrain above vanilla air. Only test orbs were moved into the player's pickup bounds; vanilla pickup credited 6 and 11 XP without direct XP grants.

Both clients saved and exited normally through authenticated launcher shutdown. Fourteen read-only saved-NBT checks confirmed the seven recipe-derived test items and five usable stored-enchantment book variants retained their exact components. The separate enemy/crate book rotations survived reload, rejected replay and continued across native session changes. Legacy saved loot ledgers and inactive equipment-claim fields remained readable; existing inventory and equipment were unchanged by loot awards. Recipe-derived equipment entities existed only in the explicitly guarded disposable QA fixture.

Both successful scenes also passed six background-engine checks for actual physics, textured poses, transparent HUD, movement and inventory. Each exported over 950 source motion/mesh frames. These component results do not establish sustained combined-game 60 FPS.

The same final JAR and Java 25.0.4.1 have intermittently crashed inside jvm.dll during startup, before entering a world. Retrying the same preserved saves has succeeded, including reconnecting the production client to an already running RE4 session. The launcher now reports nonzero client exits while RE4 is open and directs JVM crash reports to the private instance. These diagnostic changes do not establish the access-violation cause or guarantee that startup crashes are eliminated.

Pending live check: larger-crate targeting/breaking and chapter loot in an actual RE4 campaign room, plus the previously corrected automatic window reactivation. Native fixtures preserve the original algorithms but do not establish final combined gameplay or full-campaign completion.

Original game installations and the production world were not used for these test scenes. Game binaries/assets, accounts, worlds, private captures and extracted retail instruction fixtures are excluded from the release.

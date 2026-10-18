# Minecraft installation selection and startup logs

RE4Craft can reuse Minecraft Java **26.3** installed and opened once through either the official Launcher or Prism Launcher. It reads the existing client, libraries and assets, then prepares a separate persistent RE4Craft instance. Your launcher instance, its mods, account storage, settings and saves stay in place. RE4 and Minecraft may be on different drives.

For the official Launcher, the usual source is `%APPDATA%\.minecraft`. For Prism, choose its data root (the folder containing `libraries` and `assets`), its **26.3 Instance Folder**, or that instance's **Minecraft Folder**. The shared client is normally under `libraries\com\mojang\minecraft\26.3`; a Prism `versions\26.3\26.3.json` is unnecessary. Portable Prism data roots work too. If you keep Prism instances outside the data root, select the data root explicitly.

Double-click **Setup.cmd**. When the default official installation is missing, Setup opens a folder picker. To choose another existing source explicitly, run from the extracted release folder:

```powershell
.\Setup.cmd -MinecraftPath 'C:\Games\PrismLauncher'
# Or choose a 26.3 instance:
.\Setup.cmd -MinecraftPath 'C:\Games\PrismLauncher\instances\My 26.3'
# RE4 can be on a different drive:
.\Setup.cmd -GamePath 'D:\SteamLibrary\steamapps\common\Resident Evil 4' -MinecraftPath 'C:\Games\PrismLauncher'
```

For Prism, first Setup fetches only the pinned public Mojang version metadata into `.re4craft\metadata\26.3.json`. Its hash and every reused game jar/library are checked. No game assets or Minecraft account tokens are downloaded by this support. The existing Java fallback may separately fetch the pinned portable runtime if a supported runtime is missing. Re-running Setup preserves the selected RE4Craft world; do not delete its instance to troubleshoot startup.

If Minecraft exits before creating `logs\latest.log`, read `.re4craft\last-error.txt` in the release. The error includes the decimal/hex exit code and exact Java stderr/stdout paths. The configured private instance also keeps `.re4craft\last-launch.json`, timestamped `java-*.stderr.log` / `java-*.stdout.log`, and any JVM `hs_err_pid*.log` crash reports. These direct file logs survive the launcher closing and capture JVM option, classpath and Fabric failures before Minecraft logging begins. A native JVM crash may still lack an `hs_err` report; retain the exit code and matching Windows Application Error event.

Windows Java's native argument-file reader uses the active Windows ANSI code page. RE4Craft writes that encoding explicitly under both Windows PowerShell 5.1 and PowerShell 7, preserving spaces and characters supported by the Windows system language. Unsupported characters fail clearly before launch. In that case, move the extracted release/private instance or select an owned Minecraft/Java installation under a path representable in that language, then run Setup again. A semicolon in a library path is likewise rejected because Java treats it as a classpath separator.

The attached native log in [issue #2](https://github.com/Utimaa/ResidentCraft4/issues/2) shows RE4 hooks running with no Minecraft source connection; it contains no Java error that establishes that reporter's exact cause. The corrected argument encoding fixes a reproduced launcher defect. The launch arguments also retain the unconditional `-XX:StackShadowPages=32` requirement from pinned Minecraft 26.3 metadata. Early stderr capture supplies missing diagnosis for other failures; it does not establish that intermittent native JVM crashes are resolved.

Prism layout references: [Minecraft 26.3 metadata](https://github.com/PrismLauncher/meta-launcher/blob/master/net.minecraft/26.3.json), [LWJGL 3.4.3 metadata](https://github.com/PrismLauncher/meta-launcher/blob/master/org.lwjgl3/3.4.3.json), and [library storage implementation](https://github.com/PrismLauncher/PrismLauncher/blob/develop/launcher/minecraft/Library.cpp). Java argument-file encoding is described in the [Java 25 command reference](https://docs.oracle.com/en/java/javase/25/docs/specs/man/java.html#java-command-line-argument-files).

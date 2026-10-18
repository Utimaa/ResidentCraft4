# Install RE4Craft

You need Windows, **original Resident Evil 4 UHD** on Steam and an owned **Minecraft Java 26.3** installation. This alpha targets the original 32-bit RE4 UHD build tested as version 1.1.0.

1. In the official Minecraft Launcher or Prism Launcher, install **26.3** and open it once. Then close Minecraft. Its client, libraries and assets must already be installed.
2. Download `RE4Craft-0.2.0-alpha.3-Windows.zip` from the release, extract it to a folder you can write to, and double-click **Setup.cmd**. Close RE4 before setup. If the default official Minecraft installation is missing, choose your Minecraft folder or Prism data root/26.3 instance in the folder picker. Explicit-path commands are below. If Windows reports access denied, right-click **Setup.cmd** and choose **Run as administrator**.
3. Double-click **Play.cmd**. Load an RE4 campaign save, wait for the bridge to connect, then press **F8**.

Minecraft starts automatically with a hidden window. Its engine still runs in the background. The bridge uses a separate private Minecraft world and leaves your original Minecraft mods, worlds and account files alone.

Setup uses a patched Java 25 runtime. If one is missing, it downloads the pinned portable Temurin 25.0.4.1 runtime (about 58 MB) once into the release folder. Prism setup also downloads a small, checksum-verified public version metadata file. An Internet connection is required for those first downloads; existing game files and runtimes are preserved.

| Key | Action |
| --- | --- |
| F8 | Minecraft mode on/off |
| Z / Q / S / D | Move, with AZERTY controls |
| Space | Jump |
| Left Shift / Left Ctrl | Sneak / sprint |
| X | Inventory |
| F5 | First person / rear third person / front third person |
| Left / right mouse | Attack or mine / use or place |
| T | Chat |

Your starter world includes a bow. Existing bridge worlds receive it once in a free inventory slot. Inventory contents are preserved.

When finished, close RE4 normally. Play requests a graceful Minecraft save and shutdown. **StopMinecraft.cmd** can request the same shutdown manually. To remove the mod, close both bridge processes and run **Uninstall.cmd**. Your private world and backups are kept.

If Setup cannot find a game, run it from PowerShell with explicit paths:

```powershell
.\release\Setup.ps1 -GamePath 'D:\SteamLibrary\steamapps\common\Resident Evil 4' -MinecraftPath "$env:APPDATA\.minecraft"
# Prism data root, or its instances\Your 26.3 folder:
.\release\Setup.ps1 -GamePath 'D:\SteamLibrary\steamapps\common\Resident Evil 4' -MinecraftPath 'C:\Games\PrismLauncher'
```

RE4 and Minecraft can be on different drives. Prism does not need an official `versions\26.3\26.3.json`: Setup reuses its shared libraries/assets and verifies the existing client. For custom Prism instance directories outside its data root, select the data root. Full selection details are in [docs/LAUNCHER.md](docs/LAUNCHER.md).

If F8 does nothing, check `Bin32\RE4Craft.log` in RE4 and `.re4craft\last-error.txt` in the release. The latter gives the exit code and exact Java stdout/stderr paths, even if Minecraft never created `logs\latest.log`. The private instance also keeps `.re4craft\last-launch.json`, timestamped `java-*.stderr.log` / `java-*.stdout.log`, and any `hs_err_pid*.log` JVM crash report. A custom private instance uses its own paths instead of `.instance`.

Windows launch paths must be representable in the Windows system language; supported non-ASCII characters and spaces are retained. Unsupported characters or semicolons in library paths now produce a clear error instead of a damaged classpath. Choose compatible source/release paths and rerun Setup without deleting the selected world. If the background client stops, **Play.cmd** can restart it with RE4 open. Keep the extracted folder in place.

This alpha fixes a reproduced argument-file encoding defect; it does not establish the exact cause of every reported startup failure. Intermittent native JVM access violations remain unresolved. Isolated moving-platform/block-entity and source landing checks pass; combined campaign checks remain pending; see [validation details](docs/RELEASE_VALIDATION.md). No constant 60 FPS guarantee is made.

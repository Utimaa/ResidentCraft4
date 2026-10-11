# Install RE4Craft

You need Windows, **original Resident Evil 4 UHD** on Steam and an owned **Minecraft Java 26.3** installation. This alpha targets the original 32-bit RE4 UHD build tested as version 1.1.0.

1. In the official Minecraft Launcher, install **26.3** and open it once. Then close Minecraft. This installs its Java 25 runtime, libraries and assets.
2. Download `RE4Craft-0.2.0-alpha.2-Windows.zip` from the release, extract it to a folder you can write to, and double-click **Setup.cmd**. Close RE4 before setup. If Setup cannot find RE4, use the explicit-path command below. If Windows reports access denied, right-click **Setup.cmd** and choose **Run as administrator**.
3. Double-click **Play.cmd**. Load an RE4 campaign save, wait for the bridge to connect, then press **F8**.

Minecraft starts automatically with a hidden window. Its engine still runs in the background. The bridge uses a separate private Minecraft world and leaves your original Minecraft mods, worlds and account files alone.

Setup uses a patched Java 25 runtime. If one is missing, it downloads the pinned portable Temurin 25.0.4.1 runtime (about 58 MB) once into the release folder. An Internet connection is required for that first download; system Java and the Minecraft Launcher runtime are preserved.

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
```

If nothing happens with F8, check `Bin32\RE4Craft.log` in RE4 and `.instance\logs\latest.log` in the extracted release. Startup errors are also saved under `.re4craft\last-error.txt`; Java crash reports, when available, are written to `.instance\.re4craft\hs_err_pid*.log`. If the background client stops, run **Play.cmd** again to restart it while keeping RE4 open. Confirm Minecraft **26.3** was opened once in the official Launcher. Keep the extracted folder in place after setup.

This is an experimental alpha. The packaged hidden client was tested with real physics, textured meshes, HUD, graceful save/shutdown and inventory preservation across reload. The final in-game box/window corrections still need a combined RE4 gameplay check; no constant 60 FPS guarantee is made.

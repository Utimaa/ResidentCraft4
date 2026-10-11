# Put the prepared files on GitHub

The source and Windows release are prepared locally. No repository or release has been published automatically.

1. Create a GitHub repository named **RE4Craft**. Extract the Source ZIP and upload its `RE4Craft` folder's contents, keeping the directory structure. Alternatively use the prepared **RE4Craft-github** folder with Git. The root should contain `README.md`, `native`, `minecraft`, `release` and the other source folders.
2. On GitHub, choose **Releases → Draft a new release**. Create tag **v0.2.0-alpha.2** and use **RE4Craft 0.2.0-alpha.2** as the title. Mark it as a **pre-release**.
3. Attach **RE4Craft-0.2.0-alpha.2-Windows.zip** and its **.sha256** file. Paste `RELEASE_NOTES.md` into the description and publish when you are ready.

Users download the Windows ZIP, extract it and follow `INSTALL.md`. They do not need to compile the source or copy Minecraft/RE4 assets.

Keep original game files, saves, account files and private project checkpoints out of GitHub. The prepared source folder and release ZIP use an explicit file allowlist. The unfiltered development workspace is not the upload folder.

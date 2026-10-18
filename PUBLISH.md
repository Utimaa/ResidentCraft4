# Put the prepared files on GitHub

The source and Windows release are prepared locally. No repository or release has been published automatically.

1. Open **GitHub Desktop → File → Add local repository** and select the prepared **dist/RE4Craft-github** folder. Use your existing repository when it is already linked; there is no need to create another one. The source root should contain `README.md`, `native`, `minecraft`, `release` and the other source folders.
2. Review the changes, enter a summary such as **Update to 0.2.0-alpha.3**, and commit. Click **Fetch origin**, then **Pull origin** if remote commits are available, and **Push origin**. Retain existing repository edits when merging; do not force-push over them. If a merge reports a conflict, resolve it before pushing.
3. On GitHub, choose **Releases → Draft a new release**. Create tag **v0.2.0-alpha.3**, select the branch containing the update as its target, and use **RE4Craft 0.2.0-alpha.3** as the title. Mark it as a **pre-release**.
4. Attach **RE4Craft-0.2.0-alpha.3-Windows.zip** and **RE4Craft-0.2.0-alpha.3-Windows.zip.sha256**. Paste `RELEASE_NOTES.md` into the description and publish when you are ready. Keep older releases under their existing tags.

The **Source.zip** is an alternative source archive, not the playable installer. If using it, extract it and work with the contents of its `RE4Craft` folder; do not add an extra enclosing folder at the repository root. The prepared Git folder is simpler for updating an existing repository.

Official instructions: [add a local repository](https://docs.github.com/en/desktop/adding-and-cloning-repositories/adding-a-repository-from-your-local-computer-to-github-desktop), [push changes](https://docs.github.com/en/desktop/making-changes-in-a-branch/pushing-changes-to-github-from-github-desktop), [create a release](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository).

Users download the Windows ZIP, extract it and follow `INSTALL.md`. They do not need to compile the source or copy Minecraft/RE4 assets.

Keep original game files, saves, account files and private project checkpoints out of GitHub. The prepared source folder and release ZIP use an explicit file allowlist. The unfiltered development workspace is not the upload folder.

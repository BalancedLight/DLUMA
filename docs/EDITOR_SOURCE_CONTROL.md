# Editor Git

Open **Tools → Options → DLUMA**, choose **Open Git**, then close Options to use the panel. Git for Windows must be installed; the panel finds `git.exe` on the executable search path or lets you choose it.

On supported Developer Tools Editor builds, the panel shows repository status, history, and selected-file staged and working diffs. Select one changed file to inspect its diff. Untracked files can be reviewed in the working diff; binary changes are identified without displaying binary contents, and oversized previews are refused.

For an existing repository, you can stage or unstage selected files, review the full staged index, commit it, and create a branch. Creating a branch does not switch to it. A commit includes everything in the reviewed index, including files staged outside the panel; it does not automatically stage other files. Set your Git name and email before committing.

The panel operates only when the open project folder is the repository root. Linked Git worktrees are supported. Opening a project inside another repository does not authorize changes to its parent repository. Initialization, branch switching, cloning, and remote operations are not yet available in the panel.

The Editor's legacy **Source control enabled** setting must be off and its built-in provider disconnected before Git writes are available. Change that setting in Editor Options if needed, then close Options and choose **Refresh** in the Git panel. DLUMA does not change or save that preference for you.

Git operations run asynchronously and can be canceled. Cancellation can occur after Git has changed local state, so refresh and inspect the result before retrying. If a Git process cannot be stopped or the Editor connection is lost during a write, further writes are blocked until cleanup is complete; check the repository before restarting the Editor.

For troubleshooting, Developer Mode can enable `editor_git_guard_diagnostics=1` in the `[loader]` configuration. After restarting the Editor and opening Git, it records bounded numeric routing and admission observations without command text or file paths. Turn the setting off when finished.

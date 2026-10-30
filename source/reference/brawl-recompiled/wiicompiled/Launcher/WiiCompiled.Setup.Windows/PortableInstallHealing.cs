using WiiCompiled.Setup.Common;

namespace WiiCompiled.Setup.Windows;

/// <summary>
/// A portable root can be moved or renamed between operations. Every installed-host operation that
/// reads <c>install-state.json</c> passes through here first so exactly one place decides what a
/// moved installation means, and so a non-portable installation is never touched.
/// </summary>
internal static class PortableInstallHealing
{
    /// <summary>
    /// Reconciles a moved portable installation with its recorded location: the state file adopts the
    /// directory it was actually found in, and the native build tree is discarded because its
    /// CMake cache holds absolute paths from the old location. Returns whether anything was healed.
    /// </summary>
    public static bool HealMovedInstall(Installation installation, IInstallReporter? reporter = null)
    {
        // Guard: an ordinary installation that disagrees with its state file is a real problem for
        // the operation to report, not something to silently rewrite.
        if (PortableRoot.TryFind(installation.Root) is null) return false;

        var state = installation.ReadInstallState();
        if (state is not { SchemaVersion: 1 } || string.IsNullOrWhiteSpace(state.InstallDir)) return false;

        string recorded;
        try
        {
            recorded = FileSystemUtilities.NormalizePath(state.InstallDir);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            recorded = state.InstallDir;
        }
        if (recorded.Equals(installation.Root, StringComparison.OrdinalIgnoreCase)) return false;

        var previous = state.InstallDir;
        state.InstallDir = installation.Root;
        JsonState.Write(installation.InstallStatePath, state);

        // The configured native build directory bakes absolute source, toolchain, and output paths
        // into CMakeCache.txt. After a move it is unusable and would fail the next configure rather
        // than being reused, so it is removed and reconfigured from scratch on the next build.
        var nativeBuild = Path.Combine(installation.WorkspaceDirectory, "native-build");
        var hadNativeBuild = Directory.Exists(nativeBuild);
        if (hadNativeBuild) FileSystemUtilities.DeleteDirectoryIfExists(nativeBuild);

        reporter?.Diagnostic(
            $"This portable installation moved from {previous} to {installation.Root}. " +
            "The recorded location was updated" +
            (hadNativeBuild ? " and the location-bound native build cache was discarded." : "."));
        return true;
    }
}

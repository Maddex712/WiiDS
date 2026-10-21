namespace WiiCompiled.Setup;

/// <summary>
/// A portable installation is a self-contained directory tree the user can move or carry on removable media:
/// <code>
/// &lt;root&gt;\portable.txt   marker; its contents are irrelevant
/// &lt;root&gt;\Install\       the installation directory
/// &lt;root&gt;\UserData\      runtime user state (Config.toml, NAND, Cache, Logs)
/// </code>
/// The runtime finds the same root independently (<c>runtime/include/runtime_config.h</c>,
/// <c>PortableRootDirectory</c>); this class must keep the same marker name, layout, and depth bound.
/// </summary>
internal static class PortableRoot
{
    public const string MarkerFileName = "portable.txt";
    public const string UserDataDirectoryName = "UserData";
    public const string InstallDirectoryName = "Install";

    /// <summary>
    /// Parents searched above the start directory. Kept small (installs sit 2 levels below root,
    /// <c>&lt;root&gt;\Install\Base\game.exe</c>) so an unrelated marker far up a drive can't capture it.
    /// </summary>
    public const int MaximumSearchDepth = 4;

    /// <summary>
    /// The portable root <paramref name="startDirectory"/> belongs to, or null otherwise. Callers pass the
    /// installation directory (not the running executable's location), since a downloaded setup runs from Downloads.
    /// </summary>
    public static string? TryFind(string startDirectory)
    {
        if (string.IsNullOrWhiteSpace(startDirectory)) return null;
        string current;
        try
        {
            // Normalize before trimming: "C:\" trimmed to "C:" is the current directory on that
            // drive, not the drive root.
            current = Normalize(startDirectory);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return null;
        }

        for (var level = 0; level <= MaximumSearchDepth; level++)
        {
            if (File.Exists(Path.Combine(current, MarkerFileName))) return current;
            var parent = Path.GetDirectoryName(current);
            if (string.IsNullOrEmpty(parent) || parent.Equals(current, StringComparison.Ordinal)) break;
            current = parent;
        }
        return null;
    }

    public static string UserDataDirectory(string root) => Path.Combine(root, UserDataDirectoryName);

    /// <summary>Whether <paramref name="candidate"/> is <paramref name="root"/> or lives inside it.</summary>
    public static bool Contains(string root, string candidate) =>
        FileSystemUtilities.PathContains(root, candidate);

    /// <summary>
    /// Establishes the root an install is about to publish into: the marker and the user-state
    /// directory must exist before anything resolves a configuration path, because that resolution is
    /// what decides whether the installation writes portable or machine-wide settings.
    /// </summary>
    public static string Create(string root)
    {
        var full = Normalize(root);
        EnsureUsableRoot(full);
        Directory.CreateDirectory(full);
        Directory.CreateDirectory(UserDataDirectory(full));
        var marker = Path.Combine(full, MarkerFileName);
        if (!File.Exists(marker))
        {
            File.WriteAllText(marker,
                $"{ProductInfo.Name} portable installation." + Environment.NewLine +
                "This marker makes the runtime keep Config.toml, NAND, Cache, and Logs in UserData\\ " +
                "beside it instead of in %LOCALAPPDATA%." + Environment.NewLine +
                "Delete it to make this installation use per-user application data again." +
                Environment.NewLine);
        }
        return full;
    }

    /// <summary>
    /// A portable root is deleted wholesale by the user, so like an install directory it may never be a
    /// drive root or well-known system location; both share <see cref="FileSystemUtilities.EnsureUsableLocation"/>.
    /// </summary>
    public static void EnsureUsableRoot(string root) =>
        FileSystemUtilities.EnsureUsableLocation(root, "A portable installation root");

    private static string Normalize(string path) => FileSystemUtilities.NormalizePath(path);
}

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

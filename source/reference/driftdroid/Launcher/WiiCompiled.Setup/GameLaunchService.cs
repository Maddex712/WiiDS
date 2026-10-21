using System.Diagnostics;

namespace WiiCompiled.Setup;

internal static class GameLaunchService
{
    public static Task<int> LaunchAsync(BuildProfile profile)
    {
        var installation = new Installation(AppContext.BaseDirectory);
        using var operation = InstallOperationLock.Acquire(installation.Root);
        // A portable installation the user moved is reconciled before anything is read from its
        // recorded location. Launch never needs the discarded native build tree.
        PortableInstallHealing.HealMovedInstall(installation,
            new DelegatingInstallReporter(Console.Error.WriteLine));
        EnsureProductIsCurrent(installation, profile);
        return Task.FromResult(LaunchInstalledProduct(installation, profile));
    }

    private static void EnsureProductIsCurrent(Installation installation, BuildProfile profile)
    {
        var state = InspectProductForLaunch(installation, profile);

        if (state.Status == ProductStatus.Current) return;
        if (state.Status == ProductStatus.Absent)
            throw new InvalidDataException(
                "Retro Rewind is not installed. Install Retro Rewind through Wheel Wizard first.");

        throw new InvalidDataException(string.IsNullOrWhiteSpace(state.Detail)
            ? "This product must be repaired through Wheel Wizard before it can be launched."
            : state.Detail);
    }

    /// <summary>Validates the executable, inputs, build provenance, runtime assets, payload cache, and the
    /// canonical Retro Rewind root, so a missing/invalid root fails launch instead of starting a vanilla-looking game.</summary>
    internal static ProductState InspectProductForLaunch(Installation installation, BuildProfile profile)
    {
        var toolkitFingerprint = installation.ResolveToolkitFingerprint();
        if (profile == BuildProfile.Base)
            return installation.CheckBase(toolkitFingerprint);
        var canonical = installation.ResolveCanonicalCompileInputs(null, out var canonicalError);
        return installation.CheckRetroRewind(toolkitFingerprint, canonical, canonicalError);
    }

    private static int LaunchInstalledProduct(Installation installation, BuildProfile profile)
    {
        var runtimeRoot = profile == BuildProfile.Base ? installation.BaseDirectory : installation.RetroDirectory;
        var runtime = profile == BuildProfile.Base ? installation.BaseExecutable : installation.RetroExecutable;
        if (!File.Exists(runtime))
            throw new FileNotFoundException("The installed recomp is missing. Run the installer again.", runtime);
        if (!File.Exists(Path.Combine(installation.GameDataDirectory, "sys", "fst.bin")))
            throw new InvalidDataException("The installed Mario Kart Wii game data is missing. Run the installer again.");

        var info = new ProcessStartInfo
        {
            FileName = runtime,
            WorkingDirectory = runtimeRoot,
            UseShellExecute = false
        };
        using var process = Process.Start(info) ?? throw new InvalidOperationException("Could not start the recomp.");
        process.WaitForExit();
        return process.ExitCode;
    }

}

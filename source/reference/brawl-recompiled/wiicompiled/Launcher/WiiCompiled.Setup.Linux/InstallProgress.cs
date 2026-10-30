using System.Text.Json;

namespace WiiCompiled.Setup.Linux;

// Ported near-verbatim from Launcher/WiiCompiled.Setup/InstallProgress.cs: this whole file is
// platform-neutral (System.Text.Json + Console only), so the NDJSON --progress-json wire protocol
// stays byte-for-byte the same shape a future GUI already speaks on Windows.

/// <summary>
/// Stable stage identifiers reported by <c>--progress-json</c>. Kept intentionally small for this
/// lean Linux installer (no toolkit-extraction/publish-transaction stages, since there is no
/// bundled toolkit or staged workspace copy here - see the plan's "operate on a git checkout"
/// scoping decision).
/// </summary>
internal static class InstallStages
{
    public const string Validate = "validate";
    public const string ExtractDisc = "extract-disc";
    public const string Build = "build";
    public const string Shortcuts = "shortcuts";
}

/// <summary>
/// Where an installation reports what it is doing. Progress is coarse and monotonic; raw translator
/// and compiler output is a diagnostic, never progress, because it is unbounded and machine-hostile.
/// </summary>
internal interface IInstallReporter
{
    void Progress(string stage, string message, int percent);
    void Diagnostic(string line);
}

/// <summary>
/// The <c>--progress-json</c> protocol: one JSON object per line on stdout, nothing else on stdout,
/// diagnostics on stderr. The terminal <c>result</c> line is written exactly once.
/// </summary>
internal sealed class NdjsonInstallReporter : IInstallReporter
{
    private static readonly JsonSerializerOptions Options = new() { WriteIndented = false };
    private readonly object _gate = new();
    private int _lastPercent;
    private bool _finished;

    public void Progress(string stage, string message, int percent)
    {
        lock (_gate)
        {
            if (_finished) return;
            // Percentages are clamped monotonic: a caller's progress bar must never walk backwards
            // because a later stage happened to estimate a lower number.
            _lastPercent = Math.Clamp(Math.Max(percent, _lastPercent), 0, 99);
            WriteLine(new { type = "progress", stage, message, percent = _lastPercent });
        }
    }

    public void Diagnostic(string line) => Console.Error.WriteLine(line);

    public void Success(string installDirectory)
    {
        lock (_gate)
        {
            if (_finished) return;
            _finished = true;
            WriteLine(new { type = "result", success = true, version = ProductInfo.Version, installDir = installDirectory });
        }
    }

    public void Failure(string error)
    {
        lock (_gate)
        {
            if (_finished) return;
            _finished = true;
            WriteLine(new { type = "result", success = false, error });
        }
    }

    /// <summary>
    /// The terminal result line is the caller's only completion signal, so no exit path may skip it.
    /// Callers invoke this from a finally block; it is a no-op once a result was already written.
    /// </summary>
    public void EnsureFinished(string errorIfUnfinished) => Failure(errorIfUnfinished);

    private static void WriteLine(object value)
    {
        Console.Out.WriteLine(JsonSerializer.Serialize(value, Options));
        Console.Out.Flush();
    }
}

/// <summary>Plain-text console reporting for a run without <c>--progress-json</c>.</summary>
internal sealed class ConsoleInstallReporter : IInstallReporter
{
    public void Progress(string stage, string message, int percent) =>
        Console.Out.WriteLine($"[{percent,3}%] {message}");

    public void Diagnostic(string line) => Console.Out.WriteLine(line);
}

/// <summary>
/// Build step identifiers from local-build.sh's <c>MKWCBUILD:STEP:&lt;id&gt;</c> lines - the id is
/// the contract, matched against Launcher/local-build.sh's log_step() call sites.
/// </summary>
internal static class BuildStepIds
{
    public const string BuildTranslator = "build-translator";
    public const string ReuseBaseTranslation = "reuse-base-translation";
    public const string RetranslateBase = "retranslate-base";
    public const string TranslateBase = "translate-base";
    public const string EmitBaseManifest = "emit-base-manifest";
    public const string TranslateMod = "translate-mod";
    public const string GenerateDataInit = "generate-data-init";
    public const string EmitBuildShards = "emit-build-shards";
    public const string ConfigureNative = "configure-native";
    public const string Compile = "compile";
}

/// <summary>
/// Maps one local-build.sh run onto a slice of the overall percentage. local-build.sh announces
/// every step it starts with an <c>MKWCBUILD:</c> prefix, so the slice can advance on real events
/// instead of on a timer.
/// </summary>
internal sealed class BuildProgressWindow
{
    private const string Marker = "MKWCBUILD:";
    private const string StepMarker = "STEP:";

    /// <summary>The fraction the compile step reaches; beyond it, compiler output is a heartbeat.</summary>
    private const double CompileFraction = 0.58;

    private static readonly (string Id, double Fraction, string Message)[] Steps =
    [
        (BuildStepIds.BuildTranslator, 0.04, "Building the translator"),
        (BuildStepIds.ReuseBaseTranslation, 0.30, "Reusing the completed base translation"),
        (BuildStepIds.RetranslateBase, 0.08, "The base translation is stale; retranslating it"),
        (BuildStepIds.TranslateBase, 0.10, "Translating Mario Kart Wii"),
        (BuildStepIds.EmitBaseManifest, 0.34, "Creating the translation manifest"),
        (BuildStepIds.TranslateMod, 0.38, "Translating the Retro Rewind Code.pul"),
        (BuildStepIds.GenerateDataInit, 0.44, "Generating game data initialization"),
        (BuildStepIds.EmitBuildShards, 0.48, "Preparing the native build"),
        (BuildStepIds.ConfigureNative, 0.52, "Configuring the compiler"),
        (BuildStepIds.Compile, CompileFraction, "Compiling the game. This is the longest step"),
    ];

    private readonly IInstallReporter _reporter;
    private readonly string _stage;
    private readonly int _start;
    private readonly int _end;
    private double _fraction;
    private string _message = "Preparing the local build";
    private int _reportedPercent = -1;

    public BuildProgressWindow(IInstallReporter reporter, string stage, int start, int end)
    {
        _reporter = reporter;
        _stage = stage;
        _start = start;
        _end = end;
    }

    public void Observe(string line)
    {
        var index = line.IndexOf(Marker, StringComparison.Ordinal);
        if (index >= 0)
        {
            var text = line[(index + Marker.Length)..].Trim();
            if (text.StartsWith(StepMarker, StringComparison.Ordinal))
            {
                var identifier = text[StepMarker.Length..];
                var end = identifier.IndexOf(' ');
                if (end >= 0) identifier = identifier[..end];
                foreach (var (id, fraction, message) in Steps)
                {
                    if (!id.Equals(identifier, StringComparison.Ordinal)) continue;
                    if (fraction > _fraction)
                    {
                        _fraction = fraction;
                        _message = message;
                        Emit();
                    }
                    return;
                }
            }
        }

        // Anything else - a plain MKWCBUILD note, or raw tool output - stays a diagnostic and only
        // feeds the heartbeat below.
        _reporter.Diagnostic(line);
        // Compilation announces itself once and then emits thousands of compiler lines. Treat that
        // output as a heartbeat so the slice keeps creeping forward, but only publish a progress
        // line when the rounded percentage actually changes.
        if (_fraction >= CompileFraction)
        {
            _fraction = Math.Min(0.97, _fraction + 0.0015);
            Emit();
        }
    }

    private void Emit()
    {
        var percent = Interpolate(_fraction);
        if (percent == _reportedPercent) return;
        _reportedPercent = percent;
        _reporter.Progress(_stage, _message, percent);
    }

    private int Interpolate(double fraction) =>
        (int)Math.Round(_start + (_end - _start) * Math.Clamp(fraction, 0, 1));
}

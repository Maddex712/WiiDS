using System.Diagnostics;
using System.Buffers.Binary;
using System.Net;
using System.Security.Cryptography;
using System.Text.Json;

namespace WiiCompiled.Setup;

internal static class InputValidation
{
    private const long MaximumRetroWfcPayloadBytes = 16L * 1024 * 1024;
    // The payload is tens of kilobytes from a single fixed endpoint 30s is good.
    private static readonly TimeSpan RetroWfcDownloadTimeout = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan RetroWfcRetryDelay = TimeSpan.FromSeconds(1);
    private static readonly HashSet<string> SupportedDiscImageExtensions = new(
        [".iso", ".gcm", ".gcz", ".ciso", ".wbfs", ".wia", ".rvz"],
        StringComparer.OrdinalIgnoreCase);

    public const string CurrentRetroWfcPayloadUri = "http://nas.play.rwfc.net/payload?g=RMCPD00";
    private static readonly string RetroWfcOfflinePayloadFile =
        Path.Combine("binary", "payload.RMCPD00.bin");

    /// <summary>Retro-WFC production payload signing key (PROD PayloadPublicKey). Verifies
    /// the payload past the 0x110 header; on-console stage1 pins the same key, so
    /// rotating it upstream is a breaking release there too.</summary>
    private static readonly byte[] RetroWfcPayloadSigningModulus = Convert.FromHexString(
        "e6e6ce416f350422cbe26c36a67eba613dddcd27d79afd077dcc593e5319eaa6" +
        "080293400033876d3dbdfda12c15f46ac8e4f5b40c56e7b5f67e91647d618cb9" +
        "99c041581b86d103bd7723fceac03ad3ad5134bf611cd47dc527002596821e94" +
        "1c9470938fea07238a84767323e4a610bd996465e59d04dae4febd915c96fc07" +
        "39e4e818300829d78f3f2275e1f3fbd2507f1bde74f24a5285e61007b959a583" +
        "b4820d75eca76680866efe5d79590b82c3577b796155899530e305b94b4ceef4" +
        "428644b719df3d8540c9588f5bb02d83d3938255d1a1e073d3408163ff93a615" +
        "a2106a03923a397aad6a29ebb43031ed06de1575c8ee2b54678fa059e025f455");

    private const int RetroWfcPayloadSignedRegionOffset = 0x110;
    private const int RetroWfcPayloadSignatureOffset = 0x10;
    private const int RetroWfcPayloadMinimumBytes = 0x130;

    public static void ValidateExtension(string gamePath)
    {
        if (!File.Exists(gamePath))
            throw new FileNotFoundException("The selected game image does not exist.", gamePath);
        var extension = Path.GetExtension(gamePath);
        if (!SupportedDiscImageExtensions.Contains(extension))
            throw new InvalidDataException(
                "Select a complete Wii disc image in ISO, GCM, GCZ, CISO, WBFS, WIA, or RVZ format.");
    }

    public static async Task<DiscHeader> ReadDiscHeaderAsync(string dolphinTool, string gamePath,
        CancellationToken cancellationToken = default)
    {
        ValidateExtension(gamePath);
        var result = await ProcessRunner.RunAsync(dolphinTool,
            ["header", "-i", Path.GetFullPath(gamePath), "-j"], null, cancellationToken);
        if (result.ExitCode != 0)
            throw new InvalidDataException("DolphinTool could not read this disc image. " + result.CombinedOutput.Trim());

        var json = result.StandardOutput.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries)
            .FirstOrDefault(line => line.TrimStart().StartsWith('{'));
        if (json is null)
            throw new InvalidDataException("DolphinTool did not return disc metadata.");
        return JsonSerializer.Deserialize<DiscHeader>(json)
               ?? throw new InvalidDataException("DolphinTool returned invalid disc metadata.");
    }

    public static void EnsureCompatibleDisc(DiscHeader header, PayloadManifest manifest)
    {
        if (!header.GameId.Equals(manifest.ExpectedGameId, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException(
                $"This build supports Mario Kart Wii PAL ({manifest.ExpectedGameId}). " +
                $"The selected image is {header.GameId} ({header.InternalName}, {header.Region}).");
        }
    }

    public static string ValidateStagedRetroWfcPayloadDirectory(string stagedDirectory,
        RSAParameters? signingKey = null)
    {
        if (string.IsNullOrWhiteSpace(stagedDirectory))
            throw new InvalidDataException("The staged Retro-WFC payload directory is missing.");

        var root = Path.GetFullPath(stagedDirectory);
        var payload = Path.Combine(root, RetroWfcOfflinePayloadFile);
        if (!File.Exists(payload))
            throw new InvalidDataException(
                "The staged Retro-WFC payload directory does not contain binary\\payload.RMCPD00.bin.");
        ValidateRetroWfcPayloadFile(payload, signingKey);
        return root;
    }

    public static string ResolveRetroWfcPayloadFile(string stagedDirectory,
        RSAParameters? signingKey = null) =>
        Path.Combine(ValidateStagedRetroWfcPayloadDirectory(stagedDirectory, signingKey),
            RetroWfcOfflinePayloadFile);

    public static string ComputeRetroWfcPayloadSha256(string stagedDirectory,
        RSAParameters? signingKey = null) =>
        Sha256File(ResolveRetroWfcPayloadFile(stagedDirectory, signingKey));

    public static void ValidateRetroWfcPayloadUri(string uriText)
    {
        if (!string.Equals(uriText, CurrentRetroWfcPayloadUri, StringComparison.Ordinal))
            throw new InvalidDataException("The installer does not define the fixed Retro-WFC payload endpoint.");
    }

    public static async Task<RetroWfcPayloadSnapshot> DownloadRetroWfcPayloadAsync(string uriText,
        string destinationDirectory,
        CancellationToken cancellationToken)
    {
        ValidateRetroWfcPayloadUri(uriText);
        var uri = new Uri(uriText, UriKind.Absolute);

        var root = Path.GetFullPath(destinationDirectory);
        var destination = Path.Combine(root, RetroWfcOfflinePayloadFile);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        using var handler = new HttpClientHandler { AllowAutoRedirect = false };
        using var client = new HttpClient(handler) { Timeout = Timeout.InfiniteTimeSpan };

        for (var attempt = 1; attempt <= 2; attempt++)
        {
            try
            {
                return await DownloadRetroWfcPayloadAttemptAsync(client, uri, root, destination,
                    cancellationToken);
            }
            catch (Exception ex) when (attempt == 1 &&
                                       IsTransientRetroWfcDownloadFailure(ex, cancellationToken))
            {
                await Task.Delay(RetroWfcRetryDelay, cancellationToken);
            }
        }

        throw new InvalidOperationException("The Retro-WFC download retry loop ended unexpectedly.");
    }

    private static async Task<RetroWfcPayloadSnapshot> DownloadRetroWfcPayloadAttemptAsync(
        HttpClient client, Uri uri, string root, string destination, CancellationToken cancellationToken)
    {
        var temporary = destination + $".tmp-{Guid.NewGuid():N}";
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(RetroWfcDownloadTimeout);
        var attemptToken = timeout.Token;
        try
        {
            using var response = await client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, attemptToken);
            if ((int)response.StatusCode is >= 300 and < 400)
                throw new InvalidDataException(
                    "The fixed Retro-WFC payload endpoint redirected; refusing to fetch from a different target.");
            response.EnsureSuccessStatusCode();
            if (response.Content.Headers.ContentLength is > MaximumRetroWfcPayloadBytes)
                throw new InvalidDataException("The Retro-WFC payload is unexpectedly large.");
            await using var input = await response.Content.ReadAsStreamAsync(attemptToken);
            long total = 0;
            string actualSha256;
            await using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write,
                             FileShare.None, 64 * 1024, FileOptions.Asynchronous))
            {
                var buffer = new byte[64 * 1024];
                using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
                while (true)
                {
                    var read = await input.ReadAsync(buffer, attemptToken);
                    if (read == 0) break;
                    total = checked(total + read);
                    if (total > MaximumRetroWfcPayloadBytes)
                        throw new InvalidDataException("The Retro-WFC payload is unexpectedly large.");
                    hash.AppendData(buffer, 0, read);
                    await output.WriteAsync(buffer.AsMemory(0, read), attemptToken);
                }
                actualSha256 = Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
            }
            ValidateRetroWfcPayloadFile(temporary);
            File.Move(temporary, destination, overwrite: true);
            return new RetroWfcPayloadSnapshot(root, actualSha256, total);
        }
        catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"The Retro-WFC payload download did not finish within {RetroWfcDownloadTimeout.TotalMinutes:0} minutes.",
                ex);
        }
        finally
        {
            try { File.Delete(temporary); } catch { }
        }
    }

    internal static bool IsTransientRetroWfcDownloadFailure(Exception exception,
        CancellationToken cancellationToken)
    {
        if (cancellationToken.IsCancellationRequested) return false;
        if (exception is TimeoutException) return true;
        if (exception is not HttpRequestException request) return false;
        return request.StatusCode is null or HttpStatusCode.RequestTimeout or
               HttpStatusCode.TooManyRequests ||
               request.StatusCode is { } status && (int)status >= 500;
    }

    private static void ValidateRetroWfcPayloadFile(string payload, RSAParameters? signingKey = null)
    {
        byte[] image;
        using (var stream = File.OpenRead(payload))
        {
            if (stream.Length > MaximumRetroWfcPayloadBytes)
                throw new InvalidDataException("The Retro-WFC payload is unexpectedly large.");
            if (stream.Length < RetroWfcPayloadMinimumBytes)
                throw new InvalidDataException("The Retro-WFC payload has an invalid header.");
            image = new byte[stream.Length];
            stream.ReadExactly(image);
        }

        if (!image.AsSpan(0, 12).SequenceEqual("WWFC/Payload"u8))
            throw new InvalidDataException("The Retro-WFC payload has an invalid header.");
        var declaredSize = BinaryPrimitives.ReadUInt32BigEndian(image.AsSpan(0x0C));
        if (declaredSize != image.Length)
            throw new InvalidDataException(
                $"The Retro-WFC payload declares {declaredSize} bytes but contains {image.Length}.");

        using var rsa = RSA.Create();
        rsa.ImportParameters(signingKey ?? new RSAParameters
        {
            Modulus = RetroWfcPayloadSigningModulus,
            Exponent = [0x01, 0x00, 0x01],
        });
        if (!rsa.VerifyData(
                image.AsSpan(RetroWfcPayloadSignedRegionOffset),
                image.AsSpan(RetroWfcPayloadSignatureOffset,
                    RetroWfcPayloadSignedRegionOffset - RetroWfcPayloadSignatureOffset),
                HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1))
            throw new InvalidDataException(
                "The Retro-WFC payload is not signed by the pinned Retro-WFC signing key.");
    }

    public static string Sha256File(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }
}

internal sealed record ProcessResult(int ExitCode, string StandardOutput, string StandardError)
{
    public string CombinedOutput => StandardOutput + Environment.NewLine + StandardError;
}

internal static class ProcessRunner
{
    /// <summary>Runs a redirected child process to completion. <paramref name="configure"/> sets up a
    /// working directory or scrubbed environment; <paramref name="capture"/> is off for callers that only
    /// forward output live, so a build's output isn't buffered in memory for nobody to read.</summary>
    public static async Task<ProcessResult> RunAsync(string executable, IReadOnlyList<string> arguments,
        Action<string>? output, CancellationToken cancellationToken,
        Action<ProcessStartInfo>? configure = null, bool capture = true,
        Action<Exception>? onTerminationFailure = null)
    {
        var info = new ProcessStartInfo
        {
            FileName = executable,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (var argument in arguments) info.ArgumentList.Add(argument);
        configure?.Invoke(info);

        using var process = new Process { StartInfo = info, EnableRaisingEvents = true };
        var stdout = new List<string>();
        var stderr = new List<string>();
        process.OutputDataReceived += (_, e) => { if (e.Data is not null) { if (capture) stdout.Add(e.Data); output?.Invoke(e.Data); } };
        process.ErrorDataReceived += (_, e) => { if (e.Data is not null) { if (capture) stderr.Add(e.Data); output?.Invoke(e.Data); } };
        if (!process.Start()) throw new InvalidOperationException($"Could not start {executable}.");
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        await WaitForExitAsync(process, cancellationToken, onTerminationFailure);
        return new ProcessResult(process.ExitCode, string.Join(Environment.NewLine, stdout),
            string.Join(Environment.NewLine, stderr));
    }

    public static async Task WaitForExitAsync(Process process, CancellationToken cancellationToken,
        Action<Exception>? onTerminationFailure = null)
    {
        try
        {
            await process.WaitForExitAsync(cancellationToken);
        }
        catch (OperationCanceledException)
        {
            try
            {
                if (!process.HasExited) process.Kill(entireProcessTree: true);
            }
            catch (Exception ex)
            {
                onTerminationFailure?.Invoke(ex);
            }
            await process.WaitForExitAsync(CancellationToken.None);
            process.WaitForExit();
            throw;
        }
        process.WaitForExit();
    }
}

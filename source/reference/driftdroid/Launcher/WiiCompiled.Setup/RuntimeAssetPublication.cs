namespace WiiCompiled.Setup;

/// <summary>
/// The one path by which a product receives its copied runtime assets, shared by install and repair.
/// Each product gets its own moved (not copied) entry, re-verified against the authoritative identity
/// in case the source changed mid-preparation.
/// </summary>
internal static class RuntimeAssetPublication
{
    public static void AddEntries(List<InstallTransactionEntry> entries, string sourceAssets,
        string preparedRoot, IEnumerable<(string Name, string Destination)> products,
        string expectedFingerprint, string operationDescription, Action<string> diagnostic,
        CancellationToken cancellationToken)
    {
        foreach (var (name, destination) in products)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var preparedProduct = Path.Combine(preparedRoot, name);
            Directory.CreateDirectory(preparedProduct);
            FileSystemUtilities.CopyDirectory(
                Path.Combine(sourceAssets, ProductRuntimeAssets.SourceBootstrapDirectoryName),
                Path.Combine(preparedProduct, ProductRuntimeAssets.ProductBootstrapDirectoryName),
                cancellationToken);
            foreach (var (relativePath, fileName) in ProductRuntimeAssets.Files)
            {
                cancellationToken.ThrowIfCancellationRequested();
                File.Copy(ProductRuntimeAssets.SourceFile(sourceAssets, relativePath),
                    Path.Combine(preparedProduct, fileName));
            }

            if (!ToolkitFingerprint.ProductRuntimeAssetsMatch(preparedProduct, expectedFingerprint,
                    cancellationToken, diagnostic))
            {
                throw new IOException(
                    $"A copied product runtime asset changed while {operationDescription} was being prepared.");
            }

            entries.Add(InstallTransactionEntry.Directory(
                Path.Combine(preparedProduct, ProductRuntimeAssets.ProductBootstrapDirectoryName),
                Path.Combine(destination, ProductRuntimeAssets.ProductBootstrapDirectoryName)));
            foreach (var (_, fileName) in ProductRuntimeAssets.Files)
                entries.Add(InstallTransactionEntry.File(Path.Combine(preparedProduct, fileName),
                    Path.Combine(destination, fileName)));
        }
    }
}

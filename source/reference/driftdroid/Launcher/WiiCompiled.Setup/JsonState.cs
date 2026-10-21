using System.Text.Json;

namespace WiiCompiled.Setup;

/// <summary>Reads and atomically writes the small JSON state documents kept inside an installation.</summary>
internal static class JsonState
{
    private static readonly JsonSerializerOptions ReadOptions = new() { PropertyNameCaseInsensitive = true };
    private static readonly JsonSerializerOptions WriteOptions = new() { WriteIndented = true };

    public static T? TryRead<T>(string path) where T : class
    {
        try
        {
            return File.Exists(path) ? JsonSerializer.Deserialize<T>(File.ReadAllText(path), ReadOptions) : null;
        }
        catch
        {
            // A truncated or hand-edited state document must degrade into "unknown", which every
            // caller already treats as "assume stale and rebuild", not into a failed launch.
            return null;
        }
    }

    public static void Write<T>(string path, T value) =>
        FileSystemUtilities.WriteAtomic(path, JsonSerializer.Serialize(value, WriteOptions));
}

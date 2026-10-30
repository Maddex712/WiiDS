using System.Buffers.Binary;
using System.Security.Cryptography;

namespace BrawlRecompiled;

// Imports Super Smash Bros. Brawl saves into the game's NAND: a Dolphin save folder (title\00010000\52534245\data,
// the same files the game keeps) or a data.bin exported from a Wii (Brawl marks its save no-copy, so that takes a
// homebrew save manager), decrypted here the way Dolphin's WiiSave.cpp does it.
static class WiiSave
{
    public const ulong TitleId = 0x0001000052534245;  // RSBE (the USA disc)
    static readonly byte[] SdKey = Convert.FromHexString("AB01B9D8E1622B08AFBAD84DBFC2A55D");
    static readonly byte[] SdIv = Convert.FromHexString("216712E6AA1F689F95C5A22324DC6A98");
    const int HeaderSize = 0xF0C0, BkHeaderSize = 0x80, FileHeaderSize = 0x80;

    // The game's title id from its disc id ("RSBE01" -> 0001000052534245): saves live under it.
    public static ulong TitleIdOf(string gameId) => 0x0001000000000000UL | BinaryPrimitives.ReadUInt32BigEndian(System.Text.Encoding.ASCII.GetBytes(gameId.PadRight(4)[..4]));

    // The save folder the game uses for this disc. Like the runtime (nand_path.h ManagedNandRootPath): UserData\NAND
    // belongs to the first disc played (its key in NAND\.game), every other disc gets Saves\<key>\NAND.
    public static string DataDir(string userData, string discRoot, string gameId)
    {
        string nand = Path.Combine(userData, "NAND");
        string key = GameSaveKey(discRoot);
        string owner = "";
        try { owner = File.ReadLines(Path.Combine(nand, ".game")).FirstOrDefault() ?? ""; } catch (IOException) { }
        if (key.Length > 0 && owner.Length > 0 && owner != key) nand = Path.Combine(userData, "Saves", key, "NAND");
        return Path.Combine(nand, "title", "00010000", (TitleIdOf(gameId) & 0xFFFFFFFF).ToString("x8"), "data");
    }

    // nand_path.h GameSaveKey: FNV-1a 64 over the extracted disc's files (relative path with '/', then the size as 8
    // little-endian bytes), sorted by path; "" when the folder is missing or empty.
    public static string GameSaveKey(string discRoot)
    {
        if (!Directory.Exists(discRoot)) return "";
        var files = Directory.EnumerateFiles(discRoot, "*", SearchOption.AllDirectories)
            .Select(f => (name: Path.GetRelativePath(discRoot, f).Replace('\\', '/'), size: new FileInfo(f).Length))
            .OrderBy(f => f.name, StringComparer.Ordinal).ToList();
        if (files.Count == 0) return "";
        ulong hash = 1469598103934665603UL;
        void Mix(ReadOnlySpan<byte> data) { foreach (byte b in data) hash = (hash ^ b) * 1099511628211UL; }
        Span<byte> size = stackalloc byte[8];
        foreach (var (name, length) in files)
        {
            Mix(System.Text.Encoding.UTF8.GetBytes(name));
            BinaryPrimitives.WriteUInt64LittleEndian(size, (ulong)length);
            Mix(size);
        }
        return hash.ToString("x16");
    }

    // Copies the save into dataDir; an existing save there is moved to data.backup-<time> first. Returns the files written.
    public static List<string> Import(string source, string dataDir)
    {
        var files = Path.GetFileName(source).Equals("data.bin", StringComparison.OrdinalIgnoreCase)
            ? ReadDataBin(File.ReadAllBytes(source))
            : ReadDolphinFolder(Path.GetDirectoryName(Path.GetFullPath(source))!);
        if (!files.Keys.Any(n => n.StartsWith("autosv", StringComparison.Ordinal) || n == "collect.vff"))
            throw new InvalidDataException("No Super Smash Bros. Brawl save files (autosv0.bin, collect.vff) were found there.");

        if (Directory.Exists(dataDir) && Directory.EnumerateFileSystemEntries(dataDir).Any())
            Directory.Move(dataDir, dataDir + ".backup-" + DateTime.Now.ToString("yyyyMMdd-HHmmss"));
        Directory.CreateDirectory(dataDir);
        foreach (var (name, bytes) in files) File.WriteAllBytes(Path.Combine(dataDir, name), bytes);
        return files.Keys.ToList();
    }

    static Dictionary<string, byte[]> ReadDolphinFolder(string dir)
    {
        var files = new Dictionary<string, byte[]>();
        foreach (var path in Directory.GetFiles(dir))
        {
            var name = Path.GetFileName(path);
            if (!name.StartsWith('.')) files[name] = File.ReadAllBytes(path);  // autosv*.bin, net*.bin, collect.vff, banner.bin
        }
        return files;
    }

    public static Dictionary<string, byte[]> ReadDataBin(byte[] bin)
    {
        if (bin.Length < HeaderSize + BkHeaderSize) throw new InvalidDataException("This data.bin is too small to be a Wii save.");
        var header = Decrypt(bin.AsSpan(0, HeaderSize), SdIv);
        ulong tid = BinaryPrimitives.ReadUInt64BigEndian(header);
        if (tid != TitleId)
            throw new InvalidDataException($"This data.bin is not a Super Smash Bros. Brawl (USA) save (title {tid:X16}).");
        int bannerSize = (int)BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(8));

        var bk = bin.AsSpan(HeaderSize, BkHeaderSize);
        if (BinaryPrimitives.ReadUInt32BigEndian(bk[4..]) != 0x426B0001) throw new InvalidDataException("This data.bin has no file table.");
        int count = (int)BinaryPrimitives.ReadUInt32BigEndian(bk[0xC..]);

        var files = new Dictionary<string, byte[]>();
        if (bannerSize > 0 && bannerSize <= 0xF0A0) files["banner.bin"] = header.AsSpan(0x20, bannerSize).ToArray();
        int pos = HeaderSize + BkHeaderSize;
        for (int i = 0; i < count; i++)
        {
            if (pos + FileHeaderSize > bin.Length) throw new InvalidDataException("This data.bin is cut short.");
            var fh = bin.AsSpan(pos, FileHeaderSize);
            if (BinaryPrimitives.ReadUInt32BigEndian(fh) != 0x03ADF17E) throw new InvalidDataException("This data.bin has a damaged file entry.");
            int size = (int)BinaryPrimitives.ReadUInt32BigEndian(fh[4..]);
            byte type = fh[0xA];
            var name = System.Text.Encoding.ASCII.GetString(fh.Slice(0xB, 0x40)).Split('\0')[0];
            var iv = fh.Slice(0x50, 0x10).ToArray();
            pos += FileHeaderSize;
            if (type != 1) continue;  // 2 = directory, nothing to read
            int stored = (size + 0x3F) & ~0x3F;
            if (pos + stored > bin.Length) throw new InvalidDataException("This data.bin is cut short.");
            // Only plain file names: the save's files sit directly in data\.
            if (name.Length == 0 || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.Contains(".."))
                throw new InvalidDataException($"This data.bin holds an unexpected file name ({name}).");
            files[name] = Decrypt(bin.AsSpan(pos, stored), iv).AsSpan(0, size).ToArray();
            pos += stored;
        }
        return files;
    }

    static byte[] Decrypt(ReadOnlySpan<byte> data, byte[] iv)
    {
        using var aes = Aes.Create();
        aes.Key = SdKey;
        return aes.DecryptCbc(data, iv, PaddingMode.None);
    }

    // Test helper (--selftest): the inverse of ReadDataBin for the files given.
    public static byte[] WriteDataBin(Dictionary<string, byte[]> files)
    {
        using var aes = Aes.Create();
        aes.Key = SdKey;
        var header = new byte[HeaderSize];
        BinaryPrimitives.WriteUInt64BigEndian(header, TitleId);
        var banner = files.GetValueOrDefault("banner.bin") ?? Array.Empty<byte>();
        BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(8), (uint)banner.Length);
        banner.CopyTo(header, 0x20);
        var output = new List<byte>(aes.EncryptCbc(header, SdIv, PaddingMode.None));
        var entries = files.Where(f => f.Key != "banner.bin").ToList();
        var bk = new byte[BkHeaderSize];
        BinaryPrimitives.WriteUInt32BigEndian(bk, 0x70);
        BinaryPrimitives.WriteUInt32BigEndian(bk.AsSpan(4), 0x426B0001);
        BinaryPrimitives.WriteUInt32BigEndian(bk.AsSpan(0xC), (uint)entries.Count);
        output.AddRange(bk);
        foreach (var (name, data) in entries)
        {
            var fh = new byte[FileHeaderSize];
            BinaryPrimitives.WriteUInt32BigEndian(fh, 0x03ADF17E);
            BinaryPrimitives.WriteUInt32BigEndian(fh.AsSpan(4), (uint)data.Length);
            fh[0xA] = 1;
            System.Text.Encoding.ASCII.GetBytes(name).CopyTo(fh, 0xB);
            var iv = RandomNumberGenerator.GetBytes(16);
            iv.CopyTo(fh, 0x50);
            output.AddRange(fh);
            var padded = new byte[(data.Length + 0x3F) & ~0x3F];
            data.CopyTo(padded, 0);
            output.AddRange(aes.EncryptCbc(padded, iv, PaddingMode.None));
        }
        return output.ToArray();
    }
}

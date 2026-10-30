using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Reflection;

namespace BrawlRecompiled;

// The launcher's art, embedded from art\ (tools/make_launcher_art.py takes it from the game's files): fighter
// portraits, the Smash Ball, and the game's title font as a glyph sheet with its widths.
static class GameArt
{
    static readonly Dictionary<string, Bitmap> images = new();

    public static Bitmap? Image(string name)
    {
        if (images.TryGetValue(name, out var cached)) return cached;
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("art." + name + ".png");
        if (stream == null) return null;
        return images[name] = new Bitmap(stream);
    }

    // ----- The game's title font (font_hira) -----
    static Bitmap? sheet;
    static readonly Dictionary<char, (int x, int width, int left, int advance)> glyphs = new();
    static int cellW, cellH;

    static bool LoadFont()
    {
        if (sheet != null) return true;
        sheet = Image("font");
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("art.font.txt");
        if (sheet == null || stream == null) return false;
        using var reader = new StreamReader(stream);
        var head = reader.ReadLine()!.Split(' ');
        cellW = int.Parse(head[1]);
        cellH = int.Parse(head[2]);
        while (reader.ReadLine() is { Length: > 0 } line)
        {
            var f = line.Split(' ');
            glyphs[(char)int.Parse(f[0])] = (int.Parse(f[1]), int.Parse(f[2]), int.Parse(f[3]), int.Parse(f[4]));
        }
        return true;
    }

    // The text's width in font pixels (cell height = 32).
    static int Advance(string text) => text.Sum(c => glyphs.TryGetValue(c, out var g) || glyphs.TryGetValue(char.ToUpperInvariant(c), out g) ? g.advance : cellW / 2);

    // Text in the game's font, as the game draws its titles: a fill (top to bottom gradient), a dark outline, leaning
    // forward. Height in pixels; the result is cached per look.
    static readonly Dictionary<(string, int, int, int, int, bool), Bitmap> rendered = new();

    public static Bitmap Text(string text, int height, Color top, Color bottom, Color outline, bool slant = true)
    {
        var key = (text, height, top.ToArgb(), bottom.ToArgb(), outline.ToArgb(), slant);
        if (rendered.TryGetValue(key, out var cached)) return cached;
        if (!LoadFont())
        {
            // No art (a build without it): the system font, so the launcher still works.
            var fallback = new Bitmap(Math.Max(1, text.Length * height * 2 / 3), height + 4);
            using (var g = Graphics.FromImage(fallback))
            using (var f = new Font("Segoe UI", height * 0.6f, FontStyle.Bold | FontStyle.Italic, GraphicsUnit.Pixel))
                g.DrawString(text, f, new SolidBrush(top), 0, 0);
            return rendered[key] = fallback;
        }

        // The glyphs' coverage at the font's own size.
        int width = Math.Max(1, Advance(text) + 4);
        using var mask = new Bitmap(width, cellH, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(mask))
        {
            int x = 2;
            foreach (char c in text)
            {
                if (!glyphs.TryGetValue(c, out var glyph) && !glyphs.TryGetValue(char.ToUpperInvariant(c), out glyph)) { x += cellW / 2; continue; }
                if (glyph.width > 0)
                    g.DrawImage(sheet!, new Rectangle(x + glyph.left, 0, glyph.width, cellH), new Rectangle(glyph.x, 0, glyph.width, cellH), GraphicsUnit.Pixel);
                x += glyph.advance;
            }
        }

        // Scaled to the height, leaned forward, outlined and filled.
        float scale = height / (float)cellH;
        int lean = slant ? (int)(height * 0.22f) : 0;
        int pad = Math.Max(2, height / 10);
        var result = new Bitmap((int)(width * scale) + lean + pad * 2, height + pad * 2, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(result))
        {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            // Shear: the bottom stays put, the top moves right.
            var place = new Matrix(scale, 0, -lean / (float)height * scale, scale, pad + lean, pad);
            Bitmap Tinted(Color color)
            {
                var tinted = new Bitmap(mask.Width, mask.Height, PixelFormat.Format32bppArgb);
                using var tg = Graphics.FromImage(tinted);
                var matrix = new ColorMatrix(new[]
                {
                    new float[] { 0, 0, 0, 0, 0 }, new float[] { 0, 0, 0, 0, 0 }, new float[] { 0, 0, 0, 0, 0 },
                    new float[] { 0, 0, 0, color.A / 255f, 0 }, new float[] { color.R / 255f, color.G / 255f, color.B / 255f, 0, 1 },
                });
                using var attributes = new ImageAttributes();
                attributes.SetColorMatrix(matrix);
                tg.DrawImage(mask, new Rectangle(0, 0, mask.Width, mask.Height), 0, 0, mask.Width, mask.Height, GraphicsUnit.Pixel, attributes);
                return tinted;
            }
            g.Transform = place;
            using (var edge = Tinted(outline))
            {
                float o = Math.Max(1.2f, 2.4f / scale);
                for (int i = 0; i < 12; i++)
                {
                    double a = i * Math.PI / 6;
                    g.DrawImage(edge, (float)(Math.Cos(a) * o), (float)(Math.Sin(a) * o));
                }
            }
            // The fill: the glyph coverage through a vertical gradient.
            using var fill = new Bitmap(mask.Width, mask.Height, PixelFormat.Format32bppArgb);
            using (var fg = Graphics.FromImage(fill))
            using (var brush = new LinearGradientBrush(new Rectangle(0, 0, 1, cellH), top, bottom, LinearGradientMode.Vertical))
                fg.FillRectangle(brush, 0, 0, fill.Width, fill.Height);
            var bits = fill.LockBits(new Rectangle(0, 0, fill.Width, fill.Height), ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
            var maskBits = mask.LockBits(new Rectangle(0, 0, mask.Width, mask.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            unsafe
            {
                for (int y = 0; y < fill.Height; y++)
                {
                    byte* row = (byte*)bits.Scan0 + y * bits.Stride, mrow = (byte*)maskBits.Scan0 + y * maskBits.Stride;
                    for (int x = 0; x < fill.Width; x++) row[x * 4 + 3] = (byte)(row[x * 4 + 3] * mrow[x * 4 + 3] / 255);
                }
            }
            fill.UnlockBits(bits);
            mask.UnlockBits(maskBits);
            g.DrawImage(fill, 0, 0);
        }
        return rendered[key] = result;
    }
}

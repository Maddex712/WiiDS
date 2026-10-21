# Make the app icons from one square picture: Android adaptive-icon foregrounds (mipmap-*), the
# background colour, and a multi-size Windows .ico (PNG entries) for the executable.
#   make_icons.ps1 -Source icon.jpg -AndroidRes <app>\src\main\res -Ico <out>.ico
[CmdletBinding()]
param([string]$Source, [string]$AndroidRes, [string]$Ico)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @'
using System; using System.Drawing; using System.Drawing.Drawing2D; using System.Drawing.Imaging;
using System.IO; using System.Collections.Generic;
public static class Icons {
    static Bitmap Scaled(Image src, int size) {
        var b = new Bitmap(size, size, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(b)) {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.SmoothingMode = SmoothingMode.HighQuality;
            g.DrawImage(src, new Rectangle(0, 0, size, size));
        }
        return b;
    }
    // The average colour of the outer 3% of the picture: the adaptive icon's background.
    public static Color BorderColor(string path) {
        using (var src = new Bitmap(path)) {
            int w = src.Width, h = src.Height, band = Math.Max(1, w * 3 / 100);
            long r = 0, g = 0, bl = 0, n = 0;
            for (int y = 0; y < h; y += 2) for (int x = 0; x < w; x += 2) {
                if (x >= band && x < w - band && y >= band && y < h - band) continue;
                var c = src.GetPixel(x, y); r += c.R; g += c.G; bl += c.B; n++;
            }
            return Color.FromArgb(255, (int)(r / n), (int)(g / n), (int)(bl / n));
        }
    }
    // Android foreground: the picture at `inner` of a 108dp canvas, its edges faded into the
    // background colour so no seam shows; masks crop to the middle 72dp.
    public static void Foreground(string path, int canvas, double inner, string outPath) {
        using (var src = new Bitmap(path)) {
            int size = (int)Math.Round(canvas * inner);
            using (var pic = Scaled(src, size))
            using (var outBmp = new Bitmap(canvas, canvas, PixelFormat.Format32bppArgb)) {
                double feather = size * 0.08;
                for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
                    double d = Math.Min(Math.Min(x, y), Math.Min(size - 1 - x, size - 1 - y));
                    double a = Math.Min(1.0, d / feather);
                    var c = pic.GetPixel(x, y);
                    pic.SetPixel(x, y, Color.FromArgb((int)(c.A * a * a * (3 - 2 * a)), c.R, c.G, c.B));
                }
                using (var g = Graphics.FromImage(outBmp)) {
                    int off = (canvas - size) / 2;
                    g.DrawImageUnscaled(pic, off, off);
                }
                outBmp.Save(outPath, ImageFormat.Png);
            }
        }
    }
    // Windows: a rounded square at each size, PNG-encoded entries in one .ico.
    public static void Ico(string path, int[] sizes, string outPath) {
        var pngs = new List<byte[]>();
        using (var src = new Bitmap(path)) {
            foreach (int s in sizes) {
                using (var b = new Bitmap(s, s, PixelFormat.Format32bppArgb))
                using (var g = Graphics.FromImage(b)) {
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    float r = s * 0.2f;
                    using (var p = new GraphicsPath()) {
                        p.AddArc(0, 0, 2 * r, 2 * r, 180, 90); p.AddArc(s - 2 * r, 0, 2 * r, 2 * r, 270, 90);
                        p.AddArc(s - 2 * r, s - 2 * r, 2 * r, 2 * r, 0, 90); p.AddArc(0, s - 2 * r, 2 * r, 2 * r, 90, 90);
                        p.CloseFigure();
                        using (var brush = new TextureBrush(Scaled(src, s))) g.FillPath(brush, p);
                    }
                    using (var ms = new MemoryStream()) { b.Save(ms, ImageFormat.Png); pngs.Add(ms.ToArray()); }
                }
            }
        }
        using (var f = new BinaryWriter(File.Create(outPath))) {
            f.Write((short)0); f.Write((short)1); f.Write((short)sizes.Length);
            int offset = 6 + 16 * sizes.Length;
            for (int i = 0; i < sizes.Length; i++) {
                int s = sizes[i];
                f.Write((byte)(s >= 256 ? 0 : s)); f.Write((byte)(s >= 256 ? 0 : s));
                f.Write((byte)0); f.Write((byte)0); f.Write((short)1); f.Write((short)32);
                f.Write(pngs[i].Length); f.Write(offset); offset += pngs[i].Length;
            }
            foreach (var png in pngs) f.Write(png);
        }
    }
}
'@

$color = [Icons]::BorderColor($Source)
$hex = '#FF{0:X2}{1:X2}{2:X2}' -f $color.R, $color.G, $color.B
"background $hex"
if ($AndroidRes) {
    foreach ($density in @(@('mdpi', 1.0), @('hdpi', 1.5), @('xhdpi', 2.0), @('xxhdpi', 3.0), @('xxxhdpi', 4.0))) {
        $dir = Join-Path $AndroidRes "mipmap-$($density[0])"
        New-Item -ItemType Directory -Force $dir | Out-Null
        [Icons]::Foreground($Source, [int](108 * $density[1]), 0.74, (Join-Path $dir 'ic_launcher_foreground.png'))
    }
    $colors = Join-Path $AndroidRes 'values\colors.xml'
    (Get-Content -LiteralPath $colors -Raw) -replace '(<color name="ic_launcher_background">)[^<]*(</color>)', "`${1}$hex`${2}" |
        Set-Content -LiteralPath $colors -NoNewline -Encoding utf8
}
if ($Ico) {
    [Icons]::Ico($Source, @(16, 24, 32, 48, 64, 128, 256), $Ico)
    "wrote $Ico ($((Get-Item $Ico).Length) bytes)"
}

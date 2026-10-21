using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;

namespace Translator.Core;

internal sealed record NativeSourceFile(string FullPath, string RelativePath, string Content);

internal static class NativeSourceParsing
{
    public static IReadOnlyList<NativeSourceFile> ReadDirectory(string directory)
    {
        var root = Path.GetFullPath(directory);
        if (!Directory.Exists(root))
            return [];

        return Directory.EnumerateFiles(root, "*.*", SearchOption.AllDirectories)
            .Where(static path => path.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) ||
                                  path.EndsWith(".h", StringComparison.OrdinalIgnoreCase) ||
                                  path.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase))
            .Order(StringComparer.OrdinalIgnoreCase)
            .Select(path => new NativeSourceFile(
                path,
                Path.GetRelativePath(root, path).Replace('\\', '/'),
                StripCommentsAndLiterals(StripDisabledBlocks(File.ReadAllText(path)))))
            .ToArray();
    }

    /// <summary>
    /// Blanks every line inside an <c>#if 0</c> branch (up to its matching <c>#else</c>, <c>#elif</c> or
    /// <c>#endif</c>) so marker scanning sees the same registrations the compiler does. A native
    /// registration disabled with <c>#if 0</c> must not make the translator skip that address, or the
    /// function ends up neither translated nor provided natively. Other conditions are not evaluated
    /// and stay visible; lines are blanked rather than removed to keep line numbers stable.
    /// </summary>
    public static string StripDisabledBlocks(string content)
    {
        var lines = content.Split('\n');
        var frames = new List<(bool ZeroIf, bool Active)>();
        var output = new StringBuilder(content.Length);
        for (var index = 0; index < lines.Length; index++)
        {
            var line = lines[index];
            var trimmed = line.TrimStart();
            var visibleBefore = frames.TrueForAll(static frame => frame.Active);
            if (trimmed.StartsWith('#'))
            {
                var directive = trimmed[1..].TrimStart();
                if (directive.StartsWith("ifdef", StringComparison.Ordinal) ||
                    directive.StartsWith("ifndef", StringComparison.Ordinal))
                {
                    frames.Add((false, true));
                }
                else if (directive.StartsWith("if", StringComparison.Ordinal))
                {
                    var condition = directive[2..].TrimStart();
                    var zero = condition.StartsWith('0') &&
                               (condition.Length == 1 || !char.IsLetterOrDigit(condition[1]) && condition[1] != '_');
                    frames.Add((zero, !zero));
                }
                else if ((directive.StartsWith("else", StringComparison.Ordinal) ||
                          directive.StartsWith("elif", StringComparison.Ordinal)) && frames.Count > 0)
                {
                    var top = frames[^1];
                    if (top.ZeroIf)
                    {
                        frames[^1] = (true, true);  // the branch after "#if 0" is live
                    }
                }
                else if (directive.StartsWith("endif", StringComparison.Ordinal) && frames.Count > 0)
                {
                    frames.RemoveAt(frames.Count - 1);
                }
            }

            output.Append(visibleBefore && frames.TrueForAll(static frame => frame.Active) ? line : string.Empty);
            if (index < lines.Length - 1)
            {
                output.Append('\n');
            }
        }

        return output.ToString();
    }

    public static IEnumerable<string> SplitArguments(string arguments)
    {
        var start = 0;
        var depth = 0;
        for (var index = 0; index < arguments.Length; index++)
        {
            var character = arguments[index];
            if (character is '(' or '[' or '<') depth++;
            else if (character is ')' or ']' or '>') depth = Math.Max(0, depth - 1);
            else if (character == ',' && depth == 0)
            {
                yield return arguments[start..index];
                start = index + 1;
            }
        }
        yield return arguments[start..];
    }

    public static bool IsFloatingPointValueArgument(string argument) =>
        !argument.Contains('*', StringComparison.Ordinal) &&
        !argument.Contains('&', StringComparison.Ordinal) &&
        Regex.IsMatch(argument, @"\b(float|double)\b", RegexOptions.CultureInvariant);

    public static string StripCommentsAndLiterals(string content)
    {
        var output = new StringBuilder(content.Length);
        var inLineComment = false;
        var inBlockComment = false;
        var inLiteral = false;
        var quote = '\0';
        for (var index = 0; index < content.Length; index++)
        {
            var character = content[index];
            var next = index + 1 < content.Length ? content[index + 1] : '\0';
            if (inLineComment)
            {
                if (character == '\n') { inLineComment = false; output.Append(character); }
                continue;
            }
            if (inBlockComment)
            {
                if (character == '*' && next == '/') { inBlockComment = false; index++; }
                continue;
            }
            if (inLiteral)
            {
                output.Append(' ');
                if (character == '\\' && index + 1 < content.Length) { output.Append(' '); index++; }
                else if (character == quote) inLiteral = false;
                continue;
            }
            if (character == '/' && next == '/') { inLineComment = true; index++; continue; }
            if (character == '/' && next == '*') { inBlockComment = true; index++; continue; }
            if (character is '\'' or '"') { inLiteral = true; quote = character; output.Append(' '); continue; }
            output.Append(character);
        }
        return output.ToString();
    }
}

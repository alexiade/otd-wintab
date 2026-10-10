// otd-wintab: stylus names and capabilities from libwacom's wacom.stylus
// (third_party/libwacom, embedded as a resource).
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.Globalization;

namespace OtdWintab.Core;

public sealed record StylusInfo(string Name, string Type, bool IsEraser, Axes Axes);

public sealed class StylusDatabase
{
    public const uint WacomVendor = 0x56a;

    private readonly Dictionary<(uint vendor, uint id), StylusInfo> _entries = new();

    public int Count => _entries.Count;

    public static StylusDatabase LoadEmbedded()
    {
        using var stream = typeof(StylusDatabase).Assembly.GetManifestResourceStream("wacom.stylus")
                           ?? throw new InvalidOperationException("wacom.stylus resource missing");
        using var reader = new StreamReader(stream);
        return Parse(reader.ReadToEnd());
    }

    /// <summary>Parses the libwacom .stylus key file format.</summary>
    public static StylusDatabase Parse(string text)
    {
        var db = new StylusDatabase();
        (uint vendor, uint id)? key = null;
        var fields = new Dictionary<string, string>();

        void Flush()
        {
            if (key is { } k && fields.TryGetValue("Name", out var name))
                db._entries[k] = ToInfo(name, fields);
            fields.Clear();
        }

        foreach (var raw in text.Split('\n'))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line[0] == '#') continue;
            if (line[0] == '[' && line[^1] == ']')
            {
                Flush();
                key = ParseKey(line[1..^1]);
                continue;
            }
            int eq = line.IndexOf('=');
            if (eq > 0) fields[line[..eq].Trim()] = line[(eq + 1)..].Trim();
        }
        Flush();
        return db;
    }

    public StylusInfo? Lookup(uint vendor, uint toolId)
    {
        if (_entries.TryGetValue((vendor, toolId), out var info)) return info;
        // newer Wacom ids carry extra high bits; the libwacom key is the low part
        if (_entries.TryGetValue((vendor, toolId & 0xFFFFF), out info)) return info;
        return null;
    }

    private static (uint, uint)? ParseKey(string s)
    {
        // "0x56a:0x823", or the old single-id form "0x823" (Wacom)
        var parts = s.Split(':');
        if (parts.Length == 2 && TryHex(parts[0], out var v) && TryHex(parts[1], out var id)) return (v, id);
        if (parts.Length == 1 && TryHex(parts[0], out id)) return (WacomVendor, id);
        return null;
    }

    private static bool TryHex(string s, out uint value)
    {
        s = s.Trim();
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) s = s[2..];
        return uint.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value);
    }

    private static StylusInfo ToInfo(string name, Dictionary<string, string> f)
    {
        f.TryGetValue("Type", out var type);
        f.TryGetValue("Axes", out var axesText);
        var axisNames = (axesText ?? "").Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        var axes = Axes.None;
        foreach (var a in axisNames)
        {
            axes |= a switch
            {
                "Pressure" => Axes.Pressure,
                "Tilt" => Axes.Tilt,
                "RotationZ" => Axes.Rotation,
                "Slider" => Axes.Wheel,
                _ => Axes.None,
            };
        }
        // libwacom's older airbrush entries don't list the slider axis
        if (string.Equals(type, "Airbrush", StringComparison.OrdinalIgnoreCase)) axes |= Axes.Wheel;

        return new StylusInfo(name, type ?? "", f.ContainsKey("EraserType"), axes);
    }
}

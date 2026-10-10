// otd-wintab plugin tests: the shared ring's layout against the C header,
// and publishing.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using OtdWintab.Core;
using Xunit;

namespace OtdWintab.Tests;

public class RingTests
{
    /// <summary>Output of build/ringlayout.exe (compiled from the C header).</summary>
    private static Dictionary<string, uint> CLayout()
    {
        string dir = AppContext.BaseDirectory;
        string? exe = null;
        for (var d = new DirectoryInfo(dir); d != null; d = d.Parent)
        {
            var candidate = Path.Combine(d.FullName, "build", "ringlayout.exe");
            if (File.Exists(candidate)) { exe = candidate; break; }
        }
        Assert.True(exe != null, "build/ringlayout.exe not found; run .\\build.ps1 first");

        var psi = new ProcessStartInfo(exe!) { RedirectStandardOutput = true, UseShellExecute = false };
        using var p = Process.Start(psi)!;
        string output = p.StandardOutput.ReadToEnd();
        p.WaitForExit();
        return output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
                     .Select(l => l.Trim().Split('='))
                     .ToDictionary(kv => kv[0], kv => uint.Parse(kv[1]));
    }

    [Fact]
    public void Event_layout_matches_the_C_header()
    {
        var c = CLayout();
        Assert.Equal(c["otdwt_event.size"], (uint)Marshal.SizeOf<OtdwtEvent>());
        (string c, string cs)[] fields =
        {
            ("type", nameof(OtdwtEvent.Type)), ("flags", nameof(OtdwtEvent.Flags)),
            ("axes", nameof(OtdwtEvent.Axes)), ("tool_id", nameof(OtdwtEvent.ToolId)),
            ("serial", nameof(OtdwtEvent.Serial)), ("buttons", nameof(OtdwtEvent.Buttons)),
            ("x", nameof(OtdwtEvent.X)), ("y", nameof(OtdwtEvent.Y)),
            ("pressure", nameof(OtdwtEvent.Pressure)), ("tilt_x", nameof(OtdwtEvent.TiltX)),
            ("tilt_y", nameof(OtdwtEvent.TiltY)), ("wheel", nameof(OtdwtEvent.Wheel)),
            ("rotation", nameof(OtdwtEvent.Rotation)), ("time_ms", nameof(OtdwtEvent.TimeMs)),
            ("name", nameof(OtdwtEvent.Name)),
        };
        foreach (var (cName, csName) in fields)
            Assert.True(c[$"otdwt_event.{cName}"] == (uint)Marshal.OffsetOf<OtdwtEvent>(csName),
                        $"offset of {cName}");
    }

    [Fact]
    public void Header_layout_and_constants_match_the_C_header()
    {
        var c = CLayout();
        (string key, uint value)[] expected =
        {
            ("otdwt_ring.magic", RingProtocol.OffsetMagic),
            ("otdwt_ring.version", RingProtocol.OffsetVersion),
            ("otdwt_ring.ring_size", RingProtocol.OffsetRingSize),
            ("otdwt_ring.event_size", RingProtocol.OffsetEventSize),
            ("otdwt_ring.producer_pid", RingProtocol.OffsetProducerPid),
            ("otdwt_ring.write_index", RingProtocol.OffsetWriteIndex),
            ("otdwt_ring.ring", RingProtocol.OffsetRing),
            ("otdwt_ring.size", (uint)RingProtocol.TotalSize),
            ("const.OTDWT_MAGIC", RingProtocol.Magic),
            ("const.OTDWT_VERSION", RingProtocol.Version),
            ("const.OTDWT_RING_SIZE", RingProtocol.RingSize),
            ("const.OTDWT_NAME_LEN", RingProtocol.NameLength),
            ("const.OTDWT_POS_SCALE", RingProtocol.PositionScale),
            ("const.OTDWT_PRESSURE_MAX", RingProtocol.PressureMax),
            ("const.OTDWT_WHEEL_MAX", RingProtocol.WheelMax),
        };
        foreach (var (key, value) in expected)
            Assert.True(c[key] == value, $"{key}: C has {c[key]}, C# has {value}");
    }

    [Fact]
    public unsafe void Writer_initialises_the_header_and_publishes_in_order()
    {
        using var ring = new RingWriter($"plugin-test-{Environment.ProcessId}-{Guid.NewGuid():N}");
        byte* v = ring.View;
        Assert.Equal(RingProtocol.Magic, *(uint*)(v + RingProtocol.OffsetMagic));
        Assert.Equal(RingProtocol.Version, *(uint*)(v + RingProtocol.OffsetVersion));
        Assert.Equal((uint)Marshal.SizeOf<OtdwtEvent>(), *(uint*)(v + RingProtocol.OffsetEventSize));
        Assert.Equal((uint)Environment.ProcessId, *(uint*)(v + RingProtocol.OffsetProducerPid));

        for (int i = 0; i < RingProtocol.RingSize + 5; i++)
        {
            var ev = new OtdwtEvent { Type = EventType.Packet, X = i };
            ev.SetName($"tool {i}");
            ring.Publish(ev);
        }
        Assert.Equal(RingProtocol.RingSize + 5, ring.WriteIndex);
        var last = ring.Read(ring.WriteIndex - 1);
        Assert.Equal((int)RingProtocol.RingSize + 4, last.X);
        Assert.Equal($"tool {RingProtocol.RingSize + 4}", last.GetName());
    }

    [Fact]
    public void Names_are_truncated_to_fit_with_a_terminator()
    {
        var ev = new OtdwtEvent();
        ev.SetName(new string('x', 200));
        Assert.Equal(RingProtocol.NameLength - 1, ev.GetName().Length);
    }

    [Fact]
    public void Instance_names_match_the_C_side()
    {
        Assert.Equal((@"Local\otd-wintab-ring-abc", @"Local\otd-wintab-signal-abc"), RingProtocol.Names("abc"));
    }
}

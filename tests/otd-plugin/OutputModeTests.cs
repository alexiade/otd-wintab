// otd-wintab plugin tests: raw Intuos3 packets through our parser and the
// real OpenTabletDriver AbsoluteOutputMode pipeline into the ring.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.Numerics;
using OpenTabletDriver.Plugin;
using OpenTabletDriver.Plugin.Platform.Pointer;
using OpenTabletDriver.Plugin.Tablet;
using OtdWintab.Core;
using OtdWintab.Parsers;
using Xunit;
using P = OtdWintab.Tests.Intuos3Packets;

namespace OtdWintab.Tests;

public class OutputModeTests : IDisposable
{
    private sealed class FakePointer : IAbsolutePointer
    {
        public Vector2 Last;
        public int Moves;
        public void SetPosition(Vector2 pos) { Last = pos; Moves++; }
    }

    private readonly string _instance = $"mode-test-{Environment.ProcessId}-{Guid.NewGuid():N}";
    private readonly string? _savedInstance;
    private readonly FakePointer _pointer = new();
    private readonly WintabAbsoluteMode _mode;
    private readonly WacomIntuos3ReportParser _parser = new();
    private readonly RingWriter _reader;
    private ulong _seen;

    public OutputModeTests()
    {
        _savedInstance = Environment.GetEnvironmentVariable(RingProtocol.InstanceVariable);
        Environment.SetEnvironmentVariable(RingProtocol.InstanceVariable, _instance);
        _reader = new RingWriter(_instance);   // opens the same mapping, for reading back

        // a PTZ-630 as OpenTabletDriver describes it, mapped 1:1 onto a
        // 203.2 x 152.4 "screen" so positions are easy to predict
        var config = new TabletConfiguration
        {
            Name = "Wacom PTZ-630",
            Specifications = new TabletSpecifications
            {
                Digitizer = new DigitizerSpecifications { Width = 203.2f, Height = 152.4f, MaxX = 40640, MaxY = 30480 },
                Pen = new PenSpecifications { MaxPressure = 2046, ButtonCount = 2 },
            },
            DigitizerIdentifiers = new List<DeviceIdentifier> { new() { VendorID = 0x56A, ProductID = 0xB1 } },
        };
        _mode = new WintabAbsoluteMode
        {
            Pointer = _pointer,
            Tablet = new TabletReference(config, config.DigitizerIdentifiers),
            Input = new Area { Width = 203.2f, Height = 152.4f, Position = new Vector2(101.6f, 76.2f) },
            Output = new Area { Width = 2032, Height = 1524, Position = new Vector2(1016, 762) },
        };
    }

    public void Dispose()
    {
        _mode.Dispose();
        _reader.Dispose();
        Environment.SetEnvironmentVariable(RingProtocol.InstanceVariable, _savedInstance);
    }

    private void Feed(byte[] packet) => _mode.Read(_parser.Parse(packet));

    private List<OtdwtEvent> Published()
    {
        var list = new List<OtdwtEvent>();
        for (; _seen < _reader.WriteIndex; _seen++) list.Add(_reader.Read(_seen));
        return list;
    }

    [Fact]
    public void Airbrush_stroke_reaches_the_ring_with_tool_identity_and_wheel()
    {
        Feed(P.Enter(0x913, 0x0FA4E002));
        Feed(P.Pen(20320, 15240, 0));                  // tablet centre, hovering
        Feed(P.Pen(20320, 15240, 1023, tiltX: 10));    // half pressure
        Feed(P.AirbrushWheel(20320, 15240, 1023));
        Feed(P.Exit());

        var ev = Published();
        Assert.Equal(new[] { EventType.ProximityIn, EventType.Packet, EventType.Packet, EventType.Packet,
                             EventType.Packet, EventType.ProximityOut }, ev.Select(e => e.Type));
        Assert.All(ev, e => Assert.Equal("Airbrush Pen", e.GetName()));
        Assert.All(ev, e => Assert.Equal(0x0FA4E002u, e.Serial));

        // the screen position after OpenTabletDriver's own area mapping
        Assert.Equal(1016 * RingProtocol.PositionScale, ev[1].X);
        Assert.Equal(762 * RingProtocol.PositionScale, ev[1].Y);
        Assert.True(Vector2.Distance(new Vector2(1016, 762), _pointer.Last) < 0.01f,
                    $"cursor at {_pointer.Last}");                // and the cursor moved there

        Assert.Equal(0u, ev[1].Buttons);
        Assert.Equal(1u, ev[2].Buttons);
        Assert.InRange(ev[2].Pressure, 16300u, 16450u);
        Assert.Equal(100, ev[2].TiltX);
        Assert.Equal(RingProtocol.WheelMax, ev[3].Wheel);
        Assert.Equal(0u, ev[4].Buttons);                        // released before leaving
    }

    [Fact]
    public void Leaving_with_the_tip_down_releases_everything_first()
    {
        Feed(P.Enter(0x823, 7));
        Feed(P.Pen(20320, 15240, 1500, lower: true));   // tip and barrel down ...
        Feed(P.Exit());                                  // ... as the pen leaves

        var ev = Published();
        Assert.Equal(new[] { EventType.ProximityIn, EventType.Packet, EventType.Packet, EventType.ProximityOut },
                     ev.Select(e => e.Type));
        Assert.Equal(3u, ev[1].Buttons);
        Assert.Equal(0u, ev[2].Buttons);
        Assert.Equal(0u, ev[2].Pressure);
        Assert.Equal(ev[1].X, ev[2].X);                  // where the pen was
        Assert.Equal(ev[1].Y, ev[2].Y);
        Assert.Equal(2, _pointer.Moves);                 // the cursor isn't thrown to 0,0
        Assert.True(Vector2.Distance(new Vector2(1016, 762), _pointer.Last) < 0.01f);
    }

    [Fact]
    public void Flipping_the_pen_switches_to_the_eraser_tool()
    {
        Feed(P.Enter(0x823, 7));
        Feed(P.Pen(100, 100, 0));
        Feed(P.Exit());
        Feed(P.Enter(0x82B, 7));
        Feed(P.Pen(100, 100, 500));
        Feed(P.Exit());

        var ins = Published().Where(e => e.Type == EventType.ProximityIn).ToList();
        Assert.Equal(new[] { "Grip Pen", "Grip Pen Eraser" }, ins.Select(e => e.GetName()));
        Assert.True(ins[1].Flags.HasFlag(EventFlags.Eraser));
    }

    [Fact]
    public void Express_keys_and_unknown_packets_publish_nothing()
    {
        var pad = new byte[10];
        pad[0] = 0x0C;
        pad[5] = 1;
        Feed(pad);
        Feed(new byte[] { 0x02, 0x20, 0, 0, 0, 0, 0, 0, 0, 0 });   // "in range"
        Assert.Empty(Published());
    }
}

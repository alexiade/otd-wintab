// otd-wintab plugin tests: tool tracking and event generation.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.Numerics;
using OtdWintab.Core;
using Xunit;

namespace OtdWintab.Tests;

public class ToolTrackerTests
{
    private readonly EventLog _log = new();
    private readonly ToolTracker _tracker;

    public ToolTrackerTests()
    {
        _tracker = new ToolTracker(_log, StylusDatabase.LoadEmbedded(), StylusDatabase.WacomVendor);
    }

    private static PenSample At(float x, float y, float pressure = 0, Vector2? tilt = null) => new()
    {
        Position = new Vector2(x, y),
        Pressure = pressure,
        Tip = pressure > 0.01f,
        Tilt = tilt ?? Vector2.Zero,
    };

    [Fact]
    public void A_stroke_is_proximity_in_packets_proximity_out()
    {
        _tracker.OnToolReport(0x823, 0x0FA4E001, false, 0);
        _tracker.OnSample(At(10, 20), 1);
        _tracker.OnSample(At(11, 21, 0.5f), 2);
        _tracker.OnOutOfRange(3);

        Assert.Equal(new[] { EventType.ProximityIn, EventType.Packet, EventType.Packet, EventType.ProximityOut },
                     _log.Types);
        var ev = _log.Events[2];
        Assert.Equal("Grip Pen", ev.GetName());
        Assert.Equal(0x823u, ev.ToolId);
        Assert.Equal(0x0FA4E001u, ev.Serial);
        Assert.Equal(11 * RingProtocol.PositionScale, ev.X);
        Assert.Equal(21 * RingProtocol.PositionScale, ev.Y);
        Assert.Equal(RingProtocol.PressureMax / 2 + 1, ev.Pressure);
        Assert.Equal(1u, ev.Buttons);
        Assert.Equal(Axes.Pressure | Axes.Tilt, ev.Axes);
        Assert.Equal(0u, _log.Events[0].Buttons);   // nothing pressed on arrival
    }

    [Fact]
    public void Names_and_axes_come_from_libwacom()
    {
        _tracker.OnToolReport(0x913, 2, false, 0);
        _tracker.OnSample(At(1, 1), 1);
        Assert.Equal("Airbrush Pen", _log.Events[0].GetName());
        Assert.True(_log.Events[0].Axes.HasFlag(Axes.Wheel));

        _tracker.OnToolReport(0x885, 3, false, 2);
        _tracker.OnSample(At(1, 1), 3);
        var artPen = _log.Events.Last(e => e.Type == EventType.ProximityIn);
        Assert.Equal("6D Art Pen", artPen.GetName());
        Assert.True(artPen.Axes.HasFlag(Axes.Rotation));
    }

    [Fact]
    public void Switching_tools_leaves_with_the_old_and_enters_with_the_new()
    {
        _tracker.OnToolReport(0x823, 1, false, 0);
        _tracker.OnSample(At(1, 1), 1);
        _tracker.OnToolReport(0x82B, 1, true, 2);    // flipped to the eraser
        _tracker.OnSample(At(1, 1), 3);

        Assert.Equal(new[] { EventType.ProximityIn, EventType.Packet, EventType.ProximityOut,
                             EventType.ProximityIn, EventType.Packet }, _log.Types);
        Assert.Equal(0x823u, _log.Events[2].ToolId);       // the pen left
        Assert.Equal("Grip Pen Eraser", _log.Events[3].GetName());
        Assert.True(_log.Events[3].Flags.HasFlag(EventFlags.Eraser));
    }

    [Fact]
    public void Same_tool_type_with_another_serial_is_another_tool()
    {
        _tracker.OnToolReport(0x823, 1, false, 0);
        _tracker.OnSample(At(1, 1), 1);
        _tracker.OnToolReport(0x823, 2, false, 2);
        _tracker.OnSample(At(1, 1), 3);
        Assert.Equal(2, _log.Events.Count(e => e.Type == EventType.ProximityIn));
    }

    [Fact]
    public void Watchdog_ends_proximity_when_reports_stop()
    {
        _tracker.OnToolReport(0x823, 1, false, 0);
        _tracker.OnSample(At(1, 1), 1000);
        _tracker.Tick(1050, 100);
        Assert.True(_tracker.InProximity);
        _tracker.Tick(1100, 100);
        Assert.False(_tracker.InProximity);
        Assert.Equal(EventType.ProximityOut, _log.Events.Last().Type);
        _tracker.Tick(1300, 100);                  // only once
        Assert.Single(_log.Events, e => e.Type == EventType.ProximityOut);
    }

    [Fact]
    public void Watchdog_stays_off_for_tablets_that_report_leaving()
    {
        // an Intuos3 goes quiet for a while during contact; that must not
        // end the stroke
        _tracker.ReportsLeaving = true;
        _tracker.OnToolReport(0x82B, 1, true, 0);
        _tracker.OnSample(At(1, 1, 0.2f), 1000);
        _tracker.Tick(1500, 100);
        Assert.True(_tracker.InProximity);
        Assert.DoesNotContain(_log.Events, e => e.Type == EventType.ProximityOut);
    }

    [Fact]
    public void A_repeated_proximity_packet_mid_stroke_does_not_end_the_stroke()
    {
        _tracker.OnToolReport(0x82B, 1, true, 0);
        _tracker.OnSample(At(1, 1, 0.2f), 1);
        _tracker.OnToolReport(0x82B, 1, true, 2);      // the tablet repeats itself
        _tracker.OnSample(At(2, 2, 0.2f), 3);
        Assert.Single(_log.Events, e => e.Type == EventType.ProximityIn);
        Assert.DoesNotContain(_log.Events, e => e.Type == EventType.ProximityOut);
    }

    [Fact]
    public void Watchdog_survives_tick_count_wraparound()
    {
        _tracker.OnSample(At(1, 1), uint.MaxValue - 10);
        _tracker.Tick(20, 100);                    // 31 ms later, across the wrap
        Assert.True(_tracker.InProximity);
    }

    [Fact]
    public void Tablets_without_tool_reports_get_pen_and_eraser_by_flag()
    {
        var tracker = new ToolTracker(_log, StylusDatabase.LoadEmbedded(), 0x28bd);   // not Wacom
        var pen = At(1, 1);
        tracker.OnSample(pen, 1);
        var eraser = pen;
        eraser.Eraser = true;
        tracker.OnSample(eraser, 2);

        var ins = _log.Events.Where(e => e.Type == EventType.ProximityIn).ToList();
        Assert.Equal(new[] { "Pen", "Pen Eraser" }, ins.Select(e => e.GetName()));
        Assert.All(ins, e => Assert.Equal(0u, e.Serial));
    }

    [Fact]
    public void Identity_on_the_sample_wins_without_a_tool_report()
    {
        var s = At(1, 1);
        s.Tool = new ToolIdentity(0x913, 0x06C00017, false);
        _tracker.OnSample(s, 1);
        Assert.Equal("Airbrush Pen", _log.Events[0].GetName());
        Assert.Equal(0x06C00017u, _log.Events[0].Serial);
    }

    [Fact]
    public void Identity_on_the_sample_overrides_a_stale_tool_report()
    {
        _tracker.OnToolReport(0x823, 1, false, 0);
        var s = At(1, 1);
        s.Tool = new ToolIdentity(0x885, 2, false);
        _tracker.OnSample(s, 1);
        Assert.Equal("6D Art Pen", _log.Events[0].GetName());
    }

    [Fact]
    public void Unknown_tool_ids_get_a_descriptive_name()
    {
        _tracker.OnToolReport(0xABCDE, 4, false, 0);
        _tracker.OnSample(At(1, 1), 1);
        Assert.Equal("Tool 0xABCDE", _log.Events[0].GetName());
    }

    [Fact]
    public void Wheel_rotation_and_barrel_buttons_are_encoded()
    {
        _tracker.OnToolReport(0x913, 2, false, 0);
        var s = At(1, 1, 0.2f, new Vector2(12.5f, -3f));
        s.Wheel = 0.5f;
        s.Rotation = 359.95f;
        s.LowerBarrel = true;
        s.UpperBarrel = true;
        _tracker.OnSample(s, 1);

        var ev = _log.Events[1];
        Assert.Equal(512u, ev.Wheel);
        Assert.Equal(0, ev.Rotation);              // 3599.5 rounds to a full turn
        Assert.Equal(125, ev.TiltX);
        Assert.Equal(-30, ev.TiltY);
        Assert.Equal(7u, ev.Buttons);
    }
}

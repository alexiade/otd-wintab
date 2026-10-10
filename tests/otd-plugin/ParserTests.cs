// otd-wintab plugin tests: Intuos3 parser against Linux-format packets.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using OpenTabletDriver.Plugin.Tablet;
using OtdWintab.Parsers;
using Xunit;
using P = OtdWintab.Tests.Intuos3Packets;

namespace OtdWintab.Tests;

public class ParserTests
{
    private readonly WacomIntuos3ReportParser _parser = new();

    [Fact]
    public void Proximity_in_reports_tool_id_and_serial()
    {
        var r = Assert.IsType<IntuosToolReport>(_parser.Parse(P.Enter(0x823, 0x0FA4E001)));
        Assert.Equal(0x823u, r.RawToolID);
        Assert.Equal(0x0FA4E001ul, r.Serial);
        Assert.Equal(ToolType.Pen, r.Tool);
    }

    [Fact]
    public void Eraser_end_is_identified_by_tool_id_bit_3()
    {
        var r = Assert.IsType<IntuosToolReport>(_parser.Parse(P.Enter(0x82B, 1)));
        Assert.Equal(ToolType.Eraser, r.Tool);
        var pen = Assert.IsType<IntuosPenReport>(_parser.Parse(P.Pen(100, 100, 50)));
        Assert.True(pen.Eraser);
    }

    [Theory]
    [InlineData(0xC0)]
    [InlineData(0xC1)]
    [InlineData(0xC2)]
    [InlineData(0xC3)]
    public void Every_proximity_variant_is_accepted(byte b1)
    {
        // the built-in parser only knows 0xC2
        var d = P.Enter(0x913, 7);
        d[1] = b1;
        Assert.IsType<IntuosToolReport>(_parser.Parse(d));
    }

    [Fact]
    public void General_packet_decodes_position_pressure_tilt_buttons()
    {
        _parser.Parse(P.Enter(0x823, 1));
        var r = Assert.IsType<IntuosPenReport>(_parser.Parse(
            P.Pen(x: 12345, y: 23456, pressure: 1501, tiltX: -20, tiltY: 31, lower: true, distance: 9)));

        Assert.Equal(12345f, r.Position.X);
        Assert.Equal(23456f, r.Position.Y);
        Assert.Equal(1501u, r.Pressure);
        Assert.Equal(-20f, r.Tilt.X);
        Assert.Equal(31f, r.Tilt.Y);
        Assert.Equal(new[] { true, false }, r.PenButtons);
        Assert.Equal(9u, r.HoverDistance);
        Assert.False(r.Eraser);
    }

    [Fact]
    public void Airbrush_wheel_is_decoded_not_mistaken_for_a_mouse()
    {
        _parser.Parse(P.Enter(0x913, 2));
        _parser.Parse(P.Pen(1000, 1000, 800, tiltX: 5));
        var r = Assert.IsType<IntuosAirbrushReport>(_parser.Parse(P.AirbrushWheel(1002, 1001, 700, tiltX: 6)));

        Assert.Equal(700 / 1023f, r.Wheel, 4);
        Assert.Equal(800u, r.Pressure);              // carried from the general packet
        Assert.Equal(6f, r.Tilt.X);
        // later general packets keep the wheel
        var next = Assert.IsType<IntuosAirbrushReport>(_parser.Parse(P.Pen(1003, 1003, 900)));
        Assert.Equal(700 / 1023f, next.Wheel, 4);
        Assert.Equal(900u, next.Pressure);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(900)]
    [InlineData(901)]
    [InlineData(1799)]
    [InlineData(1800)]
    public void Rotation_matches_the_kernel_decoding(int raw)
    {
        _parser.Parse(P.Enter(0x885, 3));
        _parser.Parse(P.Pen(10, 10, 100));
        var r = Assert.IsType<IntuosArtPenReport>(_parser.Parse(P.Rotation(10, 10, raw)));

        int linux = P.LinuxRotation(raw);   // -900..899 over a full turn
        float expected = ((linux + 900) % 1800 + 1800) % 1800 * 0.2f;
        Assert.Equal(expected, r.Rotation, 3);
        Assert.InRange(r.Rotation, 0f, 360f);
        Assert.Equal(100u, r.Pressure);
    }

    [Fact]
    public void Exit_becomes_out_of_range()
    {
        _parser.Parse(P.Enter(0x823, 1));
        Assert.IsType<OutOfRangeReport>(_parser.Parse(P.Exit()));
        // without a known tool, packets are ignored (as in Linux) ...
        Assert.IsType<DeviceReport>(_parser.Parse(P.Pen(1, 1, 1)));
        // ... and a second exit is not reported again
        Assert.IsType<DeviceReport>(_parser.Parse(P.Exit()));
    }

    [Fact]
    public void Wheel_and_rotation_reset_on_the_next_tool()
    {
        _parser.Parse(P.Enter(0x913, 2));
        _parser.Parse(P.AirbrushWheel(1, 1, 500));
        _parser.Parse(P.Exit());
        _parser.Parse(P.Enter(0x823, 1));
        Assert.IsType<IntuosPenReport>(_parser.Parse(P.Pen(1, 1, 10)));
    }

    [Fact]
    public void Pucks_are_not_reported_as_pens()
    {
        _parser.Parse(P.Enter(0x017, 9));       // Intuos3 2D mouse
        Assert.IsType<DeviceReport>(_parser.Parse(P.Pen(5, 5, 0)));
    }

    [Fact]
    public void Wacom_driver_variant_skips_the_extra_byte()
    {
        var parser = new WacomDriverIntuos3ReportParser();
        var r = Assert.IsType<IntuosToolReport>(parser.Parse(new byte[] { 0xFF }.Concat(P.Enter(0x823, 5)).ToArray()));
        Assert.Equal(5ul, r.Serial);
    }

    [Fact]
    public void Express_keys()
    {
        var d = new byte[10];
        d[0] = 0x0C;
        d[5] = 0b0101;
        d[6] = 0b1000;
        var r = Assert.IsType<Intuos3PadReport>(_parser.Parse(d));
        Assert.Equal(new[] { true, false, true, false, false, false, false, true }, r.AuxButtons);
    }
}

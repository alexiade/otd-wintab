// otd-wintab plugin tests: helpers.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using OtdWintab.Core;

namespace OtdWintab.Tests;

/// <summary>Collects published events.</summary>
public sealed class EventLog : IEventSink
{
    public List<OtdwtEvent> Events { get; } = new();
    public void Publish(in OtdwtEvent ev) => Events.Add(ev);
    public IEnumerable<EventType> Types => Events.Select(e => e.Type);
}

/// <summary>Builds Intuos3 packets in the wire format the Linux driver decodes.</summary>
public static class Intuos3Packets
{
    /// <summary>Proximity-in: tool id and serial.</summary>
    public static byte[] Enter(uint toolId, uint serial)
    {
        var d = new byte[10];
        d[0] = 0x02;
        d[1] = 0xC2;
        d[2] = (byte)(toolId >> 4);
        d[3] = (byte)(((toolId & 0xF) << 4) | ((serial >> 28) & 0xF));
        d[4] = (byte)(serial >> 20);
        d[5] = (byte)(serial >> 12);
        d[6] = (byte)(serial >> 4);
        d[7] = (byte)((serial & 0xF) << 4 | ((toolId >> 16) & 0xF));
        d[8] = (byte)(((toolId >> 12) & 0xF) << 4);
        return d;
    }

    public static byte[] Exit() => new byte[] { 0x02, 0x80, 0, 0, 0, 0, 0, 0, 0, 0 };

    private static byte[] Positioned(byte b1, int x, int y, int distance)
    {
        var d = new byte[10];
        d[0] = 0x02;
        d[1] = b1;
        d[2] = (byte)(x >> 9);
        d[3] = (byte)(x >> 1);
        d[4] = (byte)(y >> 9);
        d[5] = (byte)(y >> 1);
        d[9] = (byte)((distance << 2) | ((x & 1) << 1) | (y & 1));
        return d;
    }

    private static void SetTilt(byte[] d, int tiltX, int tiltY)
    {
        int tx = tiltX + 64, ty = tiltY + 64;
        d[7] |= (byte)((tx >> 1) & 0x3F);
        d[8] = (byte)(((tx & 1) << 7) | (ty & 0x7F));
    }

    /// <summary>General pen packet (type 0).</summary>
    public static byte[] Pen(int x, int y, int pressure, int tiltX = 0, int tiltY = 0,
                             bool lower = false, bool upper = false, int distance = 0)
    {
        byte b1 = (byte)(0xE0 | (pressure & 1) | (lower ? 2 : 0) | (upper ? 4 : 0));
        var d = Positioned(b1, x, y, distance);
        d[6] = (byte)(pressure >> 3);
        d[7] = (byte)(((pressure >> 1) & 3) << 6);
        SetTilt(d, tiltX, tiltY);
        return d;
    }

    /// <summary>Airbrush second packet (type 0x0A): finger wheel 0..1023.</summary>
    public static byte[] AirbrushWheel(int x, int y, int wheel, int tiltX = 0, int tiltY = 0)
    {
        var d = Positioned(0xE0 | (0x0A << 1), x, y, 0);
        d[6] = (byte)(wheel >> 2);
        d[7] = (byte)((wheel & 3) << 6);
        SetTilt(d, tiltX, tiltY);
        return d;
    }

    /// <summary>Rotation packet (type 0x05) with the raw 11-bit value
    /// (its lowest bit is what Linux tests as d[7] &amp; 0x20).</summary>
    public static byte[] Rotation(int x, int y, int raw)
    {
        var d = Positioned(0xE0 | (0x05 << 1), x, y, 0);
        d[6] = (byte)(raw >> 3);
        d[7] = (byte)((raw & 7) << 5);
        return d;
    }

    /// <summary>The kernel's decoding of a rotation value (ABS_Z, -900..899).</summary>
    public static int LinuxRotation(int t) =>
        (t & 1) != 0 ? (t > 900 ? (t - 1) / 2 - 1350 : (t - 1) / 2 + 450) : 450 - t / 2;
}

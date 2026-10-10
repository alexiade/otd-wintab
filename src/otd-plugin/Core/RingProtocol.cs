// otd-wintab: C# mirror of include/otdwt_ring.h. Keep the two in sync and
// bump Version on any layout change; tests check the layout against the C
// definition.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.Runtime.InteropServices;

namespace OtdWintab.Core;

public static class RingProtocol
{
    public const string MappingName = @"Local\otd-wintab-ring";
    public const string EventName = @"Local\otd-wintab-signal";
    public const string InstanceVariable = "OTD_WINTAB_INSTANCE";

    public const uint Magic = 0x5457544F;   // "OTWT"
    public const uint Version = 1;
    public const uint RingSize = 1024;
    public const int NameLength = 64;
    public const int PositionScale = 8;
    public const uint PressureMax = 32767;
    public const uint WheelMax = 1023;

    // header layout (otdwt_ring)
    public const int OffsetMagic = 0;
    public const int OffsetVersion = 4;
    public const int OffsetRingSize = 8;
    public const int OffsetEventSize = 12;
    public const int OffsetProducerPid = 16;
    public const int OffsetWriteIndex = 24;
    public const int OffsetRing = 64;

    public static int TotalSize => OffsetRing + (int)RingSize * Marshal.SizeOf<OtdwtEvent>();

    /// <summary>The shared-memory names, honouring OTD_WINTAB_INSTANCE like the C side.</summary>
    public static (string mapping, string signal) Names(string? instance = null)
    {
        instance ??= Environment.GetEnvironmentVariable(InstanceVariable);
        return string.IsNullOrEmpty(instance)
            ? (MappingName, EventName)
            : ($"{MappingName}-{instance}", $"{EventName}-{instance}");
    }
}

public enum EventType : uint
{
    ProximityIn = 1,
    ProximityOut = 2,
    Packet = 3,
}

[Flags]
public enum EventFlags : uint
{
    None = 0,
    Eraser = 1 << 0,
}

[Flags]
public enum Axes : uint
{
    None = 0,
    Pressure = 1 << 0,
    Tilt = 1 << 1,
    Wheel = 1 << 2,
    Rotation = 1 << 3,
}

/// <summary>otdwt_event</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OtdwtEvent
{
    public EventType Type;
    public EventFlags Flags;
    public Axes Axes;
    public uint ToolId;
    public uint Serial;
    public uint Buttons;     // bit0 tip, bit1 lower barrel, bit2 upper barrel
    public int X, Y;         // virtual-screen pixels * PositionScale
    public uint Pressure;    // 0..PressureMax
    public int TiltX, TiltY; // tenths of a degree
    public uint Wheel;       // 0..WheelMax
    public int Rotation;     // tenths of a degree, 0..3599
    public uint TimeMs;
    public fixed byte Name[RingProtocol.NameLength];

    public string GetName()
    {
        fixed (byte* p = Name)
        {
            int n = 0;
            while (n < RingProtocol.NameLength && p[n] != 0) n++;
            return System.Text.Encoding.UTF8.GetString(p, n);
        }
    }

    public void SetName(string name)
    {
        var bytes = System.Text.Encoding.UTF8.GetBytes(name);
        int n = Math.Min(bytes.Length, RingProtocol.NameLength - 1);
        fixed (byte* p = Name)
        {
            for (int i = 0; i < RingProtocol.NameLength; i++)
                p[i] = i < n ? bytes[i] : (byte)0;
        }
    }
}

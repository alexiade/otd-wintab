// otd-wintab: producer side of the shared event ring (see include/otdwt_ring.h).
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.ComponentModel;
using System.Runtime.InteropServices;

namespace OtdWintab.Core;

public interface IEventSink
{
    void Publish(in OtdwtEvent ev);
}

/// <summary>
/// Creates (or reopens) the named mapping and publishes events into it.
/// Single producer; readers are the provider DLLs in applications.
/// </summary>
public sealed unsafe class RingWriter : IEventSink, IDisposable
{
    private readonly IntPtr _mapping;
    private readonly IntPtr _signal;
    private byte* _view;

    public RingWriter(string? instance = null)
    {
        var (mappingName, signalName) = RingProtocol.Names(instance);
        int size = RingProtocol.TotalSize;

        _mapping = CreateFileMappingW(new IntPtr(-1), IntPtr.Zero, PAGE_READWRITE, 0, (uint)size, mappingName);
        if (_mapping == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"CreateFileMapping {mappingName}");
        _view = (byte*)MapViewOfFile(_mapping, FILE_MAP_ALL_ACCESS, 0, 0, (UIntPtr)(uint)size);
        if (_view == null)
        {
            int err = Marshal.GetLastWin32Error();
            CloseHandle(_mapping);
            throw new Win32Exception(err, "MapViewOfFile");
        }
        _signal = CreateEventW(IntPtr.Zero, false, false, signalName);

        // (re)initialise the header; write_index is kept so readers attached
        // to an earlier producer just see new events
        *(uint*)(_view + RingProtocol.OffsetMagic) = RingProtocol.Magic;
        *(uint*)(_view + RingProtocol.OffsetVersion) = RingProtocol.Version;
        *(uint*)(_view + RingProtocol.OffsetRingSize) = RingProtocol.RingSize;
        *(uint*)(_view + RingProtocol.OffsetEventSize) = (uint)sizeof(OtdwtEvent);
        *(uint*)(_view + RingProtocol.OffsetProducerPid) = (uint)Environment.ProcessId;
    }

    public ulong WriteIndex => Volatile.Read(ref *(ulong*)(_view + RingProtocol.OffsetWriteIndex));

    public void Publish(in OtdwtEvent ev)
    {
        if (_view == null) return;
        ref ulong writeIndex = ref *(ulong*)(_view + RingProtocol.OffsetWriteIndex);
        ulong idx = writeIndex;
        var slot = (OtdwtEvent*)(_view + RingProtocol.OffsetRing) + (idx & (RingProtocol.RingSize - 1));
        *slot = ev;
        Volatile.Write(ref writeIndex, idx + 1);   // release: publishes the slot
        if (_signal != IntPtr.Zero) SetEvent(_signal);
    }

    /// <summary>Reads back a published event (for tests and diagnostics).</summary>
    public OtdwtEvent Read(ulong index)
    {
        var slot = (OtdwtEvent*)(_view + RingProtocol.OffsetRing) + (index & (RingProtocol.RingSize - 1));
        return *slot;
    }

    internal byte* View => _view;

    public void Dispose()
    {
        if (_view != null)
        {
            UnmapViewOfFile((IntPtr)_view);
            _view = null;
        }
        if (_mapping != IntPtr.Zero) CloseHandle(_mapping);
        if (_signal != IntPtr.Zero) CloseHandle(_signal);
    }

    private const uint PAGE_READWRITE = 0x04;
    private const uint FILE_MAP_ALL_ACCESS = 0xF001F;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateFileMappingW(IntPtr file, IntPtr attributes, uint protect,
                                                    uint sizeHigh, uint sizeLow, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint offsetHigh, uint offsetLow,
                                               UIntPtr bytes);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool UnmapViewOfFile(IntPtr view);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateEventW(IntPtr attributes, bool manualReset, bool initialState, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetEvent(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);
}

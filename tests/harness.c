/*
 * otd-wintab test harness: runner, provider loading, producer and pump
 * helpers. See harness.h.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "harness.h"

int test_failures;
struct wintab_api wt;
const char *dll_path;

static struct ring_handle ring;
static HMODULE dll;

const struct tool GRIP_PEN      = { "Grip Pen",        0x823, 0xA001, 0, OTDWT_AX_PRESSURE | OTDWT_AX_TILT };
const struct tool GRIP_ERASER   = { "Grip Pen Eraser", 0x82b, 0xA001, OTDWT_F_ERASER, OTDWT_AX_PRESSURE | OTDWT_AX_TILT };
const struct tool AIRBRUSH      = { "Airbrush",        0x913, 0xB002, 0, OTDWT_AX_PRESSURE | OTDWT_AX_TILT | OTDWT_AX_WHEEL };
const struct tool ART_PEN       = { "Art Pen",         0x885, 0xC003, 0, OTDWT_AX_PRESSURE | OTDWT_AX_TILT | OTDWT_AX_ROTATION };
const struct tool NO_SERIAL_PEN = { "Pen",             0x000, 0,      0, OTDWT_AX_PRESSURE };

struct ring_handle *harness_ring(void) { return &ring; }

void harness_create_ring(void)
{
    if (!ring_create(&ring))
    {
        printf("    cannot create ring\n");
        ExitProcess(2);
    }
}

void harness_start(void)
{
    /* Create the ring first so the provider attaches as soon as it starts */
    harness_create_ring();
    harness_load_dll();
}

void harness_load_dll(void)
{
    dll = LoadLibraryA(dll_path);
    if (!dll)
    {
        printf("    cannot load %s (%lu)\n", dll_path, GetLastError());
        ExitProcess(2);
    }
#define GET(field, name) wt.field = (void *)GetProcAddress(dll, name); \
    if (!wt.field) { printf("    missing export %s\n", name); ExitProcess(2); }
    GET(InfoA, "WTInfoA");
    GET(InfoW, "WTInfoW");
    GET(OpenA, "WTOpenA");
    GET(Close, "WTClose");
    GET(Packet, "WTPacket");
    GET(PacketsGet, "WTPacketsGet");
    GET(Enable, "WTEnable");
    GET(QueueSizeSet, "WTQueueSizeSet");
    GET(GetA, "WTGetA");
#undef GET
}

void harness_stop(void)
{
    if (dll) FreeLibrary(dll);
    ring_close(&ring);
}

HWND make_window(void)
{
    return CreateWindowA("STATIC", "otdwt-test", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
}

HCTX open_context(HWND hwnd, LONG out_ext_x, LONG out_ext_y, UINT device, int queue)
{
    LOGCONTEXTA lc;
    AXIS ax, ay;
    HCTX h;

    /* as gdkdevicemanager-win32.c: wintab_init_check() */
    wt.InfoA(WTI_DEVICES, DVC_X, &ax);
    wt.InfoA(WTI_DEVICES, DVC_Y, &ay);
    if (!wt.InfoA(WTI_DSCTXS, 0, &lc))
        wt.InfoA(WTI_DEFSYSCTX, 0, &lc);
    lc.lcOptions |= CXO_MESSAGES | CXO_CSRMESSAGES;
    lc.lcStatus = 0;
    lc.lcMsgBase = WT_DEFBASE;
    lc.lcDevice = device;
    lc.lcPktRate = 0;
    lc.lcPktData = PACKETDATA;
    lc.lcPktMode = 0;
    lc.lcMoveMask = PACKETDATA;
    lc.lcBtnUpMask = lc.lcBtnDnMask = ~0;
    lc.lcOutOrgX = ax.axMin;
    lc.lcOutOrgY = ay.axMin;
    lc.lcOutExtX = out_ext_x ? out_ext_x : ax.axMax - ax.axMin + 1;
    lc.lcOutExtY = out_ext_y ? out_ext_y : -(ay.axMax - ay.axMin + 1);
    h = wt.OpenA(hwnd, &lc, TRUE);
    if (h && queue) wt.QueueSizeSet(h, queue);
    return h;
}

void emit_ex(const otdwt_event *ev)
{
    ring_publish(&ring, ev);
}

void emit(uint32_t type, const struct tool *t, int x, int y, uint32_t pressure)
{
    otdwt_event ev = {0};
    ev.type = type;
    ev.flags = t->flags;
    ev.axes = t->axes;
    ev.tool_id = t->tool_id;
    ev.serial = t->serial;
    ev.buttons = pressure ? 1 : 0;
    ev.x = x * OTDWT_POS_SCALE;
    ev.y = y * OTDWT_POS_SCALE;
    ev.pressure = pressure;
    ev.tilt_x = 200;
    ev.tilt_y = -100;
    ev.wheel = 700;
    ev.rotation = 450;
    ev.time_ms = GetTickCount();
    strncpy(ev.name, t->name, OTDWT_NAME_LEN - 1);
    ring_publish(&ring, &ev);
}

void stroke(const struct tool *t, int npackets)
{
    int i;
    emit(OTDWT_EV_PROXIMITY_IN, t, 100, 200, 0);
    for (i = 1; i <= npackets; i++)
        emit(OTDWT_EV_PACKET, t, 100 + i, 200 + i, 4000 * i);
    emit(OTDWT_EV_PROXIMITY_OUT, t, 100 + npackets, 200 + npackets, 0);
}

void settle(void)
{
    Sleep(150);
}

void pump(struct msg_log *log, int ms)
{
    DWORD start = GetTickCount();
    MSG msg;
    PACKET pkt;

    while (GetTickCount() - start < (DWORD)ms)
    {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            switch (msg.message)
            {
            case WT_CSRCHANGE:
                log->csrchange++;
                /* GTK fetches the packet to learn the new cursor */
                if (wt.Packet((HCTX)msg.lParam, (UINT)msg.wParam, &pkt) && log->ncursor_names < 16)
                {
                    WCHAR w[256] = {0};
                    wt.InfoW(WTI_CURSORS + pkt.cursor, CSR_NAME, w);
                    WideCharToMultiByte(CP_UTF8, 0, w, -1, log->new_cursor_names[log->ncursor_names++],
                                        256, NULL, NULL);
                    if (log->npackets < MAX_LOG) log->packets[log->npackets++] = pkt;
                }
                break;
            case WT_PROXIMITY:
                if (LOWORD(msg.lParam)) log->proximity_in++;
                else log->proximity_out++;
                break;
            case WT_PACKET:
                log->packet_msgs++;
                if (wt.Packet((HCTX)msg.lParam, (UINT)msg.wParam, &pkt) && log->npackets < MAX_LOG)
                    log->packets[log->npackets++] = pkt;
                break;
            }
            DispatchMessageA(&msg);
        }
        Sleep(2);
    }
}

/* ---- runner ------------------------------------------------------------ */

static int run_child(const char *self, const char *name)
{
    char cmd[2048];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD code = 3;

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" %s", self, dll_path, name);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return 3;
    if (WaitForSingleObject(pi.hProcess, 30000) == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 4);
        printf("    TIMEOUT\n");
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

int main(int argc, char **argv)
{
    int i, failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2)
    {
        printf("usage: %s otd-wintab32.dll [test-name]\n", argv[0]);
        return 2;
    }
    dll_path = argv[1];

    if (argc >= 3)  /* child: run one test */
    {
        if (!GetEnvironmentVariableA("OTD_WINTAB_INSTANCE", NULL, 0))
            SetEnvironmentVariableA("OTD_WINTAB_INSTANCE", "selftest-single");
        for (i = 0; i < test_case_count; i++)
            if (!strcmp(test_cases[i].name, argv[2]))
            {
                if (test_cases[i].needs_dll) harness_start();
                test_cases[i].fn();
                harness_stop();
                return test_failures ? 1 : 0;
            }
        printf("    no such test: %s\n", argv[2]);
        return 2;
    }

    /* parent: run every test in its own process, so the provider's global
     * state (cursor table, contexts, ring attachment) starts fresh, and on
     * a private ring, so a live producer can't interfere */
    {
        char inst[32];
        snprintf(inst, sizeof(inst), "selftest-%lu", GetCurrentProcessId());
        SetEnvironmentVariableA("OTD_WINTAB_INSTANCE", inst);
        SetEnvironmentVariableA("OTD_WINTAB_FAKE", NULL);
    }
    for (i = 0; i < test_case_count; i++)
    {
        int rc;
        printf("%-34s ", test_cases[i].name);
        rc = run_child(argv[0], test_cases[i].name);
        if (rc == 0) printf("ok\n");
        else if (rc == 1) printf("FAILED\n");
        else printf("FAILED (exit code %#x)\n", (unsigned)rc);
        if (rc) failed++;
    }
    printf("\n%d/%d tests passed\n", test_case_count - failed, test_case_count);
    return failed ? 1 : 0;
}

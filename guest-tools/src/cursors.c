/*
 * cursors.c: CURSORS.EXE, each of Windows' standard cursors (IDC_*) in
 * turn, so the player can learn their pixels (player-core's
 * cursor_shape.rs, doc 15 "Player").
 *
 * A window over the whole screen holds the capture, so the cursor is its
 * own wherever the pointer is, and answers WM_SETCURSOR without touching
 * it: nothing but the timer changes it. A login's own windows (the Voodoo
 * 2 driver's notice) can be over the screen's middle for a while, so it
 * starts after 20 s, keeps the pointer in a corner, and takes the
 * foreground and the capture again at every step. Every 3 s the next one
 * is set, with its name to COM1 and to C:\2KSBOX\CURSORS.LOG (guestlog.h)
 * first, and `CURSORS DONE` after the last. The player's PLAYER_CURSOR_LOG=1
 * prints a hash for every shape the guest defines, in the same order. An
 * animated one (XP's busy cursors) is several shapes before the next name.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <windows.h>
#include <stdio.h>
#include "guestlog.h"

#ifndef IDC_HAND
#define IDC_HAND MAKEINTRESOURCE(32649)
#endif

static const struct {
    const char *name;
    LPCSTR id;
} shapes[] = {
    {"ARROW", IDC_ARROW},
    {"IBEAM", IDC_IBEAM},
    {"WAIT", IDC_WAIT},
    {"CROSS", IDC_CROSS},
    {"UPARROW", IDC_UPARROW},
    {"SIZENWSE", IDC_SIZENWSE},
    {"SIZENESW", IDC_SIZENESW},
    {"SIZEWE", IDC_SIZEWE},
    {"SIZENS", IDC_SIZENS},
    {"SIZEALL", IDC_SIZEALL},
    {"NO", IDC_NO},
    {"HAND", IDC_HAND},
    {"APPSTARTING", IDC_APPSTARTING},
    {"HELP", IDC_HELP},
};
#define NSHAPES (int)(sizeof shapes / sizeof shapes[0])

static FILE *log_file;
static HANDLE com1 = INVALID_HANDLE_VALUE;
static int next;

static void say(const char *line)
{
    DWORD n;

    if (log_file) {
        fprintf(log_file, "%s\n", line);
        fflush(log_file);
    }
    if (com1 != INVALID_HANDLE_VALUE) {
        WriteFile(com1, line, (DWORD)strlen(line), &n, NULL);
        WriteFile(com1, "\r\n", 2, &n, NULL);
    }
}

static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    char line[64];

    switch (msg) {
    case WM_SETCURSOR:
        return TRUE;
    case WM_TIMER:
        SetTimer(w, 1, 3000, NULL);
        SetForegroundWindow(w);
        SetCapture(w);
        SetCursorPos(8, 8);
        if (next == NSHAPES) {
            say("CURSORS DONE");
            DestroyWindow(w);
            return 0;
        }
        snprintf(line, sizeof line, "CURSOR %s", shapes[next].name);
        say(line);
        SetCursor(LoadCursor(NULL, shapes[next].id));
        next++;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    HWND w;
    MSG m;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);

    (void)prev;
    (void)cmd;
    (void)show;
    log_file = guest_log_open("CURSORS.LOG", "w");
    com1 = CreateFile("COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hbrBackground = (HBRUSH)(COLOR_DESKTOP + 1);
    wc.lpszClassName = "CursorsClass";
    RegisterClass(&wc);
    w = CreateWindowEx(WS_EX_TOPMOST, "CursorsClass", "Cursors", WS_POPUP | WS_VISIBLE, 0, 0, sw, sh, NULL, NULL,
                       inst, NULL);
    SetForegroundWindow(w);
    SetCursorPos(8, 8);
    SetCapture(w);
    SetTimer(w, 1, 20000, NULL);
    while (GetMessage(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    if (log_file)
        fclose(log_file);
    if (com1 != INVALID_HANDLE_VALUE)
        CloseHandle(com1);
    return 0;
}

#include "discovery.h"
#include "session.h"
#include "clipboard.h"
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string.h>

#define ID_EXIT 200
#define ID_ABOUT 201
#define ID_SHOW 202
#define ID_SEND 210
#define ID_RING 211
#define ID_CLIPBOARD 212
#define WM_TRAY (WM_APP + 1)
#define WM_DISCOVERY (WM_APP + 2)
#define ID_DISCOVERY 220
#define ID_PAIR 221
#define ID_DISCONNECT 222
#define ID_FORGET 223
#define ID_CANCEL_TRANSFER 224
#define ID_RECEIVE_CLIPBOARD 225
#define ID_SYNC_CLIPBOARD 226
#define ID_SEND_CLIPBOARD 227
#define MAX_PEERS 64

static HINSTANCE instance;
static HWND devices, heading, state, battery, sendButton, ringButton, pairButton;
static HWND clipboardButton, transferBox, transferText, progress, statusBar, cancelTransferButton;
static HFONT normalFont, titleFont;
static HICON icons[4];
static NOTIFYICONDATAW tray;
static BOOL trayAdded;
static UINT taskbarCreated;
static DiscoveryPeer peers[MAX_PEERS];
static int peerCount;
static DWORD clipboardSequence;
static uint64_t localClipboardTime;
static unsigned int appliedClipboard;

static void showPeer(void)
{
    WCHAR description[256];
    SessionStatus session;
    BOOL paired;
    int selected = ListView_GetNextItem(devices, -1, LVNI_SELECTED);
    session_status(&session);
    paired = selected >= 0 && selected < peerCount && session.phase == SESSION_PAIRED &&
             !strcmp(session.deviceId, peers[selected].identity.id);
    EnableWindow(sendButton, paired && !session.transferActive);
    EnableWindow(ringButton, paired);
    EnableWindow(clipboardButton, paired);
    CheckMenuItem(GetMenu(GetParent(devices)), ID_SYNC_CLIPBOARD,
                  MF_BYCOMMAND | (session.clipboardSync ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_SEND_CLIPBOARD, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_RECEIVE_CLIPBOARD,
                   MF_BYCOMMAND | (paired && session.clipboardRevision ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_SYNC_CLIPBOARD, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    if (paired && session.batteryCharge >= 0) {
        wsprintfW(description, L"Battery %d%%%s", session.batteryCharge, session.batteryCharging ? L" (charging)" : L"");
        SetWindowTextW(battery, description);
    } else SetWindowTextW(battery, L"Battery —");
    EnableWindow(cancelTransferButton, paired && session.transferActive);
    EnableMenuItem(GetMenu(GetParent(devices)), ID_SEND, MF_BYCOMMAND | (paired && !session.transferActive ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_RING, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    if (selected < 0 || selected >= peerCount) {
        EnableWindow(pairButton, FALSE);
        SetWindowTextW(heading, L"Connect95");
        SetWindowTextW(state, L"No device selected");
        return;
    }
    SetWindowTextW(heading, peers[selected].name);
    EnableWindow(pairButton, peers[selected].identity.version == 7);
    EnableMenuItem(GetMenu(GetParent(devices)), ID_PAIR, MF_BYCOMMAND |
                   (peers[selected].identity.version == 7 ? MF_ENABLED : MF_GRAYED));
    if (!strcmp(session.deviceId, peers[selected].identity.id) && session.phase != SESSION_IDLE) {
        SetWindowTextW(state, session.text);
        SetWindowTextW(transferText, session.transferText[0] ? session.transferText : L"No active transfers");
        SendMessageW(progress, PBM_SETPOS, session.transferPercent, 0);
        if (session.code[0] && (session.phase == SESSION_WAIT_PHONE || session.phase == SESSION_CONFIRM)) {
            wsprintfW(description, L"Pairing code %s — compare it with your phone", session.code);
            SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)description);
        } else SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)session.text);
        return;
    }
    SetWindowTextW(transferText, L"No active transfers");
    SendMessageW(progress, PBM_SETPOS, 0, 0);
    if ((DWORD)(GetTickCount() - peers[selected].seen) > 120000)
        SetWindowTextW(state, L"Discovery is stale. Refresh devices on the phone.");
    else
        SetWindowTextW(state, L"Discovered. Not connected.");
    wsprintfW(description, L"%hs:%u · Protocol %d · Not paired",
              peers[selected].address, peers[selected].identity.port,
              peers[selected].identity.version);
    SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)description);
}

static void pairSelected(HWND window)
{
    int selected = ListView_GetNextItem(devices, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= peerCount) return;
    if (peers[selected].identity.version != 7) {
        MessageBoxW(window, L"This release supports pairing with protocol 7. Protocol 8 pairing is under development.", L"Connect95", MB_OK | MB_ICONINFORMATION);
        return;
    }
    appliedClipboard = 0;
    if (!session_start(&peers[selected]))
        MessageBoxW(window, L"Could not start the connection.", L"Connect95", MB_OK | MB_ICONERROR);
    showPeer();
}

static BOOL pairedSelection(void)
{
    SessionStatus snapshot;
    int selected = ListView_GetNextItem(devices, -1, LVNI_SELECTED);
    session_status(&snapshot);
    return selected >= 0 && selected < peerCount && snapshot.phase == SESSION_PAIRED &&
           !strcmp(snapshot.deviceId, peers[selected].identity.id);
}

static void sendFile(HWND window)
{
    OPENFILENAMEW dialog;
    WCHAR path[MAX_PATH] = L"";
    if (!pairedSelection()) return;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"All files\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.lpstrTitle = L"Send file to phone";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog) && pairedSelection() && !session_send_file(path))
        MessageBoxW(window, L"A transfer is already in progress or the connection has closed.", L"Connect95", MB_OK | MB_ICONINFORMATION);
    showPeer();
}

static void sendClipboard(HWND window, BOOL automatic)
{
    char text[CLIPBOARD_MAX_BYTES + 1];
    int read;
    if (!pairedSelection()) return;
    read = clipboard_read(window, text);
    if (read == 1 && !session_send_clipboard(text, automatic)) read = -1;
    SecureZeroMemory(text, sizeof(text));
    if (read != 1 && !automatic)
        MessageBoxW(window, read == 0 ? L"The clipboard is busy. Please try again." :
            L"Copy plain text of up to 8 KiB (UTF-8) before sending it.", L"Connect95", MB_OK | MB_ICONINFORMATION);
}

static int receiveClipboard(HWND window, BOOL manual)
{
    ClipboardMessage message;
    int result;
    if (!session_received_clipboard(&message)) return -1;
    if (!manual && message.initial && message.timestamp <= localClipboardTime) {
        appliedClipboard = message.revision;
        SecureZeroMemory(&message, sizeof(message));
        return 1;
    }
    result = clipboard_write(window, message.content);
    if (result == 1) {
        appliedClipboard = message.revision;
        localClipboardTime = message.initial ? message.timestamp : clipboard_time();
        /* Ignore our own clipboard update when checking for outgoing changes. */
        clipboardSequence = GetClipboardSequenceNumber();
    }
    SecureZeroMemory(&message, sizeof(message));
    if (manual && result != 1)
        MessageBoxW(window, L"Cannot update the clipboard. Please try again.", L"Connect95", MB_OK | MB_ICONINFORMATION);
    return result;
}

static void pollClipboard(HWND window, const SessionStatus *snapshot)
{
    DWORD sequence = GetClipboardSequenceNumber();
    BOOL automatic = snapshot->phase == SESSION_PAIRED && snapshot->clipboardSync;
    if (sequence != clipboardSequence) {
        char text[CLIPBOARD_MAX_BYTES + 1];
        int read = -1;
        localClipboardTime = clipboard_time();
        if (automatic) {
            read = clipboard_read(window, text);
            if (read == 1) session_send_clipboard(text, TRUE);
            SecureZeroMemory(text, sizeof(text));
        }
        if (read != 0) clipboardSequence = sequence;
    }
    if (automatic && snapshot->clipboardRevision && snapshot->clipboardRevision != appliedClipboard)
        receiveClipboard(window, FALSE);
    if (snapshot->phase != SESSION_PAIRED) appliedClipboard = 0;
}

static void clipboardMenu(HWND window)
{
    RECT rect;
    HMENU menu = CreatePopupMenu();
    SessionStatus snapshot;
    if (!pairedSelection()) { DestroyMenu(menu); return; }
    session_status(&snapshot);
    AppendMenuW(menu, MF_STRING, ID_SEND_CLIPBOARD, L"Send PC clipboard to phone");
    AppendMenuW(menu, MF_STRING | (snapshot.clipboardRevision ? 0 : MF_GRAYED),
                ID_RECEIVE_CLIPBOARD, L"Apply latest phone clipboard");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (snapshot.clipboardSync ? MF_CHECKED : 0), ID_SYNC_CLIPBOARD,
                L"Sync text clipboard automatically");
    GetWindowRect(clipboardButton, &rect);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rect.left, rect.bottom, 0, window, NULL);
    DestroyMenu(menu);
}

static void pollSession(HWND window)
{
    static BOOL confirmationShown;
    SessionStatus snapshot;
    WCHAR question[512];
    session_status(&snapshot);
    pollClipboard(window, &snapshot);
    if (snapshot.phase != SESSION_CONFIRM) confirmationShown = FALSE;
    if (snapshot.phase == SESSION_CONFIRM && !confirmationShown) {
        confirmationShown = TRUE;
        wsprintfW(question, L"Pairing code %s\n\nDoes it match the code on your phone?\nConfirm only when both codes match.", snapshot.code);
        session_confirm(MessageBoxW(window, question, L"Connect95 — verify device",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES);
    }
    showPeer();
}

static void startDiscovery(HWND window)
{
    WCHAR errorText[160];
    int error = discovery_start(window, WM_DISCOVERY);
    if (error) {
        wsprintfW(errorText, L"UDP 1716 is unavailable (error %d). Restart discovery from the Device menu.", error);
        SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)errorText);
    } else {
        SendMessageW(statusBar, SB_SETTEXTW, 0,
            (LPARAM)L"Listening on UDP 1716. Refresh devices in KDE Connect on your phone.");
    }
}

static void receiveDiscovery(void)
{
    DiscoveryPeer peer;
    LVITEMW item;
    int batch, result, index;
    /* Bound each dispatch so network traffic cannot monopolize the UI. */
    for (batch = 0; batch < 32; ++batch) {
        result = discovery_receive(&peer);
        if (result <= 0) {
            if (result < 0) SendMessageW(statusBar, SB_SETTEXTW, 0,
                (LPARAM)L"UDP receive error. Restart discovery from the Device menu.");
            break;
        }
        if (result != 1) continue;
        for (index = 0; index < peerCount; ++index)
            if (!strcmp(peers[index].identity.id, peer.identity.id)) break;
        if (index == MAX_PEERS) continue;
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.pszText = peer.name;
        if (index == peerCount) {
            if (SendMessageW(devices, LVM_INSERTITEMW, 0, (LPARAM)&item) == -1) continue;
            ++peerCount;
        } else {
            SendMessageW(devices, LVM_SETITEMW, 0, (LPARAM)&item);
        }
        peers[index] = peer;
        if (ListView_GetNextItem(devices, -1, LVNI_SELECTED) < 0)
            ListView_SetItemState(devices, index, LVIS_SELECTED | LVIS_FOCUSED,
                                 LVIS_SELECTED | LVIS_FOCUSED);
        showPeer();
    }
}

/* XP shell32 icon indexes: computer, open folder, search, document.
 * Load from the running OS so XP supplies its own artwork. */
static HICON loadSystemIcon(int shellIndex, int fallbackResource)
{
    WCHAR path[MAX_PATH];
    HICON icon = NULL;
    UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
    if (length && length + 13 < ARRAYSIZE(path)) {
        lstrcatW(path, L"\\shell32.dll");
        ExtractIconExW(path, shellIndex, &icon, NULL, 1);
    }
    if (!icon)
        icon = LoadImageW(instance, MAKEINTRESOURCEW(fallbackResource),
                          IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    return icon;
}

static HWND control(HWND parent, const WCHAR *kind, const WCHAR *text,
                    DWORD style, int id)
{
    HWND child = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style,
                                 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                                 instance, NULL);
    if (child) SendMessageW(child, WM_SETFONT, (WPARAM)normalFont, TRUE);
    return child;
}

static HMENU createMenu(void)
{
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU device = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING | MF_GRAYED, ID_SEND, L"Send file…");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"Exit");
    AppendMenuW(device, MF_STRING, ID_DISCOVERY, L"Start discovery");
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_PAIR, L"Pair / connect");
    AppendMenuW(device, MF_STRING, ID_DISCONNECT, L"Disconnect");
    AppendMenuW(device, MF_STRING, ID_FORGET, L"Forget pairing on this PC…");
    AppendMenuW(device, MF_SEPARATOR, 0, NULL);
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_RING, L"Find phone");
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_SEND_CLIPBOARD, L"Send clipboard to phone");
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_RECEIVE_CLIPBOARD, L"Apply phone clipboard");
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_SYNC_CLIPBOARD, L"Sync text clipboard automatically");
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"About…");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"File");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)device, L"Device");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)help, L"Help");
    return menu;
}

static void addTray(HWND window)
{
    ZeroMemory(&tray, sizeof(tray));
    /* Use the pre-Vista notification icon layout supported by XP. */
    tray.cbSize = NOTIFYICONDATAW_V2_SIZE;
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = WM_TRAY;
    tray.hIcon = icons[0];
    lstrcpynW(tray.szTip, L"Connect95", ARRAYSIZE(tray.szTip));
    trayAdded = Shell_NotifyIconW(NIM_ADD, &tray);
}

static void restoreWindow(HWND window)
{
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
}

static void trayMenu(HWND window)
{
    POINT position;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_SHOW, L"Open Connect95");
    AppendMenuW(menu, MF_STRING, ID_ABOUT, L"About…");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
    SetMenuDefaultItem(menu, ID_SHOW, FALSE);
    GetCursorPos(&position);
    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, position.x, position.y, 0, window, NULL);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void layout(HWND window)
{
    RECT area, statusArea;
    int width, height, left, rightX, rightWidth, buttonWidth, buttonY;
    GetClientRect(window, &area);
    SendMessageW(statusBar, WM_SIZE, 0, 0);
    GetWindowRect(statusBar, &statusArea);
    width = area.right;
    height = area.bottom - (statusArea.bottom - statusArea.top);
    left = 150;
    rightX = left + 20;
    rightWidth = width - rightX - 10;
    buttonWidth = (rightWidth - 12) / 3;
    buttonY = 104;
    MoveWindow(devices, 8, 8, left, height - 16, TRUE);
    MoveWindow(heading, rightX + 44, 12, rightWidth - 44, 25, TRUE);
    MoveWindow(state, rightX + 44, 43, rightWidth - 44, 30, TRUE);
    MoveWindow(battery, rightX + 44, 72, rightWidth - 220, 21, TRUE);
    MoveWindow(pairButton, rightX + rightWidth - 170, 68, 170, 25, TRUE);
    MoveWindow(sendButton, rightX, buttonY, buttonWidth, 72, TRUE);
    MoveWindow(ringButton, rightX + buttonWidth + 6, buttonY, buttonWidth, 72, TRUE);
    MoveWindow(clipboardButton, rightX + (buttonWidth + 6) * 2,
               buttonY, rightWidth - (buttonWidth + 6) * 2, 72, TRUE);
    MoveWindow(transferBox, rightX, 188, rightWidth, height - 196, TRUE);
    MoveWindow(transferText, rightX + 12, 210, rightWidth - 100, 30, TRUE);
    MoveWindow(cancelTransferButton, rightX + rightWidth - 76, 207, 64, 24, TRUE);
    MoveWindow(progress, rightX + 12, 240, rightWidth - 24, 16, TRUE);
    InvalidateRect(window, NULL, TRUE);
}

static void drawButton(const DRAWITEMSTRUCT *item)
{
    WCHAR label[64];
    RECT rect = item->rcItem;
    RECT textRect = rect;
    UINT flags = DFCS_BUTTONPUSH;
    int iconIndex = (int)item->CtlID - ID_SEND + 1;
    int x = (rect.right - rect.left - 32) / 2;
    BOOL disabled = (item->itemState & ODS_DISABLED) != 0;
    if (item->itemState & ODS_SELECTED) flags |= DFCS_PUSHED;
    FillRect(item->hDC, &rect, GetSysColorBrush(COLOR_BTNFACE));
    DrawFrameControl(item->hDC, &rect, DFC_BUTTON, flags);
    DrawIconEx(item->hDC, x, 7, icons[iconIndex], 32, 32, 0, NULL, DI_NORMAL);
    GetWindowTextW(item->hwndItem, label, ARRAYSIZE(label));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
    SelectObject(item->hDC, normalFont);
    textRect.top += 47;
    textRect.left += 4;
    textRect.right -= 4;
    DrawTextW(item->hDC, label, -1, &textRect, DT_CENTER | DT_SINGLELINE);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&rect, -5, -5);
        DrawFocusRect(item->hDC, &rect);
    }
}

static BOOL createControls(HWND window)
{
    LVCOLUMNW column;
    NONCLIENTMETRICSW metrics;
    ZeroMemory(&metrics, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        return FALSE;
    normalFont = CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfHeight = -18;
    metrics.lfMessageFont.lfWeight = FW_BOLD;
    titleFont = CreateFontIndirectW(&metrics.lfMessageFont);
    devices = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, window, NULL, instance, NULL);
    SendMessageW(devices, WM_SETFONT, (WPARAM)normalFont, TRUE);
    ListView_SetExtendedListViewStyle(devices, LVS_EX_FULLROWSELECT);
    ZeroMemory(&column, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = L"Devices";
    column.cx = 125;
    SendMessageW(devices, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
    heading = control(window, L"STATIC", L"Connect95", SS_LEFT, 0);
    SendMessageW(heading, WM_SETFONT, (WPARAM)titleFont, TRUE);
    state = control(window, L"STATIC", L"No connected devices", SS_LEFT, 0);
    battery = control(window, L"STATIC", L"Battery —", SS_LEFT, 0);
    pairButton = control(window, L"BUTTON", L"Pair / connect", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, ID_PAIR);
    sendButton = control(window, L"BUTTON", L"Send file", BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, ID_SEND);
    ringButton = control(window, L"BUTTON", L"Find phone", BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, ID_RING);
    clipboardButton = control(window, L"BUTTON", L"Clipboard", BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, ID_CLIPBOARD);
    transferBox = control(window, L"BUTTON", L"File transfers", BS_GROUPBOX, 0);
    transferText = control(window, L"STATIC", L"No active transfers", SS_LEFT, 0);
    cancelTransferButton = control(window, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, ID_CANCEL_TRANSFER);
    progress = control(window, PROGRESS_CLASSW, L"", 0, 0);
    statusBar = control(window, STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP, 0);
    SendMessageW(statusBar, SB_SETTEXTW, 0,
        (LPARAM)L"  Ready.");
    return normalFont && titleFont && devices && heading && state && battery &&
        pairButton && sendButton && ringButton && clipboardButton && transferBox && transferText &&
        cancelTransferButton && progress && statusBar;
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (taskbarCreated && message == taskbarCreated) {
        addTray(window);
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        if (!createControls(window)) return -1;
        addTray(window);
        startDiscovery(window);
        SetTimer(window, 1, 500, NULL);
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && trayAdded) ShowWindow(window, SW_HIDE);
        else layout(window);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = 640;
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = 360;
        return 0;
    case WM_DRAWITEM:
        drawButton((const DRAWITEMSTRUCT *)lParam);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        DrawIconEx(dc, 170, 16, icons[0], 32, 32, 0, NULL, DI_NORMAL);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DISCOVERY:
        if (WSAGETSELECTERROR(lParam)) {
            SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)L"UDP error. Restart discovery from the Device menu.");
        } else if (WSAGETSELECTEVENT(lParam) == FD_READ) receiveDiscovery();
        return 0;
    case WM_TIMER:
        pollSession(window);
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lParam)->hwndFrom == devices &&
            ((NMHDR *)lParam)->code == LVN_ITEMCHANGED) showPeer();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_CLIPBOARD: clipboardMenu(window); break;
        case ID_SEND_CLIPBOARD: sendClipboard(window, FALSE); break;
        case ID_RECEIVE_CLIPBOARD: if (pairedSelection()) receiveClipboard(window, TRUE); break;
        case ID_SYNC_CLIPBOARD: {
            SessionStatus snapshot;
            session_status(&snapshot);
            if (pairedSelection()) {
                clipboardSequence = GetClipboardSequenceNumber();
                session_clipboard_sync(!snapshot.clipboardSync);
                showPeer();
            }
            break;
        }
        case ID_SEND: sendFile(window); break;
        case ID_RING: if (pairedSelection()) session_ring(); break;
        case ID_CANCEL_TRANSFER: session_cancel_transfer(); break;
        case ID_FORGET: {
            int selected = ListView_GetNextItem(devices, -1, LVNI_SELECTED);
            if (selected >= 0 && selected < peerCount &&
                MessageBoxW(window, L"Remove the saved certificate for the selected device on this PC?",
                    L"Connect95", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                session_forget(&peers[selected]);
                showPeer();
            }
            break;
        }
        case ID_PAIR: pairSelected(window); break;
        case ID_DISCONNECT: session_stop(); showPeer(); break;
        case ID_DISCOVERY: startDiscovery(window); break;
        case ID_EXIT: DestroyWindow(window); break;
        case ID_SHOW: restoreWindow(window); break;
        case ID_ABOUT:
            ShellAboutA(window, "Connect95", "Connect95 0.5.0\r\n"
                "KDE Connect companion for Windows XP.\r\n"
                "Written in C using Win32.\r\n"
                "Files, battery, clipboard and find phone over TLS 1.2. Mbed TLS 3.6.8, cJSON.", icons[0]);
            break;
        }
        return 0;
    case WM_TRAY:
        if (lParam == WM_LBUTTONDBLCLK) restoreWindow(window);
        else if (lParam == WM_RBUTTONUP) trayMenu(window);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        session_stop();
        discovery_stop();
        if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE current, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSEXW cls;
    INITCOMMONCONTROLSEX common;
    HWND window;
    MSG message;
    int result, index;
    (void)previous;
    (void)command;
    instance = current;
    clipboardSequence = GetClipboardSequenceNumber();
    session_init();
    common.dwSize = sizeof(common);
    common.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&common)) return 1;
    icons[0] = loadSystemIcon(17, 101);
    icons[1] = loadSystemIcon(4, 102);
    icons[2] = loadSystemIcon(22, 103);
    icons[3] = loadSystemIcon(1, 104);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ZeroMemory(&cls, sizeof(cls));
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = windowProc;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hIcon = icons[0];
    cls.hIconSm = (HICON)CopyImage(icons[0], IMAGE_ICON, 16, 16, 0);
    cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    cls.lpszClassName = L"Connect95.MainWindow";
    if (!RegisterClassExW(&cls)) return 1;
    window = CreateWindowExW(0, cls.lpszClassName, L"Connect95", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 360, NULL, createMenu(), instance, NULL);
    if (!window) {
        MessageBoxW(NULL, L"Could not create the Connect95 window.", L"Connect95", MB_ICONERROR);
        return 1;
    }
    ShowWindow(window, show);
    UpdateWindow(window);
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    session_free();
    DeleteObject(normalFont);
    DeleteObject(titleFont);
    for (index = 0; index < 4; ++index) DestroyIcon(icons[index]);
    DestroyIcon(cls.hIconSm);
    return result == -1 ? 1 : (int)message.wParam;
}

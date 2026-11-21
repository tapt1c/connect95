#include "discovery.h"
#include "language.h"
#include "network_service.h"
#include "file_jobs.h"
#include "settings.h"
#include "theme.h"
#include "sidebar_layout.h"
#include "ui_layout.h"
#include "session.h"
#include "clipboard.h"
#include "saved_devices.h"
#include "notifications_ui.h"
#include "device_name.h"
#include "../resources/resource.h"
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
#define ID_NOTIFICATIONS 230
#define ID_DEVICE_NAME 231
#define ID_PING 232
#define ID_LANGUAGE_ENGLISH 240
#define ID_LANGUAGE_RUSSIAN 241
#define ID_OPEN_FOLDER 250
#define ID_HISTORY 251
#define ID_AUTOCONNECT 252
#define ID_AUTOSTART 253
#define ID_START_TRAY 254
#define WM_INCOMING (WM_APP + 4)
#define ID_SIDE_SEND 260
#define ID_SIDE_RING 261
#define ID_SIDE_CLIPBOARD 262
#define ID_GROUP_DEVICES 270
#define ID_GROUP_FILES 271
#define ID_GROUP_OTHER 272
#define MAX_PEERS 64

static HINSTANCE instance;
static HWND devices, heading, state, battery, sendButton, ringButton, pairButton;
static HWND clipboardButton, transferText, progress, statusBar, cancelTransferButton;
static HFONT normalFont, titleFont, groupFont, linkFont;
static HICON icons[4];
static HICON taskIcons[3];
static NOTIFYICONDATAW tray;
static BOOL trayAdded, balloonBusy;
static DWORD balloonStarted, balloonEnded;
static unsigned int balloonGeneration;
static char balloonId[NOTIFICATION_ID_BYTES + 1];
static UINT taskbarCreated;
static BOOL reconnectForName;
static DiscoveryPeer peers[MAX_PEERS];
static int peerCount;
static HTREEITEM savedGroup, discoveredGroup, peerNodes[MAX_PEERS];
static BOOL peerSaved[MAX_PEERS], peerSeen[MAX_PEERS], peerConnected[MAX_PEERS];
static HIMAGELIST deviceImages;
static HWND sideSend, sideFolder, sideHistory, sideRing, sideNotifications, sideClipboard;
static HWND recentText;
static MainLayout currentLayout;
static BOOL autoPaused, startInTray, batteryAdded, lowBatteryWarned;
static DWORD lastAutoAttempt;
static int autoCursor;
static HICON batteryIcon;
static HWND groupHeaders[3];
static BOOL groupCollapsed[3];
static unsigned int batteryGeneration;
static int previousBattery = -2, previousCharging = -1;


/* The session is polled every 500 ms. Updating unchanged STATIC text erases
   its background and needlessly repaints nearby themed controls on XP. */
static void setControlText(HWND control, const WCHAR *text)
{
    WCHAR previous[512];
    int length = lstrlenW(text);
    if (length < (int)ARRAYSIZE(previous) && GetWindowTextLengthW(control) == length &&
        GetWindowTextW(control, previous, ARRAYSIZE(previous)) == length && !lstrcmpW(previous, text)) return;
    SetWindowTextW(control, text);
}

static void setStatusText(const WCHAR *text)
{
    WCHAR previous[512];
    int length = lstrlenW(text);
    if (length < (int)ARRAYSIZE(previous) &&
        LOWORD(SendMessageW(statusBar, SB_GETTEXTLENGTHW, 0, 0)) == length) {
        SendMessageW(statusBar, SB_GETTEXTW, 0, (LPARAM)previous);
        if (!lstrcmpW(previous, text)) return;
    }
    SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)text);
}

static int selectedPeer(void)
{
    TVITEMW item;
    ZeroMemory(&item, sizeof(item));
    item.mask = TVIF_PARAM;
    item.hItem = TreeView_GetSelection(devices);
    if (!item.hItem || !SendMessageW(devices, TVM_GETITEMW, 0, (LPARAM)&item) ||
        item.lParam < 1 || item.lParam > peerCount) return -1;
    return (int)item.lParam - 1;
}

static void refreshPeerNode(int index)
{
    TVINSERTSTRUCTW insert;
    WCHAR caption[300];
    HTREEITEM parent = peerSaved[index] ? savedGroup : discoveredGroup;
    BOOL selected = selectedPeer() == index;
    if (peerNodes[index] && (!peerSaved[index] && !peerSeen[index])) {
        TreeView_DeleteItem(devices, peerNodes[index]);
        peerNodes[index] = NULL;
        return;
    }
    if (peerNodes[index] && TreeView_GetParent(devices, peerNodes[index]) != parent) {
        TreeView_DeleteItem(devices, peerNodes[index]);
        peerNodes[index] = NULL;
    }
    if (!peerSaved[index] && !peerSeen[index]) return;
    lstrcpyW(caption, peers[index].name);
    if (peerSaved[index] && !peerSeen[index] && !peerConnected[index]) lstrcatW(caption, tr(L" (offline)"));
    ZeroMemory(&insert, sizeof(insert));
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.pszText = caption;
    insert.item.lParam = index + 1;
    insert.item.iImage = 0;
    insert.item.iSelectedImage = 0;
    if (!peerNodes[index]) {
        peerNodes[index] = (HTREEITEM)SendMessageW(devices, TVM_INSERTITEMW, 0, (LPARAM)&insert);
        TreeView_Expand(devices, parent, TVE_EXPAND);
        if (selected) TreeView_SelectItem(devices, peerNodes[index]);
    } else {
        insert.item.hItem = peerNodes[index];
        SendMessageW(devices, TVM_SETITEMW, 0, (LPARAM)&insert.item);
    }
}

static void syncSavedDevices(HWND window, const SessionStatus *snapshot)
{
    static int previousPhase = -1;
    static char previousId[39];
    int i;
    if (previousPhase == snapshot->phase && !strcmp(previousId, snapshot->deviceId)) return;
    previousPhase = snapshot->phase;
    strcpy(previousId, snapshot->deviceId);
    for (i = 0; i < peerCount; ++i) {
        peerConnected[i] = snapshot->phase == SESSION_PAIRED && !strcmp(peers[i].identity.id, snapshot->deviceId);
        if (!strcmp(peers[i].identity.id, snapshot->deviceId)) {
            int saved = saved_device_exists(peers[i].identity.id);
            if (saved >= 0) peerSaved[i] = saved;
            if (peerConnected[i] && peerSaved[i] && peers[i].address[0] && !saved_device_remember(&peers[i]))
                MessageBoxW(window, tr(L"Pairing is saved, but the device name and address could not be remembered."),
                            L"ConnectXP", MB_OK | MB_ICONWARNING);
            if (!peerSaved[i]) saved_device_forget_details(peers[i].identity.id);
        }
        refreshPeerNode(i);
    }
}

static DWORD clipboardSequence;
static uint64_t localClipboardTime;
static unsigned int appliedClipboard;

static void showPeer(void)
{
    WCHAR description[256];
    WCHAR localizedStatus[512];
    SessionStatus session;
    BOOL paired;
    int selected = selectedPeer();
    session_status(&session);
    paired = selected >= 0 && selected < peerCount && session.phase == SESSION_PAIRED &&
             !strcmp(session.deviceId, peers[selected].identity.id);
    EnableWindow(sendButton, paired);
    EnableWindow(sideSend, paired);
    EnableWindow(sideRing, paired);
    EnableWindow(sideClipboard, paired);
    EnableWindow(ringButton, paired);
    EnableMenuItem(GetMenu(GetParent(devices)), ID_PING, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    EnableWindow(clipboardButton, paired);
    CheckMenuItem(GetMenu(GetParent(devices)), ID_SYNC_CLIPBOARD,
                  MF_BYCOMMAND | (session.clipboardSync ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_SEND_CLIPBOARD, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_RECEIVE_CLIPBOARD,
                   MF_BYCOMMAND | (paired && session.clipboardRevision ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_SYNC_CLIPBOARD, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    if (paired && session.batteryCharge >= 0) {
        wsprintfW(description, tr(L"Battery %d%%%s"), session.batteryCharge, session.batteryCharging ? tr(L" (charging)") : L"");
        setControlText(battery, description);
    } else setControlText(battery, tr(L"Battery —"));
    EnableWindow(cancelTransferButton, paired && session.transferActive);
    EnableMenuItem(GetMenu(GetParent(devices)), ID_SEND, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(GetMenu(GetParent(devices)), ID_RING, MF_BYCOMMAND | (paired ? MF_ENABLED : MF_GRAYED));
    if (selected < 0 || selected >= peerCount) {
        EnableWindow(pairButton, FALSE);
        setControlText(heading, L"ConnectXP");
        setControlText(state, tr(L"Select a device from either group."));
        setControlText(transferText, tr(L"No active transfers"));
        SendMessageW(progress, PBM_SETPOS, 0, 0);
        EnableMenuItem(GetMenu(GetParent(devices)), ID_PAIR, MF_BYCOMMAND | MF_GRAYED);
        return;
    }
    setControlText(heading, peers[selected].name);
    EnableWindow(pairButton, peers[selected].identity.version == 7 && peers[selected].address[0]);
    EnableMenuItem(GetMenu(GetParent(devices)), ID_PAIR, MF_BYCOMMAND |
                   (peers[selected].identity.version == 7 && peers[selected].address[0] ? MF_ENABLED : MF_GRAYED));
    if (!strcmp(session.deviceId, peers[selected].identity.id) && session.phase != SESSION_IDLE) {
        setControlText(state, language_status(session.text, localizedStatus, ARRAYSIZE(localizedStatus)));
        setControlText(transferText, session.transferText[0] ? tr(session.transferText) : tr(L"No active transfers"));
        SendMessageW(progress, PBM_SETPOS, session.transferPercent, 0);
        if (session.code[0] && (session.phase == SESSION_WAIT_PHONE || session.phase == SESSION_CONFIRM)) {
            wsprintfW(description, tr(L"Pairing code %s — compare it with your phone"), session.code);
            setStatusText(description);
        } else if (session.phase == SESSION_PAIRED) {
            if (session.clipboardRevision) {
                wsprintfW(description, tr(L"Phone clipboard received at %02u:%02u:%02u. Use Edit to copy it to Windows."),
                          session.clipboardReceived.wHour, session.clipboardReceived.wMinute, session.clipboardReceived.wSecond);
                setStatusText(description);
            } else setStatusText(tr(L"Waiting for phone clipboard. Use Send clipboard in KDE Connect on the phone."));
        } else setStatusText(language_status(session.text, localizedStatus, ARRAYSIZE(localizedStatus)));
        return;
    }
    setControlText(transferText, tr(L"No active transfers"));
    SendMessageW(progress, PBM_SETPOS, 0, 0);
    if (peerSaved[selected] && !peerSeen[selected]) {
        setControlText(state, peers[selected].address[0] ? tr(L"Saved device. Try its last known address.") :
                                                     tr(L"Saved device. Refresh discovery on the phone."));
    } else if ((DWORD)(GetTickCount() - peers[selected].seen) > 120000)
        setControlText(state, tr(L"Discovery is stale. Refresh devices on the phone."));
    else
        setControlText(state, tr(L"Discovered. Not connected."));
    wsprintfW(description, peerSaved[selected] ? tr(L"%hs:%u · Protocol %d · Saved") : tr(L"%hs:%u · Protocol %d · Not paired"),
              peers[selected].address, peers[selected].identity.port,
              peers[selected].identity.version);
    setStatusText(description);
}

static void pairSelected(HWND window)
{
    int selected = selectedPeer();
    if (selected < 0 || selected >= peerCount) return;
    if (!peers[selected].address[0]) {
        MessageBoxW(window, tr(L"Refresh discovery in KDE Connect on the phone to find its address."), L"ConnectXP", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (peers[selected].identity.version != 7) {
        MessageBoxW(window, tr(L"This release supports pairing with protocol 7. Protocol 8 pairing is under development."), L"ConnectXP", MB_OK | MB_ICONINFORMATION);
        return;
    }
    appliedClipboard = 0;
    if (!session_start(&peers[selected]))
        MessageBoxW(window, tr(L"Could not start the connection."), L"ConnectXP", MB_OK | MB_ICONERROR);
    showPeer();
}

static BOOL pairedSelection(void)
{
    SessionStatus snapshot;
    int selected = selectedPeer();
    session_status(&snapshot);
    return selected >= 0 && selected < peerCount && snapshot.phase == SESSION_PAIRED &&
           !strcmp(snapshot.deviceId, peers[selected].identity.id);
}

static void sendFile(HWND window)
{
    if (pairedSelection()) jobs_select(window);
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
        MessageBoxW(window, read == 0 ? tr(L"The clipboard is busy. Please try again.") :
            tr(L"Copy plain text of up to 8 KiB (UTF-8) before sending it."), L"ConnectXP", MB_OK | MB_ICONINFORMATION);
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
        MessageBoxW(window, tr(L"Cannot update the clipboard. Please try again."), L"ConnectXP", MB_OK | MB_ICONINFORMATION);
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
    AppendMenuW(menu, MF_STRING, ID_SEND_CLIPBOARD, tr(L"Send PC clipboard to phone"));
    AppendMenuW(menu, MF_STRING | (snapshot.clipboardRevision ? 0 : MF_GRAYED),
                ID_RECEIVE_CLIPBOARD, tr(L"Paste phone clipboard to PC"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (snapshot.clipboardSync ? MF_CHECKED : 0), ID_SYNC_CLIPBOARD,
                tr(L"Sync text clipboard automatically"));
    GetWindowRect(clipboardButton, &rect);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rect.left, rect.bottom, 0, window, NULL);
    DestroyMenu(menu);
}

static void hideBalloon(HWND window)
{
    NOTIFYICONDATAW info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = NOTIFYICONDATAW_V2_SIZE;
    info.hWnd = window;
    info.uID = 1;
    info.uFlags = NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &info);
    balloonBusy = FALSE;
    balloonEnded = GetTickCount();
}

static void pollNotifications(HWND window, const SessionStatus *snapshot)
{
    PhoneNotification *notification;
    NOTIFYICONDATAW info;
    notifications_ui_tick(snapshot);
    if (balloonGeneration != snapshot->generation) {
        if (balloonBusy) hideBalloon(window);
        balloonGeneration = snapshot->generation;
        balloonId[0] = 0;
    }
    if (snapshot->phase != SESSION_PAIRED) {
        if (balloonBusy) hideBalloon(window);
        return;
    }
    notification = HeapAlloc(GetProcessHeap(), 0, sizeof(*notification));
    if (!notification) return;
    if (balloonBusy) {
        if ((balloonId[0] && !session_notification_get(balloonId, notification)) || (DWORD)(GetTickCount() - balloonStarted) > 40000)
            hideBalloon(window);
        HeapFree(GetProcessHeap(), 0, notification);
        return;
    }
    if (trayAdded && (DWORD)(GetTickCount() - balloonEnded) >= 500 && session_notification_alert(notification)) {
        ZeroMemory(&info, sizeof(info));
        info.cbSize = NOTIFYICONDATAW_V2_SIZE;
        info.hWnd = window;
        info.uID = 1;
        info.uFlags = NIF_INFO;
        info.dwInfoFlags = NIIF_INFO;
        info.uTimeout = 10000;
        notifications_balloon_text(notification, info.szInfoTitle, info.szInfo);
        if (Shell_NotifyIconW(NIM_MODIFY, &info)) {
            strcpy(balloonId, notification->id);
            balloonBusy = TRUE;
            balloonStarted = GetTickCount();
        }
    }
    HeapFree(GetProcessHeap(), 0, notification);
}

static void autoConnect(const SessionStatus *snapshot)
{
    int attempt;
    if (autoPaused || !settings_get(L"AutoConnect", TRUE) || !peerCount ||
        (snapshot->phase != SESSION_IDLE && snapshot->phase != SESSION_ERROR && snapshot->phase != SESSION_CLOSED) ||
        (DWORD)(GetTickCount() - lastAutoAttempt) < 15000) return;
    lastAutoAttempt = GetTickCount();
    for (attempt = 0; attempt < peerCount; ++attempt) {
        int index = autoCursor++ % peerCount;
        if (!peerSaved[index] || !peers[index].address[0] || peers[index].identity.version != 7 ||
            saved_device_exists(peers[index].identity.id) != 1) continue;
        if (session_start_saved(&peers[index])) TreeView_SelectItem(devices, peerNodes[index]);
        break;
    }
}

static HICON makeBatteryIcon(int charge, BOOL charging)
{
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 16, 16);
    BYTE opaque[64] = {0};
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, opaque);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    RECT box = {1, 4, 14, 13}, terminal = {14, 7, 16, 10}, fill = {3, 6, 3 + charge * 9 / 100, 11};
    ICONINFO info;
    HICON icon;
    HDC maskDc = CreateCompatibleDC(screen);
    HGDIOBJ previousMask = SelectObject(maskDc, mask);
    FillRect(maskDc, &(RECT){0, 0, 16, 16}, (HBRUSH)GetStockObject(WHITE_BRUSH));
    FillRect(maskDc, &box, (HBRUSH)GetStockObject(BLACK_BRUSH));
    FillRect(maskDc, &terminal, (HBRUSH)GetStockObject(BLACK_BRUSH));
    FillRect(dc, &(RECT){0, 0, 16, 16}, (HBRUSH)GetStockObject(BLACK_BRUSH));
    FillRect(dc, &box, GetSysColorBrush(COLOR_WINDOW));
    FrameRect(dc, &box, GetSysColorBrush(COLOR_BTNTEXT));
    FillRect(dc, &terminal, GetSysColorBrush(COLOR_BTNTEXT));
    {
        HBRUSH brush = CreateSolidBrush(GetSysColor(charging ? COLOR_HIGHLIGHT : charge <= 15 ? COLOR_HOTLIGHT : COLOR_ACTIVECAPTION));
        FillRect(dc, &fill, brush);
        DeleteObject(brush);
    }
    if (charging) {
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT)); SetBkMode(dc, TRANSPARENT);
        TextOutW(dc, 5, 0, L"+", 1);
        SetBkMode(maskDc, TRANSPARENT); SetTextColor(maskDc, RGB(0, 0, 0));
        TextOutW(maskDc, 5, 0, L"+", 1);
    }
    SelectObject(dc, previous);
    ZeroMemory(&info, sizeof(info));
    info.fIcon = TRUE; info.hbmColor = bitmap; info.hbmMask = mask;
    icon = CreateIconIndirect(&info);
    SelectObject(maskDc, previousMask); DeleteDC(maskDc);
    DeleteObject(bitmap); DeleteObject(mask); DeleteDC(dc); ReleaseDC(NULL, screen);
    return icon;
}

static void pollBattery(HWND window, const SessionStatus *snapshot)
{
    NOTIFYICONDATAW item;
    BOOL connected = snapshot->phase == SESSION_PAIRED && snapshot->batteryCharge >= 0;
    if (snapshot->generation != batteryGeneration) {
        lowBatteryWarned = FALSE; batteryGeneration = snapshot->generation;
    }
    if (!connected) {
        if (batteryAdded) { item = tray; item.uID = 2; Shell_NotifyIconW(NIM_DELETE, &item); }
        batteryAdded = FALSE; previousBattery = -2;
        return;
    }
    if (previousBattery != snapshot->batteryCharge || previousCharging != snapshot->batteryCharging || !batteryAdded) {
        HICON next = makeBatteryIcon(snapshot->batteryCharge, snapshot->batteryCharging);
        ZeroMemory(&item, sizeof(item)); item.cbSize = NOTIFYICONDATAW_V2_SIZE;
        item.hWnd = window; item.uID = 2; item.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        item.uCallbackMessage = WM_TRAY; item.hIcon = next;
        wsprintfW(item.szTip, tr(L"Phone battery: %d%%%s"), snapshot->batteryCharge, snapshot->batteryCharging ? tr(L" (charging)") : L"");
        if (next && Shell_NotifyIconW(batteryAdded ? NIM_MODIFY : NIM_ADD, &item)) {
            if (batteryIcon) DestroyIcon(batteryIcon);
            batteryIcon = next; batteryAdded = TRUE;
            previousBattery = snapshot->batteryCharge; previousCharging = snapshot->batteryCharging;
        } else if (next) DestroyIcon(next);
    }
    if (snapshot->batteryCharging || snapshot->batteryCharge > 20) lowBatteryWarned = FALSE;
    if (!lowBatteryWarned && !snapshot->batteryCharging && snapshot->batteryCharge <= 15 && !balloonBusy && trayAdded) {
        item = tray; item.uFlags = NIF_INFO; item.dwInfoFlags = NIIF_WARNING; item.uTimeout = 10000;
        lstrcpynW(item.szInfoTitle, tr(L"Phone battery is low"), ARRAYSIZE(item.szInfoTitle));
        wsprintfW(item.szInfo, tr(L"Phone battery: %d%%%s"), snapshot->batteryCharge, L"");
        if (Shell_NotifyIconW(NIM_MODIFY, &item)) {
            lowBatteryWarned = TRUE; balloonBusy = TRUE; balloonStarted = GetTickCount(); balloonId[0] = 0;
        }
    }
}

static void recentNotifications(void)
{
    NotificationSummary rows[2];
    WCHAR text[768] = L"", app[97], title[257];
    unsigned int revision;
    int count = session_notification_list(rows, currentLayout.compact ? 1 : 2, &revision), i;
    for (i = 0; i < count; ++i) {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rows[i].app, -1, app, ARRAYSIZE(app))) lstrcpyW(app, L"KDE Connect");
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rows[i].title, -1, title, ARRAYSIZE(title))) title[0] = 0;
        if (i) lstrcatW(text, L"\r\n\r\n");
        lstrcatW(text, app); lstrcatW(text, L" — "); lstrcatW(text, title);
    }
    setControlText(recentText, count ? text : tr(L"No notifications yet"));
}

static void pollSession(HWND window)
{
    static BOOL confirmationShown;
    SessionStatus snapshot;
    WCHAR question[512];
    session_status(&snapshot);
    pollBattery(window, &snapshot);
    pollNotifications(window, &snapshot);
    jobs_tick(window, normalFont, &snapshot);
    recentNotifications();
    autoConnect(&snapshot);
    if (reconnectForName && snapshot.phase == SESSION_PAIRED && !snapshot.transferActive) {
        int i;
        reconnectForName = FALSE;
        for (i = 0; i < peerCount; ++i) {
            if (!strcmp(peers[i].identity.id, snapshot.deviceId)) {
                appliedClipboard = 0;
                session_start(&peers[i]);
                break;
            }
        }
        session_status(&snapshot);
    } else if (snapshot.phase == SESSION_IDLE || snapshot.phase == SESSION_ERROR || snapshot.phase == SESSION_CLOSED)
        reconnectForName = FALSE;
    syncSavedDevices(window, &snapshot);
    pollClipboard(window, &snapshot);
    if (snapshot.phase != SESSION_CONFIRM) confirmationShown = FALSE;
    if (snapshot.phase == SESSION_CONFIRM && !confirmationShown) {
        confirmationShown = TRUE;
        wsprintfW(question, tr(L"Pairing code %s\n\nDoes it match the code on your phone?\nConfirm only when both codes match."), snapshot.code);
        session_confirm(MessageBoxW(window, question, tr(L"ConnectXP — verify device"),
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES);
    }
    showPeer();
}

static void startDiscovery(HWND window)
{
    WCHAR errorText[160];
    int error = discovery_start(window, WM_DISCOVERY);
    if (error) {
        wsprintfW(errorText, tr(L"UDP 1716 is unavailable (error %d). Restart discovery from the Device menu."), error);
        setStatusText(errorText);
    } else {
        setStatusText(tr(L"Listening on UDP 1716. Refresh devices in KDE Connect on your phone."));
    }
}

static void receiveDiscovery(void)
{
    DiscoveryPeer peer;
    BOOL detailsChanged;
    int batch, result, index;
    /* Bound each dispatch so network traffic cannot monopolize the UI. */
    for (batch = 0; batch < 32; ++batch) {
        result = discovery_receive(&peer);
        if (result <= 0) {
            if (result < 0) setStatusText(tr(L"UDP receive error. Restart discovery from the Device menu."));
            break;
        }
        if (result != 1 || network_service_is_self(peer.identity.id)) continue;
        for (index = 0; index < peerCount; ++index)
            if (!strcmp(peers[index].identity.id, peer.identity.id)) break;
        if (index == MAX_PEERS) continue;
        detailsChanged = index == peerCount || wcscmp(peers[index].name, peer.name) ||
            strcmp(peers[index].address, peer.address) || peers[index].identity.port != peer.identity.port ||
            peers[index].identity.version != peer.identity.version;
        if (index == peerCount) {
            peerSaved[index] = saved_device_exists(peer.identity.id) == 1;
            ++peerCount;
        }
        peers[index] = peer;
        peerSeen[index] = TRUE;
        if (peerSaved[index] && detailsChanged) saved_device_remember(&peers[index]);
        refreshPeerNode(index);
        if (selectedPeer() < 0 && peerNodes[index]) TreeView_SelectItem(devices, peerNodes[index]);
        showPeer();
    }
}

/* XP shell32 icon indexes: computer, open folder, search, document.
 * Load from the running OS so XP supplies its own artwork. */
static HICON loadSystemIcon(int shellIndex, int fallbackResource, int pixels)
{
    WCHAR path[MAX_PATH];
    HICON icon = NULL;
    UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
    if (length && length + 13 < ARRAYSIZE(path)) {
        lstrcatW(path, L"\\shell32.dll");
        if (pixels <= 16) ExtractIconExW(path, shellIndex, NULL, &icon, 1);
        else ExtractIconExW(path, shellIndex, &icon, NULL, 1);
    }
    if (!icon)
        icon = LoadImageW(instance, MAKEINTRESOURCEW(fallbackResource),
                          IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR);
    return icon;
}

static LRESULT CALLBACK taskLinkProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        InvalidateRect(window, NULL, FALSE);
    } else if (message == WM_MOUSELEAVE) InvalidateRect(window, NULL, FALSE);
    else if (message == WM_SETCURSOR && IsWindowEnabled(window)) {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    } else if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(window), BN_CLICKED), (LPARAM)window);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

static HWND control(HWND parent, const WCHAR *kind, const WCHAR *text,
                    DWORD style, int id)
{
    HWND child = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                                 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                                 instance, NULL);
    if (child) SendMessageW(child, WM_SETFONT, (WPARAM)normalFont, TRUE);
    if (child && (id == ID_SIDE_SEND || id == ID_OPEN_FOLDER || id == ID_HISTORY ||
                  id == ID_SIDE_RING || id == ID_NOTIFICATIONS || id == ID_SIDE_CLIPBOARD))
        SetWindowSubclass(child, taskLinkProc, 1, 0);
    return child;
}

static HMENU createMenu(void)
{
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU device = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU language = CreatePopupMenu();
    HMENU options = CreatePopupMenu();
    AppendMenuW(file, MF_STRING | MF_GRAYED, ID_SEND, tr(L"Send file…"));
    AppendMenuW(file, MF_STRING, ID_OPEN_FOLDER, tr(L"Open folder"));
    AppendMenuW(file, MF_STRING, ID_HISTORY, tr(L"Transfer history"));
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, ID_EXIT, tr(L"Exit"));
    AppendMenuW(device, MF_STRING, ID_DISCOVERY, tr(L"Start discovery"));
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_PAIR, tr(L"Pair / connect"));
    AppendMenuW(device, MF_STRING, ID_DISCONNECT, tr(L"Disconnect"));
    AppendMenuW(device, MF_STRING, ID_FORGET, tr(L"Forget pairing on this PC…"));
    AppendMenuW(device, MF_SEPARATOR, 0, NULL);
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_PING, tr(L"Send ping"));
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_RING, tr(L"Find phone"));
    AppendMenuW(edit, MF_STRING, ID_DEVICE_NAME, tr(L"PC device name…"));
    AppendMenuW(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(edit, MF_STRING | MF_GRAYED, ID_SEND_CLIPBOARD, tr(L"Send clipboard to phone"));
    AppendMenuW(edit, MF_STRING | MF_GRAYED, ID_RECEIVE_CLIPBOARD, tr(L"Paste phone clipboard to PC\tCtrl+Shift+V"));
    AppendMenuW(edit, MF_STRING | MF_GRAYED, ID_SYNC_CLIPBOARD, tr(L"Sync text clipboard automatically"));
    AppendMenuW(view, MF_STRING, ID_NOTIFICATIONS, tr(L"Notifications…"));
    AppendMenuW(language, MF_STRING, ID_LANGUAGE_ENGLISH, L"English");
    AppendMenuW(language, MF_STRING, ID_LANGUAGE_RUSSIAN, L"Русский");
    CheckMenuRadioItem(language, ID_LANGUAGE_ENGLISH, ID_LANGUAGE_RUSSIAN,
                       ID_LANGUAGE_ENGLISH + language_current(), MF_BYCOMMAND);
    AppendMenuW(view, MF_POPUP, (UINT_PTR)language, tr(L"Language"));
    AppendMenuW(options, MF_STRING | (settings_get(L"AutoConnect", TRUE) ? MF_CHECKED : 0), ID_AUTOCONNECT, tr(L"Connect automatically"));
    AppendMenuW(options, MF_STRING | (settings_autostart() ? MF_CHECKED : 0), ID_AUTOSTART, tr(L"Start with Windows"));
    AppendMenuW(options, MF_STRING | (settings_get(L"StartInTray", FALSE) ? MF_CHECKED : 0), ID_START_TRAY, tr(L"Start in tray"));
    AppendMenuW(help, MF_STRING, ID_ABOUT, tr(L"About…"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, tr(L"File"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)edit, tr(L"Edit"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)device, tr(L"Device"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, tr(L"View"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)options, tr(L"Options"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)help, tr(L"Help"));
    return menu;
}

static void updateLanguage(HWND window)
{
    HMENU previous = GetMenu(window), replacement = createMenu();
    TVITEMW item;
    int i;
    if (replacement && SetMenu(window, replacement)) DestroyMenu(previous);
    else if (replacement) DestroyMenu(replacement);
    DrawMenuBar(window);
    setControlText(groupHeaders[0], tr(L"Devices"));
    setControlText(groupHeaders[1], tr(L"File transfers"));
    setControlText(groupHeaders[2], tr(L"Other actions"));
    setControlText(sideSend, tr(L"Send file…"));
    setControlText(sideFolder, tr(L"Open folder"));
    setControlText(sideHistory, tr(L"Transfer history"));
    setControlText(sideRing, tr(L"Find phone"));
    setControlText(sideNotifications, tr(L"Notifications…"));
    setControlText(sideClipboard, tr(L"Clipboard"));
    setControlText(pairButton, tr(L"Pair / connect"));
    setControlText(sendButton, tr(L"Send file"));
    setControlText(ringButton, tr(L"Find phone"));
    setControlText(clipboardButton, tr(L"Clipboard"));
    setControlText(cancelTransferButton, tr(L"Cancel"));
    ZeroMemory(&item, sizeof(item));
    item.mask = TVIF_TEXT;
    item.hItem = savedGroup;
    item.pszText = (LPWSTR)tr(L"Saved devices");
    SendMessageW(devices, TVM_SETITEMW, 0, (LPARAM)&item);
    item.hItem = discoveredGroup;
    item.pszText = (LPWSTR)tr(L"Discovered devices");
    SendMessageW(devices, TVM_SETITEMW, 0, (LPARAM)&item);
    for (i = 0; i < peerCount; ++i) refreshPeerNode(i);
    notifications_ui_translate();
    jobs_translate();
    showPeer();
    InvalidateRect(window, NULL, TRUE);
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
    lstrcpynW(tray.szTip, L"ConnectXP", ARRAYSIZE(tray.szTip));
    trayAdded = Shell_NotifyIconW(NIM_ADD, &tray);
    balloonBusy = FALSE;
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
    AppendMenuW(menu, MF_STRING, ID_SHOW, tr(L"Open ConnectXP"));
    AppendMenuW(menu, MF_STRING, ID_NOTIFICATIONS, tr(L"Notifications…"));
    AppendMenuW(menu, MF_STRING, ID_ABOUT, tr(L"About…"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, tr(L"Exit"));
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
    UiPlacement positions[24];
    HWND links[] = {sideSend, sideFolder, sideHistory, sideRing, sideNotifications, sideClipboard};
    HWND actions[] = {sendButton, ringButton, clipboardButton};
    int count = 0, i, closed = groupCollapsed[0] | (groupCollapsed[1] << 1) | (groupCollapsed[2] << 2);
    GetClientRect(window, &area);
    SendMessageW(statusBar, WM_SIZE, 0, 0);
    GetWindowRect(statusBar, &statusArea);
    currentLayout = main_layout(area.right, area.bottom - (statusArea.bottom - statusArea.top), closed);
#define PLACE(handle, rectangle, show) positions[count++] = (UiPlacement){handle, rectangle, show}
    for (i = 0; i < 3; ++i) PLACE(groupHeaders[i], currentLayout.headers[i], 1);
    PLACE(devices, currentLayout.tree, !groupCollapsed[0]);
    PLACE(heading, currentLayout.heading, 1);
    PLACE(state, currentLayout.state, 1);
    PLACE(battery, currentLayout.battery, 1);
    PLACE(pairButton, currentLayout.pair, 1);
    for (i = 0; i < 3; ++i) PLACE(actions[i], currentLayout.actions[i], 1);
    PLACE(transferText, currentLayout.transferText, 1);
    PLACE(cancelTransferButton, currentLayout.cancel, 1);
    PLACE(progress, currentLayout.progress, 1);
    for (i = 0; i < 6; ++i) PLACE(links[i], currentLayout.links[i], !groupCollapsed[i < 3 ? 1 : 2]);
    PLACE(recentText, currentLayout.recentText, 1);
#undef PLACE
    ui_apply_layout(window, positions, count);
}

static void drawSection(HDC dc, UiRect position, const WCHAR *caption)
{
    RECT border = {position.x, position.y + 7, position.x + position.w, position.y + position.h};
    RECT text = {position.x + 8, position.y, position.x + position.w - 8, position.y + 20};
    RECT measured = text;
    DrawEdge(dc, &border, EDGE_ETCHED, BF_RECT);
    DrawTextW(dc, caption, -1, &measured, DT_CALCRECT | DT_SINGLELINE);
    measured.right += 8;
    FillRect(dc, &measured, GetSysColorBrush(COLOR_WINDOW));
    text.left += 4;
    SetTextColor(dc, theme_link_color());
    DrawTextW(dc, caption, -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void paintWorkspace(HDC dc, const RECT *client)
{
    RECT panel;
    SidebarLayout *sidebar = &currentLayout.sidebar;
    HGDIOBJ font = SelectObject(dc, normalFont);
    FillRect(dc, client, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(dc, TRANSPARENT);
    SetRect(&panel, 0, 0, currentLayout.paneWidth, currentLayout.contentHeight);
    theme_sidebar(dc, &panel);
    if (!groupCollapsed[0]) {
        SetRect(&panel, 12, sidebar->devicesTop + sidebar->headerHeight,
            currentLayout.paneWidth - 12, sidebar->devicesTop + sidebar->headerHeight + sidebar->devicesBody);
        theme_panel(dc, &panel, FALSE);
    }
    if (!groupCollapsed[1]) {
        SetRect(&panel, 12, sidebar->filesTop + sidebar->headerHeight,
            currentLayout.paneWidth - 12, sidebar->filesTop + sidebar->headerHeight + sidebar->filesBody);
        theme_panel(dc, &panel, FALSE);
    }
    if (!groupCollapsed[2]) {
        SetRect(&panel, 12, sidebar->otherTop + sidebar->headerHeight,
            currentLayout.paneWidth - 12, sidebar->otherTop + sidebar->headerHeight + sidebar->otherBody);
        theme_panel(dc, &panel, FALSE);
    }
    drawSection(dc, currentLayout.transferFrame, tr(L"File transfers"));
    drawSection(dc, currentLayout.recentFrame, tr(L"Recent notifications"));
    DrawIconEx(dc, currentLayout.icon.x, currentLayout.icon.y, icons[0], 32, 32, 0, NULL, DI_NORMAL);
    SelectObject(dc, font);
}

static void drawButton(const DRAWITEMSTRUCT *item)
{
    WCHAR label[128];
    RECT rect = item->rcItem, textRect = rect;
    int iconIndex = (int)item->CtlID - ID_SEND + 1;
    BOOL disabled = (item->itemState & ODS_DISABLED) != 0;
    BOOL side = item->hwndItem == sideSend || item->hwndItem == sideFolder || item->hwndItem == sideHistory ||
        item->hwndItem == sideRing || item->hwndItem == sideNotifications || item->hwndItem == sideClipboard;
    HGDIOBJ previous;
    if (side) {
        if (item->hwndItem == sideSend || item->hwndItem == sideFolder) iconIndex = 1;
        else if (item->hwndItem == sideRing) iconIndex = 2;
        else iconIndex = 3;
    }
    if (item->CtlID >= ID_GROUP_DEVICES && item->CtlID <= ID_GROUP_OTHER) {
        RECT parent, status, pane;
        POINT origin = {0, 0};
        GetClientRect(GetParent(item->hwndItem), &parent);
        GetWindowRect(statusBar, &status);
        MapWindowPoints(item->hwndItem, GetParent(item->hwndItem), &origin, 1);
        SetRect(&pane, 0, 0, currentLayout.paneWidth, parent.bottom - (status.bottom - status.top));
        SaveDC(item->hDC);
        OffsetViewportOrgEx(item->hDC, -origin.x, -origin.y, NULL);
        theme_sidebar(item->hDC, &pane);
        RestoreDC(item->hDC, -1);
        GetWindowTextW(item->hwndItem, label, ARRAYSIZE(label));
        theme_group_header(item->hDC, &rect, label, groupFont, groupCollapsed[item->CtlID - ID_GROUP_DEVICES], (item->itemState & ODS_FOCUS) != 0);
        return;
    }
    if (side) theme_link_background(item->hDC, &rect);
    else theme_button(item->hDC, &rect, (item->itemState & ODS_SELECTED) != 0, disabled, (item->itemState & ODS_FOCUS) != 0);
    if (iconIndex >= 1 && iconIndex <= 3) {
        int size = side || currentLayout.compact ? 16 : 32;
        DrawIconEx(item->hDC, side ? 2 : 9, (rect.bottom - size) / 2,
            side || currentLayout.compact ? taskIcons[iconIndex - 1] : icons[iconIndex], size, size, 0, NULL, DI_NORMAL);
    }
    GetWindowTextW(item->hwndItem, label, ARRAYSIZE(label));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled ? GetSysColor(COLOR_GRAYTEXT) : side ? theme_link_color() : GetSysColor(COLOR_BTNTEXT));
    {
        POINT cursor;
        RECT screen;
        GetCursorPos(&cursor);
        GetWindowRect(item->hwndItem, &screen);
        previous = SelectObject(item->hDC, side && !disabled && PtInRect(&screen, cursor) ? linkFont : normalFont);
    }
    textRect.left += side ? 26 : currentLayout.compact ? 30 : 49;
    textRect.right -= 4;
    DrawTextW(item->hDC, label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(item->hDC, previous);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&rect, -3, -3);
        DrawFocusRect(item->hDC, &rect);
    }
}

static HICON loadPhoneIcon(int pixels)
{
    WCHAR path[MAX_PATH];
    HICON icon = NULL;
    UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
    if (length && length + 12 < ARRAYSIZE(path)) {
        lstrcatW(path, L"\\tapi32.dll");
        if (pixels <= 16) ExtractIconExW(path, 0, NULL, &icon, 1);
        else ExtractIconExW(path, 0, &icon, NULL, 1);
    }
    if (!icon) icon = LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, pixels, pixels, 0);
    return icon;
}

static BOOL initializeDeviceTree(void)
{
    TVINSERTSTRUCTW group;
    HICON phone = loadPhoneIcon(16);
    int i;
    deviceImages = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
    if (!deviceImages || !phone) {
        if (phone) DestroyIcon(phone);
        return FALSE;
    }
    i = ImageList_AddIcon(deviceImages, phone);
    DestroyIcon(phone);
    if (i < 0) return FALSE;
    TreeView_SetImageList(devices, deviceImages, TVSIL_NORMAL);
    ZeroMemory(&group, sizeof(group));
    group.hParent = TVI_ROOT;
    group.hInsertAfter = TVI_LAST;
    group.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    group.item.state = TVIS_BOLD;
    group.item.stateMask = TVIS_BOLD;
    group.item.iImage = I_IMAGENONE;
    group.item.iSelectedImage = I_IMAGENONE;
    group.item.pszText = (LPWSTR)tr(L"Saved devices");
    savedGroup = (HTREEITEM)SendMessageW(devices, TVM_INSERTITEMW, 0, (LPARAM)&group);
    group.item.pszText = (LPWSTR)tr(L"Discovered devices");
    discoveredGroup = (HTREEITEM)SendMessageW(devices, TVM_INSERTITEMW, 0, (LPARAM)&group);
    if (!savedGroup || !discoveredGroup) return FALSE;
    peerCount = saved_devices_load(peers, MAX_PEERS);
    for (i = 0; i < peerCount; ++i) {
        peerSaved[i] = TRUE;
        refreshPeerNode(i);
    }
    if (peerCount) TreeView_SelectItem(devices, peerNodes[0]);
    return TRUE;
}

static BOOL createControls(HWND window)
{
    NONCLIENTMETRICSW metrics;
    ZeroMemory(&metrics, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        return FALSE;
    normalFont = CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfUnderline = TRUE;
    linkFont = CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfUnderline = FALSE;
    metrics.lfMessageFont.lfWeight = FW_BOLD;
    groupFont = CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfHeight = -18;
    metrics.lfMessageFont.lfWeight = FW_BOLD;
    titleFont = CreateFontIndirectW(&metrics.lfMessageFont);
    devices = CreateWindowExW(0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, window, NULL, instance, NULL);
    SendMessageW(devices, WM_SETFONT, (WPARAM)normalFont, TRUE);
    groupHeaders[0] = control(window, L"BUTTON", tr(L"Devices"), BS_OWNERDRAW | WS_TABSTOP, ID_GROUP_DEVICES);
    groupHeaders[1] = control(window, L"BUTTON", tr(L"File transfers"), BS_OWNERDRAW | WS_TABSTOP, ID_GROUP_FILES);
    groupHeaders[2] = control(window, L"BUTTON", tr(L"Other actions"), BS_OWNERDRAW | WS_TABSTOP, ID_GROUP_OTHER);
    sideSend = control(window, L"BUTTON", tr(L"Send file…"), BS_OWNERDRAW | WS_TABSTOP, ID_SIDE_SEND);
    sideFolder = control(window, L"BUTTON", tr(L"Open folder"), BS_OWNERDRAW | WS_TABSTOP, ID_OPEN_FOLDER);
    sideHistory = control(window, L"BUTTON", tr(L"Transfer history"), BS_OWNERDRAW | WS_TABSTOP, ID_HISTORY);
    sideRing = control(window, L"BUTTON", tr(L"Find phone"), BS_OWNERDRAW | WS_TABSTOP, ID_SIDE_RING);
    sideNotifications = control(window, L"BUTTON", tr(L"Notifications…"), BS_OWNERDRAW | WS_TABSTOP, ID_NOTIFICATIONS);
    sideClipboard = control(window, L"BUTTON", tr(L"Clipboard"), BS_OWNERDRAW | WS_TABSTOP, ID_SIDE_CLIPBOARD);
    recentText = control(window, L"STATIC", tr(L"No notifications yet"), SS_LEFT, 0);
    heading = control(window, L"STATIC", L"ConnectXP", SS_LEFT | SS_ENDELLIPSIS, 0);
    SendMessageW(heading, WM_SETFONT, (WPARAM)titleFont, TRUE);
    state = control(window, L"STATIC", tr(L"No connected devices"), SS_LEFT, 0);
    battery = control(window, L"STATIC", tr(L"Battery —"), SS_LEFT, 0);
    pairButton = control(window, L"BUTTON", tr(L"Pair / connect"), BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, ID_PAIR);
    sendButton = control(window, L"BUTTON", tr(L"Send file"), BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, ID_SEND);
    ringButton = control(window, L"BUTTON", tr(L"Find phone"), BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, ID_RING);
    clipboardButton = control(window, L"BUTTON", tr(L"Clipboard"), BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, ID_CLIPBOARD);
    transferText = control(window, L"STATIC", tr(L"No active transfers"), SS_LEFT, 0);
    cancelTransferButton = control(window, L"BUTTON", tr(L"Cancel"), BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, ID_CANCEL_TRANSFER);
    progress = control(window, PROGRESS_CLASSW, L"", 0, 0);
    statusBar = control(window, STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP, 0);
    setStatusText(tr(L"  Ready."));
    if (devices && !initializeDeviceTree()) return FALSE;
    return normalFont && titleFont && groupFont && linkFont && groupHeaders[0] && groupHeaders[1] && groupHeaders[2] && sideSend && sideFolder && sideHistory && sideRing &&
        sideNotifications && sideClipboard && recentText && devices && heading && state && battery &&
        pairButton && sendButton && ringButton && clipboardButton && transferText &&
        cancelTransferButton && progress && statusBar;
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (taskbarCreated && message == taskbarCreated) {
        addTray(window);
        batteryAdded = FALSE; previousBattery = -2;
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        if (!createControls(window)) return -1;
        theme_refresh(window);
        TreeView_SetBkColor(devices, theme_body_color());
        TreeView_SetTextColor(devices, theme_link_color());
        jobs_init();
        DragAcceptFiles(window, TRUE);
        network_service_start(window, WM_INCOMING);
        lastAutoAttempt = GetTickCount() - 12000;
        addTray(window);
        startDiscovery(window);
        SetTimer(window, 1, 500, NULL);
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) { if (trayAdded) ShowWindow(window, SW_HIDE); }
        else layout(window);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = 640;
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = 480;
        return 0;
    case WM_EXITSIZEMOVE:
        layout(window);
        return 0;
    case WM_DRAWITEM:
        drawButton((const DRAWITEMSTRUCT *)lParam);
        return TRUE;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        theme_refresh(window);
        previousBattery = -2;
        TreeView_SetBkColor(devices, theme_body_color());
        TreeView_SetTextColor(devices, theme_link_color());
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    case WM_DROPFILES:
        if (pairedSelection()) jobs_drop(window, (HDROP)wParam);
        else { DragFinish((HDROP)wParam); MessageBoxW(window, tr(L"Connect to a phone before sending files."), L"ConnectXP", MB_OK | MB_ICONINFORMATION); }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_ERASEBKGND:
        /* WM_PAINT paints the complete background through a memory DC. */
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC target = BeginPaint(window, &paint), memory;
        HBITMAP bitmap;
        HGDIOBJ previous = NULL;
        RECT client;
        GetClientRect(window, &client);
        memory = CreateCompatibleDC(target);
        bitmap = memory ? CreateCompatibleBitmap(target, client.right > 0 ? client.right : 1, client.bottom > 0 ? client.bottom : 1) : NULL;
        if (bitmap) previous = SelectObject(memory, bitmap);
        else { if (memory) DeleteDC(memory); memory = target; }
        paintWorkspace(memory, &client);
        if (bitmap) {
            /* The destination DC excludes children via WS_CLIPCHILDREN. */
            BitBlt(target, paint.rcPaint.left, paint.rcPaint.top,
                paint.rcPaint.right - paint.rcPaint.left, paint.rcPaint.bottom - paint.rcPaint.top,
                memory, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
            SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DISCOVERY:
        if (WSAGETSELECTERROR(lParam)) {
            setStatusText(tr(L"UDP error. Restart discovery from the Device menu."));
        } else if (WSAGETSELECTEVENT(lParam) == FD_READ) receiveDiscovery();
        return 0;
    case WM_TIMER:
        pollSession(window);
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lParam)->hwndFrom == devices &&
            ((NMHDR *)lParam)->code == TVN_SELCHANGEDW) showPeer();
        return 0;
    case WM_INCOMING: {
        IncomingConnection *incoming = (IncomingConnection *)lParam;
        SessionStatus snapshot;
        int index;
        session_status(&snapshot);
        for (index = 0; index < peerCount; ++index) if (!strcmp(peers[index].identity.id, incoming->peer.identity.id)) break;
        if (!incoming->peer.identity.port) incoming->peer.identity.port = index < peerCount && peers[index].identity.port ? peers[index].identity.port : 1716;
        if (!autoPaused && (snapshot.phase == SESSION_IDLE || snapshot.phase == SESSION_ERROR || snapshot.phase == SESSION_CLOSED)) {
            if (session_accept(&incoming->peer, incoming->socket)) {
                if (index == peerCount && peerCount < MAX_PEERS) {
                    ++peerCount;
                    peerSaved[index] = TRUE;
                }
                if (index < peerCount) {
                    peers[index] = incoming->peer; peerSeen[index] = TRUE;
                    refreshPeerNode(index); TreeView_SelectItem(devices, peerNodes[index]);
                }
            }
        } else closesocket(incoming->socket);
        HeapFree(GetProcessHeap(), 0, incoming);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_GROUP_DEVICES:
        case ID_GROUP_FILES:
        case ID_GROUP_OTHER: {
            int group = LOWORD(wParam) - ID_GROUP_DEVICES;
            groupCollapsed[group] = !groupCollapsed[group];
            layout(window);
            break;
        }
        case ID_AUTOCONNECT:
        case ID_START_TRAY:
        case ID_AUTOSTART: {
            BOOL ok;
            if (LOWORD(wParam) == ID_AUTOSTART) ok = settings_set_autostart(!settings_autostart());
            else {
                const WCHAR *key = LOWORD(wParam) == ID_AUTOCONNECT ? L"AutoConnect" : L"StartInTray";
                BOOL enabled = !settings_get(key, LOWORD(wParam) == ID_AUTOCONNECT);
                ok = settings_set(key, enabled);
                if (ok && LOWORD(wParam) == ID_AUTOCONNECT) { autoPaused = !enabled; lastAutoAttempt = GetTickCount() - 15000; }
            }
            if (!ok) MessageBoxW(window, tr(L"Could not save settings."), L"ConnectXP", MB_OK | MB_ICONERROR);
            updateLanguage(window);
            break;
        }
        case ID_OPEN_FOLDER: jobs_open_folder(window); break;
        case ID_HISTORY: jobs_show(window, normalFont); break;
        case ID_SIDE_SEND: sendFile(window); break;
        case ID_SIDE_RING: if (pairedSelection()) session_ring(); break;
        case ID_SIDE_CLIPBOARD: clipboardMenu(window); break;
        case ID_LANGUAGE_ENGLISH:
        case ID_LANGUAGE_RUSSIAN:
            if (language_select(LOWORD(wParam) - ID_LANGUAGE_ENGLISH)) updateLanguage(window);
            else MessageBoxW(window, tr(L"Could not save the language setting."), L"ConnectXP", MB_OK | MB_ICONERROR);
            break;
        case ID_DEVICE_NAME:
            if (device_name_dialog(window)) { reconnectForName = TRUE; network_service_announce(); }
            break;
        case ID_NOTIFICATIONS:
            restoreWindow(window);
            notifications_ui_show(window, normalFont, NULL);
            break;
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
        case ID_PING:
            if (pairedSelection() && !session_ping())
                MessageBoxW(window, tr(L"Cannot queue the ping. Check the connection or retry after the current transfer."),
                            L"ConnectXP", MB_OK | MB_ICONINFORMATION);
            break;
        case ID_RING: if (pairedSelection()) session_ring(); break;
        case ID_CANCEL_TRANSFER: jobs_cancel(); break;
        case ID_FORGET: {
            int selected = selectedPeer();
            if (selected >= 0 && selected < peerCount &&
                MessageBoxW(window, tr(L"Remove the saved certificate for the selected device on this PC?"),
                    L"ConnectXP", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                autoPaused = TRUE;
                if (session_forget(&peers[selected])) {
                    saved_device_forget_details(peers[selected].identity.id);
                    peerSaved[selected] = FALSE;
                    peerConnected[selected] = FALSE;
                    refreshPeerNode(selected);
                }
                showPeer();
            }
            break;
        }
        case ID_PAIR: autoPaused = FALSE; lastAutoAttempt = GetTickCount(); pairSelected(window); break;
        case ID_DISCONNECT: autoPaused = TRUE; session_stop(); showPeer(); break;
        case ID_DISCOVERY: startDiscovery(window); network_service_announce(); break;
        case ID_EXIT: DestroyWindow(window); break;
        case ID_SHOW: restoreWindow(window); break;
        case ID_ABOUT:
            language_about(window, icons[0]);
            break;
        }
        return 0;
    case WM_TRAY:
        if (lParam == NIN_BALLOONUSERCLICK) {
            balloonBusy = FALSE;
            balloonEnded = GetTickCount();
            restoreWindow(window);
            if (balloonId[0]) notifications_ui_show(window, normalFont, balloonId);
        } else if (lParam == NIN_BALLOONTIMEOUT || lParam == NIN_BALLOONHIDE) {
            balloonBusy = FALSE;
            balloonEnded = GetTickCount();
        } else if (lParam == WM_LBUTTONDBLCLK) restoreWindow(window);
        else if (lParam == WM_RBUTTONUP) trayMenu(window);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY: {
        MSG pending;
        network_service_stop();
        while (PeekMessageW(&pending, window, WM_INCOMING, WM_INCOMING, PM_REMOVE)) {
            IncomingConnection *incoming = (IncomingConnection *)pending.lParam;
            closesocket(incoming->socket);
            HeapFree(GetProcessHeap(), 0, incoming);
        }
        jobs_close();
        theme_close();
        notifications_ui_close();
        KillTimer(window, 1);
        session_stop();
        {
            SessionStatus snapshot;
            session_status(&snapshot);
            jobs_tick(window, normalFont, &snapshot);
        }
        discovery_stop();
        if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
        if (batteryAdded) { NOTIFYICONDATAW item = tray; item.uID = 2; Shell_NotifyIconW(NIM_DELETE, &item); }
        if (batteryIcon) { DestroyIcon(batteryIcon); batteryIcon = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE current, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSEXW cls;
    INITCOMMONCONTROLSEX common;
    HWND window;
    RECT work;
    MONITORINFO monitor;
    POINT cursor;
    WindowPlacement initial;
    HANDLE singleton;
    MSG message;
    HACCEL shortcuts;
    ACCEL pasteShortcut = {FVIRTKEY | FCONTROL | FSHIFT, 'V', ID_RECEIVE_CLIPBOARD};
    int result, index;
    (void)previous;
    startInTray = (command && strstr(command, "--tray")) || settings_get(L"StartInTray", FALSE);
    instance = current;
    language_load();
    singleton = CreateMutexW(NULL, FALSE, L"Local\\ConnectXP.Desktop");
    if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND running = FindWindowW(L"ConnectXP.MainWindow", NULL);
        if (running) { ShowWindow(running, SW_RESTORE); SetForegroundWindow(running); }
        CloseHandle(singleton);
        return 0;
    }
    clipboardSequence = GetClipboardSequenceNumber();
    if (!session_init()) {
        MessageBoxW(NULL, tr(L"Could not initialize cryptography."), L"ConnectXP", MB_OK | MB_ICONERROR);
        return 1;
    }
    common.dwSize = sizeof(common);
    common.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES | ICC_ANIMATE_CLASS;
    if (!InitCommonControlsEx(&common)) return 1;
    icons[0] = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
    icons[1] = loadSystemIcon(4, 102, 32);
    icons[2] = loadSystemIcon(22, 103, 32);
    icons[3] = loadSystemIcon(1, 104, 32);
    taskIcons[0] = loadSystemIcon(4, 102, 16);
    taskIcons[1] = loadSystemIcon(22, 103, 16);
    taskIcons[2] = loadSystemIcon(1, 104, 16);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ZeroMemory(&cls, sizeof(cls));
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = windowProc;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hIcon = icons[0];
    cls.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
    cls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    cls.lpszClassName = L"ConnectXP.MainWindow";
    if (!RegisterClassExW(&cls)) return 1;
    SetRect(&work, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    GetCursorPos(&cursor);
    ZeroMemory(&monitor, sizeof(monitor)); monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor)) work = monitor.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    initial = initial_window(work.left, work.top, work.right - work.left, work.bottom - work.top);
    window = CreateWindowExW(0, cls.lpszClassName, L"ConnectXP", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        initial.x, initial.y, initial.w, initial.h, NULL, createMenu(), instance, NULL);
    if (!window) {
        MessageBoxW(NULL, tr(L"Could not create the ConnectXP window."), L"ConnectXP", MB_ICONERROR);
        return 1;
    }
    shortcuts = CreateAcceleratorTableW(&pasteShortcut, 1);
    if (!startInTray || !trayAdded) ShowWindow(window, show);
    UpdateWindow(window);
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (jobs_message(&message)) continue;
        if (!shortcuts || (message.hwnd != window && !IsChild(window, message.hwnd)) ||
            !TranslateAcceleratorW(window, shortcuts, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (shortcuts) DestroyAcceleratorTable(shortcuts);
    session_free();
    if (deviceImages) ImageList_Destroy(deviceImages);
    DeleteObject(normalFont);
    DeleteObject(titleFont);
    DeleteObject(groupFont);
    DeleteObject(linkFont);
    for (index = 0; index < 4; ++index) DestroyIcon(icons[index]);
    for (index = 0; index < 3; ++index) DestroyIcon(taskIcons[index]);
    DestroyIcon(cls.hIconSm);
    if (singleton) CloseHandle(singleton);
    return result == -1 ? 1 : (int)message.wParam;
}

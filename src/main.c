#include "discovery.h"
#include "session.h"
#include <commctrl.h>
#include <shellapi.h>
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
#define MAX_PEERS 64

static HINSTANCE instance;
static HWND devices, heading, state, battery, sendButton, ringButton, pairButton;
static HWND clipboardButton, transferBox, transferText, progress, statusBar;
static HFONT normalFont, titleFont;
static HICON icons[4];
static NOTIFYICONDATAW tray;
static BOOL trayAdded;
static UINT taskbarCreated;
static DiscoveryPeer peers[MAX_PEERS];
static int peerCount;

static void showPeer(void)
{
    WCHAR description[256];
    SessionStatus session;
    int selected = ListView_GetNextItem(devices, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= peerCount) {
        EnableWindow(pairButton, FALSE);
        SetWindowTextW(heading, L"Connect95");
        SetWindowTextW(state, L"Нет выбранного устройства");
        return;
    }
    SetWindowTextW(heading, peers[selected].name);
    EnableWindow(pairButton, peers[selected].identity.version == 7);
    EnableMenuItem(GetMenu(GetParent(devices)), ID_PAIR, MF_BYCOMMAND |
                   (peers[selected].identity.version == 7 ? MF_ENABLED : MF_GRAYED));
    session_status(&session);
    if (!strcmp(session.deviceId, peers[selected].identity.id) && session.phase != SESSION_IDLE) {
        SetWindowTextW(state, session.text);
        if (session.code[0] && (session.phase == SESSION_WAIT_PHONE || session.phase == SESSION_CONFIRM)) {
            wsprintfW(description, L"Код сопряжения: %s — сверьте с телефоном", session.code);
            SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)description);
        } else SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)session.text);
        return;
    }
    if ((DWORD)(GetTickCount() - peers[selected].seen) > 120000)
        SetWindowTextW(state, L"Объявление устарело. Обновите поиск на телефоне.");
    else
        SetWindowTextW(state, L"Обнаружено. Соединение не установлено.");
    wsprintfW(description, L"%hs:%u · Протокол %d · Без сопряжения",
              peers[selected].address, peers[selected].identity.port,
              peers[selected].identity.version);
    SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)description);
}

static void pairSelected(HWND window)
{
    int selected = ListView_GetNextItem(devices, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= peerCount) return;
    if (peers[selected].identity.version != 7) {
        MessageBoxW(window, L"В этой версии сопряжение поддерживает протокол 7. Поддержка версии 8 ещё разрабатывается.", L"Connect95", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!session_start(&peers[selected]))
        MessageBoxW(window, L"Не удалось начать подключение.", L"Connect95", MB_OK | MB_ICONERROR);
    showPeer();
}

static void pollSession(HWND window)
{
    static BOOL confirmationShown;
    SessionStatus snapshot;
    WCHAR question[512];
    session_status(&snapshot);
    if (snapshot.phase != SESSION_CONFIRM) confirmationShown = FALSE;
    if (snapshot.phase == SESSION_CONFIRM && !confirmationShown) {
        confirmationShown = TRUE;
        wsprintfW(question, L"Код сопряжения: %s\n\nСовпадает ли он с кодом на телефоне?\nСохраняйте сопряжение только при совпадении кодов.", snapshot.code);
        session_confirm(MessageBoxW(window, question, L"Connect95 — проверка устройства",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES);
    }
    showPeer();
}

static void startDiscovery(HWND window)
{
    WCHAR errorText[160];
    int error = discovery_start(window, WM_DISCOVERY);
    if (error) {
        wsprintfW(errorText, L"UDP 1716 недоступен (ошибка %d). Повторите поиск через меню «Устройство».", error);
        SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)errorText);
    } else {
        SendMessageW(statusBar, SB_SETTEXTW, 0,
            (LPARAM)L"UDP 1716: ожидание. Обновите список устройств в KDE Connect на телефоне.");
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
                (LPARAM)L"Ошибка приёма UDP. Повторите поиск через меню «Устройство».");
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
    AppendMenuW(file, MF_STRING | MF_GRAYED, ID_SEND, L"Отправить файл…");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"Выход");
    AppendMenuW(device, MF_STRING, ID_DISCOVERY, L"Начать поиск устройств");
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_PAIR, L"Сопрячь / подключиться");
    AppendMenuW(device, MF_STRING, ID_DISCONNECT, L"Отключиться");
    AppendMenuW(device, MF_STRING, ID_FORGET, L"Забыть сопряжение на ПК…");
    AppendMenuW(device, MF_SEPARATOR, 0, NULL);
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_RING, L"Найти телефон");
    AppendMenuW(device, MF_STRING | MF_GRAYED, ID_CLIPBOARD, L"Буфер обмена");
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"О программе…");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"Файл");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)device, L"Устройство");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)help, L"Справка");
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
    AppendMenuW(menu, MF_STRING, ID_SHOW, L"Открыть Connect95");
    AppendMenuW(menu, MF_STRING, ID_ABOUT, L"О программе…");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Выход");
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
    MoveWindow(battery, rightX + 44, 72, rightWidth - 190, 21, TRUE);
    MoveWindow(pairButton, rightX + rightWidth - 140, 68, 140, 25, TRUE);
    MoveWindow(sendButton, rightX, buttonY, buttonWidth, 72, TRUE);
    MoveWindow(ringButton, rightX + buttonWidth + 6, buttonY, buttonWidth, 72, TRUE);
    MoveWindow(clipboardButton, rightX + (buttonWidth + 6) * 2,
               buttonY, rightWidth - (buttonWidth + 6) * 2, 72, TRUE);
    MoveWindow(transferBox, rightX, 188, rightWidth, height - 196, TRUE);
    MoveWindow(transferText, rightX + 12, 210, rightWidth - 24, 22, TRUE);
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
    column.pszText = L"Устройства";
    column.cx = 125;
    SendMessageW(devices, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
    heading = control(window, L"STATIC", L"Connect95", SS_LEFT, 0);
    SendMessageW(heading, WM_SETFONT, (WPARAM)titleFont, TRUE);
    state = control(window, L"STATIC", L"Нет подключённых устройств", SS_LEFT, 0);
    battery = control(window, L"STATIC", L"Заряд батареи —", SS_LEFT, 0);
    pairButton = control(window, L"BUTTON", L"Сопрячь / подключиться", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, ID_PAIR);
    sendButton = control(window, L"BUTTON", L"Отправить файл", BS_OWNERDRAW | WS_DISABLED, ID_SEND);
    ringButton = control(window, L"BUTTON", L"Найти телефон", BS_OWNERDRAW | WS_DISABLED, ID_RING);
    clipboardButton = control(window, L"BUTTON", L"Буфер обмена", BS_OWNERDRAW | WS_DISABLED, ID_CLIPBOARD);
    transferBox = control(window, L"BUTTON", L"Передача файлов", BS_GROUPBOX, 0);
    transferText = control(window, L"STATIC", L"Нет активных передач", SS_LEFT, 0);
    progress = control(window, PROGRESS_CLASSW, L"", 0, 0);
    statusBar = control(window, STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP, 0);
    SendMessageW(statusBar, SB_SETTEXTW, 0,
        (LPARAM)L"  Интерфейс готов. Поддержка KDE Connect в разработке.");
    return normalFont && titleFont && devices && heading && state && battery &&
        pairButton && sendButton && ringButton && clipboardButton && transferBox && transferText &&
        progress && statusBar;
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
            SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)L"Ошибка UDP. Повторите поиск через меню «Устройство».");
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
        case ID_FORGET: {
            int selected = ListView_GetNextItem(devices, -1, LVNI_SELECTED);
            if (selected >= 0 && selected < peerCount &&
                MessageBoxW(window, L"Удалить сохранённый сертификат выбранного устройства на этом ПК?",
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
            ShellAboutA(window, "Connect95", "Connect95 0.3.0\r\n"
                "KDE Connect companion for Windows XP.\r\n"
                "Written in C using Win32.\r\n"
                "TLS 1.2 pairing v7. Mbed TLS 3.6.8, cJSON.", icons[0]);
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
        MessageBoxW(NULL, L"Не удалось создать окно Connect95.", L"Connect95", MB_ICONERROR);
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

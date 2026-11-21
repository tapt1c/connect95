#include "language.h"
#include <shellapi.h>
#include <string.h>
#include <wchar.h>

static int selectedLanguage = LANGUAGE_ENGLISH;
static const struct { const WCHAR *english, *russian; } translations[] = {
    {L"Devices", L"Устройства"},
    {L"Other actions", L"Другие действия"},
    {L"Options", L"Параметры"},
    {L"Open folder", L"Открыть папку"},
    {L"Transfer history", L"История передач"},
    {L"No completed transfers", L"Завершённых передач нет"},
    {L"Recent notifications", L"Последние уведомления"},
    {L"No notifications yet", L"Уведомлений пока нет"},
    {L"Connect automatically", L"Подключаться автоматически"},
    {L"Start with Windows", L"Запускать вместе с Windows"},
    {L"Start in tray", L"Запускать в трее"},
    {L"Could not save settings.", L"Не удалось сохранить настройки."},
    {L"Could not initialize cryptography.", L"Не удалось инициализировать криптографию."},
    {L"Could not open the received files folder.", L"Не удалось открыть папку полученных файлов."},
    {L"Select up to 64 files while connected. Folders and paths longer than 259 characters are not supported.", L"Выберите до 64 файлов при подключённом телефоне. Папки и пути длиннее 259 символов не поддерживаются."},
    {L"Could not read the file selection. Try selecting fewer files.", L"Не удалось прочитать список выбранных файлов. Попробуйте выбрать меньше файлов."},
    {L"Connect to a phone before sending files.", L"Перед отправкой файлов подключитесь к телефону."},
    {L"Sent", L"Отправлено"},
    {L"Received", L"Получено"},
    {L"File transfer — ConnectXP", L"Передача файлов — ConnectXP"},
    {L"Receiving from phone", L"Получение с телефона"},
    {L"Sending to phone", L"Отправка на телефон"},
    {L"Time left:", L"Осталось:"},
    {L"Queued:", L"В очереди:"},
    {L"File size is unknown", L"Размер файла неизвестен"},
    {L"Phone battery: %d%%%s", L"Батарея телефона: %d%%%s"},
    {L"Phone battery is low", L"Низкий заряд телефона"},
    {L"Author - Daniel Myslivets\r\nKDE Connect implementation for Windows XP", L"Автор идеи - Daniel Myslivets\r\nРеализация KDE Connect для Windows XP"},
    {L"File", L"Файл"},
    {L"Edit", L"Правка"},
    {L"Device", L"Устройство"},
    {L"View", L"Вид"},
    {L"Help", L"Справка"},
    {L"Language", L"Язык"},
    {L"About…", L"О программе…"},
    {L"Exit", L"Выход"},
    {L"Send file…", L"Отправить файл…"},
    {L"Send file", L"Отправить файл"},
    {L"Start discovery", L"Поиск устройств"},
    {L"Pair / connect", L"Сопряжение / связь"},
    {L"Disconnect", L"Отключиться"},
    {L"Forget pairing on this PC…", L"Забыть сопряжение на ПК…"},
    {L"Send ping", L"Отправить пинг"},
    {L"Find phone", L"Найти телефон"},
    {L"PC device name…", L"Имя этого ПК…"},
    {L"Send clipboard to phone", L"Отправить буфер на телефон"},
    {L"Send PC clipboard to phone", L"Отправить буфер ПК на телефон"},
    {L"Paste phone clipboard to PC", L"Вставить буфер телефона на ПК"},
    {L"Paste phone clipboard to PC\tCtrl+Shift+V", L"Вставить буфер телефона на ПК\tCtrl+Shift+V"},
    {L"Sync text clipboard automatically", L"Синхронизировать буфер автоматически"},
    {L"Notifications…", L"Уведомления…"},
    {L"Open ConnectXP", L"Открыть ConnectXP"},
    {L"Saved devices", L"Сохранённые устройства"},
    {L"Discovered devices", L"Найденные устройства"},
    {L" (offline)", L" (не в сети)"},
    {L"No connected devices", L"Нет подключённых устройств"},
    {L"Battery —", L"Заряд батареи —"},
    {L"Battery %d%%%s", L"Заряд батареи %d%%%s"},
    {L" (charging)", L" (заряжается)"},
    {L"Clipboard", L"Буфер обмена"},
    {L"File transfers", L"Передача файлов"},
    {L"No active transfers", L"Нет активных передач"},
    {L"Cancel", L"Отмена"},
    {L"  Ready.", L"  Готово."},
    {L"Select a device from either group.", L"Выберите устройство в списке."},
    {L"Pairing code %s — compare it with your phone", L"Код сопряжения %s — сравните с телефоном"},
    {L"Phone clipboard received at %02u:%02u:%02u. Use Edit to copy it to Windows.", L"Буфер телефона получен в %02u:%02u:%02u. Вставка через меню «Правка»."},
    {L"Waiting for phone clipboard. Use Send clipboard in KDE Connect on the phone.", L"Ожидание буфера. Отправьте его из KDE Connect на телефоне."},
    {L"Saved device. Try its last known address.", L"Устройство сохранено. Можно подключиться по прежнему адресу."},
    {L"Saved device. Refresh discovery on the phone.", L"Устройство сохранено. Обновите поиск на телефоне."},
    {L"Discovery is stale. Refresh devices on the phone.", L"Данные устарели. Обновите список устройств на телефоне."},
    {L"Discovered. Not connected.", L"Обнаружено. Соединение не установлено."},
    {L"%hs:%u · Protocol %d · Saved", L"%hs:%u · Протокол %d · Сохранено"},
    {L"%hs:%u · Protocol %d · Not paired", L"%hs:%u · Протокол %d · Без сопряжения"},
    {L"Refresh discovery in KDE Connect on the phone to find its address.", L"Обновите поиск в KDE Connect на телефоне, чтобы узнать его адрес."},
    {L"This release supports pairing with protocol 7. Protocol 8 pairing is under development.", L"Эта версия поддерживает сопряжение по протоколу 7. Поддержка протокола 8 разрабатывается."},
    {L"Could not start the connection.", L"Не удалось начать подключение."},
    {L"All files\0*.*\0\0", L"Все файлы\0*.*\0\0"},
    {L"Send file to phone", L"Отправка файла на телефон"},
    {L"A transfer is already in progress or the connection has closed.", L"Передача уже выполняется или соединение закрыто."},
    {L"The clipboard is busy. Please try again.", L"Буфер обмена занят. Повторите попытку."},
    {L"Copy plain text of up to 8 KiB (UTF-8) before sending it.", L"Перед отправкой скопируйте текст размером до 8 КиБ в UTF-8."},
    {L"Cannot update the clipboard. Please try again.", L"Не удалось обновить буфер обмена. Повторите попытку."},
    {L"Pairing code %s\n\nDoes it match the code on your phone?\nConfirm only when both codes match.", L"Код сопряжения %s\n\nОн совпадает с кодом на телефоне?\nПодтверждайте только при совпадении обоих кодов."},
    {L"ConnectXP — verify device", L"ConnectXP — проверка устройства"},
    {L"UDP 1716 is unavailable (error %d). Restart discovery from the Device menu.", L"UDP 1716 недоступен (ошибка %d). Перезапустите поиск в меню «Устройство»."},
    {L"Listening on UDP 1716. Refresh devices in KDE Connect on your phone.", L"Ожидание на UDP 1716. Обновите поиск в KDE Connect на телефоне."},
    {L"UDP receive error. Restart discovery from the Device menu.", L"Ошибка приёма UDP. Перезапустите поиск в меню «Устройство»."},
    {L"UDP error. Restart discovery from the Device menu.", L"Ошибка UDP. Перезапустите поиск в меню «Устройство»."},
    {L"Cannot queue the ping. Check the connection or retry after the current transfer.", L"Не удалось отправить пинг. Проверьте связь или дождитесь окончания передачи."},
    {L"Remove the saved certificate for the selected device on this PC?", L"Удалить сохранённый сертификат выбранного устройства на этом ПК?"},
    {L"Could not create the ConnectXP window.", L"Не удалось создать окно ConnectXP."},
    {L"Pairing is saved, but the device name and address could not be remembered.", L"Сопряжение сохранено, но не удалось запомнить имя и адрес устройства."},
    {L"New notification from your phone.", L"Новое уведомление с телефона."},
    {L"Enable Notification sync and notification access for KDE Connect on your phone.\r\nSelect a notification to read its full text.", L"Включите синхронизацию и доступ к уведомлениям для KDE Connect на телефоне.\r\nВыберите уведомление, чтобы прочитать его полностью."},
    {L"This notification was removed from the phone.", L"Это уведомление удалено на телефоне."},
    {L"Phone notifications — %d", L"Уведомления телефона — %d"},
    {L"Disconnected — %d received notifications", L"Отключено — получено уведомлений: %d"},
    {L"Application", L"Приложение"},
    {L"Title", L"Заголовок"},
    {L"Notifications — ConnectXP", L"Уведомления — ConnectXP"},
    {L"Could not open the notifications window.", L"Не удалось открыть окно уведомлений."},
    {L"PC device name", L"Имя этого ПК"},
    {L"Shown in KDE Connect on your phone. An idle connection reconnects after saving.", L"Отображается в KDE Connect на телефоне. После сохранения связь будет обновлена."},
    {L"Device name (up to 32 characters)", L"Имя устройства (до 32 символов)"},
    {L"Save", L"Сохранить"},
    {L"Enter 1–32 characters without leading or trailing spaces.\nKDE Connect filters these characters: \" ' , ; : . ! ? ( ) [ ] < >", L"Введите 1–32 символа без пробелов по краям.\nKDE Connect не поддерживает символы: \" ' , ; : . ! ? ( ) [ ] < >"},
    {L"Could not save the device name in your Windows profile.", L"Не удалось сохранить имя устройства в профиле Windows."},
    {L"Could not open the device name dialog.", L"Не удалось открыть окно изменения имени устройства."},
    {L"Could not save the language setting.", L"Не удалось сохранить выбранный язык."},
    {L"Could not establish the connection.", L"Не удалось установить соединение."},
    {L"Preparing the PC certificate…", L"Подготовка сертификата ПК…"},
    {L"Cannot load the PC key. Check the user profile and other running copies of ConnectXP.", L"Не удалось загрузить ключ ПК. Проверьте профиль и другие запущенные копии ConnectXP."},
    {L"Connecting to the phone…", L"Подключение к телефону…"},
    {L"Cannot reach the phone over TCP. Refresh discovery on the phone and retry.", L"Телефон недоступен по TCP. Обновите поиск на телефоне и повторите попытку."},
    {L"TLS 1.2 — checking the phone certificate…", L"TLS 1.2 — проверка сертификата телефона…"},
    {L"TLS or certificate error. Check the date on both devices.", L"Ошибка TLS или сертификата. Проверьте дату на обоих устройствах."},
    {L"The phone certificate changed or its saved certificate is unreadable. Pairing stopped.", L"Сертификат телефона изменился или сохранённая копия повреждена. Сопряжение остановлено."},
    {L"Paired. TLS connection established.", L"Сопряжено. TLS-соединение установлено."},
    {L"Could not send the pairing request.", L"Не удалось отправить запрос сопряжения."},
    {L"Accept pairing on the phone and compare the code.", L"Примите запрос на телефоне и сравните код."},
    {L"TLS connection closed. Select Pair / connect to reconnect.", L"TLS-соединение закрыто. Нажмите «Сопряжение / связь» для подключения."},
    {L"Pairing timed out. Please try again.", L"Время ожидания сопряжения истекло. Повторите попытку."},
    {L"The phone sent an incomplete packet.", L"Телефон отправил неполный пакет."},
    {L"The phone sent an invalid or oversized packet.", L"Телефон отправил некорректный или слишком большой пакет."},
    {L"The phone sent an invalid packet.", L"Телефон отправил некорректный пакет."},
    {L"The phone unpaired, but its local trust record could not be removed.", L"Телефон отменил сопряжение, но локальную запись доверия удалить не удалось."},
    {L"The phone rejected or cancelled pairing.", L"Телефон отклонил или отменил сопряжение."},
    {L"The phone accepted. Confirm the matching code on this PC.", L"Телефон принял запрос. Подтвердите совпадение кода на этом ПК."},
    {L"Pairing cancelled on this PC.", L"Сопряжение отменено на этом ПК."},
    {L"Could not save the phone certificate.", L"Не удалось сохранить сертификат телефона."},
    {L"The incoming file announcement is invalid or unsupported.", L"Объявление входящего файла некорректно или не поддерживается."},
    {L"Transfer interrupted.", L"Передача прервана."},
    {L"Disconnected.", L"Отключено."},
    {L"Connecting…", L"Подключение…"},
    {L"Could not start the network worker.", L"Не удалось запустить сетевой поток."},
    {L"Pairing forgotten on this PC. You can pair again.", L"Сопряжение удалено на этом ПК. Можно выполнить его заново."},
    {L"Could not remove pairing from the user profile.", L"Не удалось удалить сопряжение из профиля пользователя."},
    {L"Preparing to send the file…", L"Подготовка к отправке файла…"},
    {L"File sent to the phone.", L"Файл отправлен на телефон."},
    {L"File saved in My Documents\\ConnectXP.", L"Файл сохранён в папке «Мои документы»\\ConnectXP."},
    {L"Cannot open the selected file.", L"Не удалось открыть выбранный файл."},
    {L"This file name cannot be encoded as UTF-8.", L"Имя файла не удалось преобразовать в UTF-8."},
    {L"This file name is not supported.", L"Это имя файла не поддерживается."},
    {L"The file is too large for the protocol.", L"Размер файла превышает возможности протокола."},
    {L"Cannot listen for file transfer on TCP 1739-1764.", L"Не удалось открыть TCP 1739–1764 для передачи файлов."},
    {L"Cannot announce the file to the phone.", L"Не удалось сообщить телефону о файле."},
    {L"The phone did not connect. Allow inbound TCP 1739-1764 for ConnectXP.", L"Телефон не подключился. Разрешите входящие TCP 1739–1764 для ConnectXP."},
    {L"The file connection certificate did not match the paired phone, or TLS failed.", L"Сертификат передачи не совпадает с сертификатом телефона или произошла ошибка TLS."},
    {L"File sending failed or timed out.", L"Ошибка отправки файла или превышено время ожидания."},
    {L"Transfer cancelled.", L"Передача отменена."},
    {L"Cannot create the received file in My Documents\\ConnectXP.", L"Не удалось создать входящий файл в папке «Мои документы»\\ConnectXP."},
    {L"Cannot connect to the phone's file transfer port.", L"Не удалось подключиться к порту передачи файлов телефона."},
    {L"File receiving failed, timed out, or the phone sent fewer bytes than announced.", L"Ошибка приёма, превышено время ожидания или телефон отправил неполный файл."},
    {L"The received file size did not match its announcement, or the transfer did not finish.", L"Размер полученного файла не совпал с объявленным или передача не завершилась."},
    {L"Cannot finish saving the received file.", L"Не удалось завершить сохранение полученного файла."}
};

void language_load(void)
{
    HKEY key;
    DWORD value, type, size = sizeof(value);
    selectedLanguage = LANGUAGE_ENGLISH;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Connect95", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExW(key, L"Language", NULL, &type, (BYTE *)&value, &size) == ERROR_SUCCESS &&
        type == REG_DWORD && size == sizeof(value) && value <= LANGUAGE_RUSSIAN) selectedLanguage = (int)value;
    RegCloseKey(key);
}

int language_current(void) { return selectedLanguage; }

BOOL language_select(int language)
{
    HKEY key;
    DWORD value = (DWORD)language;
    LONG result;
    if (language != LANGUAGE_ENGLISH && language != LANGUAGE_RUSSIAN) return FALSE;
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Connect95", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(key, L"Language", 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) return FALSE;
    selectedLanguage = language;
    return TRUE;
}

const WCHAR *tr(const WCHAR *english)
{
    unsigned int i;
    if (selectedLanguage == LANGUAGE_RUSSIAN)
        for (i = 0; i < sizeof(translations) / sizeof(translations[0]); ++i)
            if (!wcscmp(english, translations[i].english)) return translations[i].russian;
    return english;
}

const WCHAR *language_status(const WCHAR *english, WCHAR *buffer, int capacity)
{
    const WCHAR *suffix, *translated = tr(english);
    WCHAR prefix[512];
    int length;
    if (translated != english || !(suffix = wcsstr(english, L" (TLS -0x"))) return translated;
    length = (int)(suffix - english);
    if (length >= (int)ARRAYSIZE(prefix)) return english;
    memcpy(prefix, english, (size_t)length * sizeof(WCHAR));
    prefix[length] = 0;
    translated = tr(prefix);
    if (lstrlenW(translated) + lstrlenW(suffix) >= capacity) return english;
    lstrcpyW(buffer, translated);
    lstrcatW(buffer, suffix);
    return buffer;
}

void language_about(HWND owner, HICON icon)
{
    ShellAboutW(owner, L"ConnectXP", tr(L"Author - Daniel Myslivets\r\nKDE Connect implementation for Windows XP"), icon);
}

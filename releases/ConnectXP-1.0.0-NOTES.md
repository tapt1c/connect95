# ConnectXP 1.0

Native C / Win32 KDE Connect client for Windows XP SP3 x86.

## Начало работы

1. Распакуйте portable-архив в постоянную папку и запустите `ConnectXP.exe`.
2. Подключите ПК и телефон к одной локальной сети. Откройте KDE Connect на телефоне и обновите поиск устройств.
3. Выберите телефон в ConnectXP, нажмите «Сопряжение / связь» и подтвердите совпадение кода на обоих устройствах.
4. Для получения уведомлений включите соответствующий модуль KDE Connect и предоставьте ему доступ к уведомлениям Android.

## Что входит в 1.0

- Русский и английский интерфейсы, оформление XP с учётом установленной темы, включая ресурсы `shellstyle.dll`.
- Компактная раскладка для 800×600, изменение размера и разворачивание главного окна.
- Короткие группы «Сохранённые» и «Найденные» со значками папки и поиска; у телефонов свои значки.
- Открытие уведомлений и истории сохраняет развёрнутое состояние окна; возврат из трея восстанавливает свёрнутое окно.
- Сопряжение по протоколу 7, TLS 1.2, сохранение сертификата телефона и автоподключение.
- Приём входящих соединений от уже сопряжённых телефонов.
- Передача файлов в обе стороны, выбор нескольких файлов, перетаскивание и очередь.
- Окно передачи в духе IE6, история с выборочным удалением записей и открытие папки полученных файлов.
- Поиск телефона, пинг, текстовый буфер обмена, уведомления телефона и всплывающие сообщения в трее.
- Значки батареи Windows XP, точный процент в подсказке и предупреждение о низком заряде.
- Мультимедийный пульт через стандартные медиаклавиши Windows.
- Автозапуск, запуск в трее и изменение имени ПК для KDE Connect.

Полученные файлы сохраняются в «Мои документы\ConnectXP». Профиль, сертификаты и история сохраняются в `%LOCALAPPDATA%\ConnectXP`, настройки — в файле `settings.ini` в той же папке. Старый профиль не импортируется; после обновления потребуется повторное сопряжение.

## Практические ограничения

- Поддерживается сопряжение по протоколу 7. Сопряжение по протоколу 8 пока не реализовано.
- Одновременно поддерживается одно соединение с телефоном; автоматическое подключение использует сохранённые сертификаты.
- Передачи выполняются последовательно. Во время передачи обработка управляющих пакетов и уведомлений откладывается.
- Пути файлов ограничены возможностями XP. В очереди может быть до 64 файлов, папки не поддерживаются.
- Android может ограничивать фоновый доступ к буферу обмена. В этом случае используйте «Отправить буфер обмена» в KDE Connect на телефоне.
- Мультимедийный пульт отправляет команды воспроизведения/паузы, остановки и переключения треков совместимому проигрывателю. Название композиции, фактическое состояние воспроизведения, перемотка и управление громкостью отдельного проигрывателя не считываются.

Для поиска устройств используется UDP 1716, для входящих управляющих соединений — TCP 1716–1738, для отправки файлов — TCP 1739–1764. Системная дата должна быть правильной для проверки сертификатов.

## Quick start

Extract the portable archive into a permanent directory and run `ConnectXP.exe`.
Use the same LAN as your phone, refresh devices in Android KDE Connect, then select the phone and choose **Pair / connect**. Compare and confirm the code on both devices.

Received files go to **My Documents\ConnectXP**. Choose **View → Language** for English or Russian. Startup preferences are under **Options**. Use Ctrl/Shift in **Transfer history**, then **Remove selected** or Delete to remove history entries.

Protocol 7 pairing, one active phone and sequential file transfers are supported. Media remote uses generic Windows media keys. Android may require explicit clipboard sending. Profiles are stored in `%LOCALAPPDATA%\ConnectXP`, with settings in `settings.ini`. Old profiles are not imported; pair your phone again after updating.

## Verification

The release is checked with the i686 cross compiler, parser and plugin tests, layout checks, mocked Win32 painting checks, in-memory TLS/pairing/payload tests and PE imports/subsystem checks for XP. User testing has reported working pairing, file reception and XP interface behavior during development. The build environment does not provide a real Windows XP/Android pair; the manual checklist in the repository README remains relevant.

The application icon and the idea are credited to Daniel Myslivets. Third-party notices are included with the portable archive.

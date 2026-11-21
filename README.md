# ConnectXP

A KDE Connect client written in C and Win32, targeting Windows XP SP3 x86.

Version 0.11.0 introduces an Explorer-style task pane, compact themed action buttons,
a resizable and maximizable main window (880×560 initially, minimum 800×520),
and a separate IE6-inspired file transfer window. English and Russian interfaces
are available. Buttons and task groups use the installed Windows theme, with
system-color fallbacks for classic Windows and unavailable theme resources.
Theme and system-color changes are applied while the program is running.

The application icon was supplied by Daniel Myslivets and is retained in the
executable, window title bars, About dialog and primary tray icon. Action and
phone icons come from the installed Windows shell and telephony DLLs, with
embedded fallback icons. No Windows DLLs are distributed. The About dialog uses
`ShellAboutW`. Background painting excludes child controls, and unchanged labels
are not rewritten during polling.

## Connection and startup options

**Options → Connect automatically** is enabled by default. ConnectXP tries saved
protocol 7 devices every 15 seconds, rotating through remembered endpoints.
Discovery refreshes addresses after a network change. Automatic reconnect requires
an existing certificate pin and never silently pairs a new device or accepts a
changed certificate. One phone connection is active at a time. **Device → Disconnect**
pauses reconnect and incoming sessions until a manual pairing/connect action or
re-enabling automatic connection. Forgetting a pairing also pauses reconnect.

A background listener selects TCP 1716–1738 and advertises the PC on IPv4 UDP
1716 every 30 seconds. Incoming connections from already paired phones use TLS
in the client role and verify the same saved certificate pin. New phones must
first be paired using the PC's Pair / connect action. Allow the listener through
the Windows firewall for phone-initiated connections. Protocol 8 pairing remains
unsupported. Broadcast announcements use the system's default IPv4 route.

**Options → Start with Windows** adds a quoted path to this EXE with `--tray` to
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` under `ConnectXP`.
Disabling it removes that value. **Options → Start in tray** controls ordinary
launches. `--tray` always requests a hidden launch; if the tray icon cannot be
created, the main window opens. Double-click the tray icon to restore the window.
Startup and reconnect preferences are per user under `HKCU\Software\Connect95`.
A session-local mutex prevents a second instance from using the same profile.

## Battery tray and multimedia remote

The primary tray icon remains the custom ConnectXP artwork. A second generated
battery icon shows charge and charging state in its tooltip while connected.
A low-battery balloon appears at 15% or below when unplugged; charging or a rise
above 20% resets the warning. These colors follow system colors.

Enable the multimedia remote in KDE Connect on the phone and select
**Windows media keys**. Play/pause, stop, next and previous send standard Windows
media-key press/release events using `SendInput`, only on an authenticated paired
connection. The target player must support those keys. Play and Pause requests
both use the generic play/pause key. This fallback cannot read track metadata,
seek, control per-player volume, or report actual playback state.

## Interface language

Choose **View → Language → English / Русский** to switch immediately, including
the About text and notifications window. The choice is saved per Windows user
as a `Language` DWORD under `HKEY_CURRENT_USER\Software\Connect95`.
English is the default. Unicode shell dialogs display Russian on English Windows XP too.
Phone notification content is displayed as received.

## Pairing

1. Connect the PC and phone to the same LAN.
2. Open ConnectXP and refresh devices in KDE Connect on the phone.
3. Select the phone and click **Pair / connect**.
4. Accept the request on the phone, compare the eight-character code, and confirm the match on the PC.

Discovery listens on IPv4 UDP 1716 and accepts protocol 7 and 8 announcements.
TLS connections and pairing support **protocol 7**, with outgoing connections and
trusted incoming connections. The listener advertises protocol 7. Protocol 8 pairing is pending.

The phone certificate is pinned after confirmation. A changed certificate stops
reconnection. **Device → Disconnect** preserves trust. **Device → Forget pairing
on this PC…** removes the local pin, allowing a new pairing request. Compare the
code again when pairing with a new certificate.

## PC device name

Choose **Edit → PC device name…** to set the name shown for this PC in KDE Connect
on the phone. Names support Unicode and up to 32 UTF-16 code units. The dialog
checks characters filtered by KDE Connect and reports invalid input.

The preference is stored as `DeviceName` under `HKEY_CURRENT_USER\Software\Connect95`.
An unset preference uses **ConnectXP**. An idle paired connection reconnects after
saving a changed name. During a file transfer, reconnect waits until completion.
When disconnected, the configured name is used on the next connection.

The existing device identity and pinned certificates provide authentication on
reconnect. The name is copied into the outgoing identity packet when connecting.

## Saved devices

The native Win32 tree contains **Saved devices** and **Discovered devices** groups.
Phone icons come from the Windows telephony resources, with an embedded XP-compatible
phone icon used when necessary. Saved devices remain visible after restart and show
an offline marker until discovery or a successful connection in the current run.

Group membership follows the saved `peer-<deviceId>.der` certificates. Names and
last-known endpoints are cached separately in `peer-<deviceId>.json`. Connection
authentication continues to verify the pinned certificate during TLS.

Pairing moves a discovered device into the saved group. Forgetting pairing removes
it from that group. A recently discovered device remains in the discovered group.
Saved devices can try connecting using their remembered address. Refresh discovery
on the phone when its address changes.

Pairings from older versions initially use a name such as **Phone 12345678**.
Their names and endpoints are remembered after the next discovery announcement.
Unreadable metadata falls back to the ID-based name and waits for discovery.

## Files and find phone

After connecting, **Send file** opens the native Windows file picker with multiple
selection. Files may also be dropped onto the main window. Up to 64 pending files
are queued sequentially for the currently connected phone. Folders are rejected.
A lost session clears unsent queued files, preventing accidental sending to a
different device. Cancelling a batch clears its pending files and cancels the
current transfer. Filenames and full paths must fit XP's 259-character path limit.

The modeless transfer window shows the current filename, a segmented progress
bar, transferred/total size, average speed, estimated time remaining and queue
length. Unknown-size streams have no total or remaining-time estimate. Closing
the window hides it while transfer continues; Cancel stops the transfer.
On systems providing the shell AVI copy resource, the window plays that system
animation during transfer. No animation asset is distributed.
**File → Transfer history** reopens the window. The newest 64 results (received
and sent, timestamp, filename and outcome) are saved in
`%APPDATA%\Connect95\transfers.json` and restored at startup. **Open folder**
opens the received-files directory without opening a received file.

**Find phone** sends `kdeconnect.findmyphone.request` to the paired phone.
The phone's KDE Connect plugins must be enabled for these features.

To receive a file, share it from Android using KDE Connect and choose ConnectXP.
Files are saved under **My Documents\ConnectXP**. Earlier downloads are available
in **My Documents\Connect95**. Existing files are preserved,
with numbered names used for collisions. Files are received into temporary
`.part` files, checked for completeness, and renamed after saving. Failed or
cancelled transfers remove their temporary file. Files are never opened automatically.

A separate TLS 1.2 connection carries each file. Its certificate must match the
paired phone. Transfers support binary data, Unicode names within XP path limits,
empty files, 64-bit sizes, and unknown-length streams with an authenticated TLS
end-of-stream. The interface displays progress and provides **Cancel**.

Files are processed sequentially. A find-phone request made during a transfer is
queued until that transfer completes. For reliable transfers, start one direction
at a time. Battery reporting and text clipboard actions are available after connecting.

Allow incoming UDP 1716 for discovery, TCP 1716–1738 for trusted incoming
control connections, and TCP 1739–1764 for sending
files from the PC. File downloads connect to the paired phone's announced port.
Restrict the application firewall exception to the local network.

After upgrading, close the old executable, start the new one, and reconnect so
Android receives the updated plugin capabilities. Existing pairing is stored in
the user profile and can be reused.

## Ping

Select a connected device and choose **Device → Send ping**. ConnectXP sends the
standard `kdeconnect.ping` packet over the paired TLS connection. Enable the Ping
plugin in KDE Connect on the phone.

Incoming pings produce native XP tray balloons and entries in **Notifications**.
The title identifies the sending device. Optional message text is displayed,
and a packet with no message uses **Ping!**. Repeated identical incoming pings
create separate alerts. Ping uses the application's serialized network loop and
is queued during an active file transfer.

## Battery and clipboard

The phone battery percentage and charging status appear below the connection
status. Battery updates are accepted only from the paired TLS connection.

The **Clipboard** button opens a menu with these actions.

- **Send PC clipboard to phone** sends the current plain-text clipboard.
- **Paste phone clipboard to PC** copies the latest text received from the phone onto the Windows clipboard. Paste it into any application using Ctrl+V.
- **Sync text clipboard automatically** enables two-way synchronization for this connection.

The same paste action is available from **Edit → Paste phone clipboard to PC**,
or **Ctrl+Shift+V** while the main ConnectXP window is focused. First use KDE Connect's explicit
**Send clipboard** action on the phone if Android restricts background reading.
The paste action becomes available after text arrives from the paired phone.

Automatic synchronization defaults to off and resets on reconnect. Manual
clipboard actions remain available while synchronization is off. Incoming
connection-time clipboard packets honor their timestamps during automatic sync.
The client ignores its own clipboard writes to prevent echo loops. The clipboard
is polled using `GetClipboardSequenceNumber`, which is available on Windows XP.

Text is limited to 8 KiB of UTF-8, including empty strings and multiline Unicode.
Clipboard images and file lists are outside these text actions. Android may
require its explicit **Send clipboard** action for phone-to-PC updates because
background clipboard access is restricted by Android. Clipboard updates made
while a file transfer is running are queued, with the latest queued text retained.

## Phone notifications

Enable **Notification sync** in KDE Connect on Android and grant it notification
access in Android settings. Restart ConnectXP and reconnect so the phone sees
the notification capability.

New non-silent notifications appear as native Windows XP balloons above the tray
icon through `Shell_NotifyIconW` and `NIF_INFO`. Clicking a balloon opens the matching
entry in **Notifications**. The same window is available from **View → Notifications…**
and the tray menu. It shows application names, titles, and the full received text.
Long text is readable in the multiline detail pane.

Updates replace the existing entry with the same ID. Phone cancellations remove
entries and queued alerts. Silent packets, including the initial list from the
phone, update the list. Alert IDs are queued and repeated updates are coalesced.
Windows controls the actual balloon display and duration.

Up to 128 received active entries are held in memory for the current connection.
The list remains readable after disconnection and resets when connecting again.
Notification data uses the existing TLS connection and the 64 KiB packet limit.
The serialized network loop processes notification packets after an active file
transfer finishes.

The main status bar shows the local arrival time of the latest phone clipboard
packet, helping distinguish fresh text from an older cached clipboard. Use the
phone's explicit **Send clipboard** action when Android blocks background access.

The main window supports maximizing and restoring. The separate Notifications
window and the file-transfer window retain their compact size.

## Windows XP support

TLS 1.2 is provided by statically linked Mbed TLS 3.6.8. Randomness comes from
CryptoAPI (`CryptGenRandom`), and the local private key is protected using
user-scoped DPAPI (`CryptProtectData`). System TLS updates are unnecessary for
this application. The system date must be correct for certificate validation.

The identity is generated at startup and stored in
`%APPDATA%\Connect95\identity.dat`. Phone certificates use `peer-<deviceId>.der`
in the same directory. The established Connect95 profile and registry namespace
provide upgrade compatibility for ConnectXP. DPAPI binds the identity to its Windows user profile.
Unreadable identities are preserved for recovery. Profile access is locked against
concurrent clients, and identity and pin writes use atomic replacement.

Discovery and protocol 7 pairing have been reported working by the user.
Version 0.11.0 incoming/reconnect behavior, startup, themes, maximized layout,
transfer queue/history, battery tray and multimedia keys still need validation
with a real Windows XP/Android pair. Previous file transfer and pairing were
reported working by the user. Automated TLS tests use an in-memory
transport on Linux. Import checks catch known XP-incompatible dependencies.

## Build

Requires GNU Make 4.3+, Git, Python 3, Perl, and MinGW-w64 targeting i686 with MSVCRT.

```sh
sudo apt-get install make git python3 perl gcc-mingw-w64-i686 binutils-mingw-w64-i686
make
```

The first build fetches Mbed TLS 3.6.8 and its framework into `.deps/mbedtls`.
The source commit is pinned to `82e69999f91126589a28b70dd75b69fbcabe2711`.
Access to `github.com` is required. The output is `build/connectxp.exe`.
The executable uses system Win32 DLLs and statically linked Mbed TLS.

From a Windows shell with a compatible 32-bit MinGW toolchain and Make:

```sh
make CC=gcc WINDRES=windres AR=ar
```

The toolchain must target MSVCRT. The Makefile selects Win32 API and PE subsystem
version 5.1. Runtime compatibility requires testing on XP SP3.

## Validation

```sh
make test
make test-pairing
python3 tests/check_xp_imports.py i686-w64-mingw32-objdump build/connectxp.exe
```

The pairing tests require a native C compiler, OpenSSL CLI, and Python 3. They
build native Mbed TLS in `.deps/mbedtls-host` and generate temporary certificates.
Notification model tests run as part of `make test`. Tests cover TLS 1.2, binary payloads in both directions, verification codes checked
against an independent hash, certificate identity errors, PEM reloading, malformed
packets, file metadata, empty files, large sizes, unsafe Windows filenames, battery fields, clipboard
limits, escaped text, and connection-time clipboard timestamps, empty/custom/invalid ping messages, notification ID normalization, duplicate
updates, repeat messages with new event times, silent history, cancellation,
alert queue order, and bounded storage.

Manual checks cover pairing, reconnect, file transfer in both directions, empty
and large files, non-ASCII names, repeated names, cancellation, disconnect during
transfer, finding the phone, battery/charging updates, clipboard actions in both directions,
automatic sync on/off, reconnect, busy clipboards, menu actions, tray restore,
the About dialog, saved-device persistence across restart, offline selection,
group migration after pairing/forgetting, Ctrl+Shift+V clipboard import, notification updates/cancellations, silent initial
sync, balloon clicks, maximizing and restoring the main window, custom names including Cyrillic text, restart
persistence, renaming during a file transfer, ping sending from the Device menu, and repeated
incoming pings including Unicode messages.

The protocol was checked against KDE Connect sources at commit
`6fa6dac15f1019bff6a922cf3014e222f5140f66` and the Android implementation.
[Upstream protocol sources](https://github.com/KDE/kdeconnect-kde/tree/6fa6dac15f1019bff6a922cf3014e222f5140f66/core)

Mbed TLS is used under Apache-2.0 and cJSON 1.7.19 under MIT.
License texts are included in `releases/THIRD-PARTY-NOTICES.txt`.

The 0.11.0 manual pass should also cover phone-initiated TLS reconnect with the
original and a changed certificate, Wi-Fi loss/recovery, explicit disconnect,
quoted startup paths containing spaces, Explorer restarts with both tray icons,
multiple-selection and dropped-file batches, cancelling with queued files,
history after restart, classic/Luna theme switching while open, and media keys
with a compatible player. These are a checklist, not claimed completed tests.

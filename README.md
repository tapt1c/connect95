# Connect95

A KDE Connect client written in C and Win32, targeting Windows XP SP3 x86.

Version 0.6.0 has an English interface, native Windows shell icons, a compact
660×360 window, tray support, and an About dialog using `ShellAboutA`.
The device pane groups saved and discovered devices with phone icons. Luna appearance follows the Windows theme.

## Pairing

1. Connect the PC and phone to the same LAN.
2. Open Connect95 and refresh devices in KDE Connect on the phone.
3. Select the phone and click **Pair / connect**.
4. Accept the request on the phone, compare the eight-character code, and confirm the match on the PC.

Discovery listens on IPv4 UDP 1716 and accepts protocol 7 and 8 announcements.
TLS connections and pairing currently support **protocol 7**. Connect95 initiates
one control connection from the PC. Incoming control connections, active discovery
announcements, protocol 8 pairing, and automatic reconnect are pending.

The phone certificate is pinned after confirmation. A changed certificate stops
reconnection. **Device → Disconnect** preserves trust. **Device → Forget pairing
on this PC…** removes the local pin, allowing a new pairing request. Compare the
code again when pairing with a new certificate.

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

After connecting, **Send file** opens the native Windows file picker.
**Find phone** sends `kdeconnect.findmyphone.request` to the paired phone.
The phone's KDE Connect plugins must be enabled for these features.

To receive a file, share it from Android using KDE Connect and choose Connect95.
Files are saved under **My Documents\Connect95**. Existing files are preserved,
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

Allow incoming UDP 1716 for discovery and incoming TCP 1739–1764 for sending
files from the PC. File downloads connect to the paired phone's announced port.
Restrict the application firewall exception to the local network.

After upgrading, close the old executable, start the new one, and reconnect so
Android receives the updated plugin capabilities. Existing pairing is stored in
the user profile and can be reused.

## Battery and clipboard

The phone battery percentage and charging status appear below the connection
status. Battery updates are accepted only from the paired TLS connection.

The **Clipboard** button opens a menu with these actions.

- **Send PC clipboard to phone** sends the current plain-text clipboard.
- **Paste phone clipboard to PC** copies the latest text received from the phone onto the Windows clipboard. Paste it into any application using Ctrl+V.
- **Sync text clipboard automatically** enables two-way synchronization for this connection.

The same paste action is available from **Edit → Paste phone clipboard to PC**,
or **Ctrl+Shift+V** while Connect95 is focused. First use KDE Connect's explicit
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

## Windows XP support

TLS 1.2 is provided by statically linked Mbed TLS 3.6.8. Randomness comes from
CryptoAPI (`CryptGenRandom`), and the local private key is protected using
user-scoped DPAPI (`CryptProtectData`). System TLS updates are unnecessary for
this application. The system date must be correct for certificate validation.

The identity is generated on first connection and stored in
`%APPDATA%\Connect95\identity.dat`. Phone certificates use `peer-<deviceId>.der`
in the same directory. DPAPI binds the identity to its Windows user profile.
Unreadable identities are preserved for recovery. Profile access is locked against
concurrent clients, and identity and pin writes use atomic replacement.

Discovery and protocol 7 pairing have been reported working by the user.
Version 0.6.0 file transfer, ringing, dialogs, and filesystem handling still need
validation with a real Windows/Android pair. Automated TLS tests use an in-memory
transport on Linux. Import checks catch known XP-incompatible dependencies.

## Build

Requires GNU Make 4.3+, Git, Python 3, Perl, and MinGW-w64 targeting i686 with MSVCRT.

```sh
sudo apt-get install make git python3 perl gcc-mingw-w64-i686 binutils-mingw-w64-i686
make
```

The first build fetches Mbed TLS 3.6.8 and its framework into `.deps/mbedtls`.
The source commit is pinned to `82e69999f91126589a28b70dd75b69fbcabe2711`.
Access to `github.com` is required. The output is `build/connect95.exe`.
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
python3 tests/check_xp_imports.py i686-w64-mingw32-objdump build/connect95.exe
```

The pairing tests require a native C compiler, OpenSSL CLI, and Python 3. They
build native Mbed TLS in `.deps/mbedtls-host` and generate temporary certificates.
Tests cover TLS 1.2, binary payloads in both directions, verification codes checked
against an independent hash, certificate identity errors, PEM reloading, malformed
packets, file metadata, empty files, large sizes, unsafe Windows filenames, battery fields, clipboard
limits, escaped text, and connection-time clipboard timestamps.

Manual checks cover pairing, reconnect, file transfer in both directions, empty
and large files, non-ASCII names, repeated names, cancellation, disconnect during
transfer, finding the phone, battery/charging updates, clipboard actions in both directions,
automatic sync on/off, reconnect, busy clipboards, menu actions, tray restore,
the About dialog, saved-device persistence across restart, offline selection,
group migration after pairing/forgetting, and Ctrl+Shift+V clipboard import.

The protocol was checked against KDE Connect sources at commit
`6fa6dac15f1019bff6a922cf3014e222f5140f66` and the Android implementation.
[Upstream protocol sources](https://github.com/KDE/kdeconnect-kde/tree/6fa6dac15f1019bff6a922cf3014e222f5140f66/core)

Mbed TLS is used under Apache-2.0 and cJSON 1.7.19 under MIT.
License texts are included in `releases/THIRD-PARTY-NOTICES.txt`.

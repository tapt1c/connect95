# Connect95

A KDE Connect client written in C and Win32, targeting Windows XP SP3 x86.

Version 0.4.0 has an English interface, native Windows shell icons, a compact
660×360 window, tray support, and an About dialog using `ShellAboutA`.
The Pair / connect button is wider. Luna appearance follows the Windows theme.

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
at a time. Clipboard synchronization and battery reporting are pending.

Allow incoming UDP 1716 for discovery and incoming TCP 1739–1764 for sending
files from the PC. File downloads connect to the paired phone's announced port.
Restrict the application firewall exception to the local network.

After upgrading, close the old executable, start the new one, and reconnect so
Android receives the updated plugin capabilities. Existing pairing is stored in
the user profile and can be reused.

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
Version 0.4.0 file transfer, ringing, dialogs, and filesystem handling still need
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
packets, file metadata, empty files, large sizes, and unsafe Windows filenames.

Manual checks cover pairing, reconnect, file transfer in both directions, empty
and large files, non-ASCII names, repeated names, cancellation, disconnect during
transfer, finding the phone, menu actions, tray restore, and the About dialog.

The protocol was checked against KDE Connect sources at commit
`6fa6dac15f1019bff6a922cf3014e222f5140f66` and the Android implementation.
[Upstream protocol sources](https://github.com/KDE/kdeconnect-kde/tree/6fa6dac15f1019bff6a922cf3014e222f5140f66/core)

Mbed TLS is used under Apache-2.0 and cJSON 1.7.19 under MIT.
License texts are included in `releases/THIRD-PARTY-NOTICES.txt`.

# Password Manager

This project is a complete remake of my original password manager written in Java. I developed it in part to learn programming in C, and also to use my own design for secure password storage.

## Features / Technical Details

- Supports multiple accounts for each system user; accounts are stored in `~/.local/share/passwdmngr/` (Linux), `%APPDATA%\Local\passwdmngr\` (Windows), or `~/Library/Application Support/passwdmngr/` (Mac).

- Vaults encrypted with libsodium's crypto_aead_aes256gcm functions.

- Vault header includes hash over the remaining portion of the header and ciphertext to detect corruption and basic tampering.

- Retains master password only as long as is necessary to derive decryption and authentication keys, then securely wipes the password to prevent discovery through core dumps.

- Mobile version uses a native layer for most core functions; the mobile-specific part only handles basic logic & user interfaces, while the native layer handles secure access and storage of data.

- Zero-trust cloud syncing (in progress):

    - Cloud syncing will always be an optional feature, but if it is enabled, the server only ever sees the user's encrypted vault and metadata that is already accessible without decryption.
    - It is not possible for a malicious server to read user data, as the client encrypts all sensitive data before sending it, and does not provide decryption keys. A malicious client with proper authentication may be able to obtain the encrypted vault and basic account information from the server, but it will still be unable to decrypt the vault without the master password or a recovery key. It also may be able to delete the remote vault, but this will not affect other installations using that account; they will notify the user that entries were deleted and provide instructions for data/account recovery.
    - Server protocol is mainly JSON-based, running over an encrypted TCP/TLS connection provided by OpenSSL.
    - Server supports email verification through one-time passwords, if configured by an admin.
    - Decentralized server design: server code is available at [this repo](https://github.com/Samuelf09-Java/passwdmngr-server); users can self-host their server to retain control of their data, or use publicly available servers (once I get it set up).

- Command line interface (`passwdmngr --cli`), allowing users to perform basic operations such as creating/deleting/viewing accounts & entries, as well as inspecting app files in PWMNGR or PWMACC format. _Note: Linenoise is currently broken on windows and I have not tested it on mac_

## Installation

### Installing from a pre-built package

 - Linux: .deb and .rpm packages are available for Debian and Fedora-based systems. Download the installer from the latest release and run `dpkg -i passwdmngr-[version]_[arch].deb` (for Debian) or `rpm -ivh passwdmngr-[version]_[arch].rpm` (Fedora).

 - Windows: Download and run the `passwdmngr-[version]_[arch].msi` installer from the latest release. The installer contains several dlls packaged alongside the main app that are required for the runtime.

 - Macintosh: Download `passwdmngr-[version]_[arch].dmg` from the latest release. Open it and drag the application icon into your Applications folder. You will likely need to allow it to run under System Settings -> Privacy & Security -> (find the message about the blocked app, towards the bottom) -> Open Anyway. _Note: the dependencies are currently not being packaged correctly in the .dmg; I will work on fixing it soon. In the meantime, you will have to build it from source through brew, which I also have not fully tested._

### Installing from source

Before you install from source, make sure you have [these](#dependencies) runtime dependencies, as well as these build tools:
 - git (only required for cloning repo)
 - gcc/clang
 - g++ (for building linenoise)
 - make
 - cmake (also for linenoise)
 - pkg-config
 - jq (for compaction of embedded json)
 - glib (if not included with gtk)

This app is mainly built for Linux; if you want to build it for windows, you will likely need the 'mingw-w64-x86_64' versions of these build tools and dependencies. See the repo's [github actions workflow](.github/workflows/installers-build.yml) for an example build workflow on each platform. To build it on Mac, use the tools from Homebrew.  

Linux build:

```
#! /bin/bash

git clone https://github.com/Samuelf09-Java/passwdmngr.git
cd passwdmngr
make
# run this to install system-wide (executable in /usr/bin/, desktop entry in /usr/share/applications/, icon in /usr/share/icons/hicolor/128x128/apps/)
sudo make install
```

### Dependencies:

 - Gtk 4.0 (graphics)
 - Sodium (cryptography)
 - OpenSSL (mainly TLS server connection)
 - cJSON (json support; vendored)
 - Linenoise-ng (CLI processing; vendored)
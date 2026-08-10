#!/usr/bin/env bash
set -e

APP_NAME="PasswordManager.app"
APP_DIR="dist/$APP_NAME"
BIN="$APP_DIR/Contents/MacOS/passwdmngr"
RES="$APP_DIR/Contents/Resources"
FW="$APP_DIR/Contents/Frameworks"
SHARE="$RES/share"

VERBOSE=${VERBOSE:-0}

log() {
    [[ "$VERBOSE" == 1 ]] && echo "$@"
}

mkdir -p "$FW" "$SHARE"

echo "Bundling macOS app: $APP_NAME"

bundle_lib() {
    local lib="$1"

    # Skip system libs
    if [[ "$lib" == /usr/lib/* ]] || [[ "$lib" == /System/* ]]; then
        return
    fi

    # Resolve @rpath libraries
    if [[ "$lib" == @rpath/* ]]; then
        local base=$(basename "$lib")
        local real=$(find /opt/homebrew -name "$base" 2>/dev/null | head -n 1)

        if [[ -z "$real" ]]; then
            log "WARNING: Could not resolve $lib"
            return
        fi

        log "Resolved $lib → $real"
        lib="$real"
    fi

    local base=$(basename "$lib")
    local dest="$FW/$base"

    if [[ ! -f "$dest" ]]; then
        log "Copying $lib → $dest"
        cp "$lib" "$dest"
        chmod 755 "$dest"

        install_name_tool -id "@rpath/$base" "$dest"

        # Rewrite dependencies inside the library
        for dep in $(otool -L "$dest" | awk '{print $1}' | tail -n +2); do
            if [[ "$dep" == /usr/lib/* ]] || [[ "$dep" == /System/* ]]; then
                continue
            fi

            local depbase=$(basename "$dep")
            log "Fixing dependency $dep → @rpath/$depbase"
            install_name_tool -change "$dep" "@rpath/$depbase" "$dest"

            bundle_lib "$dep"
        done
    fi
}

echo "Scanning executable dependencies..."
for lib in $(otool -L "$BIN" | awk '{print $1}' | tail -n +2); do
    bundle_lib "$lib"
done

echo "Rewriting executable library paths..."
for dep in $(otool -L "$BIN" | awk '{print $1}' | tail -n +2); do
    if [[ "$dep" == /usr/lib/* ]] || [[ "$dep" == /System/* ]]; then
        continue
    fi

    depbase=$(basename "$dep")
    log "Fixing executable dep $dep → @executable_path/../Frameworks/$depbase"
    install_name_tool -change "$dep" "@executable_path/../Frameworks/$depbase" "$BIN"
done

echo "Copying GTK runtime data..."

GTK_PREFIX=$(brew --prefix gtk4)
GLIB_PREFIX=$(brew --prefix glib)
GDKPIXBUF_PREFIX=$(brew --prefix gdk-pixbuf)

[[ -d "$GTK_PREFIX/share" ]] && cp -R "$GTK_PREFIX/share" "$SHARE/gtk4"
[[ -d "$GLIB_PREFIX/share/glib-2.0/schemas" ]] && {
    mkdir -p "$SHARE/glib-2.0/schemas"
    cp "$GLIB_PREFIX/share/glib-2.0/schemas/"* "$SHARE/glib-2.0/schemas/" || true
    glib-compile-schemas "$SHARE/glib-2.0/schemas" || true
}
[[ -d "$GDKPIXBUF_PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders" ]] && {
    mkdir -p "$RES/lib/gdk-pixbuf-2.0/2.10.0/loaders"
    cp "$GDKPIXBUF_PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders/"* "$RES/lib/gdk-pixbuf-2.0/2.10.0/loaders" || true
}
[[ -d "$GTK_PREFIX/lib/pango" ]] && {
    mkdir -p "$FW/pango"
    cp "$GTK_PREFIX/lib/pango/"* "$FW/pango" || true
}
[[ -d "$GTK_PREFIX/share/icons" ]] && {
    mkdir -p "$SHARE/icons"
    cp -R "$GTK_PREFIX/share/icons" "$SHARE/icons" || true
}

echo "Bundling complete."
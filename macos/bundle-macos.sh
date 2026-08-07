#!/usr/bin/env bash
set -e

APP_NAME="PasswordManager.app"
APP_DIR="dist/$APP_NAME"
BIN="$APP_DIR/Contents/MacOS/passwdmngr"
RES="$APP_DIR/Contents/Resources"
LIB="$RES/lib"
SHARE="$RES/share"

mkdir -p "$LIB" "$SHARE"

echo "Bundling macOS app: $APP_NAME"
echo "Executable: $BIN"

bundle_lib() {
    local lib="$1"

    # Skip system libs
    if [[ "$lib" == /usr/lib/* ]] || [[ "$lib" == /System/* ]]; then
        return
    fi

    # Resolve @rpath libraries
    if [[ "$lib" == @rpath/* ]]; then
        local base=$(basename "$lib")

        # Search Homebrew for the real file
        local real=$(find /opt/homebrew -name "$base" 2>/dev/null | head -n 1)

        if [[ -z "$real" ]]; then
            echo "WARNING: Could not resolve $lib"
            return
        fi

        echo "Resolved $lib → $real"
        lib="$real"
    fi

    local base=$(basename "$lib")
    local dest="$LIB/$base"

    if [[ ! -f "$dest" ]]; then
        echo "Copying $lib → $dest"
        cp "$lib" "$dest"
        chmod 755 "$dest"

        # Fix install name
        install_name_tool -id "@loader_path/$base" "$dest"

        # Fix references inside the library
        for dep in $(otool -L "$dest" | awk '{print $1}' | tail -n +2); do
            if [[ "$dep" == /usr/lib/* ]] || [[ "$dep" == /System/* ]]; then
                continue
            fi

            local depbase=$(basename "$dep")
            echo "Fixing dependency $dep → @loader_path/$depbase"
            install_name_tool -change "$dep" "@loader_path/$depbase" "$dest"

            # Recursively bundle dependency
            bundle_lib "$dep"
        done
    fi
}

echo "Scanning executable for dependencies..."
for lib in $(otool -L "$BIN" | awk '{print $1}' | tail -n +2); do
    bundle_lib "$lib"
done

echo "Copying GTK runtime data..."

GTK_PREFIX=$(brew --prefix gtk4)
GLIB_PREFIX=$(brew --prefix glib)
GDKPIXBUF_PREFIX=$(brew --prefix gdk-pixbuf)

# GTK4 share data (themes, CSS, etc.)
if [[ -d "$GTK_PREFIX/share" ]]; then
    cp -R "$GTK_PREFIX/share" "$SHARE/gtk4"
fi

# GLib schemas (if any)
if [[ -d "$GLIB_PREFIX/share/glib-2.0/schemas" ]]; then
    mkdir -p "$SHARE/glib-2.0/schemas"
    cp "$GLIB_PREFIX/share/glib-2.0/schemas/"* "$SHARE/glib-2.0/schemas/" || true
    glib-compile-schemas "$SHARE/glib-2.0/schemas" || true
fi

# GDK-Pixbuf loaders (from gdk-pixbuf, not gtk4)
if [[ -d "$GDKPIXBUF_PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders" ]]; then
    mkdir -p "$LIB/gdk-pixbuf-2.0/2.10.0/loaders"
    cp "$GDKPIXBUF_PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders/"* "$LIB/gdk-pixbuf-2.0/2.10.0/loaders" || true
fi

# Pango modules (if present under gtk4)
if [[ -d "$GTK_PREFIX/lib/pango" ]]; then
    mkdir -p "$LIB/pango"
    cp "$GTK_PREFIX/lib/pango/"* "$LIB/pango" || true
fi

# Icon themes
if [[ -d "$GTK_PREFIX/share/icons" ]]; then
    mkdir -p "$SHARE/icons"
    cp -R "$GTK_PREFIX/share/icons" "$SHARE/icons" || true
fi


echo "Bundling complete."
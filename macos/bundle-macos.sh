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

GTK_PREFIX=$(brew --prefix gtk4)
GLIB_PREFIX=$(brew --prefix glib)
JSON_PREFIX=$(brew --prefix json-glib)
SODIUM_PREFIX=$(brew --prefix libsodium)
OPENSSL_PREFIX=$(brew --prefix openssl)

echo "Copying GTK runtime data..."
cp -R "$GTK_PREFIX/share" "$SHARE/gtk4"
cp -R "$GLIB_PREFIX/share/glib-2.0" "$SHARE/glib-2.0"
cp -R "$JSON_PREFIX/share/json-glib-1.0" "$SHARE/json-glib-1.0"

echo "Copying GSettings schemas..."
mkdir -p "$SHARE/glib-2.0/schemas"
cp "$GLIB_PREFIX/share/glib-2.0/schemas/"* "$SHARE/glib-2.0/schemas/"
glib-compile-schemas "$SHARE/glib-2.0/schemas"

echo "Copying GDK-Pixbuf loaders..."
mkdir -p "$LIB/gdk-pixbuf-2.0/2.10.0/loaders"
cp "$GTK_PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders/"* "$LIB/gdk-pixbuf-2.0/2.10.0/loaders"

echo "Copying Pango modules..."
mkdir -p "$LIB/pango"
cp "$GTK_PREFIX/lib/pango/"* "$LIB/pango"

echo "Copying icon themes..."
mkdir -p "$SHARE/icons"
cp -R "$GTK_PREFIX/share/icons" "$SHARE/icons"

echo "Bundling complete."
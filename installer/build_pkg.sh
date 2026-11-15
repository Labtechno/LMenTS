#!/bin/bash
# Fabrique l'image disque LMenTS.dmg : « Installer LMenTS.pkg » et
# « Désinstaller LMenTS.pkg ».
#
# Usage : bash installer/build_pkg.sh <dossier des artefacts JUCE> [dossier de sortie]
#   par exemple : bash installer/build_pkg.sh build/LMenTS_artefacts/Release dist
#
# L'installateur retire d'abord l'ancienne version, puis pose :
#   - le plugin VST3   dans /Library/Audio/Plug-Ins/VST3   (le dossier que Reason lit)
#   - l'application    dans /Applications                  (LMenTS sans Reason)
# Aucun firmware n'est fourni : on charge le sien depuis le plugin (.wav ou .bin).
#
# Repris du projet Tresses. Licence MIT.
set -euo pipefail
cd "$(dirname "$0")/.."

ARTEFACTS="${1:?dossier des artefacts JUCE (…/LMenTS_artefacts/Release)}"
OUTDIR="${2:-dist}"
mkdir -p "$OUTDIR"
VERSION="$(sed -n 's/^project(LMenTS VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

stage() {  # nom, chemin d'installation, source
  mkdir -p "$WORK/$1/$2"
  cp -R "$3" "$WORK/$1/$2/"
}
stage vst3 "Library/Audio/Plug-Ins/VST3" "$ARTEFACTS/VST3/LMenTS.vst3"
stage app  "Applications"                "$ARTEFACTS/Standalone/LMenTS.app"

# Signature locale (ad hoc) : sans elle, un Mac Apple Silicon refuse de charger le code.
codesign --force --deep --sign - "$WORK/vst3/Library/Audio/Plug-Ins/VST3/LMenTS.vst3"
codesign --force --deep --sign - "$WORK/app/Applications/LMenTS.app"

mkdir -p "$WORK/pkgs"
scripts() {  # nom -> dossier de scripts du composant (ou rien)
  if [ -d "installer/scripts/$1" ]; then
    mkdir -p "$WORK/scripts-$1"
    cp installer/scripts/"$1"/* installer/scripts/common/* "$WORK/scripts-$1/"
    chmod +x "$WORK/scripts-$1"/pre* "$WORK/scripts-$1"/post* 2>/dev/null || true
    echo --scripts "$WORK/scripts-$1"
  fi
}

component() {  # nom, identifiant
  # Les bundles ne doivent pas être « déplaçables » : sinon, s'il existe déjà un
  # LMenTS ailleurs sur le disque, l'installateur l'écraserait à sa place.
  pkgbuild --analyze --root "$WORK/$1" "$WORK/$1.plist" >/dev/null
  python3 - "$WORK/$1.plist" <<'PY'
import plistlib, sys
path = sys.argv[1]
with open(path, 'rb') as f:
    bundles = plistlib.load(f)
for b in bundles:
    b['BundleIsRelocatable'] = False
with open(path, 'wb') as f:
    plistlib.dump(bundles, f)
PY
  pkgbuild --root "$WORK/$1" --component-plist "$WORK/$1.plist" $(scripts "$1") \
           --identifier "$2" --version "$VERSION" --install-location / "$WORK/pkgs/$1.pkg"
}
component vst3 com.labtechno.lments.vst3
component app  com.labtechno.lments.app

sed "s/@VERSION@/$VERSION/g" installer/distribution.xml > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --resources installer/resources \
             --package-path "$WORK/pkgs" "$OUTDIR/Installer LMenTS.pkg"

# Désinstallateur : un paquet sans fichiers, qui exécute le script de retrait.
mkdir -p "$WORK/uninstall-pkgs"
pkgbuild --nopayload $(scripts uninstall) --identifier com.labtechno.lments.uninstall \
         --version "$VERSION" "$WORK/uninstall-pkgs/uninstall.pkg"
sed "s/@VERSION@/$VERSION/g" installer/distribution-uninstall.xml > "$WORK/distribution-uninstall.xml"
productbuild --distribution "$WORK/distribution-uninstall.xml" --resources installer/resources-uninstall \
             --package-path "$WORK/uninstall-pkgs" "$OUTDIR/Désinstaller LMenTS.pkg"

mkdir -p "$WORK/dmg"
cp "$OUTDIR/Installer LMenTS.pkg" "$OUTDIR/Désinstaller LMenTS.pkg" "$WORK/dmg/"
rm -f "$OUTDIR/LMenTS.dmg"
hdiutil create -volname "LMenTS $VERSION" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO -ov "$OUTDIR/LMenTS.dmg" >/dev/null
echo "Image disque : $OUTDIR/LMenTS.dmg (version $VERSION)"

; LMenTS — installateur Windows (Inno Setup 6).
;
;   iscc /DVersion=0.5.0 /DArtefacts=build\LMenTS_artefacts\Release /DOutDir=dist installer\windows\LMenTS.iss
;
; Pose le plugin VST3 dans C:\Program Files\Common Files\VST3 (le dossier que lit
; Reason) et l'application dans C:\Program Files\LMenTS. Retire d'abord
; l'ancienne version. Le désinstallateur (menu Démarrer, ou Paramètres >
; Applications) retire les deux et garde les projets et les fichiers de firmware.
; Aucun firmware n'est fourni. Licence MIT.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\LMenTS_artefacts\Release"
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif

[Setup]
AppId={{6C1E2B57-3F0A-4D5B-9E2C-4C4D4D454E54}
AppName=LMenTS
AppVersion={#Version}
AppPublisher=Labtechno
AppPublisherURL=https://github.com/Labtechno/LMenTS
DefaultDirName={autopf}\LMenTS
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName=LMenTS
OutputDir={#OutDir}
OutputBaseFilename=LMenTS-Windows-Installer
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=LMenTS
UninstallDisplayIcon={app}\LMenTS.exe
CloseApplications=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
fr.WelcomeLabel2=Cet assistant va installer LMenTS {#Version} :%n%n- le plugin VST3, pour Reason ;%n- l'application LMenTS, pour jouer sans Reason.%n%nS'il y a déjà une version de LMenTS, elle est remplacée. Ferme Reason avant de continuer.%n%nAucun firmware n'est fourni : au premier lancement, charge le fichier de mise à jour officiel du module (elements_1.1.wav) ou un fichier .bin.

[InstallDelete]
; L'ancienne version du plugin est retirée en entier : aucun vieux fichier ne reste.
Type: filesandordirs; Name: "{commoncf64}\VST3\LMenTS.vst3"

[Files]
Source: "{#Artefacts}\VST3\LMenTS.vst3\*"; DestDir: "{commoncf64}\VST3\LMenTS.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\LMenTS.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\LMenTS"; Filename: "{app}\LMenTS.exe"
Name: "{group}\Désinstaller LMenTS"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\LMenTS.vst3"
; Réglages de l'application et liste des firmwares récents (pas les firmwares eux-mêmes).
Type: filesandordirs; Name: "{userappdata}\LMenTS"
Type: files; Name: "{userappdata}\LMenTS.settings"

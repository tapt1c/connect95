; Encoding: UTF-8 with BOM, CRLF. Inno Setup 5.6.1 Unicode.
#ifndef UNICODE
  #error Use Inno Setup 5.6.1 Unicode (u), not ANSI (a).
#endif
; PayloadDir may be overridden with /DPayloadDir="C:\ConnectXP".
#define ConnectXPMainScript
#define AppVersion "1.0.0"
#ifndef PayloadDir
  #if FileExists(SourcePath + "ConnectXP.exe")
    #define PayloadDir SourcePath
  #else
    #define PayloadDir SourcePath + "..\releases"
  #endif
#endif
#ifndef PayloadFile
  #if FileExists(PayloadDir + "\ConnectXP.exe")
    #define PayloadFile PayloadDir + "\ConnectXP.exe"
  #else
    #define PayloadFile PayloadDir + "\ConnectXP-1.0.0-x86.exe"
  #endif
#endif
#ifndef IconFile
  #if FileExists(SourcePath + "ConnectXP.ico")
    #define IconFile SourcePath + "ConnectXP.ico"
  #else
    #define IconFile SourcePath + "..\resources\connectxp.ico"
  #endif
#endif
#ifndef NoticesFile
  #if FileExists(SourcePath + "THIRD-PARTY-NOTICES.txt")
    #define NoticesFile SourcePath + "THIRD-PARTY-NOTICES.txt"
  #else
    #define NoticesFile SourcePath + "..\releases\THIRD-PARTY-NOTICES.txt"
  #endif
#endif

[Setup]
AppId=tapt1c.ConnectXP
AppName=ConnectXP
AppVersion={#AppVersion}
AppPublisher=Daniel Myslivets
AppPublisherURL=https://github.com/tapt1c/connect95
DefaultDirName={win}\ConnectXP
UsePreviousAppDir=no
DisableDirPage=yes
DisableReadyPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=admin
MinVersion=5.1sp3
ArchitecturesAllowed=x86 x64
AppMutex=Local\ConnectXP.Desktop
OutputDir=output
OutputBaseFilename=ConnectXP-{#AppVersion}-Setup-x86
Compression=lzma
SolidCompression=yes
UninstallDisplayIcon={app}\ConnectXP.exe
SetupIconFile={#IconFile}
VersionInfoVersion=1.0.0.0
WizardImageFile=wizardconn.bmp
WizardSmallImageFile=miniconn.bmp
WizardImageStretch=no
ShowLanguageDialog=auto

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "{#PayloadFile}"; DestDir: "{app}"; DestName: "ConnectXP.exe"; Flags: ignoreversion
Source: "{#NoticesFile}"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\license_ru.txt"; DestDir: "{app}"; DestName: "terms_ru.txt"; Flags: ignoreversion
Source: "licenses\license_en.txt"; DestDir: "{app}"; DestName: "terms_en.txt"; Flags: ignoreversion
Source: "licenses\license_ru.txt"; Flags: dontcopy
Source: "licenses\license_en.txt"; Flags: dontcopy

[Icons]
Name: "{commonprograms}\ConnectXP\ConnectXP"; Filename: "{app}\ConnectXP.exe"; WorkingDir: "{app}"
Name: "{commonprograms}\ConnectXP\{cm:UninstallShortcut}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\ConnectXP"; Filename: "{app}\ConnectXP.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\ConnectXP.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[CustomMessages]
en.UninstallShortcut=Uninstall ConnectXP
ru.UninstallShortcut=Удалить ConnectXP
en.LaunchApp=Launch ConnectXP
ru.LaunchApp=Запустить ConnectXP
en.LicenseReadError=Cannot read the terms of use. Setup cannot continue.
ru.LicenseReadError=Не удалось прочитать условия использования. Продолжение установки невозможно.
en.LicenseEmpty=The terms of use are empty. Setup cannot continue.
ru.LicenseEmpty=Текст условий использования пуст. Продолжение установки невозможно.

[Messages]
ru.WelcomeLabel1=Добро пожаловать в ConnectXP
ru.WelcomeLabel2=Ваш телефон рядом — даже в Windows XP!%n%nПередавайте файлы, получайте уведомления и буфер обмена с телефона, находите его звуковым сигналом и управляйте музыкой на ПК.%n%nМастер установит ConnectXP 1.0 в папку Windows\ConnectXP и создаст ярлыки в меню «Пуск» и на рабочем столе. На телефоне понадобится KDE Connect; оба устройства должны быть доступны в одной локальной сети.%n%nНажмите «Далее», чтобы продолжить.
en.WelcomeLabel1=Welcome to ConnectXP
en.WelcomeLabel2=Keep your phone close — even on Windows XP!%n%nTransfer files, receive phone notifications and clipboard text, ring your phone to find it, and control music on your PC.%n%nSetup will install ConnectXP 1.0 in Windows\ConnectXP and create Start menu and desktop shortcuts. You will need KDE Connect on your phone and both devices reachable on the same local network.%n%nClick Next to continue.
ru.FinishedLabel=ConnectXP установлен.%n%nОткройте KDE Connect на телефоне и обновите список устройств. Выберите телефон в ConnectXP, нажмите «Сопрячь / подключить» и сравните код на обоих устройствах.%n%nЕсли устройства не находят друг друга, проверьте подключение к локальной сети и разрешения брандмауэра.
en.FinishedLabel=ConnectXP is installed.%n%nOpen KDE Connect on your phone and refresh the device list. Select your phone in ConnectXP, click Pair / connect and compare the code on both devices.%n%nIf the devices cannot find each other, check the local network connection and firewall permissions.

[Code]
var
  CustomLicensePage: TWizardPage;
  LicensePanel: TPanel;
  LicenseLogo: TBitmapImage;
  LicenseTitle: TNewStaticText;
  LicenseDescription: TNewStaticText;
  LicenseText: TNewMemo;
  AcceptRadio: TNewRadioButton;
  DeclineRadio: TNewRadioButton;

const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

function OwnAutorun(): Boolean;
var
  Command, ExePath: String;
  P: Integer;
begin
  Result := False;
  if not RegQueryStringValue(HKCU, RunKey, 'ConnectXP', Command) then Exit;
  Command := Trim(Command);
  if Copy(Command, 1, 1) = '"' then begin
    Delete(Command, 1, 1);
    P := Pos('"', Command);
    if P = 0 then Exit;
    ExePath := Copy(Command, 1, P - 1);
  end else begin
    P := Pos(' ', Command);
    if P = 0 then ExePath := Command else ExePath := Copy(Command, 1, P - 1);
  end;
  Result := CompareText(ExePath, ExpandConstant('{app}\ConnectXP.exe')) = 0;
end;

procedure LicenseChoiceChanged(Sender: TObject);
begin
  if WizardForm.CurPageID = CustomLicensePage.ID then
    WizardForm.NextButton.Enabled := AcceptRadio.Checked;
end;

procedure InitializeWizard();
var
  X, W, PanelHeight, MemoHeight: Integer;
  IsRussian: Boolean;
  LicenseFileName: String;
begin
  IsRussian := ActiveLanguage() = 'ru';
  CustomLicensePage := CreateCustomPage(wpWelcome, '', '');

  { Белая область на всю высоту заголовка и содержимого страницы. }
  { Кнопки навигации мастера остаются за пределами этой области. }
  LicensePanel := TPanel.Create(WizardForm);
  LicensePanel.Parent := WizardForm;
  LicensePanel.BevelOuter := bvNone;
  LicensePanel.Caption := '';
  LicensePanel.Color := clWhite;
  PanelHeight := WizardForm.Bevel.Top - WizardForm.MainPanel.Top;
  LicensePanel.SetBounds(0, WizardForm.MainPanel.Top,
    WizardForm.ClientWidth, PanelHeight);
  LicensePanel.Visible := False;

  { Используем вашу большую картинку с исходной страницы приветствия. }
  LicenseLogo := TBitmapImage.Create(WizardForm);
  LicenseLogo.Parent := LicensePanel;
  LicenseLogo.SetBounds(0, 0,
    WizardForm.WizardBitmapImage.Width, PanelHeight);
  LicenseLogo.Bitmap.Assign(WizardForm.WizardBitmapImage.Bitmap);
  LicenseLogo.Stretch := False;

  X := LicenseLogo.Width + ScaleX(12);
  W := LicensePanel.ClientWidth - X - ScaleX(16);

  LicenseTitle := TNewStaticText.Create(WizardForm);
  LicenseTitle.Parent := LicensePanel;
  LicenseTitle.SetBounds(X, ScaleY(17), W, ScaleY(29));
  LicenseTitle.AutoSize := False;
  LicenseTitle.Font.Style := [fsBold];
  LicenseTitle.Font.Size := 12;

  LicenseDescription := TNewStaticText.Create(WizardForm);
  LicenseDescription.Parent := LicensePanel;
  LicenseDescription.SetBounds(X, ScaleY(55), W, ScaleY(34));
  LicenseDescription.AutoSize := False;
  LicenseDescription.WordWrap := True;

  LicenseText := TNewMemo.Create(WizardForm);
  LicenseText.Parent := LicensePanel;
  MemoHeight := PanelHeight - ScaleY(153);
  LicenseText.SetBounds(X, ScaleY(95), W, MemoHeight);
  LicenseText.ReadOnly := True;
  LicenseText.WordWrap := True;
  LicenseText.ScrollBars := ssVertical;

  AcceptRadio := TNewRadioButton.Create(WizardForm);
  AcceptRadio.Parent := LicensePanel;
  AcceptRadio.SetBounds(X, PanelHeight - ScaleY(50), W, ScaleY(21));
  AcceptRadio.OnClick := @LicenseChoiceChanged;

  DeclineRadio := TNewRadioButton.Create(WizardForm);
  DeclineRadio.Parent := LicensePanel;
  DeclineRadio.SetBounds(X, PanelHeight - ScaleY(27), W, ScaleY(21));
  DeclineRadio.OnClick := @LicenseChoiceChanged;
  DeclineRadio.Checked := True;

  if IsRussian then
  begin
    LicenseTitle.Caption := 'Условия использования';
    LicenseDescription.Caption :=
      'Ознакомьтесь с условиями перед продолжением установки.';
    AcceptRadio.Caption := 'Я принимаю условия';
    DeclineRadio.Caption := 'Я не принимаю условия';
    LicenseFileName := 'license_ru.txt';
  end
  else
  begin
    LicenseTitle.Caption := 'Terms of use';
    LicenseDescription.Caption :=
      'Please read the terms before continuing with setup.';
    AcceptRadio.Caption := 'I accept the terms';
    DeclineRadio.Caption := 'I do not accept the terms';
    LicenseFileName := 'license_en.txt';
  end;

  try
    ExtractTemporaryFile(LicenseFileName);
    LicenseText.Lines.LoadFromFile(
      ExpandConstant('{tmp}\') + LicenseFileName);
  except
    RaiseException(CustomMessage('LicenseReadError') + #13#10 + GetExceptionMessage);
  end;
  if Trim(LicenseText.Text) = '' then
    RaiseException(CustomMessage('LicenseEmpty'));
end;

procedure CurPageChanged(CurPageID: Integer);
var
  ShowLicense: Boolean;
begin
  ShowLicense := CurPageID = CustomLicensePage.ID;
  LicensePanel.Visible := ShowLicense;
  if ShowLicense then
  begin
    LicensePanel.BringToFront;
    WizardForm.NextButton.Enabled := AcceptRadio.Checked;
  end
  else if CurPageID = wpWelcome then
    WizardForm.NextButton.Enabled := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = CustomLicensePage.ID then
    Result := AcceptRadio.Checked;
end;


#include "uninstall.iss"

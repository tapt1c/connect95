#ifndef ConnectXPMainScript
  #error Compile connectxp.iss, not uninstall.iss.
#endif
[CustomMessages]
; Encoding: UTF-8 with BOM, CRLF. Included by connectxp.iss.
en.RemoveTitle=Remove ConnectXP
ru.RemoveTitle=Удаление ConnectXP
en.RemoveDescription=ConnectXP and its shortcuts will be removed from this computer. Only the account shown below can have its settings and pairings cleared with this option.
ru.RemoveDescription=ConnectXP и его ярлыки будут удалены с этого компьютера. Отметка ниже очищает настройки и сопряжения только указанной учётной записи.
en.RemoveAccount=Account: %1
ru.RemoveAccount=Учётная запись: %1
en.RemoveData=Delete my pairings, transfer history and settings
ru.RemoveData=Удалить мои сопряжения, историю передач и настройки
en.RemoveDataNote=Received files in My Documents are always kept. Other Windows users' settings are not changed. Silent removal keeps personal data.
ru.RemoveDataNote=Полученные файлы в «Моих документах» сохранятся. Настройки других пользователей не изменяются. Тихое удаление сохраняет личные данные.
en.RemoveContinue=Continue
ru.RemoveContinue=Продолжить
en.RemoveCancel=Cancel
ru.RemoveCancel=Отмена
en.CloseApp=Please exit ConnectXP using its tray menu, then run removal again.
ru.CloseApp=Завершите работу ConnectXP через меню значка в трее, затем повторите удаление.
en.CleanupIncomplete=Some ConnectXP settings or profile files could not be removed. Received files have been kept. Check %LOCALAPPDATA%\ConnectXP if you still want to clear personal data.
ru.CleanupIncomplete=Часть настроек или файлов профиля ConnectXP удалить не удалось. Полученные файлы сохранены. Если очистка ещё нужна, проверьте %LOCALAPPDATA%\ConnectXP.

[Code]
var
  DeleteOwnData: Boolean;

function ProfileAttributes(Path: String): LongWord;
#ifdef UNICODE
  external 'GetFileAttributesW@kernel32.dll stdcall';
#else
  external 'GetFileAttributesA@kernel32.dll stdcall';
#endif

function KnownProfileFile(Name: String): Boolean;
var
  ID, Ext: String;
  I, P: Integer;
begin
  Name := Lowercase(Name);
  Result := (Name = 'identity.dat') or (Name = 'identity.dat.tmp') or
    (Name = 'identity.lock') or (Name = 'transfers.json') or (Name = 'transfers.tmp') or
    (Name = 'settings.ini');
  if Result or (Copy(Name, 1, 5) <> 'peer-') then Exit;
  Delete(Name, 1, 5);
  P := Pos('.', Name);
  if P = 0 then Exit;
  ID := Copy(Name, 1, P - 1);
  Ext := Copy(Name, P, Length(Name));
  if (Length(ID) < 32) or (Length(ID) > 38) then Exit;
  if (Ext <> '.der') and (Ext <> '.json') and
     (Ext <> '.der.tmp') and (Ext <> '.json.tmp') then Exit;
  for I := 1 to Length(ID) do
    if Pos(Copy(ID, I, 1), 'abcdefghijklmnopqrstuvwxyz0123456789_-') = 0 then Exit;
  Result := True;
end;

function ClearOwnProfile(): Boolean;
var
  Root: String;
  Found: TFindRec;
  Attributes: LongWord;
begin
  Result := True;
  Root := ExpandConstant('{localappdata}\ConnectXP');
  if DirExists(Root) then begin
    Attributes := ProfileAttributes(Root);
    { Never recurse through a redirected/reparse-point profile directory. }
    if (Attributes = $FFFFFFFF) or ((Attributes and $400) <> 0) then begin
      Result := False;
      Exit;
    end;
    if not FindFirst(Root + '\*', Found) then begin
      Result := False;
      Exit;
    end;
    try
      repeat
        if KnownProfileFile(Found.Name) and ((Found.Attributes and ($10 or $400)) = 0) then
          if not DeleteFile(Root + '\' + Found.Name) then Result := False;
      until not FindNext(Found);
    finally
      FindClose(Found);
    end;
    { Unknown files and subdirectories are kept; remove the directory only if empty. }
    RemoveDir(Root);
  end;

end;

function ChooseUninstallData(): Boolean;
var
  Form: TSetupForm;
  Text, Account, Note: TNewStaticText;
  Choice: TNewCheckBox;
  Proceed, Cancel: TNewButton;
begin
  DeleteOwnData := False;
  Result := True;
  if UninstallSilent() then Exit;
  Form := CreateCustomForm();
  try
    Form.Caption := CustomMessage('RemoveTitle');
    Form.ClientWidth := ScaleX(430);
    Form.ClientHeight := ScaleY(225);
    Text := TNewStaticText.Create(Form);
    Text.Parent := Form;
    Text.AutoSize := False;
    Text.WordWrap := True;
    Text.SetBounds(ScaleX(16), ScaleY(16), ScaleX(398), ScaleY(56));
    Text.Caption := CustomMessage('RemoveDescription');
    Account := TNewStaticText.Create(Form);
    Account.Parent := Form;
    Account.SetBounds(ScaleX(16), ScaleY(76), ScaleX(398), ScaleY(18));
    Account.Caption := FmtMessage(CustomMessage('RemoveAccount'), [GetUserNameString()]);
    Choice := TNewCheckBox.Create(Form);
    Choice.Parent := Form;
    Choice.SetBounds(ScaleX(16), ScaleY(103), ScaleX(398), ScaleY(20));
    Choice.Caption := CustomMessage('RemoveData');
    Choice.Checked := False;
    Note := TNewStaticText.Create(Form);
    Note.Parent := Form;
    Note.AutoSize := False;
    Note.WordWrap := True;
    Note.SetBounds(ScaleX(16), ScaleY(130), ScaleX(398), ScaleY(48));
    Note.Caption := CustomMessage('RemoveDataNote');
    Proceed := TNewButton.Create(Form);
    Proceed.Parent := Form;
    Proceed.SetBounds(ScaleX(226), ScaleY(190), ScaleX(90), ScaleY(25));
    Proceed.Caption := CustomMessage('RemoveContinue');
    Proceed.ModalResult := mrOk;
    Proceed.Default := True;
    Cancel := TNewButton.Create(Form);
    Cancel.Parent := Form;
    Cancel.SetBounds(ScaleX(324), ScaleY(190), ScaleX(90), ScaleY(25));
    Cancel.Caption := CustomMessage('RemoveCancel');
    Cancel.ModalResult := mrCancel;
    Cancel.Cancel := True;
    Result := Form.ShowModal() = mrOk;
    if Result then DeleteOwnData := Choice.Checked;
  finally
    Form.Free();
  end;
end;


function InitializeUninstall(): Boolean;
begin
  DeleteOwnData := False;
  Result := not CheckForMutexes('Local\ConnectXP.Desktop');
  if not Result then begin
    SuppressibleMsgBox(CustomMessage('CloseApp'), mbError, MB_OK, IDOK);
    Exit;
  end;
  Result := ChooseUninstallData();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    if CheckForMutexes('Local\ConnectXP.Desktop') then
      RaiseException(CustomMessage('CloseApp'));
    { Preserve an autostart entry belonging to a different, portable copy. }
    if OwnAutorun() then
      if not RegDeleteValue(HKCU, RunKey, 'ConnectXP') then
        Log('Could not remove ConnectXP startup entry.');
    if DeleteOwnData then
      if not ClearOwnProfile() then
        SuppressibleMsgBox(CustomMessage('CleanupIncomplete'), mbInformation, MB_OK, IDOK);
  end;
end;

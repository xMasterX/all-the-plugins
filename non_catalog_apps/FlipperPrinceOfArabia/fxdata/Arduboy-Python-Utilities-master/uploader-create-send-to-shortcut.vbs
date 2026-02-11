Set WshShell = WScript.CreateObject("WScript.Shell")
Set objFSO = CreateObject("Scripting.FileSystemObject")
filename = objFSO.GetParentFolderName(WScript.ScriptFullName) & "\uploader.py"
shortcutsCreated = 0

if objFSO.FileExists(filename) then
  strShortcutFileName = WshShell.SpecialFolders("SendTo") & "\Arduboy uploader.lnk"
  Set oShellLink = WshShell.CreateShortcut(strShortcutFileName)
  oShellLink.TargetPath = filename
  oShellLink.IconLocation = "shell32.dll,12"
  oShellLink.Save
  shortcutsCreated = 1
end if

filename = objFSO.GetParentFolderName(WScript.ScriptFullName) & "\uploader-1309.py"
if objFSO.FileExists(filename) then
  strShortcutFileName = WshShell.SpecialFolders("SendTo") & "\Arduboy uploader (SSD1309).lnk"
  Set oShellLink = WshShell.CreateShortcut(strShortcutFileName)
  oShellLink.TargetPath = filename
  oShellLink.IconLocation = "shell32.dll,12"
  oShellLink.Save
  shortcutsCreated = shortcutsCreated + 1
end if

filename = objFSO.GetParentFolderName(WScript.ScriptFullName) & "\uploader-micro.py"
if objFSO.FileExists(filename) then
  strShortcutFileName = WshShell.SpecialFolders("SendTo") & "\Arduboy uploader (Micro).lnk"
  Set oShellLink = WshShell.CreateShortcut(strShortcutFileName)
  oShellLink.TargetPath = filename
  oShellLink.IconLocation = "shell32.dll,12"
  oShellLink.Save
  shortcutsCreated = shortcutsCreated + 1
end if

filename = objFSO.GetParentFolderName(WScript.ScriptFullName) & "\uploader-micro-1309.py"
if objFSO.FileExists(filename) then
  strShortcutFileName = WshShell.SpecialFolders("SendTo") & "\Arduboy uploader (Micro SSD1309).lnk"
  Set oShellLink = WshShell.CreateShortcut(strShortcutFileName)
  oShellLink.TargetPath = filename
  oShellLink.IconLocation = "shell32.dll,12"
  oShellLink.Save
  shortcutsCreated = shortcutsCreated + 1
end if

if shortcutsCreated = 1 then
  MsgBox "Shortcut added to 'Send To' menu.", vbInformation,"Information"
end if

if shortcutsCreated = 2 then
  MsgBox "Shortcuts added to 'Send To' menu.", vbInformation,"Information"
end if

wscript.quit






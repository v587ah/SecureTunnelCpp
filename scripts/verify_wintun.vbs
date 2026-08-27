Set shell = CreateObject("Shell.Application")
Set fso = CreateObject("Scripting.FileSystemObject")

' Double-click this file to verify Wintun (requests UAC automatically)
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(scriptDir)
bat = scriptDir & "\verify_wintun.bat"

If Not fso.FileExists(bat) Then
    MsgBox "Missing: " & bat, vbCritical, "SecureTunnel"
    WScript.Quit 1
End If

shell.ShellExecute bat, "", root, "runas", 1

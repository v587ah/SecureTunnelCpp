Set shell = CreateObject("Shell.Application")
Set fso = CreateObject("Scripting.FileSystemObject")

' VBS launcher - double-click this if .bat flashes too fast
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(scriptDir)
worker = scriptDir & "\_client_wintun_worker.bat"

If Not fso.FileExists(worker) Then
    MsgBox "Missing: " & worker, vbCritical, "SecureTunnel"
    WScript.Quit 1
End If

' Run worker in elevated cmd that stays open (/k)
cmd = "cmd.exe"
args = "/k cd /d """ & root & """ && """ & worker & """"
shell.ShellExecute cmd, args, root, "runas", 1

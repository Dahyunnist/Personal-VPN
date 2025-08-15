@echo off
set WINSCP_PATH="C:\Program Files (x86)\WinSCP\WinSCP.com"
set SOURCE_PATH="/mnt/tasks/task1/server/client_config"
set TARGET_PATH="C:\"
set PASSWORD="Sailing2024!"
set HOSTKEY="ssh-ed25519 255 C/dOvYZ4hNrO6yWcA6fKtsjtcYAjz6e+se9peB6jHyU"


%WINSCP_PATH% /log="C:\winscp_log.txt" /command ^
  "open scp://root:%PASSWORD%@172.20.68.63 -hostkey=%HOSTKEY%" ^
  "get -r %SOURCE_PATH% %TARGET_PATH%" ^
  "exit"

pause
# Light Host Modern 1.1.1

- Restores the active plugin chain by default when the app starts.
- Makes device persistence stricter during startup and recovery, preventing automatic fallback devices from being saved as valid preferred devices.
- Keeps safe-mode recovery available through `--safe-mode` and adds `--no-restore-active-plugins` for startup troubleshooting.

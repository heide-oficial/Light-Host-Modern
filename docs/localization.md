# Localization

The WinUI shell discovers flat JSON translation catalogues at runtime. Translators edit string values while keeping application keys stable. The native tray uses embedded catalogues and currently supports English and Brazilian Portuguese.

## Catalogue location

Language files are stored in:

```text
WinUI\LightHostModern.WinUI\Locales\
```

The base catalogue is `en-us.json`; Brazilian Portuguese is provided by `pt-br.json`. The WinUI project includes `Locales\*.json` through a wildcard, so new catalogues are copied into the UI payload automatically. At runtime, `LocalizationCatalog` reads the `Locales` directory beside `LightHostModernWinUI.exe`, including inside a versioned portable payload.

The native host embeds `en-us.json` and `pt-br.json` through the `LightHostModernLocales` CMake target. Its tray and Windows release notices select Portuguese only for `pt-br`; other language selections use English. Adding a UI catalogue alone does not add native tray support. That requires extending the embedded resources and `trayLocale()` in `Source/IconMenu.cpp`, then rebuilding the host.

## Adding a language

1. Copy `en-us.json`.
2. Rename the copy with a lowercase language code such as `es-es.json`, `de-de.json`, or `fr-fr.json`, using hyphens rather than underscores.
3. Translate the JSON values without changing the keys.
4. Set `Language.DisplayName` to the language name written in that language.
5. Preserve placeholders such as `{0}`, punctuation that belongs to formatting, and escaped `\n` line breaks.
6. Build the UI, select the language in Settings, and check the translated screens and dialogs. For a complete native tray translation, also make the host changes described above.
7. Submit the catalogue and any necessary host changes for review.

Example:

```json
{
  "Language.DisplayName": "Español",
  "nav.dashboard": "Panel",
  "common.save": "Guardar"
}
```

## Fallback behavior

The loader first attempts to read English, then overlays string values from the selected catalogue. Missing keys and non-string values leave the English entry intact. If the selected file is absent or cannot be parsed, the active catalogue falls back to English. If a key is absent from both catalogues, the caller's fallback text is used; the runtime does not synthesize a translation. An empty string is a valid string value and replaces the English text.

Language codes are trimmed, lowercased and normalized from underscores to hyphens before lookup. Name files in that normalized form. This is filename-based lookup, not automatic selection from the Windows display language. English is the default when no preference is saved.

The legacy `language.name` metadata key is accepted for compatibility, but new catalogues should use `Language.DisplayName`.

The language menu lists `.json` files beside the UI and sorts English first. A file with invalid metadata may still appear under its filename, so appearing in the menu does not prove that a catalogue parsed successfully.

## Runtime language changes

The selected language is saved as `[Localization] Language` in `ui-settings.ini`: normally under `%LOCALAPPDATA%\LightHostModern`, or inside the selected test profile. Changing it queues a UI refresh of navigation, loaded pages, plugin views, status text and accessibility labels without restarting the host. Pages and dialogs created afterward use the current catalogue. Reopen the tray menu to see the host's supported language selection.

Plugin names, manufacturer names, audio driver/device names and user-defined labels are not translated. Plugin-owned editor windows use the plugin's own language behavior.

## Translation checklist

- Keep every JSON key unchanged.
- Keep the file valid UTF-8 JSON.
- Do not translate product names such as LightHostModern, VST2, VST3, ASIO, Mica, or GitHub unless the target language convention requires it.
- Verify long labels in compact layout and dialogs.
- Verify placeholders with real values.
- Preserve placeholder indexes such as `{0}` and `{1}`; the formatter replaces these literally and does not provide plural rules or named arguments.
- Check hover help, accessible names and menu items as well as visible page text.
- Check the native tray separately; a successful UI translation does not verify its embedded catalogues.
- Compare the new file with `en-us.json` before submitting to ensure no keys were omitted unintentionally.

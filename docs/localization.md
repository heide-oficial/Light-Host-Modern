# Localization

Light Host Modern discovers flat JSON translation catalogues at runtime. This matches the contribution model used by VMWV: translators edit values without changing stable application keys.

## Catalogue location

Language files are stored in:

```text
WinUI\LightHost.WinUI\Locales\
```

The base catalogue is `en-us.json`. Brazilian Portuguese is provided by `pt-br.json`. The WinUI project includes `Locales\*.json` through a wildcard, so new catalogues are copied into packaged and portable builds automatically.

## Adding a language

1. Copy `en-us.json`.
2. Rename the copy with a normalized language code such as `es-es.json`, `de-de.json`, or `fr-fr.json`.
3. Translate the JSON values without changing the keys.
4. Set `Language.DisplayName` to the language name written in that language.
5. Preserve placeholders such as `{0}`, punctuation that belongs to formatting, and escaped `\n` line breaks.
6. Build and test the app, then submit the new file through a pull request or GitHub issue.

Example:

```json
{
  "Language.DisplayName": "Español",
  "nav.dashboard": "Panel",
  "common.save": "Guardar"
}
```

## Fallback behavior

English is always loaded as the base catalogue. The selected language is applied on top of it. If a key is missing, malformed, or unavailable, the English value remains visible. An invalid language code also falls back to English.

The legacy `language.name` metadata key is accepted for compatibility, but new catalogues should use `Language.DisplayName`.

## Runtime language changes

The selected language is saved in WinUI preferences. Changing it refreshes navigation labels, page titles and subtitles, settings cards, dialogs, plugin actions, status text, and support content without restarting the host.

Plugin names, manufacturer names, audio driver names, and device names come from external software and are not translated.

## Translation checklist

- Keep every JSON key unchanged.
- Keep the file valid UTF-8 JSON.
- Do not translate product names such as Light Host Modern, VST2, VST3, ASIO, Mica, or GitHub unless the target language convention requires it.
- Verify long labels in compact layout and dialogs.
- Verify placeholders with real values.
- Compare the new file with `en-us.json` before submitting to ensure no keys were omitted unintentionally.

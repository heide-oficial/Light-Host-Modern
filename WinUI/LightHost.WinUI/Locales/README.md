# Contributing translations

Light Host Modern discovers translations from the JSON files in this folder at runtime, using the same flat key/value catalogue model as VMWV.

To add a language:

1. Copy `en-us.json` to a file named with a normalized language code, such as `es-es.json` or `de-de.json`.
2. Translate the values without changing the keys.
3. Set `Language.DisplayName` to the language name written in that language.
4. Keep placeholders such as `{0}` unchanged and preserve `\n` line breaks where present.
5. Build the app. The project wildcard automatically includes every `Locales\*.json` file in packaged and portable builds.

Missing keys automatically fall back to `en-us.json`. Invalid or unavailable language codes also fall back to English. The legacy `language.name` metadata key remains accepted for compatibility, but new translations should use `Language.DisplayName`.

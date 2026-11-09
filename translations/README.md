# Translations

The application is written in English. Every user-visible string goes through Qt's
translation system (`tr()` / `QCoreApplication::translate`), and translations are built
into the app as `.qm` files under `:/i18n`.

- `twisted-harness-designer_en.ts` holds only the English **plural forms**
  ("1 wire" / "2 wires"). It is always loaded when no catalog exists for the user's language.

## Adding a language

1. In the top-level `CMakeLists.txt`, add the language to `qt_standard_project_setup`:

   ```cmake
   qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE en I18N_TRANSLATED_LANGUAGES de)
   ```

2. Re-configure, then create or refresh the `.ts` files from the sources:

   ```sh
   cmake --preset debug
   cmake --build --preset debug --target update_translations
   ```

   This writes `translations/twisted-harness-designer_de.ts`.

3. Translate it with Qt Linguist (`linguist translations/twisted-harness-designer_de.ts`).
   Keep `%1`, `%2`, `%n` placeholders and fill every plural form.

4. Build. The `.qm` file is generated and embedded automatically; the app picks the catalog
   that matches the system language at startup.

Run `update_translations` again whenever source strings change; existing translations are
kept and changed strings are marked unfinished.

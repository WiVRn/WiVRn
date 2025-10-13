# Translating

The headset application and the server dashboard are translated, contributions are welcome to improve and extend them.

# Required software
You only need `gettext` and a text editor, you can optionally use a graphical editor such as [GNOME Translation Editor](https://wiki.gnome.org/Apps/Gtranslator/) or [Lokalize](https://apps.kde.org/lokalize/).

If using Lokalize, make sure to erase the "Default mailing list" in the settings, or it will write a KDE mailing list in the translation file and you will have to manually remove it.

# Adding a new language

* Run `tools/update_messages.sh LANG` where LANG is the 2 letter [ISO 639](https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes) language code, this will create files in the `locale` directory for the new language. Fill in the fields, `git add` the files, commit and file a pull request.

# Editing existing translations

Run the `tools/update_messages.sh [lang...]` script with the list of languages you wish to edit, this will add the lines for the missing translations, edit them, then `git add` the files for the language you just edited, commit and file a pull request.

Note: when running wivrn-dashboard from the build folder, it will load translation files from system install so local modifications won't be loaded. You can override it by setting `XDG_DATA_DIRS=<build-dir>/dashboard/share/:$XDG_DATA_DIRS for `wivrn-dashboard`

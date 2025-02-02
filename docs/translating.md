# Translating

The headset application and the server dashboard are translated, contributions are welcome to improve and extend them.

# Required software
You only need `gettext` and a text editor, you can optionally use a graphical editor such as [GNOME Translation Editor](https://wiki.gnome.org/Apps/Gtranslator/) or [Lokalize](https://apps.kde.org/lokalize/).

# Adding a new language

* Edit the `tools/update_messages.sh`, adding the 2 letter [ISO 639](https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes) language code to the `LANGS` variable.
* (optional) Edit the `tools/check_po.py`, add to the `countries` mapping a value with the language code on the left and the [ISO 3166-1](https://en.wikipedia.org/wiki/ISO_3166-1) code of a flag on the right. The flag will only be used for continuous integration messages.
* Run the `tools/update_messages.sh` script, this will create files in the `locale` directory for the new language. Fill in the fields, `git add` the files, commit and file a pull request.

# Editing existing translations

Run the `tools/update_messages.sh` script, this will add the lines for the missing translations, edit your language, then `git add` the files for the language you just edited, commit and file a pull request.

# QuikMark Language Packs

English menu strings are the source language and are built into the
application. Simplified Chinese is provided by the gettext catalog at:

```text
zh_CN/LC_MESSAGES/quikmark.po
zh_CN/LC_MESSAGES/quikmark.mo
```

After changing the `.po` file, regenerate the runtime catalog:

```powershell
D:\msys64-33\usr\bin\msgfmt.exe `
  -o assets\locale\zh_CN\LC_MESSAGES\quikmark.mo `
  assets\locale\zh_CN\LC_MESSAGES\quikmark.po
```

At runtime QuikMark looks for the catalog next to the executable, in the
platform installation paths, and in the source-tree `assets/locale` directory.

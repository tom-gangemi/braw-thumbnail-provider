# Blackmagic Raw Thumbnail Provider
Enables Windows to create thumbnails for Blackmagic's raw video format.

### Install
1. Copy BRawThumbnailProvider.dll and BlackmagicRawAPI.dll to your desired install directory.
2. Run `Regsvr32.exe BRawThumbnailProvider.dll` - You should see a popup notifying you that registration of the DLL succeeded.

### Uninstall
1. Run `Regsvr32.exe /u BRawThumbnailProvider.dll`.

### Testing & Debugging
The Windows SDK sample "UsingThumbnailProvider" can be used to request thumbnails for given files.
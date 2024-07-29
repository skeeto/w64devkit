CROSS   = x86_64-w64-mingw32-
CC      = $(CROSS)gcc
CXX     = $(CROSS)g++
WINDRES = $(CROSS)windres
LDFLAGS = -mwindows -s -Wl,--gc-sections
LDLIBS  = -lcomdlg32 -lole32 -loleaut32 -luuid
CFLAGS  = -fno-ident -Oz \
  -DZ7_SFX \
  -DZ7_EXTRACT_ONLY \
  -DZ7_NO_CRYPTO \
  -DZ7_NO_REGISTRY \
  -DZ7_NO_READ_FROM_CODER \

obj = \
  CPP/7zip/Bundles/SFXWin/resource.o \
  CPP/7zip/Bundles/SFXWin/SfxWin.o \
  CPP/7zip/UI/GUI/ExtractDialog.o \
  CPP/7zip/UI/GUI/ExtractGUI.o \
  CPP/Common/CRC.o \
  CPP/Common/CommandLineParser.o \
  CPP/Common/IntToString.o \
  CPP/Common/NewHandler.o \
  CPP/Common/MyString.o \
  CPP/Common/StringConvert.o \
  CPP/Common/MyVector.o \
  CPP/Common/Wildcard.o \
  CPP/Windows/Clipboard.o \
  CPP/Windows/CommonDialog.o \
  CPP/Windows/DLL.o \
  CPP/Windows/ErrorMsg.o \
  CPP/Windows/FileDir.o \
  CPP/Windows/FileFind.o \
  CPP/Windows/FileIO.o \
  CPP/Windows/FileName.o \
  CPP/Windows/MemoryGlobal.o \
  CPP/Windows/PropVariant.o \
  CPP/Windows/PropVariantConv.o \
  CPP/Windows/ResourceString.o \
  CPP/Windows/Shell.o \
  CPP/Windows/Synchronization.o \
  CPP/Windows/System.o \
  CPP/Windows/Window.o \
  CPP/Windows/Control/ComboBox.o \
  CPP/Windows/Control/Dialog.o \
  CPP/Windows/Control/ListView.o \
  CPP/7zip/Common/CreateCoder.o \
  CPP/7zip/Common/CWrappers.o \
  CPP/7zip/Common/FilePathAutoRename.o \
  CPP/7zip/Common/FileStreams.o \
  CPP/7zip/Common/InBuffer.o \
  CPP/7zip/Common/FilterCoder.o \
  CPP/7zip/Common/LimitedStreams.o \
  CPP/7zip/Common/OutBuffer.o \
  CPP/7zip/Common/ProgressUtils.o \
  CPP/7zip/Common/PropId.o \
  CPP/7zip/Common/StreamBinder.o \
  CPP/7zip/Common/StreamObjects.o \
  CPP/7zip/Common/StreamUtils.o \
  CPP/7zip/Common/VirtThread.o \
  CPP/7zip/UI/Common/ArchiveExtractCallback.o \
  CPP/7zip/UI/Common/ArchiveOpenCallback.o \
  CPP/7zip/UI/Common/DefaultName.o \
  CPP/7zip/UI/Common/Extract.o \
  CPP/7zip/UI/Common/ExtractingFilePath.o \
  CPP/7zip/UI/Common/LoadCodecs.o \
  CPP/7zip/UI/Common/OpenArchive.o \
  CPP/7zip/UI/Explorer/MyMessages.o \
  CPP/7zip/UI/FileManager/BrowseDialog.o \
  CPP/7zip/UI/FileManager/ComboDialog.o \
  CPP/7zip/UI/FileManager/ExtractCallback.o \
  CPP/7zip/UI/FileManager/FormatUtils.o \
  CPP/7zip/UI/FileManager/OverwriteDialog.o \
  CPP/7zip/UI/FileManager/PasswordDialog.o \
  CPP/7zip/UI/FileManager/ProgressDialog2.o \
  CPP/7zip/UI/FileManager/PropertyName.o \
  CPP/7zip/UI/FileManager/SysIconUtils.o \
  CPP/7zip/Archive/SplitHandler.o \
  CPP/7zip/Archive/Common/CoderMixer2.o \
  CPP/7zip/Archive/Common/ItemNameUtils.o \
  CPP/7zip/Archive/Common/MultiStream.o \
  CPP/7zip/Archive/Common/OutStreamWithCRC.o \
  CPP/7zip/Archive/7z/7zDecode.o \
  CPP/7zip/Archive/7z/7zExtract.o \
  CPP/7zip/Archive/7z/7zHandler.o \
  CPP/7zip/Archive/7z/7zIn.o \
  CPP/7zip/Archive/7z/7zRegister.o \
  CPP/7zip/Compress/Bcj2Coder.o \
  CPP/7zip/Compress/Bcj2Register.o \
  CPP/7zip/Compress/BcjCoder.o \
  CPP/7zip/Compress/BcjRegister.o \
  CPP/7zip/Compress/BranchMisc.o \
  CPP/7zip/Compress/BranchRegister.o \
  CPP/7zip/Compress/CopyCoder.o \
  CPP/7zip/Compress/CopyRegister.o \
  CPP/7zip/Compress/DeltaFilter.o \
  CPP/7zip/Compress/Lzma2Decoder.o \
  CPP/7zip/Compress/Lzma2Register.o \
  CPP/7zip/Compress/LzmaDecoder.o \
  CPP/7zip/Compress/LzmaRegister.o \
  CPP/7zip/Compress/PpmdDecoder.o \
  CPP/7zip/Compress/PpmdRegister.o \
  C/7zCrc.o \
  C/7zCrcOpt.o \
  C/7zStream.o \
  C/Alloc.o \
  C/Bcj2.o \
  C/Bra.o \
  C/Bra86.o \
  C/BraIA64.o \
  C/CpuArch.o \
  C/Delta.o \
  C/DllSecur.o \
  C/Lzma2Dec.o \
  C/Lzma2DecMt.o \
  C/LzmaDec.o \
  C/MtDec.o \
  C/Ppmd7.o \
  C/Ppmd7Dec.o \
  C/Sha256.o \
  C/Sha256Opt.o \
  C/Threads.o \

7z.sfx: $(obj)
	$(CXX) $(LDFLAGS) -o $@ $(obj) $(LDLIBS)
clean:
	rm -f 7z.sfx $(obj)
%.o: %.cpp
	$(CXX) -c $(CFLAGS) -o $@ $^
%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $^
%.o: %.rc
	$(WINDRES) -o $@ $^

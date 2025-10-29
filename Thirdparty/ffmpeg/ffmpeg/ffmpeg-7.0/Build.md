# FFmpeg Custom Build Guide (AMF Support)
This guide explains how to build **FFmpeg** from source with full control over its dependencies and configuration. 

The process covers:
- Setting up the build environment on Windows (MSYS2 or Visual Studio) and Linux.
- Building static libraries for zlib, OpenSSL, and libaom.
- Integrating custom static dependencies into the FFmpeg build.
- Troubleshooting common compiler and linker issues.

---

## Environment Setup

### Windows (MSYS2):

1. Download **MSYS2**:
- Download msys2-x86_64-xxx.exe from  http://www.msys2.org/
- Install msys2 to default path C:\msys64\
- Run MSYS2
- Execute 'pacman -Syu' (confirm with "y" on prompt)
     When prompted terminate shell and re-run MSYS2
- Execute 'pacman -Su' (confirm with "y" on prompt)
     Run (confirm with "y" on prompts)
- Install required packages:
     pacman -S make
     pacman -S diffutils
     pacman -S yasm
     pacman -S mingw-w64-x86_64-gcc
     pacman -S mingw-w64-i686-gcc
     pacman -S mingw-w64-x86_64-gtk3
     pacman -S mingw-w64-i686-gtk3
     pacman -S cmake

Note that 'C:/msys64/mingw64/share' is not in the search path
set by the XDG_DATA_HOME and XDG_DATA_DIRS
environment variables, so applications may not
be able to find it until you set them.

2. Install **pkg-config** for both architectures:
- Download http://ftp.gnome.org/pub/gnome/binaries/win64/dependencies/pkg-config_0.23-2_win64.zip
And unzip and copy pkg-config.exe to E:\msys64\mingw64\bin
- Download http://ftp.gnome.org/pub/gnome/binaries/win32/dependencies/pkg-config_0.26-1_win32.zip
And unzip and copy pkg-config.exe to E:\msys64\mingw32\bin

3. Fix missing `intl.dll`:
cd C:\msys64\\mingw32\bin
copy libintl-8.dll intl.dll
cd C:\msys64\\mingw64\bin
copy libintl-8.dll intl.dll

4. Rename import libraries in msys64\mingw64\lib and in msys64\mingw32\lib:
- libpthread.dll.a → libpthread.dll.a-org
- libwinpthread.dll.a → libwinpthread.dll.a-org
- libbz2.dll.a → libbz2.dll.a-org
- libz.dll.a → libz.dll.a-org
- libiconv.dll.a → libiconv.dll.a-org
- liblzma.dll.a → liblzma.dll.a-org
- libSDL.dll.a → libSDL.dll.a-org

---

## Building Dependencies

### Building zlib

---

#### Windows

1. Grab the zlib sources version (http://zlib.net/)
     - Edit win32/Makefile.msc so that it uses -MT instead of -MD, since this is how FFmpeg is built as well.
     - Edit zconf.h and remove its inclusion of unistd.h. 
This gets erroneously included when building FFmpeg.
2. Build Zlib:
   - Using Visual Studio (recommended)
   Under \zlib-1.3.1\contrib\vstudio\vc17 there is a VisualStudio project for building zlib.
   
     💡 Tip: 
     >   When building with Visual Studio, make sure to change the Runtime Library setting in each project to Multi-threaded (/MT) instead of the default.
     >   This ensures static linking of the C runtime and avoids dependency on external DLLs.
     >   For x86, compile the ReleaseWithoutAsm - the asm code seems to be causing a crash in the OpenSSL unit tests
     >   For x64, compile the Release
   
   - Using x64 or x86 Native Tools Command Prompt for VS
   $ nmake -f win32/Makefile.msc.
   
     💡 Tip: 
     >   Once you build the projects will build zlibstat.lib you will need to rename this to zlib.lib or ffmpeg will not link properly  

3. Move zlib.lib, zconf.h, and zlib.h to somewhere MSVC can see.
**Example:**
```
C:/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0\zlib\x64\
├── zlib.h
├── zconf.h
└── zlib.lib
```

---

#### Linux (x64)

1. Configure and select your compiler to gcc/g++
   ```bash
   export CMAKE_CXX_COMPILER=/usr/bin/g++
   export CMAKE_C_COMPILER=/usr/bin/gcc
   export CXX=/usr/bin/g++
   export CC=/usr/bin/gcc
   ```

2. Build:
   ```bash
   ./configure --static --64
   make
   ```
     💡 Tip: 
     >   To verify architecture:
     >   ```bash
     >   objdump -f libz.a | grep ^architecture
     >   ```

---

#### Linux (ARM64)

1. Install cross compiler:
   ```bash
   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
   ```

2. Configure and select your compiler to aarch64-linux-gnu-gcc/aarch64-linux-gnu-g++:
   ```bash
   export CMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++
   export CMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc
   export CXX=/usr/bin/aarch64-linux-gnu-g++
   export CC=/usr/bin/aarch64-linux-gnu-gcc
   ```

3. Build:
   ```bash
   ./configure --static
   make
   ```

---

### Building OpenSSL

---

#### Windows

1. Grab the OpenSSL sources.  (https://www.openssl.org/)
   Read INSTALL and NOTES.WIN (or NOTES.UNIX)

2. Download and install Perl:
   -  Strawberry Perl from http://strawberryperl.com (recommended, contains all necessary libraries)
   or
   - ActiveState Perl from https://www.activestate.com/ActivePerl
   You also need the perl module Text::Template, available on CPAN.
   Please read NOTES.PERL for more information.

3. Download and install NASM.  
   - Netwide Assembler, a.k.a. NASM, available from https://www.nasm.us,
   is required. Note that NASM is the only supported assembler. Even
   though Microsoft provided assembler is NOT supported, contemporary
   64-bit version is exercised through continuous integration of
   VC-WIN64A-masm target.

4. Run x64 or x86 Native Tools Command Prompt for VS and move into OpenSSL source folder
   ```bash
   cd C:\openssl-openssl-3.3.0
   ```

5. Add NASM to the path
   ```bash
   set path=%PATH%;C:\Program Files\NASM
   ```
6. Configure OpenSSL to use zlib.
   Change path of the command below to your location of zlib.lib, zlib.h zconf.h files.
   
   **For x64:**
   ```bash
   perl Configure VC-WIN64A --prefix=C:\openssl\x64 no-shared -static zlib --with-zlib-include=C:/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/zlib/x64 --with-zlib-lib=C:/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/zlib/x64/zlib.lib
   ```

   **For x86:**
   ```bash
   perl Configure VC-WIN32 --prefix=C:\openssl\x86 no-shared -static zlib --with-zlib-include=C:/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/zlib/x86 --with-zlib-lib=C:/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/zlib/x86/zlib.lib
   ```
     💡 Tip: 
     >   While Configure is running, it pops up a window suggesting that nmake is not in the path, and offering to download dmake. 
     >   Ignore this message! 
     
7. Update makefile to use /MT and /MP
   replace CNF_CFLAGS=/Gs0 /GF /Gy
   with    CNF_CFLAGS=/Gs0 /GF /Gy /MT /MP
   
   /MT to eliminate the linking to runtime DLLs (like VCRUNTIME140.dll)
   /MP for multi-processor compilation - though I haven't seen any build performance difference than without it

8. Build
   ```bash
   nmake
   ```
     💡 Tip:  
     >   Error: 'rc' is not recognized as an internal or external command, operable program or batch file. NMAKE : fatal error U1077: '"rc' : return code '0x1'    
     >   Solution for x64:
     >   ```bash
     >   set path=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\x64
     >   ```
     >
     >   Solution for x86:
     >   ```bash
     >   set path=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\x86
     >   ```
     >   
     >   Or restart windows after changing from x64 to x86.
     >   Error: fatal error LNK1112: module machine type 'x64' conflicts with target machine type 'x86'.
     >   Solution: Compiler doesn't suit to your build. Change your compiler on VC manually:
     >   Restart your VC.
     >   ```bash
     >   cd C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build
     >   ```
     >   
     >   Solution for x64:
     >   ```bash
     >   vcvarsall.bat x64
     >   ```
     >
     >   Solution for x86:
     >   ```bash
     >   vcvarsall.bat x86
     >   ```

   ```bash
   nmake test
   nmake install
   ```

---

#### Linux (x64 and ARM64)

Not required

---

### Building Libaom

---

#### Windows

1. Grab the AOM sources from git.  (https://aomedia.googlesource.com/aom/)
   The website already has most instructions 
   ```bash
   git clone --depth 1 https://aomedia.googlesource.com/aom
   ```
2. Run 32/64 bit VC env variable command prompt (depending on configuration required)
   Move to folder near aom (cd C:/)
   ```bash
   mkdir aom_build
   cd aom_build
   ```
3. Generate Visual studio sln
   
   **Visual Studio x64**

     Change path/to/aom to real path of aom location (C:/aom)
     ```bash
     cmake path/to/aom -G "Visual Studio 17 2022"
     cmake --build . --config Release
     ```

   **Visual Studio x86**

     Change path/to/aom to real path of aom location (C:/aom)
     ```bash
     cmake path/to/aom -G "Visual Studio 17 2022 -A Win32"
     cmake --build . --config Release
     ```

4. Open aom_build\AOM.sln with VS
   Check build dependencies of project "aom"
   Change all depended projects (including aom) Properies -> C/C++ -> Code Generation -> Runtime Library - > change it to Multi-threaded (/MT)
   Build project "aom"

5. Move binaries, and headers somewhere MSVC can see.
   x64 or x86 depending on configuration required

     💡 Tip:  
     >   aom.pc file (located in C:/aom_build) should be moved into pkgconfig folder.
     >   Update aom.pc file prefix path according to new location:
     >   prefix=/c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/aom/x64 
     >   or x86 depending on configuration

**Example:**
```
C:/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0\aom\x64\
├── include
│   └── <header folders from C:/aom>
├── bin
│   └── <binaries from C:/aom_build/Release>
└── lib
     ├── <lib files from C:/aom_build/Release>
     └── pkgconfig
          └── aom.pc
```

---

#### Linux

1. Install pre requirements according to FFmpeg guide in Get the Dependencies section
   https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu
   However, -DBUILD_SHARED_LIBS is not enough for static lib build. 
   To avoid error "can not be used when making a shared object; recompile with -fPIC/usr/bin/ld: final link failed: bad value" Please use the following cmake cmd
   The flag -march=znver2 is optional for performance, you can adject with your GPU

   ```bash
   	$ git clone --depth 1 https://aomedia.googlesource.com/aom
   	$ mkdir aom_build
   ```

   **Linux (x64)**
   ```bash
   cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX="/path/to/aom_build_lnx64" -DBUILD_SHARED_LIBS=0 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-flto -O3 -march=znver2" -DCMAKE_C_FLAGS="-flto -O3 -march=znver2" -DCMAKE_C_FLAGS_INIT="-flto=8 -static" -DENABLE_NASM=on path/to/aom
   ```

   **Linux (ARM64)**
   ```bash
   cmake -DCMAKE_INSTALL_PREFIX="${PWD}/release" -DBUILD_SHARED_LIBS=0 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-flto -O3" -DCMAKE_C_FLAGS="-flto -O3" -DCMAKE_C_FLAGS_INIT="-flto=8 -static" -DENABLE_NASM=on -DCMAKE_TOOLCHAIN_FILE=../aom/build/cmake/toolchains/arm64-linux-gcc.cmake ../aom
   ```

   ```bash
   make 
   make install
   ```
     💡 Tip:  
     >   ARM64: switch -march=znver2 to -march=native

---

## Building FFmpeg

1. Getting source code:
   - Update the version of ffmpeg in the "get_sourcecode" script
   - Download and unpack ffmpeg source code:
2. Run C:\msys64\msys2_shell.cmd -mingw64
   ```bash
   cd /c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0
   ./get_sourcecode
   ```

---

### Windows (MSYS2 + GCC)

**Build 64**

3. Run C:\msys64\msys2_shell.cmd -mingw64
   ```bash
   cd /c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0
   ./ffmpeg-build-win release 64
   ```

---

**Build 32**

3. Run E:\msys64\msys2_shell.cmd -mingw32
   ```bash
   cd /c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0
   ./ffmpeg-build-win release 32
   ```

---

### Build with VC++

3. Navigate to cd c:/drivers/drivers/amf/Thirdparty/ffmpegy/ffmpeg/ffmpeg-7.0
4. Edit the "configure" file, due to OpenSSL and zlib.

     💡 Tip:  
     >   This change is not needed for mingw builds (Windows)
     >   
     >   Find the following code (32 & 64bit): (for OpenSSL)
     >   
     >   enabled openssl           && { check_pkg_config openssl openssl openssl/ssl.h OPENSSL_init_ssl ||
     >                                  check_pkg_config openssl openssl openssl/ssl.h SSL_library_init ||
     >                                  check_lib	openssl		openssl/ssl.h SSL_library_init -lssl -lcrypto ||
     >                                  check_lib	openssl		openssl/ssl.h SSL_library_init -lssl32 -leay32 ||
     >                                  check_lib	openssl		openssl/ssl.h SSL_library_init -lssl -lcrypto -lws2_32 -lgdi32 ||
     >                                  die "ERROR: openssl not found"; }
     >   
     >   Replace it with the code below:
     >   
     >   For Windows:
     >   
     >   enabled openssl           && { check_pkg_config openssl openssl openssl/ssl.h OPENSSL_init_ssl ||
     >                                  check_pkg_config openssl openssl openssl/ssl.h SSL_library_init ||
     >                                  check_lib	openssl		openssl/ssl.h OPENSSL_init_ssl -llibssl -llibcrypto -lzlib -lws2_32 -lgdi32 -luser32 -ladvapi32 -lcrypt32 ||
     >                                  check_lib	openssl		openssl/ssl.h SSL_library_init -lssl -lcrypto ||
     >                                  check_lib	openssl		openssl/ssl.h SSL_library_init -lssl32 -leay32 ||
     >                                  check_lib	openssl		openssl/ssl.h SSL_library_init -lssl -lcrypto -lws2_32 -lgdi32 ||
     >                                  die "ERROR: openssl not found"; }
     >   
     >   Which basically adds:
     >                                  check_lib	openssl		openssl/ssl.h OPENSSL_init_ssl -llibssl -llibcrypto -lzlib -lws2_32 -lgdi32 -luser32 -ladvapi32 -lcrypt32 ||
     >   
     >   To enable the check for the OpenSSL staic library to succeed

---

**Build Windows x64**

5. Run x64 Native Tools Command Prompt for VS.

     💡 Tip:  
     >   Open VC as administrator, due to OpenSSL also create files in "Program Files (x86)" folder:
     >   
     >   I had to add also those path to VC, or my build fail:
     >   ```bash
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\shared\x64
     >   set path=%PATH%;C:\Program Files\NASM
     >   ```
     >   This line depends on your Windows Kit and MSVC compiler version: "...\10.0.22621.0\..." 

6. Add path to zlib
   ```bash
   set include=%include%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\zlib\x64
   set lib=%lib%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\zlib\x64
   ```
7. Add path to OpenSSL lib
   ```bash
   set include=%include%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\openssl\x64\include
   set lib=%lib%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\openssl\x64\lib
   set path=%path%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\openssl\x64\include
   ```
8. Add path to libaom 
   ```bash
   set include=%include%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\aom\x64\include
   set lib=%lib%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\aom\x64\lib
   set path=%path%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\aom\x64\include
   ```
9. Run msys:
   ```bash
   C:\msys64\msys2_shell.cmd -mingw64 -use-full-path
   ```
10. Navigate to ffmpeg:
   ```bash
   cd /c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0
   ```
11. Export pkgconfig:
   ```bash
   export PKG_CONFIG_PATH=path/to/aom_build_x64/lib/pkgconfig
   ```
   **Example:**
   ```bash
   export PKG_CONFIG_PATH=/c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/aom/x64/lib/pkgconfig
   ```
12. Run build script
   ```bash
   ./ffmpeg-build-win release 64 msvc
   ```
     💡 Tip:
     >   If you get this error: cl.exe is unable to create an executable file. 
     >   If cl.exe is a cross-compiler, use the --enable-cross-compile option. 
     >   Only do this if you know what cross compiling means. C compiler test failed.
     >   First thing to check is where the cl.exe and link.exe are. 
     >   Run "which cl.exe" and "which cl.exe" and make sure its pointing to MSVC if its pointing to msys either rm the file or rename it. 
     >   If that doesnt work look in the log file I had the error in "mslink" with the error "LINK : fatal error LNK1104: cannot open file 'kernel32.lib'" 
     >   If this is the case its having trouble finding your Windows SDK. 
     >   In the mslink file change
     >   "$LINK_EXE_PATH" $@
     >   For:
     >   "$LINK_EXE_PATH" $@ -verbose -LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64" -LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64" -LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt_enclave\x64"
     >   Replace the paths with a link to your sdk and make sure you have links to um, ucrt, and ucrt_enclave. 
     >   When you compile x32 you will need to change the path to the x32 folders. 
     >   Note you done need -verbose this just gives you more info	
     >   
     >   If you get "ERROR: aom >= 1.0.0 not found using pkg-config" when you 100% followed the steps its because even though you are giving it the path to the aom.pc file it still doesnt want to work. 
     >   First thing run pkg-config --libs "aom >= 1.0.0" and if it finds the package in the correct directory you are having the same problem as me. 
     >   Go into the aom.pc file and make sure that the top of the file the element "prefix" is an abs path to the directory that has the lib/include/bin for the correct build type.
	
     💡 Tip 2:  
     >   Error: LINK : fatal error LNK1104: cannot open file 'LIBCMT.lib'
     >   Error: LINK : fatal error LNK1104: cannot open file 'OLDNAMES.lib'
     >   Solution: Make sure that your Windows Kit version is the same as MSVC compiler version.
     >   If it's different, then add it to environment variable path or set manually in VC (i used version 10.0.22621.0):
     >   ```bash
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x86
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x86
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\shared\x86
     >   ```
     >   Error: LINK : error LNK2001: unresolved external symbol mainCRTStartup
     >   Error: aom_integer.h(15): fatal error C1083: Cannot open include file: 'stddef.h': No such file or directory
     >   Error: Out of tree builds are impossible with config.h in source dir.
     >   Solution: seems that your aom builded with wrong Runtime Library (default /MD) - you need to return back to aom.
     >   Open AOM.sln with VisualStudio, change build to Release x64 or Release x32 (depends on your build), and change Runtime Library on each project (including in folders) to /MT:
     >   Open Properties of each project file -> C/C++ -> Code Generation -> Runtime Library

---

**Build Windows x32**

5. Run x32 Native Tools Command Prompt for VS.

     💡 Tip:  
     >   Open VC as administrator, due to OpenSSL also create files in "Program Files (x86)" folder:
     >   
     >   I had to add also those path to VC, or my build fail:
     >   ```bash
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um
     >   set include=%include%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x86
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x86
     >   set lib=%lib%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\shared\x86
     >   set path=%PATH%;C:\Program Files\NASM
     >   ```
     >   This line depends on your Windows Kit and MSVC compiler version: "...\10.0.22621.0\..." 

6. Add path to zlib
   ```bash
   set include=%include%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\zlib\x86
   set lib=%lib%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\zlib\x86
   ```

     💡 Tip:  
     >   Add the following code in zlib.h: (for zlib)
     >        #undef ZEXPORT
     >        #define ZEXPORT __stdcall
     >   so ffmpeg will link properly with zlib

7. OpenSSL lib
     Not necessary 
8. Add path to libaom 
   ```bash
   set include=%include%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\aom\x86\include
   set lib=%lib%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\aom\x86\lib
   set path=%path%;C:\drivers\drivers\amf\Thirdparty\ffmpeg\ffmpeg\ffmpeg-7.0\aom\x86\include
   ```
9. Run msys:
   ```bash
   C:\msys64\msys2_shell.cmd -mingw32 -use-full-path
   ```
10. Navigate to ffmpeg:
   ```bash
   cd /c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0
   ```
11. Export pkgconfig:
   ```bash
   export PKG_CONFIG_PATH=path/to/aom_build_x86/lib/pkgconfig
   ```
**Example:**
```bash
export PKG_CONFIG_PATH=/c/drivers/drivers/amf/Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/aom/x86/lib/pkgconfig
```
12. Run build script
   ```bash
   ./ffmpeg-build-win release 32 msvc
   ```

     💡 Tip:  
     >   If get #ERROR: zlib requested but not found
     >   Skip 4th point in VC 32 build. Do not add #undef ZEXPORT.

---

### Linux (x64 + ARM64)

---

**Linux (x64)**

3. Update build script
   Open Terminal and move into ffmpeg-build-linux location
   ```bash
   pwd
   ```
   Take path from "pwd" command and update futher paths in ffmpeg-build-linux script
   extracflags='--extra-cflags=-I/<ffmpeg-build-linux path>/zlib/lnx64,-I/<ffmpeg-build-linux path>/aom/lnx64/include'
   extralinkflags='--extra-ldflags=-Wl,-rpath=$ORIGIN,-L/<ffmpeg-build-linux path>/zlib/lnx64,-L/<ffmpeg-build-linux path>/aom/lnx64/lib'

4. Add AOM pkg-config path to environment 
   ```bash
   export PKG_CONFIG_PATH="/<ffmpeg-build-linux path>/aom/lnx64/lib/pkgconfig"	 
   ```
5.  Run build script
   ```bash
   ./ffmpeg-build-linux release 64 linux
   ```

     💡 Tip:  
     >   If error: /bin/bash^M: bad interpreter: No such file or directory
     >   Then need to bring script in Linux format: 
     >   ```bash
     >   sudo apt-get install dos2unix
     >   dos2unix ffmpeg-build-linux
     >   ```

     💡 Tip 2:  
     >   Run patchelf to set binaries runpath to current working directory
     >   ffmpeg doesn't set runpath to current directly correctly with flag -rpath=$ORIGIN, we still need to use other tools to achieve this. 
     >   I recommend patchelf. Please do this for all the generated binaries.
     >   eg. sudo patchelf --set-rpath '$ORIGIN' libswscale.so.5
     >   (to check the binary's runpath, objdump -p libswscale.so.5 | grep RUN)

---

**Linux (ARM64)**

3. Update build script
   Open Terminal and move into ffmpeg-build-linux location
   ```bash
   pwd
   ```
   Take path from "pwd" command and update futher paths in ffmpeg-build-linux script
   extracflags='--extra-cflags=-I/home/<ffmpeg-build-linux path>/zlib/arm64,-I/home/<ffmpeg-build-linux path>/aom/arm64/include'
   extralinkflags='--extra-ldflags=-Wl,-rpath=$ORIGIN,-L/<ffmpeg-build-linux path>/zlib/arm64,-L/<ffmpeg-build-linux path>/aom/arm64/lib'

4. Add AOM pkg-config path to environment 
   ```bash
   export PKG_CONFIG_PATH="/<ffmpeg-build-linux path>/aom/arm64/lib/pkgconfig"	 
   ```
5.  Run build script
   ```bash
   ./ffmpeg-build-linux release arm64 linux
   ```
     💡 Tip:  
     >    If error: /bin/bash^M: bad interpreter: No such file or directory
     >    Then need to bring script in Linux format: 
     >   ```bash
     >   sudo apt-get install dos2unix
     >   dos2unix ffmpeg-build-linux
     >   ```

     💡 Tip 2:  
     >    Error: libavutil/aarch64/bswap.h:42: Error: no such instruction: `rev %ax,%ax'
     >    Try to add to ffmpeg-build-linux file to 125 line: --enable-cross-compile --cross-prefix=aarch64-linux-gnu- --target-os=linux 
     >    If still fail, resolve it by removing AOM from build:
     >         delete "--enable-libaom" from "ffmpeg-build-linux" file.

6. Run patchelf to set binaries runpath to current working directory
   ```bash
   sudo patchelf --set-rpath '$ORIGIN' libswscale.so.8
   ```

---

## Final Notes

After completing these steps, you will have a fully functional, statically linked **FFmpeg** build with:
- zlib compression support (for formats like EXR)
- OpenSSL for secure protocols
- libaom for AV1 encoding/decoding
- AMF hardware acceleration (on supported AMD hardware)

🧩 **Next step:** integrate and test your FFmpeg build in your media workflow or applications.

---

**FFmpeg Custom Build Guide (AMF Support)** — last updated oct.2025.

WINDOWS = windows
LINUX = linux
MACOS = macos
DIRS = $(WINDOWS) $(LINUX) $(MACOS)

CLANG = g++
WINDOWS_CFLAGS = -I./SDL3/include -march=x86-64 -O3 -fno-trapping-math -funsafe-math-optimizations -ffast-math -fopenmp
LINUX_CFLAGS =  -target x86_64-unknown-linux-gnu  -I./linuxinc -march=x86-64 -O3 -fno-trapping-math -funsafe-math-optimizations -ffast-math -fopenmp
GPPLIMITS = -fconstexpr-ops-limit=360554466 -fconstexpr-loop-limit=360554466
STATIC_WINDOWS_LDFLAGS = -LC:/SDL3/lib \
    -static-libgcc -static-libstdc++ \
    -Wl,-Bstatic \
    -lgomp \
    -lSDL3 \
    -lwinpthread \
    -Wl,-Bdynamic \
    -lkernel32 -luser32 \
	-lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 \
	-lsetupapi -lversion -luuid -ldinput8 -ldxguid -ldxerr8
WINDOWS_LDFLAGS = -lSDL3 -mwindows

LINUX_LDFLAGS = -L./linuxinc -lSDL3 -lpthread -lm -ldl -lrt -Wl,-rpath,'$$ORIGIN'
# Compile resources.res from resources.rc
set_icon:
	@# This sets the metadata icon for the actual executable file
	gio set ./linux/spectralpathtracer metadata::custom-icon "file://$(shell pwd)/favicon.ico"
	@# This tells Linux the file is safe to run as a program
	chmod +x ./linux/spectralpathtracer
resources: resources.rc favicon.ico
	windres resources.rc -o resources.res -O coff
# Compile the program
ifeq ($(OS),Windows_NT)
lin: spectralpathtracer.cpp resources.res | $(DIRS) copy_assets_lin
else
lin: spectralpathtracer.cpp | $(DIRS) copy_assets_lin set_icon
endif
	$(CLANG) spectralpathtracer.cpp $(LINUX_CFLAGS) -o $(LINUX)/spectralpathtracer $(LINUX_LDFLAGS)
win: spectralpathtracer.cpp resources.res | $(DIRS) copy_assets_win
	$(CLANG) spectralpathtracer.cpp resources.res $(WINDOWS_CFLAGS) -o $(WINDOWS)/spectralpathtracer.exe $(WINDOWS_LDFLAGS)
copy_assets_win: | $(DIRS)
	cp -rn assets/. windows/
	cp -rn windowlib/. windows/
copy_assets_lin: | $(DIRS)
	cp -rns assets/. linux/
	cp -rns linuxlib/. linux/
copy_assets_mac: | $(DIRS)
	cp -rn assets/. macos/
	cp -rn macoslib/. macos/
$(DIRS):
	mkdir -p $@

anyways its a zip file. so.. you had to unzip spectral path tracer source code c++.zip

you also need g++ 15.2.0 which is what am using.

you should download https://www.msys2.org/ if your on windows

and type this on msys2 ucrt64 SPECIFICALLY ucrt64
```
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-toolchain
```
now type 
```
g++ --version
```
to check if you have it

now use 
```
cd "C:\Users\User\Downloads\spectral path tracer source code C++"
make win
```
this is for compiling a program to exe
i may be fool. but thats what i think readme is

or for linux
```
cd "~/Downloads/spectral path tracer source code C++"
make lin
``` 
use this to compile the program in linux

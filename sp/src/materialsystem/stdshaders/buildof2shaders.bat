@echo off
setlocal

rem OF2: compiles the shaders listed in of2_dx9_20b.txt into mod_episodic\shaders\fxc.
rem Run it from this directory. It is buildepisodicshaders.bat with three differences:
rem   - only our own short shader list, not the full Mapbase set (which takes far longer)
rem   - nmake comes from VS2022 instead of VS2013
rem   - perl comes from Git for Windows, since there is no other perl on this machine

set GAMEDIR=%cd%\..\..\..\game\mod_episodic
set SDKBINDIR=C:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin
set SOURCEDIR=..\..

call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul
set PATH=%PATH%;C:\Program Files\Git\usr\bin

rem The Valve perl scripts "require" their helpers from the current directory,
rem which perl 5.26+ only allows with this set.
set PERL_USE_UNSAFE_INC=1

rem Git's perl is a Unix one: it leaves the carriage return on every line of the (CRLF)
rem shader sources. That ends up inside the SKIP rules the scripts hand to shadercompile,
rem which then applies none of them: every combination gets compiled, the ones that cannot
rem compile fail the whole shader, and a big shader looks like millions of combinations.
rem This makes it read and write text the way a Windows perl does.
set PERLIO=:crlf

rem String::CRC32 isn't part of Git's perl; devtools\bin\perl_lib has a stand-in.
rem devtools\bin itself is listed because Git's perl doesn't take the backslashes in
rem the script path as directory separators, so the scripts can't find their helpers.
set PERL5LIB=../../devtools/bin/perl_lib:../../devtools/bin

rem buildshaders.bat rewrites inclist.txt (tracked, lists the full Mapbase set) for our short list
copy /y inclist.txt inclist.txt.keep >nul

call .\buildshaders.bat of2_dx9_20b -game %GAMEDIR% -source %SOURCEDIR%
rem The shader model 3 builds of those that have one (what any current card runs)
call .\buildshaders.bat of2_dx9_30 -game %GAMEDIR% -source %SOURCEDIR% -dx9_30 -force30

rem Tidy up. The worklists are only needed while compiling, and Git's perl turns
rem a "> nul" redirect into a real file called nul.
move /y inclist.txt.keep inclist.txt >nul
del /f /q filelist.txt filelistgen.txt filestocopy.txt uniquefilestocopy.txt vcslist.txt 2>nul
del /f /q makefile.of2_dx9_20b makefile.of2_dx9_20b.copy 2>nul
del /f /q makefile.of2_dx9_30 makefile.of2_dx9_30.copy 2>nul
del /f /q "\\?\%cd%\nul" 2>nul

echo Finished building shaders

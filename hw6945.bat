:: ----------------------------------------------------------------------------
:: Project: OpenDMTP Reference Implementation - C client 
:: URL    : http://www.opendmtp.org
:: File   : hw6945.bat
:: Desc   : Create ".\build_hw6945" for creating DMTP.EXE for the HP hw6945
:: ----------------------------------------------------------------------------

:: --- destination build directory
set HW6945_DIR=.\build_hw6945

:: --- set file locations
if not exist ".\WCE\hw6945" goto PHONE
set WCE_HW6945=.\WCE\hw6945
goto CONTINUE
:PHONE
set WCE_HW6945=.\WCE\PHONE
:CONTINUE

:: --- re-build destination directory
rmdir /s /q %HW6945_DIR%
mkdir %HW6945_DIR%\src

:: --- base
mkdir %HW6945_DIR%\src\base
copy /B src\base\accting.c %HW6945_DIR%\src\base\accting.cpp
copy /B src\base\accting.h %HW6945_DIR%\src\base\accting.h
copy /B src\base\cdiags.h %HW6945_DIR%\src\base\cdiags.h
copy /B src\base\cerrors.h %HW6945_DIR%\src\base\cerrors.h
copy /B src\base\cmderrs.h %HW6945_DIR%\src\base\cmderrs.h
copy /B src\base\event.c %HW6945_DIR%\src\base\event.cpp
copy /B src\base\event.h %HW6945_DIR%\src\base\event.h
copy /B src\base\events.c %HW6945_DIR%\src\base\events.cpp
copy /B src\base\events.h %HW6945_DIR%\src\base\events.h
copy /B src\base\mainloop.c %HW6945_DIR%\src\base\mainloop.cpp
copy /B src\base\mainloop.h %HW6945_DIR%\src\base\mainloop.h
copy /B src\base\packet.c %HW6945_DIR%\src\base\packet.cpp
copy /B src\base\packet.h %HW6945_DIR%\src\base\packet.h
copy /B src\base\pqueue.c %HW6945_DIR%\src\base\pqueue.cpp
copy /B src\base\pqueue.h %HW6945_DIR%\src\base\pqueue.h
copy /B src\base\propman.c %HW6945_DIR%\src\base\propman.cpp
copy /B src\base\propman.h %HW6945_DIR%\src\base\propman.h
copy /B src\base\props.h %HW6945_DIR%\src\base\props.h
copy /B src\base\protocol.c %HW6945_DIR%\src\base\protocol.cpp
copy /B src\base\protocol.h %HW6945_DIR%\src\base\protocol.h
copy /B src\base\serrors.h %HW6945_DIR%\src\base\serrors.h
copy /B src\base\statcode.h %HW6945_DIR%\src\base\statcode.h

:: --- tools
mkdir %HW6945_DIR%\src\tools
copy /B src\tools\base64.c %HW6945_DIR%\src\tools\base64.cpp
copy /B src\tools\base64.h %HW6945_DIR%\src\tools\base64.h
copy /B src\tools\bintools.c %HW6945_DIR%\src\tools\bintools.cpp
copy /B src\tools\bintools.h %HW6945_DIR%\src\tools\bintools.h
copy /B src\tools\buffer.c %HW6945_DIR%\src\tools\buffer.cpp
copy /B src\tools\buffer.h %HW6945_DIR%\src\tools\buffer.h
copy /B src\tools\checksum.c %HW6945_DIR%\src\tools\checksum.cpp
copy /B src\tools\checksum.h %HW6945_DIR%\src\tools\checksum.h
copy /B src\tools\comport.c %HW6945_DIR%\src\tools\comport.cpp
copy /B src\tools\comport.h %HW6945_DIR%\src\tools\comport.h
copy /B src\tools\gpstools.c %HW6945_DIR%\src\tools\gpstools.cpp
copy /B src\tools\gpstools.h %HW6945_DIR%\src\tools\gpstools.h
copy /B src\tools\io.c %HW6945_DIR%\src\tools\io.cpp
copy /B src\tools\io.h %HW6945_DIR%\src\tools\io.h
copy /B src\tools\random.c %HW6945_DIR%\src\tools\random.cpp
copy /B src\tools\random.h %HW6945_DIR%\src\tools\random.h
copy /B src\tools\sockets.c %HW6945_DIR%\src\tools\sockets.cpp
copy /B src\tools\sockets.h %HW6945_DIR%\src\tools\sockets.h
copy /B src\tools\stdtypes.h %HW6945_DIR%\src\tools\stdtypes.h
copy /B src\tools\strtools.c %HW6945_DIR%\src\tools\strtools.cpp
copy /B src\tools\strtools.h %HW6945_DIR%\src\tools\strtools.h
copy /B src\tools\threads.c %HW6945_DIR%\src\tools\threads.cpp
copy /B src\tools\threads.h %HW6945_DIR%\src\tools\threads.h
copy /B src\tools\utctools.c %HW6945_DIR%\src\tools\utctools.cpp
copy /B src\tools\utctools.h %HW6945_DIR%\src\tools\utctools.h

:: --- modules
mkdir %HW6945_DIR%\src\modules
copy /B src\modules\motion.c %HW6945_DIR%\src\modules\motion.cpp
copy /B src\modules\motion.h %HW6945_DIR%\src\modules\motion.h
copy /B src\modules\odometer.c %HW6945_DIR%\src\modules\odometer.cpp
copy /B src\modules\odometer.h %HW6945_DIR%\src\modules\odometer.h

:: --- custom
mkdir %HW6945_DIR%\src\custom
mkdir %HW6945_DIR%\src\custom\transport
mkdir %HW6945_DIR%\src\custom\tools
mkdir %HW6945_DIR%\src\custom\wince
copy /B src\custom\custprop.inc %HW6945_DIR%\src\custom\custprop.inc
copy /B src\custom\custprop.h %HW6945_DIR%\src\custom\custprop.h
copy /B src\custom\defaults.h %HW6945_DIR%\src\custom\defaults.h
copy /B src\custom\gps.c %HW6945_DIR%\src\custom\gps.cpp
copy /B src\custom\gps.h %HW6945_DIR%\src\custom\gps.h
copy /B src\custom\gpsmods.c %HW6945_DIR%\src\custom\gpsmods.cpp
copy /B src\custom\gpsmods.h %HW6945_DIR%\src\custom\gpsmods.h
copy /B src\custom\log.c %HW6945_DIR%\src\custom\log.cpp
copy /B src\custom\log.h %HW6945_DIR%\src\custom\log.h
copy /B src\custom\startup.c %HW6945_DIR%\src\custom\startup.cpp
copy /B src\custom\startup.h %HW6945_DIR%\src\custom\startup.h
copy /B src\custom\transport.c %HW6945_DIR%\src\custom\transport.cpp
copy /B src\custom\transport.h %HW6945_DIR%\src\custom\transport.h
copy /B src\custom\transport\socket.c %HW6945_DIR%\src\custom\transport\socket.cpp
copy /B src\custom\transport\socket.h %HW6945_DIR%\src\custom\transport\socket.h
copy /B src\custom\wince\wceos.cpp %HW6945_DIR%\src\custom\wince\wceos.cpp
copy /B src\custom\wince\wceos.h %HW6945_DIR%\src\custom\wince\wceos.h
copy /B src\custom\wince\wcegprs.cpp %HW6945_DIR%\src\custom\wince\wcegprs.cpp
copy /B src\custom\wince\wcegprs.h %HW6945_DIR%\src\custom\wince\wcegprs.h

:: --- WindowCE
copy /B src\StdAfx.c %HW6945_DIR%\StdAfx.cpp
copy /B src\stdafx.h %HW6945_DIR%\stdafx.h

:: --- Top-level project source/tools
copy /B %WCE_HW6945%\DMTP.cpp %HW6945_DIR%\.
copy /B %WCE_HW6945%\DMTP.h %HW6945_DIR%\.
copy /B %WCE_HW6945%\DMTP.ico %HW6945_DIR%\.
copy /B %WCE_HW6945%\DMTP.rc %HW6945_DIR%\.
copy /B %WCE_HW6945%\newres.h %HW6945_DIR%\.
copy /B %WCE_HW6945%\resource.h %HW6945_DIR%\.
copy /B %WCE_HW6945%\props.conf %HW6945_DIR%\.

:: --- Command line nmake files
copy /B %WCE_HW6945%\DMTP.VCN %HW6945_DIR%\.
copy /B %WCE_HW6945%\DMTP.DEP %HW6945_DIR%\.

:: --- eVC IDE files
copy /B %WCE_HW6945%\DMTP.VCW %HW6945_DIR%\.
copy /B %WCE_HW6945%\DMTP.VCP %HW6945_DIR%\.
copy /B %WCE_HW6945%\DMTP.VCO %HW6945_DIR%\.

:: --- 
@echo.
@echo.
@echo ---------------------------------------------------------------------------
@echo 1) Open ".\build_hw6945\DMTP.VCW" with "Microsoft eVC++" and build.
@echo 2) Modify ".\build_hw6945\propp.conf" as necessary for your environment.
@echo 3) Copy the following files to a new directory "\DMTP" on your hw6945:
@echo     .\build_hw6945\ARMV4Rel\DMTP.EXE
@echo     .\build_hw6945\props.conf
@echo 4) Start up the program "\DMTP\DMTP.EXE" on your hw6945
@echo.

:: ---


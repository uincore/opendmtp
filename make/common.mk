# -----------------------------------------------------------------------------

# --- commands/constants
MKDIR       = /bin/mkdir
RM          = /bin/rm
CP          = /bin/cp
LS          = /bin/ls
PWD         = .
LINUX_LIBS  = /usr/lib

# -----------------------------------------------------------------------------
# --- include compile time options
include ./options.mk
# this defines ${OPTIONS}
# -----------------------------------------------------------------------------

# --- default destination
ifeq ($(dest),)
  ifeq ($(findstring Windows,$(OS)),Windows)
    dest = cygwin
  endif
  ifeq ($(findstring Linux,$(OS)),Linux)
    dest = linux
  endif
endif

# --- destination platform
destok      = X
ifeq ($(findstring gum,$(dest)),gum)
  destok    = 1
  # --- platform specific (change to actual location of the GumStix buildroot)
  GSTX_PATH = /gumstix-buildroot
  GSTX_BIN  = $(GSTX_PATH)/build_arm_nofpu/staging_dir/bin
  GSTX_LIBS = $(GSTX_PATH)/build_arm_nofpu/staging_dir/lib
  # --- to compile on Linux for GumStix
  BUILD_DIR = ./build_gum
  CC        = $(GSTX_BIN)/arm-linux-cc
  AR        = $(GSTX_BIN)/arm-linux-ar
  RANLIB    = $(GSTX_BIN)/arm-linux-ranlib
  STRIP     = $(GSTX_BIN)/arm-linux-strip
  CFLAGS    = -Wall -DTARGET_GUMSTIX $(XPORT_MEDIA) $(OPTIONS) -Isrc
  SOLIBS    = $(GSTX_LIBS)/libpthread.so $(GSTX_LIBS)/libm.so
  EXE_EXT   =
endif
ifeq ($(findstring lin,$(dest)),lin)
  destok    = 1
  # --- to compile on Linux for Linux
  BUILD_DIR = ./build_lin
  CC        = cc
  AR        = ar
  RANLIB    = ranlib
  STRIP     = strip
  CFLAGS    = -Wall -DTARGET_LINUX $(XPORT_MEDIA) $(OPTIONS) -Isrc
#SOLIBS    = $(LINUX_LIBS)/libpthread.so $(LINUX_LIBS)/libm.so
  SOLIBS    =
  EXE_EXT   =
endif
ifeq ($(findstring cyg,$(dest)),cyg)
  destok    = 1
  # --- to compile on WinXP for Cygwin
  BUILD_DIR = ./build_cyg
  CC        = /bin/gcc
  AR        = /bin/ar
  RANLIB    = /bin/ranlib
  STRIP     = /bin/strip # @echo "Not Stripping "
  CFLAGS    =  -Wall -DTARGET_CYGWIN $(XPORT_MEDIA) $(OPTIONS) -Isrc
  SOLIBS    =
  EXE_EXT   = .exe
endif
ifeq ($(destok),X)
  MISSING   = missing
  BUILD_DIR = ./build
endif

# -----------------------------------------------------------------------------

# --- source/destination directories
SRC_DIR     = ./src
OBJ_DIR     = $(BUILD_DIR)/obj
LIB_DIR     = $(OBJ_DIR)/lib

# --- base libaries
#ALIBS       = -L$(LIB_DIR) -lmodule -lbase -ltools
ALIBS       = -L$(LIB_DIR) -lmodule -lbase -ltools -lm -lpthread

# -----------------------------------------------------------------------------

# --- missing dest
.PHONY : missing
missing:
	@echo ""
	@echo "Missing 'dest={gumstix|linux|cygwin}'"
	@exit 1

# -----------------------------------------------------------------------------

# --- clean
.PHONY : clean
clean:
	@echo ""
	@echo "Clean ..."
	$(RM) -rf $(BUILD_DIR)

# -----------------------------------------------------------------------------

# --- *.c => *.o    
#.c.o:
$(OBJ_DIR)/%.o: $(MISSING) $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# --- *.cpp => *.o    
#.cpp.o:
$(OBJ_DIR)/%.o: $(MISSING) $(SRC_DIR)/%.cpp
	$(CPP) $(CFLAGS) -c -o $@ $<

# -----------------------------------------------------------------------------

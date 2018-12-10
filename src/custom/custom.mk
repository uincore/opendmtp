# -----------------------------------------------------------------------------
include make/common.mk
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------

# --- sample module extensions
# 'geofence.c' is a non-scalable example (limited to a very small number of geofence zones)
# of how geozone/geofence arrival/departure messaging might be implemented.
# This simple module is currently not used here, in favor of the more full-featured example
# (geozone.c) found in the "src/modules" directory.
#CMODULE_SRC = custom/modules/geofence.c
CMODULE_SRC =

# --- sample tools extensions
# - TODO: need to separate this into linux/cygwin/gumstix
#CTOOLS_SRC = custom/linux/gpio.c
CTOOLS_SRC = custom/linux/os.c 

# --- sample dmtp 
CUSTOM_SRC  = custom/startup.c custom/transport.c custom/log.c custom/gps.c custom/gpsmods.c $(CTOOLS_SRC) $(CMODULE_SRC)
CUSTOM_OBJ  = $(CUSTOM_SRC:%.c=$(OBJ_DIR)/%.o)

# -----------------------------------------------------------------------------

.PHONY : dmtpd_all
dmtpd_all: $(MISSING) dmtpd_title dmtpd_exe

.PHONY : dmtpd_title
dmtpd_title: 
	@echo ""
	@echo "Making sample DMTP reference implementation ..."

# --- create build directory
.PHONY : dmtp_dirs
dmtp_dirs: $(MISSING)
	@echo ""
	@echo "Make dmtp object dirs ..."
	$(MKDIR) -p $(OBJ_DIR)/custom
	$(MKDIR) -p $(OBJ_DIR)/custom/linux
	$(MKDIR) -p $(OBJ_DIR)/custom/modules
	$(MKDIR) -p $(OBJ_DIR)/dmtp
	@echo ""

# --- create binary 'dmtpd' client
.PHONY : dmtpd_exe
dmtpd_exe: $(MISSING) dmtp_dirs $(CUSTOM_OBJ)
	@echo ""
	@echo "Linking 'dmtpd' ..."
	$(CC) -o $(OBJ_DIR)/dmtp/dmtpd$(EXE_EXT) $(CFLAGS) $(SOLIBS) $(CUSTOM_OBJ) $(ALIBS)
	@echo ""
	@echo "Stripping 'dmtpd' ..."
	$(STRIP) $(OBJ_DIR)/dmtp/dmtpd$(EXE_EXT)
	$(LS) -laF $(OBJ_DIR)/dmtp/dmtpd$(EXE_EXT)
	$(CP) $(OBJ_DIR)/dmtp/dmtpd$(EXE_EXT) $(BUILD_DIR)/dmtpd$(EXE_EXT)
	@echo "+++++ Created 'dmtpd' ..."
	@echo ""

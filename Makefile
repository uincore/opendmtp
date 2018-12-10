
#DEBUG_OPT = -DDEBUG_COMPILE

# -----------------------------------------------------------------------------

# --- usage
.PHONY : usage
usage:
	@echo "Usage: make dest={gumstix|linux|cygwin} <target>"
	@echo "Targets:"
	@echo "    libs        - build libraries (base, module, tools)"
	@echo "    dmtp        - build DMTP client with default support"
	@echo "    dmtp_file   - build DMTP client with File support"
	@echo "    dmtp_gprs   - build DMTP client with GPRS support"
	@echo "    dmtp_socket - build DMTP client with Socket support"
	@echo "    dmtp_serial - build DMTP client with Serial support"
	@echo "    sockserv    - build simple sample socket server"
	@echo "    scomserv    - build simple sample serial server"
	@echo "    parsefile   - build DMTP packet file parser"
	@echo "    clean       - clean build files"

# -----------------------------------------------------------------------------

# --- commands/constants
CHMOD = /bin/chmod
ifeq ($(findstring Windows,$(OS)),Windows)
  FIND = /bin/find
else
  FIND = /usr/bin/find
endif

# -----------------------------------------------------------------------------
include make/common.mk
# -----------------------------------------------------------------------------

# --- tools library
TOOLS_SRC   := tools/checksum.c tools/base64.c tools/bintools.c tools/buffer.c tools/gpstools.c
TOOLS_SRC   += tools/strtools.c tools/utctools.c tools/threads.c tools/sockets.c tools/io.c
TOOLS_SRC   += tools/comport.c tools/random.c
TOOLS_OBJ   := $(TOOLS_SRC:%.c=$(OBJ_DIR)/%.o)

# --- base library
BASE_SRC    := base/mainloop.c base/propman.c base/event.c
BASE_SRC    += base/events.c base/packet.c base/pqueue.c base/protocol.c base/accting.c base/upload.c
BASE_OBJ    := $(BASE_SRC:%.c=$(OBJ_DIR)/%.o)

# --- modules library
MODULE_SRC  := modules/odometer.c modules/motion.c
MODULE_SRC  += modules/geozone.c
MODULE_OBJ  := $(MODULE_SRC:%.c=$(OBJ_DIR)/%.o)

# --- server common
COMSERV_SRC := server/log.c server/packet.c server/events.c server/protocol.c server/upload.c
COMSERV_SRC += server/geozone.c
# --- socket server
SKSERVE_SRC := $(COMSERV_SRC) server/sock/server.c server/sock/main.c
SKSERVE_OBJ := $(SKSERVE_SRC:%.c=$(OBJ_DIR)/%.o)
# --- serial server
SCSERVE_SRC := $(COMSERV_SRC) server/serial/server.c server/serial/main.c
SCSERVE_OBJ := $(SCSERVE_SRC:%.c=$(OBJ_DIR)/%.o)

# --- event packet file parser
PARSFIL_SRC := base/packet.c parsfile/main.c parsfile/parsfile.c parsfile/log.c parsfile/events.c
PARSFIL_OBJ := $(PARSFIL_SRC:%.c=$(OBJ_DIR)/%.o)

# --- uploaded file encoder (if available)
ENCODE_SRC  := encode/log.c encode/encode.c
ENCODE_OBJ  := $(ENCODE_SRC:%.c=$(OBJ_DIR)/%.o)

# -----------------------------------------------------------------------------
# --- create libs

.PHONY : libs
libs: $(MISSING) tools modules base

# -----------------------------------------------------------------------------
# --- 'libtools.a'

# --- 'tools' target
.PHONY : tools
tools: $(MISSING) tools_dirs $(TOOLS_OBJ)
	@echo ""
	@echo "Make '$(LIB_DIR)/libtools.a' ..."
	$(AR) rc $(LIB_DIR)/libtools.a $(TOOLS_OBJ)
	$(RANLIB) $(LIB_DIR)/libtools.a

# --- create build directory
.PHONY : tools_dirs
tools_dirs: $(MISSING)
	@echo ""
	@echo "Make tools dirs ..."
	$(MKDIR) -p $(OBJ_DIR)
	$(MKDIR) -p $(OBJ_DIR)/tools
	$(MKDIR) -p $(LIB_DIR)
	@echo ""

# -----------------------------------------------------------------------------
# --- 'libbase.a'

# --- 'base' target
.PHONY : base
base: $(MISSING) base_dirs $(BASE_OBJ)
	@echo ""
	@echo "Make '$(LIB_DIR)/libbase.a' ..."
	$(AR) rc $(LIB_DIR)/libbase.a $(BASE_OBJ)
	$(RANLIB) $(LIB_DIR)/libbase.a

# --- create build directory
.PHONY : base_dirs
base_dirs: $(MISSING)
	@echo ""
	@echo "Make base dirs ..."
	$(MKDIR) -p $(OBJ_DIR)
	$(MKDIR) -p $(OBJ_DIR)/base
	$(MKDIR) -p $(LIB_DIR)
	@echo ""

# -----------------------------------------------------------------------------
# --- 'libmodule.a'

# --- 'module' target
.PHONY : modules
modules: $(MISSING) module_dirs $(MODULE_OBJ)
	@echo ""
	@echo "Make '$(LIB_DIR)/libmodule.a' ..."
	$(AR) rc $(LIB_DIR)/libmodule.a $(MODULE_OBJ)
	$(RANLIB) $(LIB_DIR)/libmodule.a

# --- create build directory
.PHONY : module_dirs
module_dirs: $(MISSING)
	@echo ""
	@echo "Make module dirs ..."
	$(MKDIR) -p $(OBJ_DIR)
	$(MKDIR) -p $(OBJ_DIR)/modules
	$(MKDIR) -p $(LIB_DIR)
	@echo ""

# -----------------------------------------------------------------------------

.PHONY : sockserv
sockserv: $(MISSING) sockserv_exe

.PHONY : sockserv_dirs
sockserv_dirs: $(MISSING)
	@echo ""
	@echo "Make socket 'sockserv' object dirs ..."
	$(MKDIR) -p $(OBJ_DIR)/server
	$(MKDIR) -p $(OBJ_DIR)/server/sock
	$(RM) -rf $(OBJ_DIR)/tools
	$(MKDIR) -p $(OBJ_DIR)/tools
	@echo ""

.PHONY : sockserv_exe
sockserv_exe: OPTIONS+=-DENABLE_SERVER_SOCKET
sockserv_exe: $(MISSING) sockserv_dirs $(TOOLS_OBJ) $(SKSERVE_OBJ)
	@echo ""
	@echo "Linking 'sockserv' ..."
	$(CC) -o $(OBJ_DIR)/server/sockserv$(EXE_EXT) $(CFLAGS) $(SOLIBS) $(TOOLS_OBJ) $(SKSERVE_OBJ) -lm -lpthread
	@echo ""
	@echo "Stripping 'sockserv' ..."
	$(STRIP) $(OBJ_DIR)/server/sockserv$(EXE_EXT)
	$(LS) -laF $(OBJ_DIR)/server/sockserv$(EXE_EXT)
	$(CP) $(OBJ_DIR)/server/sockserv$(EXE_EXT) $(BUILD_DIR)/sockserv$(EXE_EXT)
	@echo "+++++ Created 'sockserv' ..."
	@echo ""

# -----------------------------------------------------------------------------

.PHONY : scomserv
scomserv: $(MISSING) scomserv_exe

.PHONY : scomserv_dirs
scomserv_dirs: $(MISSING)
	@echo ""
	@echo "Make serial 'scomserv' object dirs ..."
	$(MKDIR) -p $(OBJ_DIR)/server
	$(MKDIR) -p $(OBJ_DIR)/server/serial
	@echo ""

.PHONY : scomserv_exe
scomserv_exe: $(MISSING) tools scomserv_dirs $(SCSERVE_OBJ)
	@echo ""
	@echo "Linking 'scomserv' ..."
	$(CC) -o $(OBJ_DIR)/server/scomserv$(EXE_EXT) $(CFLAGS) $(SOLIBS) $(SCSERVE_OBJ) -L$(LIB_DIR) -ltools -lm -lpthread
	@echo ""
	@echo "Stripping 'scomserv' ..."
	$(STRIP) $(OBJ_DIR)/server/scomserv$(EXE_EXT)
	$(LS) -laF $(OBJ_DIR)/server/scomserv$(EXE_EXT)
	$(CP) $(OBJ_DIR)/server/scomserv$(EXE_EXT) $(BUILD_DIR)/scomserv$(EXE_EXT)
	@echo "+++++ Created 'scomserv' ..."
	@echo ""

# -----------------------------------------------------------------------------

.PHONY : parsfile
parsfile: $(MISSING) parsefile

.PHONY : parsefile
parsefile: $(MISSING) parsefile_title parsefile_exe

# --- display 'parsefile' title
.PHONY : parsefile_title
parsefile_title: 
	@echo ""
	@echo "Making DMTP file packet parser ..."

# --- create build directory
.PHONY : parsefile_dirs
parsefile_dirs: $(MISSING)
	@echo ""
	@echo "Make parsefile object dirs ..."
	$(MKDIR) -p $(OBJ_DIR)/base
	$(MKDIR) -p $(OBJ_DIR)/parsfile
	@echo ""

# --- create binary 'parsfile'
.PHONY : parsefile_exe
parsefile_exe: $(MISSING) tools parsefile_dirs $(PARSFIL_OBJ)
	@echo ""
	@echo "Linking 'parsfile' ..."
	$(CC) -o $(OBJ_DIR)/parsfile/parsfile$(EXE_EXT) $(CFLAGS) $(SOLIBS) $(PARSFIL_OBJ) -L$(LIB_DIR) -ltools -lm
	@echo ""
	@echo "Stripping 'parsfile' ..."
	$(STRIP) $(OBJ_DIR)/parsfile/parsfile$(EXE_EXT)
	$(LS) -laF $(OBJ_DIR)/parsfile/parsfile$(EXE_EXT)
	$(CP) $(OBJ_DIR)/parsfile/parsfile$(EXE_EXT) $(BUILD_DIR)/parsfile$(EXE_EXT)
	@echo "+++++ Created 'parsfile' ..."
	@echo ""

# -----------------------------------------------------------------------------

.PHONY : encode
encode: $(MISSING) encode_title encode_exe

# --- display 'encode' title
.PHONY : encode_title
encode_title: 
	@echo ""
	@echo "Making DMTP upload file encoder ..."

# --- create 'encode' build directory
.PHONY : encode_dirs
encode_dirs: $(MISSING)
	@echo ""
	@echo "Make upload file encoder object dirs ..."
	$(MKDIR) -p $(OBJ_DIR)/encode
	@echo ""

# --- create binary 'encode'
.PHONY : encode_exe
encode_exe: $(MISSING) tools encode_dirs $(ENCODE_OBJ)
	@echo ""
	@echo "Linking 'encode' ..."
	$(CC) -o $(OBJ_DIR)/encode/encode$(EXE_EXT) $(CFLAGS) $(SOLIBS) $(ENCODE_OBJ) -L$(LIB_DIR) -ltools
	@echo ""
	@echo "Stripping 'encode' ..."
	$(STRIP) $(OBJ_DIR)/encode/encode$(EXE_EXT)
	$(LS) -laF $(OBJ_DIR)/encode/encode$(EXE_EXT)
	$(CP) $(OBJ_DIR)/encode/encode$(EXE_EXT) $(BUILD_DIR)/encode$(EXE_EXT)
	@echo "+++++ Created 'encode' ..."
	@echo ""

# -----------------------------------------------------------------------------

# --- create binary 'dmtpd' with default support
.PHONY : dmtp
dmtp: $(MISSING) dmtp_socket

# --- create binary 'dmtpd' with default support
.PHONY : dmtpd
dmtpd: $(MISSING) dmtp_socket

# --- create binary 'dmtpd' with File support
.PHONY : dmtp_file
dmtp_file: XPORT_MEDIA=-DTRANSPORT_MEDIA_FILE="dmtpdata.dmt"
dmtp_file: $(MISSING) libs
	$(MAKE) dest=$(dest) XPORT_MEDIA='-DTRANSPORT_MEDIA_FILE="dmtpdata.dmt"' -f src/custom/custom.mk dmtpd_all

# --- create binary 'dmtpd' with GPRS support
.PHONY : dmtp_gprs
dmtp_gprs: XPORT_MEDIA=-DTRANSPORT_MEDIA_GPRS
dmtp_gprs: $(MISSING) libs
	$(MAKE) dest=$(dest) XPORT_MEDIA=-DTRANSPORT_MEDIA_GPRS -f src/custom/custom.mk dmtpd_all

# --- create binary 'dmtpd' with Socket support
.PHONY : dmtp_socket
dmtp_socket: XPORT_MEDIA=-DTRANSPORT_MEDIA_SOCKET
dmtp_socket: $(MISSING) libs
	$(MAKE) dest=$(dest) XPORT_MEDIA=-DTRANSPORT_MEDIA_SOCKET -f src/custom/custom.mk dmtpd_all

# --- create binary 'dmtpd' with Serial support
.PHONY : dmtp_serial
dmtp_serial: XPORT_MEDIA=-DTRANSPORT_MEDIA_SERIAL
dmtp_serial: $(MISSING) libs
	$(MAKE) dest=$(dest) XPORT_MEDIA=-DTRANSPORT_MEDIA_SERIAL -f src/custom/custom.mk dmtpd_all

# -----------------------------------------------------------------------------

# --- 'make' test (test that everything builds)
# - NOTE: This doesn't build the Windows CE/Mobile packages (which requires eVC++)
.PHONY : maketest
maketest:
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean tools
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean dmtp_file
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean dmtp_gprs
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean dmtp_socket
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean dmtp_serial
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean sockserv
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean scomserv
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean parsefile
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean encode
	@echo; echo "----------------------------------------------------------------------"
	$(MAKE) clean
	@echo; echo "----------------------------------------------------------------------"

# -----------------------------------------------------------------------------
# ---

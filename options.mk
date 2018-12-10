
# --- compile time options (these will override the options specified in 'defaults.h')

USE_THREADS      := -DPROTOCOL_THREAD -DGPS_THREAD

#DEBUG_OPT       := -DDEBUG_COMPILE

OPTIONS := $(USE_THREADS)
ifdef DEBUG_OPT
OPTIONS += $(DEBUG_OPT)
endif
# ---

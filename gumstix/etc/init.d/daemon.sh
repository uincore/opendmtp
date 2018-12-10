#!/bin/sh # <-- placed here for ide parsing only
# daemon start/stop functions

# -----------------------------------------------------------------------------
# --- these variables/functions must be defined before including this module
# - $DAEMON         (for upgrade and 'start')
# - $DAEMON_ARGS    (for 'start' command)

# -----------------------------------------------------------------------------
# --- Some basic upgrade rules:
# - Upgrading over the top of an already upgraded system does no harm.
# - 

# -----------------------------------------------------------------------------
# --- destination directories of installed binaries on GumStix flash
EXPORT_BIN=/dmtp/bin
EXPORT_CONF=/dmtp/conf

# -----------------------------------------------------------------------------
# --- optional temporary upgrade script
DMTP_UPGRADE=dmtp_upgrade

# -----------------------------------------------------------------------------
# --- Start/Stop functions

DAEMON_PATH=$EXPORT_BIN/$DAEMON
DAEMON_PID=/var/run/$DAEMON.pid

# --- start daemon
start() {
    # External vars:
    #   $DAEMON
    #   $DAEMON_ARGS
    #   $DAEMON_PATH
    #   $DAEMON_PID
    # WARNING: This does not check to see if '$DAEMON' is already running!!!
    # pidof $DAEMON > /dev/null
    echo -n "Starting $DAEMON $DAEMON_ARGS: "
    if [ -x $DAEMON_PATH ] ; then
        #start-stop-daemon --start -x $DAEMON_PATH -p $DAEMON_PID
        #$DAEMON_PATH $DAEMON_ARGS > /dev/null 2>&1 &
        # --- the pre-sleep seems necessary to make sure that output is properly syslog'ed
        # --- otherwise no syslog output is generated (all is sent to the bit-bucket).
        ( sleep 10; $DAEMON_PATH $DAEMON_ARGS ) &
        #$DAEMON_PATH $DAEMON_ARGS &
        echo " ... started"
    else
        echo "Error '$DAEMON_PATH' does not exist."
    fi
}

# --- stop daemon
stop() {
    # External vars:
    #   $DAEMON
    echo -n "Stopping $DAEMON: "
    #start-stop-daemon --stop --name $DAEMON
    killall $DAEMON > /dev/null 2>&1 || true
    echo " ... stopped"
}

# --- restart daemon
restart() {
    stop
    start
}

# -----------------------------------------------------------------------------
# --- Upgrade functions

# --- location of binaries to be installed from local GumStix flash
INSTALL=/install

# --- location of binaries to be installed from SD/MMC flash
MMC_MOUNT=/mnt/mmc
MMC=$MMC_MOUNT/$INSTALL

# --- upgrade specified file
upgrade_file() {    # - <filePath> <executable>
    # External vars:
    #   $INSTALL
    #   $MMC
    filePath=$1     # - file path
    isExec=$2       # - 0 | 1
    #echo "File: $filePath [executable = $isExec]"
    installFilePath=$INSTALL/$filePath
    installFilePathGZ=$INSTALL/$filePath.gz
    mmcFilePath=$MMC/$filePath
    if [ -e "$mmcFilePath" -o -e "$installFilePath" -o -e "$installFilePathGZ" ] ; then
        echo -n "Upgrading $filePath: "
        # --- remove existing (if it exists)
        rm -f $filePath
        # --- upgrade to new version
        if [ -e "$mmcFilePath" ] ; then
            # --- copy from MMC flash 
            echo -n "MMC"
            cp $mmcFilePath $filePath
        else
            # --- unzip
            if [ -e "$installFilePathGZ" ] ; then
                # --- unzip 'FILE.gz', replacing 'FILE' if it exists
                echo -n "gunzip,"
                gunzip -f $installFilePathGZ
            fi
            # --- move from install directory 
            echo -n "install"
            mv $installFilePath $filePath
        fi
        # --- make sure 'install' version no longer exists
        rm -f $installFilePath $installFilePathGZ
        # --- make it's executable
        if [ "$isExec" -eq 1 ] ; then
            chmod a+x $filePath
        else
            chmod a-x $filePath
        fi
        echo " ... done"
    fi
}

# --- check for upgrade binaries
upgrade_daemon() {
    # External vars:
    #   $INSTALL
    #   $EXPORT_BIN
    #   $EXPORT_CONF
    #   $MMC_MOUNT
    daemon=$1
    if [ "$daemon" != "" ] ; then
        # --- '$daemon' MUST be stopped, or this will fail
    
        # --- make sure export/install directories exist
        mkdir -p $EXPORT_BIN
        mkdir -p $EXPORT_CONF
        mkdir -p $INSTALL/$EXPORT_BIN
        mkdir -p $INSTALL/$EXPORT_CONF
        
        # --- make sure the MMC flash is mounted (if it exists)
        mount $MMC_MOUNT > /dev/null 2>&1 || true
        sleep 1
        
        # --- upgrade
        if   [ -x "$MMC/$DMTP_UPGRADE" ] ; then
            # - MMC upgrade
            echo "Calling $MMC/$DMTP_UPGRADE ..."
            $MMC/$DMTP_UPGRADE # - (calling 'reboot' could cause an endless reboot loop!)
        elif [ -x "$INSTALL/$DMTP_UPGRADE" ] ; then
            # - Local flash upgrade (may reboot)
            echo "Calling $INSTALL/$DMTP_UPGRADE ..."
            cp $INSTALL/$DMTP_UPGRADE /tmp/$DMTP_UPGRADE
            rm -f $INSTALL/$DMTP_UPGRADE
            /tmp/$DMTP_UPGRADE # - may call 'reboot'
        else
            # - Standard upgrade.
            upgrade_file "$EXPORT_BIN/$daemon" 1
            upgrade_file "/etc/init.d/S25$daemon" 1
            upgrade_file "/etc/init.d/S60$daemon" 1
            upgrade_file "$EXPORT_CONF/$daemon.conf" 0
            #$EXPORT_BIN/$daemon -resetHostname # - this may not be necessary
        fi
        
    fi
}

# -----------------------------------------------------------------------------

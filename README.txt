-----------------------------------------------------------------------------------
Project: OpenDMTP Reference Implementation - C client 
URL    : http://www.opendmtp.org
File   : README.txt
-----------------------------------------------------------------------------------

This is version v1.2.3 update to the OpenDMTP C reference implementation.  This
release contains the following changes (see CHANGELOG.txt for detailed change 
history):
- Added additional logging for any errors returned by 'closedir', 'fflush', 'fclose'.
- Additional changes made to facilitate 'dual transport' support.
- Property 'PROP_COMM_HOST' no longer uses 'localhost' as a default.  Also, if 
  this property remains undefined during deployment, no events will be queued.
- When Geozone arrival/departure 'delay' is in effect, the generated event will
  be the time and GPS fix of the moment that the geozone boundary crossing was
  detected.
- See 'CHANGELOG.txt' for details.

Note: The OpenDMTP Protocol Specification manual, previously part of this package,
has been moved to its own separate downloadable SourceForge package at:
  http://sourceforge.net/project/showfiles.php?group_id=151031

README Contents:
   1) Introduction
   2) OpenDMTP Protocol Overview
   3) Supported Platforms
   4) Supported GPS Devices
   5) Quick-Start Build/Installation
   6) Building OpenDMTP for the GumStix Platform
   7) Using a GPRS Modem
   8) Customizing for Specific Applications
   9) The DMTP Server
  10) Source Directory Layout
  11) Future OpenDMTP Reference Implementation Features
  12) Frequently Asked Questions
  13) Contact Info
  
-----------------------------------------------------------------------------------
1) Introduction:

The "Open Device Monitoring and Tracking Protocol", otherwise known as OpenDMTP(tm), 
is a protocol and framework that allows bi-directional data communications between 
servers and client devices over the Internet and similar networks.  OpenDMTP is 
particularly geared towards location-based information, such as GPS, as well as 
other types of data collected in remote-monitoring devices. OpenDMTP is small, and 
is especially suited for micro-devices such as PDA's, mobile phones, and custom OEM 
devices.

OpenDMTP is a protocol definition and is therefore separate from the implementation
in any specific language.  At this time the OpenDMTP client/server protocol has been
implemented in C, Java, and PHP.  This package includes a reference implementation
of the OpenDMTP protocol written in C, and is intended to be used as a development
"kit" for creating customized feature-rich device monitoring and tracking systems.  
A full reference implementation is included as a starting pointt that provides the 
following motion and odometer features:
  - Start-of-motion events.
  - In-motion events.
  - Stopped-motion events.
  - Dormant events (as a result of non-motion).
  - Odometer events.
  - Geofence arrival/departure.

Here are a few example applications that describe how OpenDMTP can be used:
  - Track the location of your vehicle every minute while it is in motion.  While
    it is stopped, have the vehicle report once per hour.
  - Have your vehicle notify you every 4K miles when it is time to change the oil.
  - Have your vehicle notify you when it arrives at a particular destination.
  - Collect and report telemetry data, such as ice-box temperatures, or fuel level,
    and generate an event when the values are outside of an acceptable range.
  - Have location based data transmitted back to a central server over GPRS for 
    immediate query on a map.
  - Retain the collected data in the vehicle until it returns home, then download
    the collected data via WiFi or BlueTooth.
  - Collect vehicle travel history on an SD flash card, then remove the card from
    the device and place it in a PC for viewing on a Map.

-----------------------------------------------------------------------------------
2) OpenDMTP Protocol Overview:

The protocol was specifically designed as a means to communicate with remote
trackable devices over a high-latency/low-bandwidth media.  Here is a brief
list of some of the supported DMTP protocol features:
  - A Simple set of packet types that provide a rich set of features.  See the 
    source file "base/packet.h" for a list of client and server packet types.
  - Supports both binary and ascii encodings (Base64 and Hex).  Based on the 
    transport media used, and the specific requirements, the client can decide 
    which packet encoding it wishes to use.
  - Supports both Duplex and Simplex data communication.  Duplex connections
    allow the server to provide acknowledgment and reconfiguration information
    to the client, however Simplex connections (eg. UDP) may be desirable or 
    even necessary in some circumstances.
  - Supports periodic, intermittent, and dedicated connections types.
    Whether communicating periodically over a wireless GSM/GPRS media, or
    intermittently over a BlueTooth media, the protocol is adaptable to all
    connection types.
  - Highly extensible.  New client property types, transport media types, and
    GPS rule modules, can easily be supported in the protocol.
  - Rich set of remotely settable client-side properties.  Including setting
    many different types of configuration properties or sending special commands
    to the client. See the source file "base/props.h" for a list of the standard
    property types.
  - Supports custom event packets and custom event packet negotiation. Different 
    applications will need different types of data from the client device.  The 
    client side of the protocol provides for custom 'event' packet definitions
    containing only the data it needs to transmit.  If the server does not 
    understand a clients custom 'event' packet, the protocol provides a means for 
    the client to supply a custom definition template to the server so that the 
    server is able to accurately parse the custom packets.  This is known as 
    "custom event packet negotiation".

The OpenDMTP protocol definition reference manual can be found in a separate 
"protocol-spec" package which can be downloaded from SourceForge at this location:
  http://sourceforge.net/project/showfiles.php?group_id=151031

-----------------------------------------------------------------------------------
3) Supported Platforms:

This reference implementation is currently designed to compile on Linux and Cygwin 
platforms using the 'gcc' compiler, and has been tested on several Linux 
distributions (including embedded devices) as well as Windows XP (with Cygwin 
installed).

This reference implementation now also will build on Window CE/Mobile platforms
using the Microsoft eMbedded Visual C++ compiler.   See the included document
"HW6945.txt" for information regarding how to build the OpenDMTP reference 
implementation for the new Hewlett Packard hw6945 phone with an embedded GPS
receiver.

If you have another platform that you would like the DMTP client to run on, please 
let us know and we'll work together to make this happen.

-----------------------------------------------------------------------------------
4) Supported GPS Devices:

The means of obtaining GPS information is not defined by the protocol.  However,
for this reference implementation, any GPS device that outputs standard NMEA-0183 
compliant GPS records over an RS232 serial port can be used.  USB GPS modules can 
be supported as well provided the operating system provides an RS232 emulation 
driver and can assign the USB device to a local RS232 port name.

Example RS232 device(s);
- Garmin GPS18PC with a cigarette lighter adapter for power, and a DB9 serial
  interface connector.

Example USB device(s):
- Microsoft branded Pharos "GPS-360":  Works as-is on Windows XP (via serial port
  interface).  Also works under Linux using the "pl2303" USB Serial kernel driver 
  with modern Linux kernels.
- Other USB GPS devices using the Prolific PL2303 chip should also work under Linux 
  using the "pl2303" driver [reportedly working devices can be found from Aten (the 
  UC-232) and IO-Data].

-----------------------------------------------------------------------------------
5) Quick-Start Build/Installation:

  1) Unzip the OpenDMTP C client package in a convenient directory.
  2) Compile the application for your target environment.  Examples:
      - To compile on Linux enter "make dest=linux dmtp"
      - To compile on Cygwin enter "make dest=cygwin dmtp"
  3) Plug in a supported GPS device into a known RS232 serial port.
  4) Start the dmtp program as follows:
       ./build_lin/dmtpd -debug -pf debug.conf -gps <port>
     or  
       ./build_cyg/dmtpd -debug -pf debug.conf -gps <port>
     Where "<port>" is the serial port of the GPS module specified in the form
     com3, com4, etc., or ttyS2, ttyS3, etc.   The option "-debug" and the option
     "-pf debug.conf" (which loads the debug property file removing the connection
     restrictions) are for debug/testing use only and should be removed when used
     in a production environment.
  5) As shipped, start/stop and in-motion packets will be sent to the file 
     "dmtpdata.dmt" in the current directory. (Drive around with a laptop to obtain 
     tracking information).  A file parser is also provided by compiling the make 
     target 'parsfile' (eg. "make dest=linux parsfile").  This command will parse
     the event packets found in the file and generate CSV records containing the
     event data.  Run the command with the '-help' option (e.g. "parsfile -h") for 
     additional information. (You may modify the code as necessary to output the
     fields and format needed for your application).
  6) If using a socket or GPRS based communication with a server, then you may
     wish to relax the default "connection accounting" restrictions when debugging
     your application.   The property defaults can be overridden by placing a file 
     named "props.conf" in the current directory with "<PropertyKey>=<Value>" 
     entries (see 'propman.c' for a full list of valid <PropertyKey> names).  For 
     debugging purposes, create a "props.conf" file with the following contents:
        # --- connection parameters
        # - maximum number of total connections, duplex connections, and time frame
        # - ("1,1,0" means "no maximum")
        com.maxconn=1,1,0
        # - absolute minimum time delay between transmissions
        com.mindelay=0
        # - minimum time delay between transmissions for non-critical events
        com.minrate=0 
     Use this ONLY for debugging purposes, and not for production use. 
     The default property override config file name is "props.conf", however this 
     can be changed on the command line by adding the argument "-pfile ./props.conf" 
     and changing "./props.conf" to the name of the file you wish to load instead 
     of the default.

Thread support is enabled by default (in "make/options.mk"), and 'malloc' is used 
for memory allocation in some of the source modules.  These features, and others, 
can be enabled/changed by modifying the header file 'custom/defaults.h', or the
common makefile "make/common.h", and re-compiling the DMTP client executable.

-----------------------------------------------------------------------------------
6) Building OpenDMTP for the GumStix Platform:

The GumStix single board computer (http://www.gumstix.com) makes a perfect reference 
platform for the OpenDMTP protocol and reference implementation.  With the addition 
of a GPS device, it can be used to record tracking information which can be 
transmitted to a back-end server, sent over BlueTooth, or recorded onto an SD flash 
card.  GumStix is planning on releasing a daughter board soon with an on-board GPS
receiver that is ready made for the OpenDMTP reference implementation.

The OpenDMTP reference implementation already provides build targets for the
GumStix platform.  To build the OpenDMTP reference implementation for the GumStix 
platform, please refer to the included document "gumstix/GumStix.txt".

-----------------------------------------------------------------------------------
7) Using a GPRS Modem:

Support for transmitting collected data wirelessly over a GPRS modem is available 
in this reference implementation.  To build the OpenDMTP reference implementation 
with this feature turned on, change the "custom/defaults.h" file to uncomment the 
compile time variable "TRANSPORT_MEDIA_GPRS", then compile the client as indicated 
above.  (Note: "dmtp_gprs" is also an acceptable make target to do the same.)
Then start the 'dmtpd' client with the name of of the serial port were the GPRS 
modem is attached.  For example:
   % dmtpd -gps <port> -gprs <gprs_modem_port> -server <dmtp_server> <dmtp_port>
   
Currently, the specific GPRS modem supported is the BlueTree M2M Epress GPRS.
Internally this is a Wavecom modem, thus it may be possible to use any Wavecom 
GPRS modem, but this has not been verified.

-----------------------------------------------------------------------------------
8) Customizing for Specific Applications:

This reference implementation is highly customizable for many different types of 
devices and requirements.  This reference implementation currently ships with 
support for communicating with a DMTP service provider over a straight socket, a
GPRS modem, or just simply writing GPS events to a local file (the default 
configuration specified in 'default.h'). However, many other transports media types 
are also possible, such as communicating over BlueTooth to another device.

To implement your own customized configuration, start with the following files:
   src/custom/defaults.h
   src/custom/startup.c

To implement new transport support, add the files to custom/transport/. and modify 
the following files accordingly:
   src/custom/defaults.h
   src/custom/startup.c
   src/custom/transport.c
   src/custom/transport.h

For instance, to build the reference implementation with socket support modify 
'defaults.h' and uncomment 'TRANSPORT_MEDIA_SOCKET', then recompile. (The host:port 
definitions currently defaults to "localhost" and port 31000, and can be changed in
the source file "custom/startup.c").  A simple socket based server is also provided 
by compiling the make target 'sockserv'.

-----------------------------------------------------------------------------------
9) The DMTP Socket Server:

A simple TCP socket transport server is provided in this reference implementation
to allow testing the DMTP client socket transport.  This server implementation 
currently accepts only a single DMTP client connect at a time, and does not support
custom event packet negotiation (however, custom event packet types can be added
explicitly to the server if necessary).

To build the DMTP TCP server, issue the command "make dest=linux sockserv" (or
specify another destination platform as necessary).  The server can be started
with the following command:
    sockserv -tcp <socketPort>
Where <socketPort> is the local port to which to bind and make the service
available.  This port must be the same as the client will be using to transmit
data (the default port used in the client is 31000).

The 'sockserv' server currently writes received events to the file "sockserv.dmt"
in the current directory, however this can be overridden with the "-output" option:
    sockserv -tcp <socketPort> -output myfile.dmt
In this configuration, the data written to this file is parsable with the command
'parsfile' as described above.  To configure 'sockserv' to write events in CSV
format specify 'csv' after the filename:
    sockserv -tcp <socketPort> -output myfile.csv csv

The DMTP client must also be configured to reference the host that the server is
running on.  Typically, when testing and debugging connections, this will be
"localhost".

Since this server only supports TCP connections, the client should insure that
only 'Duplex' connections are used to transmit data. This reference implementation
provides a simple way to force duplex-only connections by simply supplying the
argument '-duplex' as a command-line option when starting the DMTP client.

-----------------------------------------------------------------------------------
10) Source Directory Layout:

./src/
    DMTP source directory.

./src/base/
    Basic DMTP client-side support source files.

./src/modules/
    Standard GPS event modules.
    - Motion and Odometer modules are provided in the reference implementation.

./src/tools/
    Miscellaneous tools and utilities.

./src/custom/
    Custom DMTP reference implementation.

./src/custom/transport/
    Custom transport layer implementation.
    - File and Socket transports are provided in the reference implementation.

./src/parsfile/
    Stand-alone support for parsing DMTP event packets from a file.
    - All non event packets are ignored.
    - Custom event packet formats are not currently implemented.
    - This implementation supports all packet encoding formats.
    
./src/server/
    Common server support source files.

./src/server/serial
    Simple stand-alone serial port transport server.
    - Suitable for use with serial port communications
    - Custom event packet negotiation is not implemented.
    - File download is supported.

./src/server/sock
    Simple stand-alone socket transport server.
    - This server provides a simple TCP socket transport (UDP not yet implemented).
    - Custom event packet negotiation is not implemented.
    - File download is not fully supported.

-------------------------------------------------------------------------------
11) Future OpenDMTP Reference Implementation Features:

The following items are also in the works for future OpenDMTP support:
  - Additional platform support.
  - Free DMTP Internet service providing Internet access to GPS tracking data.
  - Etc!

-------------------------------------------------------------------------------
12) Troubleshooting:

Here are a list of a few of the most frequently asked questions:

- Q: I've started it up and it appears to be working, but I'm not getting any
     GPS fixes.
  A: Several possibilities here.  Make sure that the GPS receiver itself is
     working.   Use a terminal emulator, such as Hyper terminal, to test that
     you are seeing valid GPS 'fix' information coming from the receiver.
     Also check to see that the communication settings aren't so restrictive that
     they aren't allowing data to be transmitted for extended periods of time.
     The config file 'debug.conf' is included that will remove all communication
     restrictions for testing and debugging purposes.
     
- Q: I've tried compiling for file transport media, then tried compiling for 
     socket based transport media, but the client is no longer running properly.
  A: Make sure you always issue a "make clean" when switching from one type of
     transport media to another.  Some object files may be left around that 
     may need to be recomplied when switching to a different transport media.

- Q: Can the protocol be extended to add new status types?
  A: Absolutely, the protocol was designed specifically with extensibility in
     mind.  New status types and even custom event packet definitions can 
     easily be added to the OpenDMTP implementation.
     
- Q: Do you already have OpenDMTP working on device XXXXXX?
  A: Probably not yet.  There are hundreds of devices out there, most already 
     have their own proprietary protocol, and we are learning about new devices
     every day.  For OpenDMTP to work on a device, it will likely need the 
     cooperation of the hardware manufacturer.  We see a lot of devices that 
     have a less-than-optimal communication protocol, and we are attempting to
     work with those we can to get OpenDMTP implemented on them.
     
- Q: I see that your example OpenDMTP tracking is using Google Maps. Can it use
     other mapping services as well?
  A: OpenDMTP is a protocol definition and is actually independent of any back
     end support services.  Google Map was used show to showcase the features of
     OpenDMTP because it provides a great mapping platform with a great API.  
     However, with the proper interface, any web-based mapping service can also
     be used, provided you have the proper subscription to their service and 
     have implemented their custom mapping API.

- Q: Can OpenDMTP be used for in-car navigation?
  A: While OpenDMTP probably can be used for such a platform, there are other
     possibly better equipped systems and protocols out there for providing that 
     service.  OpenDMTP was designed to efficiently transmit GPS based events to 
     a remote server (via GPRS, 802.11, BlueTooth, etc) for external analysis.  
     In-car navigation typically requires more elaborate hardware with a display 
     screen, internal mapping database, and possibly a keyboard, and protocol
     efficiency may not be as much of an issue.

-------------------------------------------------------------------------------
13) Contact Info:

Please feel free to contact us regarding questions on this package.  Or if you
would like to be be kept up to date on the progress of this project, please let
us know.

Thanks,
Martin D. Flynn
devstaff@opendmtp.org

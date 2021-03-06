#!/usr/bin/env python3
#-----------------------------------------------------------------------------
# Title      : PyRogue Server
#-----------------------------------------------------------------------------
# File       : python/pyrogue_server.py
# Created    : 2017-06-20
#-----------------------------------------------------------------------------
# Description:
# Python script to start a PyRogue Control Server
#-----------------------------------------------------------------------------
# This file is part of the pyrogue-control-server software platform. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the rogue software platform, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------
import sys
import getopt
import socket
import os
import subprocess
import time
import struct
from packaging import version
from pathlib import Path
import re

import pyrogue
import pyrogue.utilities.fileio
import rogue.interfaces.stream
import Smurf

PIDFILE = '/tmp/smurf.pid'

# Print the usage message
def usage(name):
    # Number of space of the string "Usage: {name} ". Use to align the following lines.
    num_spaces=len(name) + 8

    print("Usage: {} [-a|--addr IP_address] [-d|--defaults config_file]".format(name))
    print("{s: <{c}}[-s|--server] [-p|--pyro group_name] [-e|--epics prefix]".format(s='', c=num_spaces))
    print("{s: <{c}}[-n|--nopoll] [-b|--stream-size byte_size] [-f|--stream-type data_type]".format(s='', c=num_spaces))
    print("{s: <{c}}[-c|--commType comm_type] [-l|--pcie-rssi-link index] [-b|--stream-size data_size]".format(s='', c=num_spaces))
    print("{s: <{c}}[-f|--stream-type data_type] [-u|--dump-pvs file_name] [--disable-bay0]".format(s='', c=num_spaces))
    print("{s: <{c}}[--disable-bay1] [--disable-gc] [-w|--windows-title title] [--pcie-dev pice_device]".format(s='', c=num_spaces))
    print("{s: <{c}}[-h|--help]".format(s='', c=num_spaces))
    print("")
    print("    -h|--help                  : Show this message")
    print("    -a|--addr IP_address       : FPGA IP address. Required when"\
        "the communication type is based on Ethernet.")
    print("    -d|--defaults config_file  : Default configuration file")
    print("    -p|--pyro group_name       : Start a Pyro4 server with",\
        "group name \"group_name\"")
    print("    -e|--epics prefix          : Start an EPICS server with",\
        "PV name prefix \"prefix\"")
    print("    -s|--server                : Server mode, without staring",\
        "a GUI (Must be used with -p and/or -e)")
    print("    -n|--nopoll                : Disable all polling")
    print("    -c|--commType comm_type    : Communication type with the FPGA",\
        "(defaults to \"eth-rssi-non-interleaved\"")
    print("    -l|--pcie-rssi-link index  : PCIe RSSI link (only needed with"\
        "PCIe). Supported values are 0 to 5")
    print("    -b|--stream-size data_size : Expose the stream data as EPICS",\
        "PVs. Only the first \"data_size\" points will be exposed.",\
        "(Must be used with -e)")
    print("    -f|--stream-type data_type : Stream data type (UInt16, Int16,",\
        "UInt32 or Int32). Default is UInt16. (Must be used with -e and -b)")
    print("    -u|--dump-pvs file_name    : Dump the PV list to \"file_name\".",\
        "(Must be used with -e)")
    print("    --disable-bay0             : Disable the instantiation of the"\
        "devices for Bay0")
    print("    --disable-bay1             : Disable the instantiation of the"\
        "devices for Bay1")
    print("    --disable-gc               : Disable python's garbage collection"\
        "(enabled by default)")
    print("    -w|--windows-title title   : Set the GUI windows title. If not"\
        "specified, the default windows title will be the name of this script."\
        "This value will be ignored when running in server mode.")
    print("    --pcie-dev pice_device     : Set the PCIe card device name"\
        "(defaults to '/dev/datadev_0')")
    print("")
    print("Examples:")
    print("    {} -a IP_address                            :".format(name),\
        " Start a local rogue server, with GUI, without Pyro nor EPICS servers")
    print("    {} -a IP_address -e prefix                  :".format(name),\
        " Start a local rogue server, with GUI, with EPICS server")
    print("    {} -a IP_address -e prefix -p group_name -s :".format(name),\
        " Start a local rogure server, without GUI, with Pyro and EPICS servers")
    print("")

# Cretae gui interface
def create_gui(root, title=""):
    app_top = pyrogue.gui.application(sys.argv)
    app_top.setApplicationName(title)
    gui_top = pyrogue.gui.GuiTop(group='GuiTop')
    gui_top.addTree(root)
    print("Starting GUI...\n")

    try:
        app_top.exec_()
    except KeyboardInterrupt:
        # Catch keyboard interrupts while the GUI was open
        pass

    print("GUI was closed...")

# Exit with a error message
def exit_message(message):
    print(message)
    print("")
    exit()

# Get the hostname of this PC
def get_host_name():
    return subprocess.check_output("hostname").strip().decode("utf-8")

class DataBuffer(rogue.interfaces.stream.Slave):
    """
    Data buffer class use to capture data comming from the stream FIFO \
    and copy it into a local buffer using a especific data format.
    """
    def __init__(self, size, data_type):
        rogue.interfaces.stream.Slave.__init__(self)
        self._buf = [0] * size

        # Supported data format and byte order
        self._data_format_dict = {
            'B': 'unsigned 8-bit',
            'b': 'signed 8-bit',
            'H': 'unsigned 16-bit',
            'h': 'signed 16-bit',
            'I': 'unsigned 32-bit',
            'i': 'signed 32-bit'}

        self._data_byte_order_dict = {
            '<': 'little-endian',
            '>': 'big-endian'}

        # Get data format and size from data type
        if data_type == 'UInt16':
            self._data_format = 'H'
            self._data_size = 2
        elif data_type == 'Int16':
            self._data_format = 'h'
            self._data_size = 2
        elif data_type == 'UInt32':
            self._data_format = 'I'
            self._data_size = 4
        else:
            self._data_format = 'i'
            self._data_size = 4

        # Byte order: LE
        self._data_byte_order = '<'

        # Callback function
        self._callback = lambda: None

    def _acceptFrame(self, frame):
        """
        This method is called when a stream frame is received
        """
        data = bytearray(frame.getPayload())
        frame.read(data, 0)
        self._buf = struct.unpack('{}{}{}'.format((self._data_byte_order, \
            (len(data)//self._data_size), self._data_format), data))
        self._callback()

    def set_callback(self, callback):
        """
        Function to set the callback function
        """
        self._callback = callback

    def read(self):
        """
        Function to read the data buffer
        """
        return self._buf

    def get_data_format_string(self):
        """
        Function to get the current format string
        """
        return '{}{}'.format(self._data_byte_order, self._data_format)

    def get_data_format_list(self):
        """
        Function to get a list of supported data formats
        """
        return list(self._data_format_dict.values())

    def get_data_byte_order_list(self):
        """
        Function to get a list of supported data byte order options
        """
        return list(self._data_byte_order_dict.values())

    def set_data_format(self, dev, var, value):
        """
        Function to set the data format
        """
        if (value < len(self._data_format_dict)):
            data_format = (list(self._data_format_dict)[value])
            if data_format == 'B' or data_format == 'b':      # uint8, int8
                self._data_format = data_format
                self._data_size = 1
            elif data_format == 'H' or  data_format == 'h':     # uint16, int16
                self._data_format = data_format
                self._data_size = 2
            elif data_format == 'I' or data_format == 'i':    # uint32, int32
                self._data_format = data_format
                self._data_size = 4

    def get_data_format(self):
        """
        Function to read the data format
        """
        return list(self._data_format_dict).index(self._data_format)

    def set_data_byte_order(self, dev, var, value):
        """
        Function to set the data byte order
        """
        if (value < len(self._data_byte_order_dict)):
            self._data_byte_order = list(self._data_byte_order_dict)[value]

    def get_data_byte_order(self):
        """
        Function to read the data byte order
        """
        return list(self._data_byte_order_dict).index(self._data_byte_order)

class LocalServer(pyrogue.Root):
    """
    Local Server class. This class configure the whole rogue application.
    """
    def __init__(self, ip_addr, config_file, server_mode, group_name, epics_prefix,\
        polling_en, comm_type, pcie_rssi_link, stream_pv_size, stream_pv_type,\
        pv_dump_file, disable_bay0, disable_bay1, disable_gc, windows_title, pcie_dev):

        try:
            pyrogue.Root.__init__(self, name='AMCc', description='AMC Carrier')

            # File writer for streaming interfaces
            # DDR interface (TDEST 0x80 - 0x87)
            stm_data_writer = pyrogue.utilities.fileio.StreamWriter(name='streamDataWriter')
            self.add(stm_data_writer)
            # Streaming interface (TDEST 0xC0 - 0xC7)
            stm_interface_writer = pyrogue.utilities.fileio.StreamWriter(name='streamingInterface')
            self.add(stm_interface_writer)

            # Workaround to FpgaTopLelevel not supporting rssi = None
            if pcie_rssi_link == None:
                pcie_rssi_link = 0

            # Instantiate Fpga top level
            fpga = FpgaTopLevel(ipAddr=ip_addr,
                commType=comm_type,
                pcieRssiLink=pcie_rssi_link,
                disableBay0=disable_bay0,
                disableBay1=disable_bay1)

            # Add devices
            self.add(fpga)

            # Create stream interfaces
            self.ddr_streams = []       # DDR streams
            self.streaming_streams = [] # Streaming interface streams

            # If the packetizer is being used, the FpgaTopLevel class will defined a 'stream' interface exposing it.
            # Otherwise, we are using DMA engine without packetizer. Create the stream interface accordingly.
            if hasattr(fpga, 'stream'):
                for i in range(8):
                    self.ddr_streams.append(fpga.stream.application(0x80 + i))
                    self.streaming_streams.append(fpga.stream.application(0xC0 + i))
            else:
                for i in range(8):
                    self.ddr_streams.append(rogue.hardware.axi.AxiStreamDma(pcie_dev,(pcie_rssi_link*0x100 + 0x80 + i), True))
                    self.streaming_streams.append(rogue.hardware.axi.AxiStreamDma(pcie_dev,(pcie_rssi_link*0x100 + 0xC0 + i), True))

            # Our smurf_processor receiver
            # The data stream comes from TDEST 0xC1
            # We use a FIFO between the stream data and the receiver:
            # Stream -> FIFO -> smurf_processor receiver
            self.smurf_processor = Smurf.SmurfProcessor()
            self.smurf_processor.setDebug( False )
            self.smurf_processor_fifo = rogue.interfaces.stream.Fifo(1000,0,True)
            pyrogue.streamConnect(self.streaming_streams[1], self.smurf_processor_fifo)
            pyrogue.streamConnect(self.smurf_processor_fifo, self.smurf_processor)

            # Add data streams (0-7) to file channels (0-7)
            for i in range(8):

                ## DDR streams
                pyrogue.streamConnect(self.ddr_streams[i],
                    stm_data_writer.getChannel(i))

                ## Streaming interface streams

                # We have already connected TDEST 0xC1 to the smurf_processor receiver,
                # so we need to tapping it to the data writer.
                if i == 1:
                    pyrogue.streamTap(self.streaming_streams[i],
                        stm_interface_writer.getChannel(i))
                # The rest of channels are connected directly to the data writer.
                else:
                    pyrogue.streamConnect(self.streaming_streams[i],
                        stm_interface_writer.getChannel(i))

            # Look for the TesBias registers
            # TesBias register are located on
            # FpgaTopLevel.AppTop.AppCore.RtmCryoDet.RtmSpiMax
            # And their name is TesBiasDacDataRegCh[n], where x = [0:31]
            self.TestBiasVars = []
            self.TestBiasRegEx = re.compile('.*TesBiasDacDataRegCh\[(\d+)\]$')
            for var in self.FpgaTopLevel.AppTop.AppCore.RtmCryoDet.RtmSpiMax.variableList:
                m = self.TestBiasRegEx.match(var.name)
                if m:
                    reg_index = int(m[1]) - 1
                    if reg_index < 32:
                        print(f'Found TesBias register: {var.name}, with index {reg_index}')
                        self.TestBiasVars.append(var)

            # Check that we have all 32 TesBias registers
            if len(self.TestBiasVars) == 32:
                print(f'Found 32 TesBias registers. Assigning listener functions')
                # Add listener to the TesBias registers
                for var in self.TestBiasVars:
                    var.addListener(self.send_test_bias)
                # Prepare a buffer to holds the TesBias register values
                self.TesBiasValue = [0] * 32
            else:
                print(f'Error: {len(self.TestBiasVars)} TesBias register were found instead of 32. Aborting')

            # Run control for streaming interfaces
            self.add(pyrogue.RunControl(
                name='streamRunControl',
                description='Run controller',
                cmd=fpga.SwDaqMuxTrig,
                rates={
                    1:  '1 Hz',
                    10: '10 Hz',
                    30: '30 Hz'}))

            # PVs for stream data, used on PCAS-based EPICS server
            if epics_prefix and stream_pv_size:
                if use_pcas:

                    print("Enabling stream data on PVs (buffer size = {} points, data type = {})"\
                        .format(stream_pv_size,stream_pv_type))

                    # Add data streams (0-7) to local variables so they are expose as PVs
                    # Also add PVs to select the data format
                    self.stream_fifos = []
                    self.data_buffers = []
                    for i in range(8):

                        # Calculate number of bytes needed on the fifo
                        if '16' in stream_pv_type:
                            fifo_size = stream_pv_size * 2
                        else:
                            fifo_size = stream_pv_size * 4

                        # Setup a FIFO tapped to the steram data and a Slave data buffer
                        # Local variables will talk to the data buffer directly.
                        self.stream_fifos.append(rogue.interfaces.stream.Fifo(0, fifo_size, 0))
                        stream_fifo = self.stream_fifos[i]

                        self.data_buffers.append(DataBuffer(size=stream_pv_size, data_type=stream_pv_type))
                        data_buffer = self.data_buffers[i]

                        stream_fifo._setSlave(data_buffer)

                        #pyrogue.streamTap(fpga.stream.application(0x80 + i), stream_fifo)

                        # Variable to read the stream data
                        stream_var = pyrogue.LocalVariable(
                            name='Stream{}'.format(i),
                            description='Stream {}'.format(i),
                            mode='RO',
                            value=0,
                            localGet=data_buffer.read,
                            update=False,
                            hidden=True)

                        # Set the buffer callback to update the variable
                        data_buffer.set_callback(stream_var.updated)

                        # Variable to set the data format
                        data_format_var = pyrogue.LocalVariable(
                            name='StreamDataFormat{}'.format(i),
                            description='Type of data being unpacked',
                            mode='RW',
                            value=0,
                            enum={i:j for i,j in enumerate(data_buffer.get_data_format_list())},
                            localSet=data_buffer.set_data_format,
                            localGet=data_buffer.get_data_format,
                            hidden=True)

                        # Variable to set the data byte order
                        byte_order_var = pyrogue.LocalVariable(
                            name='StreamDataByteOrder{}'.format(i),
                            description='Byte order of data being unpacked',
                            mode='RW',
                            value=0,
                            enum={i:j for i,j in enumerate(data_buffer.get_data_byte_order_list())},
                            localSet=data_buffer.set_data_byte_order,
                            localGet=data_buffer.get_data_byte_order,
                            hidden=True)

                        # Variable to read the data format string
                        format_string_var = pyrogue.LocalVariable(
                            name='StreamDataFormatString{}'.format(i),
                            description='Format string used to unpack the data',
                            mode='RO',
                            value=0,
                            localGet=data_buffer.get_data_format_string,
                            hidden=True)

                        # Add listener to update the format string readback variable
                        # when the data format or data byte order is changed
                        data_format_var.addListener(format_string_var)
                        byte_order_var.addListener(format_string_var)

                        # Add the local variable to self
                        self.add(stream_var)
                        self.add(data_format_var)
                        self.add(byte_order_var)
                        self.add(format_string_var)

            # lcaPut limits the maximun lenght of a string to 40 chars, as defined
            # in the EPICS R3.14 CA reference manual. This won't allowed to use the
            # command 'ReadConfig' with a long file path, which is usually the case.
            # This function is a workaround to that problem. Fomr matlab one can
            # just call this function without arguments an the function ReadConfig
            # will be called with a predefined file passed during startup
            # However, it can be usefull also win the GUI, so it is always added.
            self.config_file = config_file
            self.add(pyrogue.LocalCommand(
                name='setDefaults',
                description='Set default configuration',
                function=self.set_defaults_cmd))

            # If Garbage collection was disable, add this local variable to allow users
            # to manually run the garbage collection.
            if disable_gc:
                self.add(pyrogue.LocalCommand(
                    name='runGarbageCollection',
                    description='runGarbageCollection',
                    function=self.run_garbage_collection))

            self.add(pyrogue.LocalVariable(
                name='smurfProcessorDebug',
                description='Enable smurf processor transmit debug',
                mode='RW',
                value=False,
                localSet=lambda value: self.smurf_processor.setDebug(value),
                hidden=False))

            # Lost frame counter from smurf_processor
            self.add(pyrogue.LocalVariable(
                name='frameLossCnt',
                description='Lost frame Counter',
                mode='RO',
                value=0,
                localGet=self.smurf_processor.getFrameLossCnt,
                pollInterval=1,
                hidden=False))

            # Received frame counter from smurf_processor
            self.add(pyrogue.LocalVariable(
                name='frameRxCnt',
                description='Received frame Counter',
                mode='RO',
                value=0,
                localGet=self.smurf_processor.getFrameRxCnt,
                pollInterval=1,
                hidden=False))

            # Out-of-order frame counter from smurf_processor
            self.add(pyrogue.LocalVariable(
                name='frameOutOrderCnt',
                description='Number of time out-of-order frames are detected',
                mode='RO',
                value=0,
                localGet=self.smurf_processor.getFrameOutOrderCnt,
                pollInterval=1,
                hidden=False))

            # Command to clear all the frame counters on smurf_processor
            self.add(pyrogue.LocalCommand(
                name='clearFrameCnt',
                description='Clear all frame counters',
                function=self.smurf_processor.clearFrameCnt))

            # Start the root
            if group_name:
                # Start with Pyro4 server
                host_name = get_host_name()
                print("Starting rogue server with Pyro using group name \"{}\"".format(group_name))
                self.start(pollEn=polling_en, pyroGroup=group_name, pyroHost=host_name, pyroNs=None)
            else:
                # Start without Pyro4 server
                print("Starting rogue server")
                self.start(pollEn=polling_en)

            self.ReadAll()

            # Call the get() method on the tesBias variable to force the call to
            # send_test_bias and update the array in Smurf2MCE
            for var in self.TestBiasVars:
                var.get()

        except KeyboardInterrupt:
            print("Killing server creation...")
            super(LocalServer, self).stop()
            exit()

        # Show image build information
        try:
            print("")
            print("FPGA image build information:")
            print("===================================")
            print("BuildStamp              : {}"\
                .format(self.FpgaTopLevel.AmcCarrierCore.AxiVersion.BuildStamp.get()))
            print("FPGA Version            : 0x{:x}"\
                .format(self.FpgaTopLevel.AmcCarrierCore.AxiVersion.FpgaVersion.get()))
            print("Git hash                : 0x{:x}"\
                .format(self.FpgaTopLevel.AmcCarrierCore.AxiVersion.GitHash.get()))
        except AttributeError as attr_error:
            print("Attibute error: {}".format(attr_error))
        print("")

        # Start the EPICS server
        if epics_prefix:
            print("Starting EPICS server using prefix \"{}\"".format(epics_prefix))

            # Choose the appropiate epics module:
            if use_pcas:
                self.epics = pyrogue.epics.EpicsCaServer(base=epics_prefix, root=self)
            else:
                self.epics = pyrogue.protocols.epics.EpicsCaServer(base=epics_prefix, root=self)

                # PVs for stream data, used on GDD-based EPICS server
                if stream_pv_size:

                    print("Enabling stream data on PVs (buffer size = {} points, data type = {})"\
                        .format(stream_pv_size,stream_pv_type))

                    self.stream_fifos  = []
                    self.stream_slaves = []
                    for i in range(8):
                        self.stream_slaves.append(self.epics.createSlave(name="AMCc:Stream{}".format(i), maxSize=stream_pv_size, type=stream_pv_type))

                        # Calculate number of bytes needed on the fifo
                        if '16' in stream_pv_type:
                            fifo_size = stream_pv_size * 2
                        else:
                            fifo_size = stream_pv_size * 4

                        self.stream_fifos.append(rogue.interfaces.stream.Fifo(1000, fifo_size, True)) # changes
                        self.stream_fifos[i]._setSlave(self.stream_slaves[i])
                        pyrogue.streamTap(self.ddr_streams[i], self.stream_fifos[i])

            self.epics.start()

            # Dump the PV list to the especified file
            if pv_dump_file:
                try:
                    # Try to open the output file
                    f = open(pv_dump_file, "w")
                except IOError:
                    print("Could not open the PV dump file \"{}\"".format(pv_dump_file))
                else:
                    with f:
                        print("Dumping PV list to \"{}\"...".format(pv_dump_file))
                        try:
                            try:
                                # Redirect the stdout to the output file momentarily
                                original_stdout, sys.stdout = sys.stdout, f
                                self.epics.dump()
                            finally:
                                sys.stdout = original_stdout

                            print("Done!")
                        except:
                            # Capture error from epics.dump() if any
                            print("Errors were found during epics.dump()")

        # If no in server Mode, start the GUI
        if not server_mode:
            create_gui(self, title=windows_title)
        else:
            # Stop the server when Crtl+C is pressed
            print("")
            print("Running in server mode now. Press Ctrl+C to stop...")
            try:
                # Wait for Ctrl+C
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                pass

    # Function for setting a default configuration.
    def set_defaults_cmd(self):
        # Check if a default configuration file has been defined
        if not self.config_file:
            print('No default configuration file was specified...')
            return

        print('Setting defaults from file {}'.format(self.config_file))
        self.ReadConfig(self.config_file)

    def stop(self):
        print("Stopping servers...")
        if hasattr(self, 'epics'):
            print("Stopping EPICS server...")
            self.epics.stop()
        super(LocalServer, self).stop()

    def run_garbage_collection(self):
        print("Running garbage collection...")
        gc.collect()
        print( gc.get_stats() )

    # Send TesBias to Smurf2MCE
    def send_test_bias(self, path, value, disp):
        # Look for the register index
        m = self.TestBiasRegEx.match(path)
        if m:
            reg_index = int(m[1]) - 1
            if reg_index < 32:

                # Update reg value in the buffer
                self.TesBiasValue[reg_index] = value

                # The index  send to Smurf2MCE
                tes_bias_index = reg_index // 2

                # Calculate the difference between DAC bias values
                tes_bias_val = self.TesBiasValue[2*tes_bias_index+1] - self.TesBiasValue[2*tes_bias_index]

                # Send the difference value to smurf2mce
                self.smurf_processor.setTesBias(tes_bias_index, tes_bias_val)

class PcieCard():
    """
    Class to setup the PCIe RSSI card.

    This class takes care of setting up PCIe card according to the communication
    type used.

    If the PCIe card is present in the system:
    - All the RSSI connection links which point to the target IP address will
      be closed.
    - If PCIe comunication type is used, the RSSI connection is open in the
      specific link. Also, when the the server is closed, the RSSI connection
      is closed.

    If the PCIe card is not present:
    - If PCIe comunication type is used, the program is terminated.
    - If ETH communication type is used, then this class does not do anything.

    This class must be used in a 'with' block in order to ensure that the
    RSSI connection is close correctly during exit even in the case of an
    exepction condition.
    """

    def __init__(self, comm_type, link, ip_addr, dev):

        print("Setting up the RSSI PCIe card...")

        # Get system status:

        # Check if the PCIe card is present in the system
        if Path(dev).exists():
            self.pcie_present = True
        else:
            self.pcie_present = False

        # Check if we use the PCIe for communication
        if 'pcie-' in comm_type:
            self.use_pcie = True
        else:
            self.use_pcie = False

        # Look for configuration errors:

        # Check if we are trying to use PCIe communication without the Pcie
        # card present in the system
        if self.use_pcie and not self.pcie_present:
            exit_message("  ERROR: PCIe device {} does not exist.".format(dev))

        # When the PCIe is in used verify the link number is valid
        if self.use_pcie:
            if link == None:
                exit_message("  ERROR: Must specify an RSSI link number")

            if link in range(0, 6):
                self.link = link
            else:
                exit_message("  ERROR: Invalid RSSI link number. Must be between 0 and 5")

        # Should need to check that the IP address is defined when PCIe is present
        # and not in used, but that is enforce in the main function. We need to
        # know the IP address so we can look for all RSSI links that point to it
        # and close their connections.

        # Not more configuration errors at this point

        # Prepare the PCIe when present
        if self.pcie_present:

            # Build the pyrogue device for the PCIe board
            import rogue.hardware.axi
            import SmurfKcu1500RssiOffload as fpga
            self.pcie = pyrogue.Root(name='pcie',description='')
            memMap = rogue.hardware.axi.AxiMemMap(dev)
            self.pcie.add(fpga.Core(memBase=memMap))
            self.pcie.start(pollEn='False',initRead='True')

            # Verify if the PCIe card is configured with a MAC and IP address.
            # If not, load default values before it can be used.
            valid_local_mac_addr = True
            local_mac_addr = self.pcie.Core.EthLane[0].EthConfig.LocalMac.get()
            if local_mac_addr == "00:00:00:00:00:00":
                valid_local_mac_addr = False
                self.pcie.Core.EthLane[0].EthConfig.LocalMac.set("08:00:56:00:45:50")
                local_mac_addr = self.pcie.Core.EthLane[0].EthConfig.LocalMac.get()

            valid_local_ip_addr = True
            local_ip_addr = self.pcie.Core.EthLane[0].EthConfig.LocalIp.get()
            if local_ip_addr == "0.0.0.0":
                valid_local_ip_addr = False
                self.pcie.Core.EthLane[0].EthConfig.LocalIp.set("10.0.3.99")
                local_ip_addr = self.pcie.Core.EthLane[0].EthConfig.LocalIp.get()


            # If the IP was not defined, read the one from the register space.
            # Note: this could be the case only the PCIe is in used.
            if not ip_addr:
                ip_addr = self.pcie.Core.EthLane[0].UdpClient[self.link].ClientRemoteIp.get()

                # Check if the IP address read from the PCIe card is valid
                try:
                    socket.inet_pton(socket.AF_INET, ip_addr)
                except socket.error:
                    exit_message("ERROR: IP Address read from the PCIe card: {} is invalid.".format(ip_addr))

            # Update the IP address.
            # Note: when the PCIe card is not in used, the IP will be defined
            # by the user.
            self.ip_addr = ip_addr

        # Print system configuration and status
        print("  - PCIe present in the system             : {}".format(
            "Yes" if self.pcie_present else "No"))
        print("  - PCIe based communicartion selected     : {}".format(
            "Yes" if self.use_pcie else "No"))

        # Show IP address and link when the PCIe is in use
        if self.use_pcie:
            print("  - Valid MAC address                      : {}".format(
                "Yes" if valid_local_mac_addr else "No. A default address was loaded"))
            print("  - Valid IP address                       : {}".format(
                "Yes" if valid_local_ip_addr else "No. A default address was loaded"))
            print("  - Local MAC address:                     : {}".format(local_mac_addr))
            print("  - Local IP address:                      : {}".format(local_ip_addr))
            print("  - Using IP address                       : {}".format(self.ip_addr))
            print("  - Using RSSI link number                 : {}".format(self.link))

        # Print the FW version information when the PCIe is present
        if self.pcie_present:
            self.print_version()

        # When the PCIe card is not present we don't do anything

    def __enter__(self):
        # Close all RSSI links that point to the target IP address
        self.close_all_rssi()

        # Open the RSSI link
        self.open_rssi()

        return self

    def __exit__(self, exc_type, exc_value, traceback):
        # Close the RSSI link before exit
        self.close_rssi()

        # Stop the device
        if self.pcie_present:
            self.pcie.stop()

    def open_rssi(self):
        """
        Open the RSSI connection in the specified link
        """

        # Check if the PCIe is present and in used
        if self.pcie_present and self.use_pcie:
            print("  * Opening RSSI link...")
            self.__configure(open=True, link=self.link)
            print("  Done!")
            print("")

    def close_rssi(self):
        """
        Close the RSSI connection in the specified link
        """

        # Check if the PCIe is present and in used
        if self.pcie_present and self.use_pcie:
            print("  * Closing RSSI link...")
            self.__configure(open=False, link=self.link)
            print("  Done!")
            print("")

    def close_all_rssi(self):
        """
        Close all links with the target IP address
        """

        # Check if the PCIe is present
        if self.pcie_present:
            print("  * Looking for RSSI links pointing to {}...".format(self.ip_addr))
            # Look for links with the target IP address, and close their RSSI connection
            for i in range(6):
                if self.ip_addr == self.pcie.Core.EthLane[0].UdpClient[i].ClientRemoteIp.get():
                    print("    RSSI Link {} points to it. Disabling it...".format(i))
                    self.__configure(open=False, link=i)
                    print("")
            print("  Done!")
            print("")

    def print_version(self):
        """
        Print the FW version information
        """

        # Print inforamtion if the PCIe is present
        if self.pcie_present:
            # Call readAll so that the LinkVariables get updated correctly.
            self.pcie.ReadAll.call()
            print("  ==============================================================")
            print("                         PCIe information")
            print("  ==============================================================")
            print("    FW Version      : 0x{:08X}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.FpgaVersion.get()))
            print("    FW GitHash      : 0x{:040X}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.GitHash.get()))
            print("    FW image name   : {}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.ImageName.get()))
            print("    FW build env    : {}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.BuildEnv.get()))
            print("    FW build server : {}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.BuildServer.get()))
            print("    FW build date   : {}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.BuildDate.get()))
            print("    FW builder      : {}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.Builder.get()))
            print("    Up time         : {}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.UpTime.get()))
            print("    Xilinx DNA ID   : 0x{:032X}".format(
                self.pcie.Core.AxiPcieCore.AxiVersion.DeviceDna.get()))
            print("  ==============================================================")
            print("")

    def __configure(self, open, link):

        # Read the bypass RSSI mask
        mask = self.pcie.Core.EthLane[0].EthConfig.BypRssi.get()
        dis  = mask
        dis |= (1<<link)
        self.pcie.Core.EthLane[0].EthConfig.BypRssi.set(dis)
        time.sleep(1)

        if open:
            print("    Opening PCIe RSSI link {}".format(link))

            # Clear the RSSI bypass bit
            mask &= ~(1<<link)

            # Setup udp client IP address and port number
            self.pcie.Core.EthLane[0].UdpClient[link].ClientRemoteIp.set(self.ip_addr)
            self.pcie.Core.EthLane[0].UdpClient[link].ClientRemotePort.set(8198)
        else:
            print("    Closing PCIe RSSI link {}".format(link))

            # Set the RSSI bypass bit
            mask |= (1<<link)

            # Setup udp client port number
            self.pcie.Core.EthLane[0].UdpClient[link].ClientRemotePort.set(8192)

        # Set the bypass RSSI mask
        self.pcie.Core.EthLane[0].EthConfig.BypRssi.set(mask)

        # Set the Open and close connection registers
        self.pcie.Core.EthLane[0].RssiClient[link].CloseConn.set(int(not open))
        self.pcie.Core.EthLane[0].RssiClient[link].OpenConn.set(int(open))
        self.pcie.Core.EthLane[0].RssiClient[link].HeaderChksumEn.set(1)

        # Printt register status after setting them
        print("      PCIe register status:")
        print("      EthConfig.BypRssi = 0x{:02X}".format(
            self.pcie.Core.EthLane[0].EthConfig.BypRssi.get()))
        print("      UdpClient[{}].ClientRemoteIp = {}".format(link,
            self.pcie.Core.EthLane[0].UdpClient[link].ClientRemoteIp.get()))
        print("      UdpClient[{}].ClientRemotePort = {}".format(link,
            self.pcie.Core.EthLane[0].UdpClient[link].ClientRemotePort.get()))
        print("      RssiClient[{}].CloseConn = {}".format(link,
            self.pcie.Core.EthLane[0].RssiClient[link].CloseConn.get()))
        print("      RssiClient[{}].OpenConn = {}".format(link,
            self.pcie.Core.EthLane[0].RssiClient[link].OpenConn.get()))

def kill_old_process():
    try:
        finp = open(PIDFILE)
        pid = int(finp.readlines()[0][:-1])
        finp.close()
        cmd = "kill -9 %d" % pid
        os.system(cmd)
        print(' ')
        print(' ')
        print(' ')
        print(' ')
        print(' SMURF already running: killing pid=', str(pid), ' at ', str(time.ctime()))
        print(' ')
        print(' ')
        print(' ')
        print(' ')
    except:
        pass

def save_pid():
    """ save pid for later killing """
    fpid = open(PIDFILE, 'w')
    fpid.write("%d\n" % os.getpid() )
    fpid.close()

# Main body
if __name__ == "__main__":
    ip_addr = ""
    group_name = ""
    epics_prefix = ""
    config_file = ""
    server_mode = False
    polling_en = True
    stream_pv_size = 0
    stream_pv_type = "UInt16"
    stream_pv_valid_types = ["UInt16", "Int16", "UInt32", "Int32"]
    comm_type = "eth-rssi-non-interleaved";
    comm_type_valid_types = ["eth-rssi-non-interleaved", "eth-rssi-interleaved", "pcie-rssi-interleaved"]
    pcie_rssi_link=None
    pv_dump_file= ""
    pcie_dev="/dev/datadev_0"
    disable_bay0=False
    disable_bay1=False
    disable_gc=False
    windows_title=""

    # Read Arguments
    try:
        opts, _ = getopt.getopt(sys.argv[1:],
            "ha:sp:e:d:nb:f:c:l:u:w:",
            ["help", "addr=", "server", "pyro=", "epics=", "defaults=", "nopoll",
            "stream-size=", "stream-type=", "commType=", "pcie-rssi-link=", "dump-pvs=",
            "disable-bay0", "disable-bay1", "disable-gc", "windows-title=", "pcie-dev="])
    except getopt.GetoptError:
        usage(sys.argv[0])
        sys.exit()

    for opt, arg in opts:
        if opt in ("-h", "--help"):
            usage(sys.argv[0])
            sys.exit()
        elif opt in ("-a", "--addr"):        # IP Address
            ip_addr = arg
        elif opt in ("-s", "--server"):      # Server mode
            server_mode = True
        elif opt in ("-p", "--pyro"):        # Pyro group name
            group_name = arg
        elif opt in ("-e", "--epics"):       # EPICS prefix
            epics_prefix = arg
            PIDFILE = '/tmp/smurf_%s.pid'%epics_prefix
        elif opt in ("-n", "--nopoll"):      # Disable all polling
            polling_en = False
        elif opt in ("-b", "--stream-size"): # Stream data size (on PVs)
            try:
                stream_pv_size = int(arg)
            except ValueError:
                exit_message("ERROR: Invalid stream PV size")
        elif opt in ("-f", "--stream-type"): # Stream data type (on PVs)
            if arg in stream_pv_valid_types:
                stream_pv_type = arg
            else:
                print("Invalid data type. Using {} instead".format(stream_pv_type))
        elif opt in ("-d", "--defaults"):   # Default configuration file
            config_file = arg
        elif opt in ("-c", "--commType"):   # Communication type
            if arg in comm_type_valid_types:
                comm_type = arg
            else:
                print("Invalid communication type. Valid choises are:")
                for c in comm_type_valid_types:
                    print("  - \"{}\"".format(c))
                exit_message("ERROR: Invalid communication type")
        elif opt in ("-l", "--pcie-rssi-link"):       # PCIe RSSI Link
            pcie_rssi_link = int(arg)
        elif opt in ("-u", "--dump-pvs"):   # Dump PV file
            pv_dump_file = arg
        elif opt in ("--disable-bay0"):
            disable_bay0 = True
        elif opt in ("--disable-bay1"):
            disable_bay1 = True
        elif opt in ("--disable-gc"):
            disable_gc = True
        elif opt in ("-w", "--windows-title"):
            windows_title = arg
        elif opt in ("--pcie-dev"):
            pcie_dev = arg

    # Disable garbage collection if requested
    if disable_gc:
        import gc
        gc.disable()
        print("GARBAGE COLLECTION DISABLED")

    # kill/save here so we get the epics_prefix tag from the above option parsing
    kill_old_process()
    save_pid()

    # Verify if IP address is valid
    if ip_addr:
        try:
            socket.inet_pton(socket.AF_INET, ip_addr)
        except socket.error:
            exit_message("ERROR: Invalid IP Address.")

    # Check connection with the board if using eth communication
    if "eth-" in comm_type:
        if not ip_addr:
            exit_message("ERROR: Must specify an IP address for ethernet base communication devices.")

        print("")
        print("Trying to ping the FPGA...")
        try:
           dev_null = open(os.devnull, 'w')
           subprocess.check_call(["ping", "-c2", ip_addr], stdout=dev_null, stderr=dev_null)
           print("    FPGA is online")
           print("")
        except subprocess.CalledProcessError:
           exit_message("    ERROR: FPGA can't be reached!")

    if server_mode and not (group_name or epics_prefix):
        exit_message("    ERROR: Can not start in server mode without Pyro or EPICS server")

    # Try to import the FpgaTopLevel defintion
    try:
        from FpgaTopLevel import FpgaTopLevel
    except ImportError as ie:
        print("Error importing FpgaTopLevel: {}".format(ie))
        exit()

    # If EPICS server is enable, import the epics module
    if epics_prefix:
        # Choose the appropiate epics module:
        #  - until version 2.6.0 rogue uses PCASpy
        #  - later versions use GDD
        use_pcas = True
        try:
            ver = pyrogue.__version__
            if (version.parse(ver) > version.parse('2.6.0')):
                use_pcas = False
        except AttributeError:
            pass

        if use_pcas:
            print("Using PCAS-based EPICS server")
            import pyrogue.epics
        else:
            print("Using GDD-based EPICS server")
            import pyrogue.protocols.epics

    # Import the QT and gui modules if not in server mode
    if not server_mode:
        import pyrogue.gui

    # The PCIeCard object will take care of setting up the PCIe card (if present)
    with PcieCard(link=pcie_rssi_link, comm_type=comm_type, ip_addr=ip_addr, dev=pcie_dev):

        # Start pyRogue server
        server = LocalServer(
            ip_addr=ip_addr,
            config_file=config_file,
            server_mode=server_mode,
            group_name=group_name,
            epics_prefix=epics_prefix,
            polling_en=polling_en,
            comm_type=comm_type,
            pcie_rssi_link=pcie_rssi_link,
            stream_pv_size=stream_pv_size,
            stream_pv_type=stream_pv_type,
            pv_dump_file=pv_dump_file,
            disable_bay0=disable_bay0,
            disable_bay1=disable_bay1,
            disable_gc=disable_gc,
            windows_title=windows_title,
            pcie_dev=pcie_dev)

    # Stop server
    server.stop()

    os.remove(PIDFILE)

    print("")

#!/usr/bin/env python
#-----------------------------------------------------------------------------
# Title      : PySMuRF Data Unwrapper
#-----------------------------------------------------------------------------
# File       : __init__.py
# Created    : 2019-09-30
#-----------------------------------------------------------------------------
# Description:
#    SMuRF Data Unwrapper Python Package
#-----------------------------------------------------------------------------
# This file is part of the smurf software platform. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the smurf software platform, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import pyrogue
import smurf

class Unwrapper(pyrogue.Device):
    """
    SMuRF Data Unwrapper Python Wrapper.
    """
    def __init__(self, name, **kwargs):
        self._unwrapper = smurf.core.unwrappers.Unwrapper()
        pyrogue.Device.__init__(self, name=name, description='SMuRF Data Unwrapper', **kwargs)

        # Add "Disable" variable
        self.add(pyrogue.LocalVariable(
            name='Disable',
            description='Disable the processing block. Data will just pass thorough to the next slave.',
            mode='RW',
            value=False,
            localSet=lambda value: self._unwrapper.setDisable(value),
            localGet=self._unwrapper.getDisable))

    # Method called by streamConnect, streamTap and streamConnectBiDir to access slave
    def _getStreamSlave(self):
        return self._unwrapper

    # Method called by streamConnect, streamTap and streamConnectBiDir to access master
    def _getStreamMaster(self):
        return self._unwrapper

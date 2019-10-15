#ifndef _SMURF_CORE_TRANSMITTERS_BASETRANSMITTER_H_
#define _SMURF_CORE_TRANSMITTERS_BASETRANSMITTER_H_

/**
 *-----------------------------------------------------------------------------
 * Title         : SMuRF Data Base Transmitter
 * ----------------------------------------------------------------------------
 * File          : BaseTransmitter.h
 * Created       : 2019-09-27
 *-----------------------------------------------------------------------------
 * Description :
 *    SMuRF Data Base Transmitter Class.
 *-----------------------------------------------------------------------------
 * This file is part of the smurf software platform. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
    * https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the smurf software platform, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *-----------------------------------------------------------------------------
**/

#include <iostream>
#include <rogue/interfaces/stream/Frame.h>
#include <rogue/interfaces/stream/FrameLock.h>
#include <rogue/interfaces/stream/FrameIterator.h>
#include <rogue/interfaces/stream/Slave.h>
#include <rogue/GilRelease.h>

namespace bp  = boost::python;
namespace ris = rogue::interfaces::stream;

namespace smurf
{
    namespace core
    {
        namespace transmitters
        {
            class BaseTransmitter;
            typedef std::shared_ptr<BaseTransmitter> BaseTransmitterPtr;

            class BaseTransmitter : public ris::Slave
            {
            public:
                BaseTransmitter();
                ~BaseTransmitter() {};

                static BaseTransmitterPtr create();

                static void setup_python();

                // Disable the processing block. The data
                // will just pass through to the next slave
                void       setDisable(bool d);
                const bool getDisable() const;

                // Accept new frames
                void acceptFrame(ris::FramePtr frame);

            private:
                bool disable; // Disable flag
            };
        }
    }
}

#endif

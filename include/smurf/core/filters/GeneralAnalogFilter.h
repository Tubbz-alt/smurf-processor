#ifndef _SMURF_CORE_FILTERS_GENERALANALOGFILTER_H_
#define _SMURF_CORE_FILTERS_GENERALANALOGFILTER_H_

/**
 *-----------------------------------------------------------------------------
 * Title         : SMuRF Data GeneralAnalogFilter
 * ----------------------------------------------------------------------------
 * File          : GeneralAnalogFilter.h
 * Created       : 2019-09-27
 *-----------------------------------------------------------------------------
 * Description :
 *    SMuRF Data GeneralAnalogFilter Class.
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
#include "smurf/core/common/BaseSlave.h"
#include "smurf/core/common/BaseMaster.h"

namespace bp  = boost::python;
namespace ris = rogue::interfaces::stream;
namespace scc = smurf::core::common;

namespace smurf
{
    namespace core
    {
        namespace filters
        {
            class GeneralAnalogFilter;
            typedef boost::shared_ptr<GeneralAnalogFilter> GeneralAnalogFilterPtr;

            class GeneralAnalogFilter : public scc::BaseSlave, public scc::BaseMaster
            {
            public:
                GeneralAnalogFilter(std::size_t s);
                ~GeneralAnalogFilter() {};

                static GeneralAnalogFilterPtr create(std::size_t s);

                static void setup_python();

                // This will be call by the BaseSlave class after updating
                // the base counters
                void rxtFrame(ris::FramePtr frame);

            private:
                std::size_t size;
            };
        }
    }
}

#endif
/**
 *-----------------------------------------------------------------------------
 * Title      : Python Module For Counters
 * ----------------------------------------------------------------------------
 * File       : module.cpp
 * Created    : 2016-09-27
 * ----------------------------------------------------------------------------
 * Description:
 * Python module setup
 * ----------------------------------------------------------------------------
 * This file is part of the smurf software platform. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the smurf software platform, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/

#include <boost/python.hpp>
#include "smurf/core/counters/module.h"
#include "smurf/core/counters/FrameStatistics.h"

namespace bp  = boost::python;
namespace scc = smurf::core::counters;

void scc::setup_module()
{
    // map the IO namespace to a sub-module
    bp::object module(bp::handle<>(bp::borrowed(PyImport_AddModule("smurf.core.counters"))));

    // make "from mypackage import class1" work
    bp::scope().attr("counters") = module;

    // set the current scope to the new sub-module
    bp::scope io_scope = module;

    scc::FrameStatistics::setup_python();
}

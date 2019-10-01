/**
 *-----------------------------------------------------------------------------
 * Title         : SMuRF Data Re-orderer
 * ----------------------------------------------------------------------------
 * File          : Reorderer.cpp
 * Created       : 2019-09-27
 *-----------------------------------------------------------------------------
 * Description :
 *   SMuRF Data Re-orderer Class.
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

#include <boost/python.hpp>
#include "smurf/core/reorderer/Reorderer.h"

namespace scr  = smurf::core::reorderer;

scr::Reorderer::Reorderer()
:
    ris::Slave(),
    ris::Master(),
    disable(false),
    frameCnt(0),
    frameSize(0)
{
    std::cout << "Reorderer created" << std::endl;
}

scr::ReordererPtr scr::Reorderer::create()
{
    return boost::make_shared<Reorderer>();
}

// Setup Class in python
void scr::Reorderer::setup_python()
{
    bp::class_<scr::Reorderer, scr::ReordererPtr, bp::bases<ris::Slave,ris::Master>, boost::noncopyable >("Reorderer", bp::init<>())
        .def("setDisable",          &Reorderer::setDisable)
        .def("getDisable",          &Reorderer::getDisable)
        .def("getFrameCnt",         &Reorderer::getFrameCnt)
        .def("getFrameSize",        &Reorderer::getFrameSize)
        .def("clearCnt",            &Reorderer::clearCnt)
    ;
    bp::implicitly_convertible< scr::ReordererPtr, ris::SlavePtr >();
    bp::implicitly_convertible< scr::ReordererPtr, ris::MasterPtr >();
}

void scr::Reorderer::setDisable(bool d)
{
    disable = d;
}

const bool scr::Reorderer::getDisable() const
{
    return disable;
}


const std::size_t scr::Reorderer::getFrameCnt() const
{
    return frameCnt;
}

const std::size_t scr::Reorderer::getFrameSize() const
{
    return frameSize;
}

void scr::Reorderer::clearCnt()
{
    frameCnt         = 0;
}

void scr::Reorderer::acceptFrame(ris::FramePtr frame)
{
    std::cout << "Reorderer. Frame received..." << std::endl;
    std::cout << "Size = " << frame->getPayload() << std::endl;

    // If the processing block is disabled, just send the frame
    // to the next slave.
    if (disable)
    {
        sendFrame(frame);
        return;
    }

    //Increase the frame counter
    ++frameCnt;

    // Update the last frame size
    frameSize = frame->getPayload();


    // Request a new frame
    ris::FramePtr newFrame = reqFrame(128, true);

    // Iterator to the input frame
    ris::FrameIterator itIn = frame->beginRead();

    // Iterator to the output frame
    ris::FrameIterator itOut = newFrame->beginWrite();

    // Copy the header from the input frame to the output frame.
    for (std::size_t i{0}; i < 128; ++i)
            *(itOut+1) = *(itIn+1);

    // Set the frame size
    newFrame->setPayload(128);

    // Send the frame
    sendFrame(newFrame);
}
/**
 *-----------------------------------------------------------------------------
 * Title         : SMuRF Data GeneralAnalogFilter
 * ----------------------------------------------------------------------------
 * File          : GeneralAnalogFilter.cpp
 * Created       : 2019-09-27
 *-----------------------------------------------------------------------------
 * Description :
 *   SMuRF Data GeneralAnalogFilter Class.
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
#include "smurf/core/filters/GeneralAnalogFilter.h"

namespace scf = smurf::core::filters;

scf::GeneralAnalogFilter::GeneralAnalogFilter()
:
    scc::BaseSlave(),
    scc::BaseMaster(),
    numCh(0),
    order(0),
    gain(1),
    a(1,1),
    b(1,1),
    lastPointIndex(0),
    x( order, std::vector<output_data_t>(numCh) ),
    y( order, std::vector<output_data_t>(numCh) )
{
}

scf::GeneralAnalogFilterPtr scf::GeneralAnalogFilter::create()
{
    return boost::make_shared<GeneralAnalogFilter>();
}

void scf::GeneralAnalogFilter::setup_python()
{
    bp::class_<scf::GeneralAnalogFilter, scf::GeneralAnalogFilterPtr, bp::bases<scc::BaseSlave,scc::BaseMaster>, boost::noncopyable >("GeneralAnalogFilter",bp::init<>())
        .def("setOrder", &GeneralAnalogFilter::setOrder)
        .def("setA",     &GeneralAnalogFilter::setA)
        .def("setB",     &GeneralAnalogFilter::setB)
        .def("setGain",  &GeneralAnalogFilter::setGain)
        .def("getNumCh", &GeneralAnalogFilter::getNumCh)
    ;
    bp::implicitly_convertible< scf::GeneralAnalogFilterPtr, scc::BaseSlavePtr  >();
    bp::implicitly_convertible< scf::GeneralAnalogFilterPtr, scc::BaseMasterPtr >();
}

void scf::GeneralAnalogFilter::setOrder(std::size_t o)
{
    // Check if the new order is different from the current one
    if ( o != order )
    {
        order = o;

        // When the order it change, reset the filter
        reset();
    }
}

void scf::GeneralAnalogFilter::setA(boost::python::list l)
{
    std::vector<double> temp;

    std::size_t listSize = len(l);

    // Verify that the input list is not empty.
    // If empty, set the coefficients vector to a = [1.0].
    if (listSize == 0)
    {
        // This should go to a logger instead
        std::cerr << "ERROR: Trying to set an empty set of a coefficients. Defaulting to 'a = [1.0]'"<< std::endl;
        temp.push_back(1.0);
        b.swap(temp);

        return;
    }

    // Verify that the first coefficient is not zero.
    // if it is, set the coefficients vector to a = [1.0].
    if (l[0] == 0)
    {
        // This should go to a logger instead
        std::cerr << "ERROR: The first a coefficient can not be zero. Defaulting to 'a = [1.0]'"<< std::endl;
        temp.push_back(1.0);
        b.swap(temp);
        return;
    }

    // Extract the coefficients coming from python into a temporal vector
    for (std::size_t i{0}; i < listSize; ++i)
    {
        temp.push_back(boost::python::extract<double>(l[i]));
    }

    // Update the a vector with the new values
    a.swap(temp);

    // Check that the a coefficient vector size is at least 'order + 1'.
    // If not, add expand it with zeros.
    if ( a.size() < (order + 1) )
        a.resize(order +  1, 0);
}

void scf::GeneralAnalogFilter::setB(boost::python::list l)
{
    std::vector<double> temp;

    std::size_t listSize = len(l);

    // Verify that the input list is not empty.
    // If empty, set the coefficients vector to a = [0.0].
    if (listSize == 0)
    {
        // This should go to a logger instead
        std::cerr << "ERROR: Trying to set an empty set of a coefficients. Defaulting to 'b = [0.0]'"<< std::endl;
        temp.push_back(0.0);
        b.swap(temp);

        return;
    }

    // Extract the coefficients coming from python into a temporal vector
    for (std::size_t i{0}; i < len(l); ++i)
    {
        temp.push_back(boost::python::extract<double>(l[i]));
    }

    // Update the a vector with the new values
    b.swap(temp);

    // Check that the b coefficient vector size is at least 'order + 1'.
    // If not, add expand it with zeros.
    if ( b.size() < (order + 1) )
        b.resize(order +  1, 0);
}

void scf::GeneralAnalogFilter::setGain(double g)
{
    gain = g;
}

const std::size_t scf::GeneralAnalogFilter::getNumCh() const
{
    return numCh;
}

void scf::GeneralAnalogFilter::reset()
{
    // Resize and re-initialize the data buffer
    std::vector< std::vector<output_data_t> >(order, std::vector<output_data_t>(numCh)).swap(x);
    std::vector< std::vector<output_data_t> >(order, std::vector<output_data_t>(numCh)).swap(y);

    // Check that a coefficient vector size is at least 'order + 1'.
    // If not, add expand it with zeros.
    if ( a.size() < (order + 1) )
        a.resize(order +  1, 0);

    // Check that b coefficient vector size is at least 'order + 1'.
    // If not, add expand it with zeros.
    if ( b.size() < (order + 1) )
        b.resize(order +  1, 0);
}

void scf::GeneralAnalogFilter::rxFrame(ris::FramePtr frame)
{
    // If the processing block is disabled, do not process the frame
    if (isRxDisabled())
    {
        // Send the frame to the next slave.
        // This method will check if the Tx block is disabled, as well
        // as updating the Tx counters
        txFrame(frame);

        return;
    }

    // (smart) pointer to the smurf header in the input frame (Read-only)
    SmurfHeaderROPtr smurfHeaderIn(SmurfHeaderRO::create(frame));

    // Get the number of channels in the input frame. The number of output channel will be the same
    std::size_t newNumCh = smurfHeaderIn->getNumberChannels();

    // Verify if the frame size has changed
    if (numCh != newNumCh)
    {
        // Update the number of channels we are processing
        numCh = newNumCh;

        // When the number of channel change, reset the filter
        reset();
    }

    // Request a new frame, to hold the same payload as the input frame
    //std::size_t outFrameSize = SmurfHeader::SmurfHeaderSize + sizeof(output_data_t) * numCh;
    // For now we want to keep packet of the same size, so let's do this instead:
    std::size_t outFrameSize = SmurfHeader::SmurfHeaderSize +
        ( ( frame->getPayload() - SmurfHeader::SmurfHeaderSize )/sizeof(input_data_t) ) * sizeof(output_data_t);
    ris::FramePtr outFrame = reqFrame(outFrameSize, true);
    outFrame->setPayload(outFrameSize);

    // Fill the output frame payload with zeros.
    // This is only for convenience, as the header says the number of channel which have
    // valid data. The rest of payload will have only garbage.
    //std::fill(outFrame->beginWrite() + SmurfHeader::SmurfHeaderSize + numCh * sizeof(output_data_t),
    //    outFrame->endWrite(), 0);

    // Iterator to the input frame
    ris::FrameIterator inFrameIt = frame->beginRead();

    // Iterator to the output frame
    ris::FrameIterator outFrameIt = outFrame->beginWrite();

    // Copy the header from the input frame to the output frame.
    outFrameIt = std::copy(inFrameIt, inFrameIt + SmurfHeader::SmurfHeaderSize, outFrameIt);

    // Filter the data

    // Iterate over the channel sample
    for (std::size_t ch{0}; ch < numCh; ++ch)
    {
        // Get the new data from from the input frame
        output_data_t inCasted = static_cast<output_data_t>(helpers::getWord<input_data_t>(inFrameIt, ch));
        double in = static_cast<double>(inCasted);

        // Start computing the output value
        double out = b.at(0) * in;

        // Iterate over the pass samples
        for (std::size_t t{1}; t <= order; ++t)
        {
            // Compute the correct index in the 'circular' buffer
            std::size_t i{ ( order + lastPointIndex - t ) % order };
            out += b.at(t) * x.at(i).at(ch) - a.at(t) * y.at(i).at(ch);
        }

        // Divide the resulting value by the first a coefficient
        out /= a.at(0);

        // Multiply by the gain
        out *= gain;

        // Copy the new output value into the output frame
        output_data_t outCasted = static_cast<output_data_t>(out);
        helpers::setWord<output_data_t>(outFrameIt, ch, outCasted);

        // If the filter order > 0, copy the new output value as well
        // as the current input value into the data buffers
        if (order > 0)
        {
            y.at(lastPointIndex).at(ch) = outCasted; // Output value, casted to 'output_data_t'
            x.at(lastPointIndex).at(ch) = inCasted;  // Input value, casted to 'output_data_t'
        }
    }

    // Update the index to point to the now older point in the 'circular' buffer
    // if the order > 0
    if (order > 0)
        lastPointIndex = (lastPointIndex + 1) % order;

    // Send the frame to the next slave.
    // This method will check if the Tx block is disabled, as well
    // as updating the Tx counters
    txFrame(outFrame);
}
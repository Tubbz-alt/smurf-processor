
/*
 *-----------------------------------------------------------------------------
 * Title      : Source code for MceTransmit
 *-----------------------------------------------------------------------------
 * File       : exoTest.py
 * Created    : 2018-02-28
 *-----------------------------------------------------------------------------
 * This file is part of the rogue_example software. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the rogue_example software, inclu ding this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt  fil e.
 *-----------------------------------------------------------------------------
*/

#include "smurf_processor.h"

SmurfProcessor::SmurfProcessor()
: ris::Slave(),
txBuffer             ( 10, 2                                               ),
runTxThread          ( true                                                ),
pktReaderIndexTx     ( 0                                                   ),
pktReaderIndexFile   ( 1                                                   ),
pktTransmitterThread ( std::thread( &SmurfProcessor::pktTansmitter, this ) ),
pktWriterThread      ( std::thread( &SmurfProcessor::pktWriter, this )     )
{
  rxCount = 0;
  rxBytes = 0;
  rxLast = 0; // from test program
  initialized = false;
  average_counter= 1;
  bufn = 0; // current buffer
  internal_counter = 0;
  fast_internal_counter = 0;
  int j;
  last_syncword = 0;
  last_1hz_counter = 0;
  frame_error_counter = 0;
  last_frame_counter = 0;
  debug_ = false;

  C = new SmurfConfig(); // will hold config info - testing for now
  // M = new MCEHeader();  // creates a MCE header class
  H = new SmurfHeader();
  D = new SmurfDataFile();  // holds output data
  V = new SmurfValidCheck();
  F = new SmurfFilter(smurfsamples, 16);  //
  T = new SmurfTestData(smurf_raw_samples, smurfsamples);

  average_counter = 0; // counter used for test averaging , not  needed in real program
  for(j = 0; j < 2; j++)
  {  // allocate 2 buffers, so we can swap up/ back for background subtraction.
    if(!(b[j] = (uint8_t*)malloc(pyrogue_buffer_length)))
    {
      error("could not allocate smurf2mce buffer");
      return;
    }

    memset(b[j], 0, pyrogue_buffer_length); // zero to start with
  }

 // if(!(average_mce_samples = (avgdata_t*)malloc(smurfsamples * sizeof(avgdata_t))))
 //    {
 //      error("could not allocate mce data sample buffer");
 //      return;
 //    }

  if(!(input_data = (avgdata_t*)malloc(smurfsamples * sizeof(avgdata_t))))
  {
    error("could not allocate input data sample buffer");
    return;
  }

  if(!(wrap_counter = (wrap_t*)malloc(smurfsamples * sizeof(wrap_t))))
  {
    error("could not allocate wrap_counter");
    return;
  }

  if(!(mask = (uint*)malloc(smurfsamples * sizeof(uint))))
  {
    error("could not allocate mask  buffer");
    return;
  }

  memset(mask, 0, smurfsamples * sizeof(uint)); // set to all off to start
  memset(input_data, 0, smurfsamples * sizeof(avgdata_t)); // set to all off to start
  read_mask(NULL);  // will use real file name later
  memset(wrap_counter, wrap_start, smurfsamples * sizeof(wrap_t));

  queue_.setThold(queueDepth);

  // thread_ = new boost::thread(boost::bind(&SmurfProcessor::runThread, this));
  thread_ = new boost::thread(&SmurfProcessor::runThread, this);

  initialized = true;

  // Set thread names
  if( pthread_setname_np( pktTransmitterThread.native_handle(), "pktTransmitter" ) )
    perror( "pthread_setname_np failed for pktTransmitterThread" );
  if( pthread_setname_np( pktWriterThread.native_handle(), "pktWriter" ) )
    perror( "pthread_setname_np failed for pktWriterThread" );
}

// This function does most of the work. Runs every smurf frame
void SmurfProcessor::runThread()
{
  printf("\n");
  printf("Starting SmurfProcessor::runThread()\n");
  printf("\n");

  ris::FramePtr frame;
  smurf_t *d, *p;  // d is this buffer, p is last buffer;
  char *pm;
  //avgdata_t *a; // used for averaging loop
  uint j, k;
  uint dctr; // counter for input data array
  // char *tcpbuf;
  uint32_t cnt;
  int tmp;
  // MCE_t checksum;  // fixed for now, #2 for testing
  uint tcp_buflen; // holds filled length oftcp buffer
  // uint32_t *bufx; // holds tcp buffer mapped to 32 bit for checksum
  uint32_t avgtmp;
  smurf_t dx; // current sample in loop
  uint64_t tm;

  // zmq::context_t context(1);
  // zmq::socket_t socket(context, ZMQ_PUSH);
  // printf("Connecting to %s\n", endpoint);
  // socket.connect(endpoint);

  // Variables used to count lost frames
  uint32_t    prevFrameNumber  = 0;
  uint32_t    frameNumber      = 0;
  std::size_t frameNumberDelta = 0;
  bool        firstFrame       = true;

  try
  {
    while(1)
    {
      // zmq::message_t message(MCE_frame_length * sizeof(MCE_t));

      frame = queue_.pop();
      buffer = b[bufn]; // buffer swap
      bufn = bufn ? 0 : 1; // swap buffer reference
      buffer_last = b[bufn]; // now that we've swapped them
      frameToBuffer(frame, buffer);

      H->copy_header(buffer);
      d = (smurf_t*) (buffer+smurfheaderlength); // pointer to data
      p =  (smurf_t*) (buffer_last+smurfheaderlength);  // pointer to previous data set
      // V->run(H);

      // Check if we are missing frames
      prevFrameNumber = frameNumber;            // Previous frame number
      frameNumber     = H->get_frame_counter(); // Current frame number

      // Don't compare the first frame
      if (firstFrame)
      {
        firstFrame = false;
      }
      else
      {
        // If we are missing frame, add the number of missing frames to the counter
        frameNumberDelta = frameNumber - prevFrameNumber - 1;
        if ( frameNumberDelta )
          frameLossCnt += frameNumberDelta;
      }

      if(H->get_test_mode())
        T->gen_test_smurf_data(d, H->get_test_mode(), H->get_syncword(), H->get_test_parameter());   // are we using test data, use pointer to data

      for(j = 0; j < smurfsamples; j++)
      {
        dctr = mask[j];

        if((mask[j] < 0) || (mask[j] > 4095))
        {
          printf("bad mask %u \n", mask[j]);\
          break;
        }

        if ((d[dctr] > upper_unwrap) && (p[dctr] < lower_unwrap)) // unwrap, add 1
        {
          wrap_counter[j]-= 0x10000; // decrement wrap counter
        }
        else if((d[dctr] < lower_unwrap) && (p[dctr] > upper_unwrap))
        {
          wrap_counter[j]+= 0x10000; // inccrement wrap counter
        }
        else; // nothing here

        input_data[j] = (avgdata_t)(d[dctr]) + (avgdata_t) wrap_counter[j];
      }

      average_samples = F->filter(input_data, C->filter_order, C->filter_a, C->filter_b, C->filter_g); // Low Pass Filter
      cnt = H->average_control(C->num_averages);

      if(H->get_clear_bit()) // clear averages and wraps
      {
        memset(wrap_counter, wrap_start, smurfsamples * sizeof(wrap_t));  // clear wraps
        H->clear_average();  // clears averaging
        F->clear_filter();
      }

      if(H->read_config_file())
      {
        if(!(fast_internal_counter++ % 5000))
        {
          C->read_config_file();  // checks for config changes, read immediately, then 1H
          read_mask(NULL);  // re-read the mask file while held at zero
        }
      }
      else
        fast_internal_counter = 0;

      if (!cnt)
      {
        last_frame_counter = H->get_frame_counter(); // does this belont here???? not used anyway
        last_1hz_counter = H->get_1hz_counter();
        continue;  // just average, otherwise send frame  END OF Smurf frame rate LOOP **************************
      }

      F->end_run();  // clears if we are doing a straight average
      // M->make_header(); // increments counters, readies counter
      // M->set_word( mce_h_offset_status, mce_h_status_value);
      // M->set_word( MCEheader_CC_counter_offset, M->CC_frame_counter);
      // M->set_word( MCEheader_row_len_offset,  H->get_row_len());
      // M->set_word( MCEheader_num_rows_reported_offset, H->get_num_rows_reported());
      // M->set_word( MCEheader_data_rate_offset, H->get_data_rate());  // test with fixed average
      // M->set_word( MCEheader_CC_ARZ_counter, smurfsamples);
      // M->set_word( MCEheader_version_offset,  MCEheader_version); // can be in constructor
      // M->set_word( MCEheader_num_rows_offset, H->get_num_rows());
      // M->set_word( MCEheader_syncbox_offset, H->get_syncword());

      // test data insertion
      if(H->get_test_mode())
        T->gen_test_mce_data(average_samples, H->get_test_mode(), H->get_syncword(), H->get_test_parameter());

      // data munging for MCE format - needs 7 bit shift left for data mode 10
      // for(j = 0;j < smurfsamples; j++)
      // {
      //   average_mce_samples[j] = (average_samples[j] & 0x1FFFFFF) << 7;
      // }

      // tcpbuf = (char *) (message.data());  // returns location to put data (8 bytes beyond tcp start)
      // memcpy(tcpbuf, M->mce_header, MCEheaderlength * sizeof(MCE_t));  // copy over MCE header to output buffer
      // memcpy(tcpbuf + MCEheaderlength * sizeof(MCE_t), average_mce_samples, smurfsamples * sizeof(avgdata_t)); //copy data
      // tcp_buflen =  MCEheaderlength * sizeof(MCE_t) + smurfsamples * sizeof(avgdata_t);
      // bufx       = (uint32_t*) (message.data());  // map buffer to 32 bit

      // checksum  = *(uint32_t *)(message.data());
      //checksum  = bufx[0];
      // for (j = 1; j < MCE_frame_length-1; j++) checksum = checksum ^ bufx[j]; // calculate checksum (needed for MCE)
      if (H->get_test_mode() == 14)
      {
        if(!(H->get_syncword() % 1000))
        {
          printf("INTENTIONALLY BROKEN CHECKSUM \n");
          // checksum = checksum + 1;   // FORCE BROKEN CHECKSUM
        }
      }

      // memcpy(tcpbuf + MCEheaderlength * sizeof(MCE_t) + smurfsamples * sizeof(avgdata_t), &checksum, sizeof(MCE_t));
      if ( debug_ &&  ( !(internal_counter++ % slow_divider) ) )
      {
        printf("num_avg=%3u, syncword =%6u, epics_deltaT = %" PRIu64 " us, unixdeltaT = %" PRIu64 " us \n", cnt ,H->get_syncword(),V->Timingsystem->delta/1000, V-> Unix_time->delta/1000 );
        printf("syn error = %5u, smurf_frame_error = %u, timing_sysetem_error = %u, unix_error = %u\n", V->Syncbox->error_count, V->Smurf_frame->error_count, V->Timingsystem->error_count, V->Unix_time->error_count);
        printf("clr_avg= %d, dsabl_strm=%d, dsabl_file=%d, read_config = %d, test_mode = %u, %u\n\n", H->get_clear_bit(), H->disable_stream(),
        H->disable_file_write(), H->read_config_file(),  H->get_test_mode() ,H->get_test_parameter());
        for(uint nx = 0; nx < 4; nx++) printf("%6d ", average_samples[nx]);  // diagnostic printout
        printf("\n");
        V->reset();
      }

      last_epicsns = H->get_epics_nanoseconds();

      // if (!H->disable_stream())
      // {
      //   // socket.send(message,  ZMQ_NOBLOCK);
      //   //socket.send(message);
      // }
      // else
      // {
      //   // M->CC_frame_counter = 0;  // set to zero when not streaming
      // }

      V->run(H);
      tm = V->Unix_time->current;
      H->put_field(h_unix_time_offset,  h_unix_time_width, &tm); // add time to data stream
      H->set_num_channels(smurfsamples);

      if(C->data_frames)
      {
        // Add a SMuRF packet in the TX buffer so it can be processed by the transmit method
        try
        {
          // Add the packet into the buffer
          SmurfPacket sp = txBuffer.getWritePtr();  // Get write pointer to buffer area
          sp->copyHeader(H->header);                // Write the header content
          sp->copyData(average_samples);            // Write the data content

          // Mark the writing operation as done.
          txBuffer.doneWriting();
        }
        catch (std::runtime_error &e)
        {
          std::cout << "runThread: Exception caught when writing the data buffer: " << e.what() << std::endl;
        }
      }

      // tcpbuf   = NULL;  // returns location to put data (8 bytes beyond tcp start)
      // bufx     = NULL;
      boost::this_thread::interruption_point();
    }
  }
  catch (boost::thread_interrupted&)
  {
    printf("caught error\n\n\n");
  }
}


void SmurfProcessor::read_mask(char *filename)  // ugly, hard coded file name. fire the programmer
{
  FILE *fp;
  int ret;
  uint j = 0;
  uint x = 0;
  uint m;

  fp = fopen("mask.txt", "r");

  if(fp == NULL)
  {
    printf("unable to open mask file\n");
    return;
  }

  for(j =0; j < smurfsamples; j++)
  {
    ret = fscanf(fp,"%u", &m);  // read next line

    if ((ret == EOF) || (ret == 0))
      break;  // done

    mask[j] = (m < smurf_raw_samples) ? m : 0;
  }

  fclose(fp);
}


// ###############################################################################################################
// THIS IS CALLED BY PYROGUE FOR EACH FRAME


void SmurfProcessor::acceptFrame ( ris::FramePtr frame )
{
  rogue::GilRelease noGil;

  if ( queue_.busy() || frame->getError() || (frame->getFlags() & 0x100) )
    return;  //don't copy data or process

  queue_.push(frame);

  return;
}

void SmurfProcessor::frameToBuffer( ris::FramePtr frame, uint8_t * const buffer)
{
  ris::Frame::BufferIterator src;
  uint8_t *dst = buffer;

  for (src=frame->beginBuffer(); src != frame->endBuffer(); ++src)
  {
    dst = std::copy((*src)->begin(), (*src)->endPayload(), dst);
  }
}

void SmurfProcessor::pktTansmitter()
{
  std::cout << "Transmitter thread started..." << std::endl;

  // Infinite loop
  for(;;)
  {
    // Check the status of the data buffer
    if ( txBuffer.isEmpty(pktReaderIndexTx) )
    {
      // If the buffer is empty, wait until new data is ready, with a 10s timeout
      std::unique_lock<std::mutex> lock(*txBuffer.getMutex());
      txBuffer.getDataReady()->wait_for( lock, std::chrono::seconds(10) );
    }
    else
    {
      try
      {
        // Call processing method passing a read pointer to the buffer area
        transmit(txBuffer.getReadPtr(pktReaderIndexTx));
        // Tell the buffer we are done reading this packet
        txBuffer.doneReading(pktReaderIndexTx);
      }
      catch (std::runtime_error &e)
      {
        std::cout << "pktTansmitter: Exception caught when reading the data buffer: " << e.what() << std::endl;
      }
    }

    // Check if we should stop the loop
    if (!runTxThread)
    {
      std::cout << "pktTansmitter interrupted." << std::endl;
      return;
    }
  }
}

void SmurfProcessor::pktWriter()
{
  std::cout << "File writer thread started..." << std::endl;

  // Infinite loop
  for(;;)
  {
    // Check the status of the data buffer
    if ( txBuffer.isEmpty(pktReaderIndexFile) )
    {
      // If the buffer is empty, wait until new data is ready, with a 10s timeout
      std::unique_lock<std::mutex> lock(*txBuffer.getMutex());
      txBuffer.getDataReady()->wait_for( lock, std::chrono::seconds(10) );
    }
    else
    {
      try
      {
        // Write the packet to file
        D->write_file(txBuffer.getReadPtr(pktReaderIndexFile), C);

        // Tell the buffer we are done reading this area
        txBuffer.doneReading(pktReaderIndexFile);
      }
      catch (std::runtime_error &e)
      {
        std::cout << "pktWriter: Exception caught when reading the data buffer: " << e.what() << std::endl;
      }
    }

    // Check if we should stop the loop
    if (!runTxThread)
    {
      std::cout << "pktWriter interrupted." << std::endl;
      return;
    }
  }
}

void SmurfProcessor::printTransmitStatistic() const
{
  txBuffer.printStatistic();
}

SmurfProcessor::~SmurfProcessor() // destructor
{
}



// manages the header for MCE data
// MCEHeader::MCEHeader()
// {
//   memset(mce_header, 0, MCEheaderlength * sizeof(MCE_t));
//   mce_header[MCEheader_version_offset] = MCEheader_version;  // current version.
//   CC_frame_counter = 0; // counter for MCE frame
// }

// void MCEHeader::make_header(void)
// {
//   mce_header[MCEheader_CC_counter_offset] = CC_frame_counter++;  // increment counter, put in header
//   return;
// }

// void MCEHeader::set_word(uint offset, uint32_t value)
// {
//   mce_header[offset] = value & 0xffffffff; // just write value.
// }

// Reads and interprest the smurf.cfg file.
SmurfConfig::SmurfConfig(void)
{
  ready = false;  // has file ben read yet?
  filename = (char*) malloc(1024 * sizeof(char));
  // memset(receiver_ip, NULL, 40); // clear the IP string
  strcpy(filename, "smurf.cfg");  // kludge for now.
  num_averages = 0; // default value
  data_frames = 0;
  // strcpy(receiver_ip, "tcp://127.0.0.1:3333"); // default
  // strcpy(port_number, "3333");  // default
  strcpy(data_file_name, "data"); // default
  file_name_extend = 1;  // default is to append time
  filter_order = 0; // default for block average
  filter_g     = 1; // default gain

  for(uint j =0; j < 16; j++) // clear vilter values
  {
    filter_a[j] = 0;
    filter_b[j] = 0;
  }

  filter_a[0] = 1;  // first filter element is 1 for simple filter
  filter_b[0] = 1;
  ready = read_config_file();
}

// reads config file.  Ugly code, should fix some day, but works.
bool SmurfConfig::read_config_file(void)
{
  FILE *fp;
  int n, r;
  char variable[100];
  char value[100];
  int tmp;
  double tmpd;
  double tmpf;
  char *endptr; // used but discarded in conversion

  if(!( fp = fopen(filename,"r")))
    return(false); // open config file

  printf("reading config file\n");

  do
  {
    n = fscanf(fp, "%s", variable);  // read into buffer

    if(n != 1)
      continue; // eof or lost here

    n = fscanf(fp, "%s", value);  // read into buffer

    if(n != 1)
      continue; // probably lost if we got here

    if(!strcmp(variable, "num_averages"))
    {
      tmp = strtol(value, &endptr, 10);  // base 10, last parameter
      if (num_averages != tmp)
      {
        printf("num averages updated from %d to %d\n", num_averages, tmp);
        num_averages = tmp;
      }

      continue;
    }

    if(!strcmp(variable, "data_frames"))
    {
      tmp = strtol(value, &endptr, 10);  // base 10 last parameter

      if (data_frames != tmp)
      {
        printf("data_frames updated from %d to %d\n", data_frames, tmp);
        data_frames = tmp;
      }

      continue;
    }

    // if(!strcmp(variable, "receiver_ip"))
    // {

    //   if(strcmp(value, receiver_ip)) // update if different
    //   {
    //     printf("updated ip from %s,  to %s \n", receiver_ip, value);
    //     strncpy(receiver_ip, value, 40); // copy into IP string
    //   }

    //   continue;
    // }

    // if(!strcmp(variable, "port_number"))
    // {
    //   if(strcmp(value, port_number)) // update if different
    //   {
    //     printf("updated port number from %s,  to %s \n", port_number, value);
    //     strncpy(port_number, value, 8); // copy into IP string
    //   }

    //   continue;
    // }

    if(!strcmp(variable, "data_file_name"))
    {
      if(strcmp(value, data_file_name)) // update if different
      {
        printf("updated data file name from  %s,  to %s \n", data_file_name, value);
        strncpy(data_file_name, value, 100); // copy into IP string
      }

      continue;
    }

    if(!strcmp(variable, "file_name_extend"))
    {
      tmp = strtol(value, &endptr, 10);  // base 10, doh
      if (file_name_extend != tmp)
      {
        if(tmp) printf("adding time to file name");
        else  printf("not adding time to file name");
        file_name_extend = tmp;
      }

      continue;
    }

    if(!strcmp(variable, "filter_order"))
    {
      tmp = strtol(value, &endptr, 10);  // base 10, doh
      if (filter_order != tmp)
      {
        printf("updated filter order from %d to %d\n", filter_order, tmp);
        filter_order = tmp;
      }

      continue;
    }

    if(!strcmp(variable, "filter_gain"))
    {
      tmpf = strtof(value, &endptr);  // base 10, doh
      printf("updated filter gain from %g to %g, str=%s\n", filter_g, tmpf, value);
      filter_g = (filter_t) tmpf;
      continue;
    }

    for (uint n = 0; n < 16;  n++)
    {
      char tmpa[100]; // holds string
      char tmpb[100];
      sprintf(tmpa, "filter_a%d",n);
      sprintf(tmpb, "filter_b%d",n);

      if(!strcmp(variable, tmpa))
      {
        tmpf = strtof(value, &endptr);  // conver to float
        printf("filter_a%d updated from %lg to %g, str = %s\n", n, filter_a[n], tmpf, value);
        filter_a[n] = (filter_t) tmpf;
        break;
      }

      if(!strcmp(variable, tmpb))
      {
        tmpf = strtof(value, &endptr);  // conver to float
        printf("filter_b%d updated from %lg to %g str = %s\n", n, filter_b[n], tmpf, value);
        filter_b[n] = (filter_t) tmpf;
        break;
      }

    }
  }
  while ((n!=0) && (n != EOF));  // end when n ==0, end of  file

  fclose(fp); // done with file
}


SmurfDataFile::SmurfDataFile(void) : open_(false), part_(0)
{
  filename = (char*) malloc(1024 * sizeof(char)); // too big for
  memset(filename, 0, 1024); // zero for now
  frame_counter = 0; // frames written
  header_length = smurfheaderlength; // from header file for now
  sample_points = smurfsamples;  // from header file (ugly)
  frame = (uint8_t*) malloc(60000); // just a big number for now
  fd = 0; // shows that we don't have a pointer yet
}

uint SmurfDataFile::write_file(SmurfPacket_RO packet, SmurfConfig *config)
{
  time_t tx;
  char tmp[100]; // for strings
  if(packet->getDisableFileWriteBit())
  {
    part_ = 0;

    if(fd)  // close file
    {
      close(fd);
      fd = 0;
    }

    frame_counter = 0;

    return(frame_counter);
  }

  if ( (config->file_name_extend==0) && (0 != strcmp(config->data_file_name, filename))) // name has changed.
  {
    printf("file name has changed from %s to %s \n", filename, config->data_file_name);
    if(fd)
      close(fd); // close existing file if its open

    fd = 0;
  }

  if(!fd) // need to open a file
  {
    memset(filename, 0, 1024); // zero for now
    strcat(filename, config->data_file_name); // add file name

    if (config->file_name_extend)
    {
      tx = time(NULL);
      sprintf(tmp, ".part_%05u", part_);  // LAZY - need to use a real time converter.
      strcat(filename, tmp);
    }
    //else strcat(filename, ".dat");  // just use base name, Dont append dat.

    printf("new filename = %s \n", filename);
    unlink(filename); // try to delete file if it exists before creating

    if (!(fd = open(filename, O_WRONLY | O_CREAT | O_NONBLOCK, S_IRUSR | S_IWUSR))) // testing non blocking
    //if (!(fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK, S_IRUSR | S_IWUSR))) // testing non blocking
    {
      printf("coult not open: %s \n", filename);
      return(0); // failed to open file
    }
    else
      printf("opened file %s  fd = %d \n", filename, fd);
  }

  // Write packet to file
  packet->writeToFile(fd);

  frame_counter++;

  if(frame_counter >= config->data_frames)
  {
    if(fd)
      close(fd);

    fd = 0;
    frame_counter = 0;
    part_ = (part_ + 1) % 99999;
  }

  return(frame_counter);
}


SmurfTime::SmurfTime(void)
{
  current = 0;
  delta = 0;
  error_count = 0;
  max_allowed_delta = 1000000000; // will be changed later
  min_allowed_delta = 0; // will be changed later
}

bool SmurfTime::update(uint64_t val)
{
  delta = val - current;
  current = val;
  mindelta = (delta > mindelta) ? mindelta : delta; // collect min and max
  maxdelta = (delta < maxdelta) ? maxdelta : delta;

  if ((delta > max_allowed_delta) || (delta < min_allowed_delta))
  {
    error_count++;
    return(true);
  }

  return(false);
}


SmurfValidCheck::SmurfValidCheck() // just creates  all variables.
{
  Unix_time = new SmurfTime();
  Syncbox = new SmurfTime();
  Timingsystem = new SmurfTime();
  Counter_1hz = new SmurfTime();
  Smurf_frame = new SmurfTime();
  Smurf2mce = new SmurfTime();
  init = false;
  ready = false;
  missed_syncbox = 0;
  last_frame_jump = 0;
  frame_wait = 0; // Testing for now;
  Unix_time->max_allowed_delta  = 10000000; // 10ms second, not an accurat e clock
  Unix_time->min_allowed_delta = 2000000; // 2ms to see if we are writing too fast sometimes.
  Syncbox->max_allowed_delta = 1;
  Timingsystem->max_allowed_delta = 10000000; // 10ms
  Counter_1hz->max_allowed_delta = 1000000000;
  Smurf2mce->max_allowed_delta = 100000000; // 1 s for now
  Smurf_frame->max_allowed_delta = 100;  // basically disable for now, just use syncbox jumps

  if(!(fp = fopen("frame_jump_log.txt", "w")))
  {
    printf("unable to open frame jump log file frame_jump_log.txt\n");
  }
  else
  {
    fprintf(fp, "Frame Jump file \n");
    fclose(fp);
  }
}


void SmurfValidCheck::run(SmurfHeader *H)
{
  timespec tmp_t;  // structure seconds, nanoseconds
  uint64_t tmp;
  bool jump = false;

  if (!init)
  {
    initial_timing_system = H->get_epics_seconds();
    printf("initial time = %" PRIu64 " \n", initial_timing_system);
    init = true;
  }

  if(( H->get_epics_seconds() -initial_timing_system) < 30) // not started yet
    return;

  if (!ready)
  {
    printf("*******************starting to record frame jump log**********************************\n");
    ready = true;

    if(!(fp = fopen("frame_jump_log.txt", "a")))
    {
      printf("unable to open frame jump log file frame_jump_log.txt\n");
    }
    else
    {
      printf("Starting to record frame jumps\n");
      fprintf(fp, "columns are: syncbox#, syncbox_delta, smurf_frame_delta, timing_sysetm_deltaT_us, Unix_deltaT_us, smurf2mce_delay_us");
      fclose(fp);
    }
  }

  //clock_gettime(CLOCK_REALTIME, &tmp_t);  // get time s, ns,  might be expensive
  //tmp = 1000000000l * (uint64_t) tmp_t.tv_sec + (uint64_t) tmp_t.tv_nsec;  //  multiply to 64 uint
  tmp = get_unix_time();
  Smurf2mce->update(tmp);
  jump = Unix_time->update(tmp) ? true: jump;
  jump = Syncbox->update(H->get_syncword()) ? true: jump;
  tmp = 1000000000l * (uint64_t) H->get_epics_seconds() + (uint64_t) H->get_epics_nanoseconds();
  jump = Timingsystem->update(tmp) ? true : jump;
  Counter_1hz->update(H->get_1hz_counter());
  jump = Smurf_frame->update(H->get_frame_counter()) ? true : jump;

  if(jump && (Smurf_frame->current > (last_frame_jump + frame_wait)))   // On frame jump, write file,
  {
    last_frame_jump = Smurf_frame->current;

    if(!(fp = fopen("frame_jump_log.txt", "a")))
    {
      printf("unable to open frame jump log file frame_jump_log.txt\n");
    }
    else
    {
      // fprintf(fp, "Frame jump at syncbox = %u, timingsystem time = %lu seconds \n", Syncbox->current, Timingsystem->current/1000000000);
      // fprintf(fp, "syncbox delta (~200Hz) = %4u \n ", Syncbox->delta);
      // fprintf(fp, "timing system delta = %10u us \n", Timingsystem->delta/1000);
      // fprintf(fp, "Unix time delta = %10u us \n", Unix_time->delta/1000);
      // fprintf(fp, "counter_delta(480KHz) = %6u\n ", Counter_1hz->delta);
      // fprintf(fp, "smurf_frame_delta = %4u \n", Smurf_frame->delta);
      // fprintf(fp, "smurf2mce delay = %u us, \n\n", Smurf2mce->delta/1000);
      fprintf(fp, "%10" PRIu64 ", %2" PRIu64 ", %4" PRIu64 ", %" PRIu64 " %6" PRIu64 " %6" PRIu64 " \n", Syncbox->current, Syncbox->delta, Smurf_frame->delta, Timingsystem->delta/1000, Unix_time->delta/1000, Smurf2mce->delta/1000);
      fclose(fp);
    }
  }
}


void SmurfValidCheck::reset()
{
  //Unix_time->reset();
  Syncbox->reset();
  Timingsystem->reset();
  Counter_1hz->reset();
  Smurf_frame->reset();
}



SmurfFilter::SmurfFilter(uint num_samples, uint num_records)
{
  records = num_records;
  samples = num_samples;
  clear = false;
  xd = (filter_t*)malloc(samples * records * sizeof(filter_t));  // don't bother checking valid, only at startup, fix later
  yd =  (filter_t*)malloc(samples * records * sizeof(filter_t));
  output = (avgdata_t*) malloc(samples * sizeof(avgdata_t));
  bn = 0;
  clear_filter();
}


void SmurfFilter::clear_filter(void)
{
  memset(xd, 0, records * samples * sizeof(filter_t));
  memset(yd, 0, records * samples * sizeof(filter_t));
  samples_since_clear = 0;  // reset
  order_n = -1;
  bn = 0;  // ring buffer pointers back to zero
}

void SmurfFilter::end_run()
{
  if(order_n == -1)
  {
    clear_filter();
  }
}

avgdata_t *SmurfFilter::filter(avgdata_t *data, int order, filter_t *a, filter_t *b, filter_t g)
{
  if(order_n != order)
  {
    clear_filter();
  }

  order_n = order;  // used to clear when using the flat average filter
  samples_since_clear++;

  if (order == -1) // special case flat average filter
  {
    for(uint n = 0; n <  samples;  n++)
    {
      *(yd + bn * samples + n) += (filter_t) (*(data+n)); // just sum into first record.
      *(output+n) = (avgdata_t) (*(yd+bn*samples + n) / (filter_t) samples_since_clear);  // convert
    }
  }
  else
  {
    bn = (bn + 1) % records;  // increment ring buffer pointer

    for (uint n = 0; n < samples; n++)
    {
      *(xd + bn * samples + n) = (filter_t) (*(data+n));  // convert to doubles
    }

    memset(yd + bn * samples, 0, samples * sizeof(filter_t)); // clear new y data

    for (uint n = 0; n < samples; n++) // loop over channels for filter
    {
      *(yd + bn * samples + n) = b[0] * *(xd + bn * samples + n);

      for(int r = 1; r <= order; r++) // one more record than order.(eg order = 0 is record)
      {
        int nx = (bn - r) % records;  // should give the correct buffer reference
        *(yd + bn * samples + n) += b[r] * *(xd + nx * samples + n) - a[r] * *(yd + nx * samples + n);
      }

      *(yd +bn * samples + n) = *(yd+bn * samples +n) / a[0];  // divide final answer
      *(output+n) = (avgdata_t) ( *(yd+bn*samples + n) * (g) );
    }
  }
  return(output);
}



SmurfTestData::SmurfTestData(uint ssamples_in, uint msamples_in)
{
  smurf_samples = ssamples_in;
  MCE_samples = msamples_in;
  counter = 0;
  counter16 = 0;
  toggle = 0;
  initial_sync = 0;
  init = false;
}

smurf_t* SmurfTestData::gen_test_smurf_data(smurf_t *input, uint mode, uint sync, uint8_t param)
{
  double xd;
  double s;

  switch (mode)
  {
    case 0:
      return(input);
      break;

    case 1:  // just return zeros
      memset(input, 0, smurf_samples * sizeof(smurf_t));
      break;

    case 2:  // linera increment with channel number
      for(uint j = 0; j < smurf_samples; j++)
        input[j] = j;  // just linear ramp
      break;

    case 3:
      for (uint j = 0; j < smurf_samples; j++)
      {
        input[j] = 2*toggle * square_wave_amplitude - square_wave_amplitude;
      }
      if (  (counter != sync) &&   ((sync % square_wave_cycles) == 0))
      {
        toggle = toggle ? 0 : 1;
        counter = sync;
      }
      break;

    case 4:
      for (uint j = 0; j < smurf_samples; j++)
      {
        int x = rand(); // random  number
        xd = x;   // convert to double for simplicity
        xd =  xd / (double) RAND_MAX - 0.5;  // scale
        xd =(double)  random_amplitude * xd;
        input[j] = (smurf_t) xd;
      }
      break;

    case 5:
      counter16++; // 16 bit wrapping counter
      xd = counter16;
      xd = 2 * 3.14159* xd / 65536.0;
      s = square_wave_amplitude *  sin(xd * (1.0 + (double)param));
      for (uint j = 0; j < smurf_samples; j++)
      {
        input[j] = s;
      }

    default:
      return(input);
  }
}

avgdata_t* SmurfTestData::gen_test_mce_data(avgdata_t *input, uint mode, uint sync , uint8_t param)
{
  if(!init)
  {
    init = true;
    initial_sync = sync;
  }

  switch (mode)
  {
    case 0:
      return(input);
      break;

    case 8:
      memset(input, 0, MCE_samples * sizeof(avgdata_t));
      break;

    case 9:
      for (uint j = 0; j < MCE_samples; j++) input[j] = j;
      break;

    case 10:
      for (uint j = 0; j < MCE_samples; j++) input[j] = param * (sync - initial_sync);
      break;

    case 14:    // also breaks checksum.  do ramp
      for (uint j = 0; j < MCE_samples; j++) input[j] = param* (sync - initial_sync);
      break;

    case 15:  // force framedrops
      for (uint j = 0; j < MCE_samples; j++) input[j] = 10000000;
      if (!(sync % 1000))  // drop one out of 1000 frames
      {
        printf("delay by %u ms ", param);
        delaytime.tv_sec = 0;  // 0 seconds in delay
        delaytime.tv_nsec = param * 1000000 + 250000000;  // WTF??? Has a 250ms offset.  why, oh god why?
        int q = nanosleep(&delaytime, NULL);
        printf("end delay, %d\n", q);
      }
      break;

    default:
      return(input);
  }
  return(input);
}


BOOST_PYTHON_MODULE(Smurf)
{
  PyEval_InitThreads();

  try
  {
    SmurfProcessor::setup_python();
  }
  catch (...)
  {
    printf("Failed to load the Smurf module. import rogue first\n");
  }

  printf("Smurf module loaded\n");
};

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <signal.h> 
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef __SMURF2MCE_H__
#define __SMURF2MCE_H__


// do we really need all these includes? 

//const uint smurf_raw_samples = 1024; // samples before masking. 
const uint smurf_raw_samples = 4096; // samples before masking.  
const uint smurfsamples = 528;  // number of SMuRF samples in a   frame was 528


const uint smurfheaderlength =128; // number of bytes in smurf header


typedef int16_t smurf_t;  // raw smurf data type. treat as uint
const uint smurfdatalength = smurf_raw_samples * sizeof(smurf_t) + smurfheaderlength; //  legnth of expected data from  pyrogue


typedef int avgdata_t;  // data type for averaged data

const uint tcp_header_size = 8; // number of bytes in tcp header for data checking
const uint32_t header = 0x89ABCDEF; // header used on TCP to help test transmission
const uint numframes = 8; // number of frames that can be received at one time.  (shoudl only need one unless link is slow) 

// unwrap rules
const int upper_unwrap = 0x6000;  // if we are above this and jump, assume a wrap
const int lower_unwrap = -0x6000; // if we are below this and jump, assume a wrap
typedef char wrap_t; // type for wrap counter, use char for now
const wrap_t wrap_start = 0x80;  //starting wrap value

const uint32_t average_sample_offset= 0; // used to offset average data to avoid wrap, may not be neeeded. 

const uint MCEheaderlength = 43; // words in MCE header note words are 32 bit
const uint MCEheader_CC_counter_offset = 1; 
typedef uint32_t MCE_t;
const uint MCE_frame_length = MCEheaderlength + smurfsamples; // number of words in MCE data. 

const uint MCE_header_version = 7;  // current header version
// mce header word offsets
const int mce_h_offset_header_version = 6;  // offset to header version. 
const int mce_h_syncbox_offset = 10;  // words offset to syncbox output



const size_t pyrogue_buffer_length = 0x8000; // not sure what the maximum size could be 


// smurf header byte offsets
const int h_version_offset = 0; // offset of version word
const int h_version_width = 1; // bytes of version word
const int h_ext_counter_offset = 68;  // resets with next MCE word
const int h_ext_counter_width = 4; // width
const int h_frame_counter_offset = 84;  // raw frame counter. 
const int h_frame_counter_width = 4; 
const int h_mce_syncword_offset = 96;  // 20 bit MCE sync workd
const int h_mce_syncword_width = 5;  // yes 40 bits, bletch. 




const uint datalen = tcp_header_size + MCEheaderlength*sizeof(MCE_t) + smurfsamples * sizeof(MCE_t); // bytes of  data to tcp
const uint tcplen = datalen * 2; // after byte split
const uint tcpreclen = tcplen * 0x10000;  // allow for multiple  reads in one frame proably big enough

const char server_port_number[] = "5433";  // default unless overridden

//const char server_ip_addr[] = "127.0.0.1"; //default
//const char server_ip_addr[] = "134.79.216.240"; //default - lcls-dev3
const char server_ip_addr[] = "134.79.228.97"; // tid-pc93130
const char pipe_name[] = "/tmp/smurffifo"; // named pipe for MCE interface


#endif

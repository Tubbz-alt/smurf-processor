#include <time.h> // used for the test  sleep function. Just for the t est prgram. 
//#include "smurf2mce.h"  // defines all the SMURF stuff

#ifndef __SMURFTCP_H__
#define __SMURFTCP_H__

void error(const char *msg); // error handler


class Smurftcp  // does data transfer
{
public:
  bool initialized;  // has all data been allocated, connection made. 
  int sockfd;  // socket 
  struct addrinfo *server;  // will hold server address structure
  char *databuffer; // data before splitting into nibbles for tcpbuffer
  char *tcpbuffer;  // data sent over tcp, uses lower nibble, top bit as marker.
  const char *ip;
  const char *port;  
  bool connected; // are we connected? 


  Smurftcp(const char *port_number, const char *ip_string);  // constructor
  bool connect_link(void); // tries to make tcp connection
  bool disconnect_link(void); // cleans up link to try again
  char *get_buffer_pointer(void );// returns pointer for writing data
  void write_data(size_t bytes); // writes data to tcp, (does most of the work).
  
  ~Smurftcp(); // destructor, probably not needed
};

class MCEHeader // generates the MCE header data
{
public:
  MCE_t mce_header[MCEheaderlength]; // header.
  MCEHeader(void); // creates header  
};


class SmurfHeader //generates and decodes SMURF data header
{
public:
  char header[smurfheaderlength]; // full header bytes
  SmurfHeader(void); // creates header with num samples
  void set_average_bit(int n);
  void clear_average_bit(int n);
  int get_average_bit(int n); 

};



#endif

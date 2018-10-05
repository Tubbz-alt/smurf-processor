// smurfpipetest
#include "../../common/smurf2mce.h"

void error(const char *msg){ perror(msg);};  

class check_data
{
 public:
  uint last_CC_counter;
  uint missed_frame_count;
  uint last_syncbox; 
  uint total_cycles;

  check_data(void);
  bool test(MCE_t *data);

};

int main()
{
  int fifo_fd; // fifo  descritor
  int j, num, x;
  bool runforever = true; 
  MCE_t data[MCE_frame_length]; // will hold received data
  check_data *C;
  C = new check_data();
  printf("starting SMuRF pipe test \n");
  
  
  if(-1 == mkfifo(pipe_name, 0666)) 
    {
      error("can't create new fifo (OK if alreadyexists) \n");
      //exit(0);
    }
  if( -1 == (fifo_fd = open(pipe_name, O_RDONLY)))
    {
      printf("error opening fifo\n");
      unlink(pipe_name);  // delete the fifo
      exit(0);
    }
  printf("fifo_fd = %d\n", fifo_fd);
  for(j = 0; 1; j++)// loop forever
    {
      if(-1 == (num = read(fifo_fd, data, sizeof(MCE_t) * MCE_frame_length)))
	{ 
	  error("read error"); 
	  break;
	}
      if(!num) {j--; continue; }; // read 0 bytes, try again (inefficient)
      if(num != (MCE_frame_length * sizeof(MCE_t)))
	{
	  printf("frame len = %u, wanted %u\n", num, MCE_frame_length);
	}
      C->test(data); // check that daata is OK
      if(!(j % slow_divider))
	{
	  printf("P: Local_frame = %u, sync = %u,  data0 = %d,  missed_frm = %u\n", j,  data[MCEheader_syncbox_offset] & 0xFFFFFFFF, data[43], C->missed_frame_count);
	  //scanf("%d", &x);
	}
    }
  close(fifo_fd);
  unlink(pipe_name);  // delete the fif o
}


check_data::check_data(void)
{
  last_CC_counter = 0; 
  missed_frame_count =  0; 
  total_cycles = 0;
  last_syncbox = 0;
}

bool check_data::test(MCE_t *data)
{
  uint x;
  uint j;
  MCE_t checksum;
  uint32_t syncbox; 
  
  x = data[MCEheader_CC_counter_offset];
  if ((x != last_CC_counter + 1))
    {
      last_CC_counter = x;
      printf("counter error %u \n", x); 
      return(false);
    }
  last_CC_counter = x;
  syncbox = data[MCEheader_syncbox_offset];
  
  if (total_cycles > 1000) // allow startup time
    {
      if ((syncbox != last_syncbox + 1))
	{
	  missed_frame_count = missed_frame_count + 1; 
	  //printf("mised frames = %u \n", missed_frame_count);
	}
    }

  last_syncbox = syncbox; 
  total_cycles++; 
  checksum = data[0];
  for(j = 1; j < MCE_frame_length; j++) checksum = checksum ^ data[j];
  if(checksum)
    {
      printf(" cerror %x dl = %x, %x,  %x\n", checksum,  data[6], data[MCE_frame_length-2], data[MCE_frame_length-1]);
      return(false);
    }
  
  return(true); 
}

#include <stdio.h>
#include <stdlib.h>
#include "include/voideye.h"


int main( int argc , char ** argv )
{
  printf( "Starting up VoidEye test.\n" );
  int rp = atoi( argv[1] );
  printf( "Red procentage: %d\n" , rp );
  init_test( (const char*)rp );
  video_loop();
  return 0;
}

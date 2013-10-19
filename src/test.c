#include <stdio.h>
#include <stdlib.h>
#include "include/voideye.h"


int main( int argc , char ** argv )
{
  printf( "Starting up VoidEye test.\n" );
  int rp = atoi( argv[1] );
  printf( "Red procentage: %d\n" , rp );
  init_test( (const char*)rp );
  update_texture();
  //remove_colours();
  //update_texture();
  // int b , d , a;
  // b = find_brightest();
  // d = find_darkest();
  // a = find_avarage();
  // printf( "b: %d d: %d a: %d\n", b , d , a );
  apply_contrast( 256 );
  //update_texture();
  create_groups();
  return 0;
}

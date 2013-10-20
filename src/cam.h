#ifndef __CAM_H__
#define __CAM_H__
typedef struct
{
  char * primary_buffer;
  char * secondary_buffer;
  int primary_state;
  int secondary_state;
} DualBuffer;

//DualBuffer new_dualbuffer( int );
//void free_dualbuffer( DualBuffer * );


int init_cam();
void take_frame( char * );
void end_cam();

#endif
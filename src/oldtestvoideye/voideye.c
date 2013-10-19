#include <stdio.h>
#include <stdlib.h>
#include "include/voideye.h"
#include <SDL/SDL.h>
#include <math.h>
#include "include/picam.h"
#include "interface/mmal/mmal.h"

typedef unsigned char byte;

typedef struct
{
  byte * b;
  byte * g;
  byte * r;
} RGB24;

typedef struct
{
  byte colour;
  int id;
} Unit;

typedef struct
{
  short w,h;
  Unit * units;
} Cell;

typedef struct 
{
  int x , y;
  byte colour;
  int id;
} Job;

SDL_Surface * input;
SDL_Texture * texture;
SDL_Window * window;
SDL_Renderer * renderer;

int idPool = 1;
int nextSeed = 0;
Job * jobQueue;
int jobQueueIndex = 0;

void init_test( const char * fname )
{
  atexit( quit_test );

  // CAMERA STUFFF

  long bufsize = 0L;
  PicamParams parms;
  parms.exposure = MMAL_PARAM_EXPOSUREMODE_AUTO;
  parms.meterMode = MMAL_PARAM_EXPOSUREMETERINGMODE_AVERAGE;
  parms.imageFX = MMAL_PARAM_IMAGEFX_NONE;
  parms.awbMode = MMAL_PARAM_AWBMODE_AUTO;
  parms.ISO = 400;
  parms.sharpness = 0;
  parms.contrast = 0;
  parms.brightness= 50;
  parms.saturation = 0;
  parms.videoStabilisation = 0; 
  parms.exposureCompensation = 0 ;
  parms.rotation = 0;
  parms.hflip = 0;
  parms.vflip = 0;
  char * buffer = takeRGBPhotoWithDetails( 640 , 480 , &parms , &bufsize );
  if( buffer && bufsize )
  {
    FILE * fp = fopen( "grap.bmp" , "w" );
    fwrite( buffer , sizeof( char ) , bufsize , fp );
    fclose( fp );
    printf( "Write file ( %d bytes )" , bufsize );
  }else
    printf( "FAILED TO GRAP IMAGE: %d:%d" , buffer , bufsize );

  // REST

  printf( "Initializing SDL2.\n" );
  if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ) )
  {
    printf( "Failed to load SDL2: %s\n" , SDL_GetError() );
    exit(1);
  }

  printf( "Initializing SDL_image.\n" );
  if( IMG_Init( IMG_INIT_PNG ) != IMG_INIT_PNG )
  {
    printf( "Failed to load SDL_image: %s\n" , IMG_GetError() );
    exit(1);
  }

  input = IMG_Load( "test.png" );
  if( !input )
  {
    printf( "Failed to load input! %s\n" , IMG_GetError() );
    exit( 1 );
  }

  printf( "Creating a canvas.\n" );
  window = SDL_CreateWindow( "VoidEye" , SDL_WINDOWPOS_UNDEFINED , 
    SDL_WINDOWPOS_UNDEFINED , input->w , input->h , SDL_WINDOW_SHOWN );
  if( ! window )
  {
    printf( "Failed to create a window: %s\n" , SDL_GetError() );
    exit(1);
  }

  renderer = SDL_CreateRenderer( window , -1 , 0 );
  if( ! renderer )
  {
    printf( "Failed to create a renderer: %s\n" , SDL_GetError() );
    exit(1);
  }

  texture = SDL_CreateTexture( renderer ,
                               input->format->format,
                               SDL_TEXTUREACCESS_STREAMING,
                               input->w , input->h );

  printf( "Initialized.\n" );
}

void update_texture()
{
  SDL_UpdateTexture( texture , NULL, input->pixels , input->pitch );
  SDL_SetRenderDrawColor( renderer , 0, 0, 0, 255);
  SDL_RenderCopy( renderer , texture , NULL , NULL );
  SDL_RenderPresent( renderer );
  //SDL_Delay( 1000 );
}

void remove_colours()
{
  byte * pixels = ( byte * ) input->pixels;
  int sum;
  for( int i = 0; i < input->w * input->h * input->format->BytesPerPixel; i += 3 )
  {
    sum = ( pixels[i+2] + pixels[i+1] + pixels[i] ) / 3;
    pixels[i+2] = pixels[i+1] = pixels[i] = sum;
  }
}

int find_brightest()
{
  byte * pixels = ( byte * ) input->pixels;
  int brightest = 0;
  for( int i = 0; i < input->w * input->h * input->format->BytesPerPixel; i += 3 )
  {
    if( pixels[i] > brightest ) brightest = pixels[i];
  }
  return brightest;
}

int find_darkest()
{
  byte * pixels = ( byte * ) input->pixels;
  int darkest = 255;
  for( int i = 0; i < input->w * input->h * input->format->BytesPerPixel; i += 3 )
  {
    if( pixels[i] < darkest ) darkest = pixels[i];
  }
  return darkest;
}

int find_avarage()
{
  byte * pixels = ( byte * ) input->pixels;
  int avarage = pixels[0];
  for( int i = 3; i < input->w * input->h * input->format->BytesPerPixel; i += 3 )
  {
    avarage = ( avarage + pixels[i] ) / 2;
  }
  return avarage;
}

void apply_contrast( int amount )
{
  byte * pixels = ( byte * ) input->pixels;
  for( int i = 0; i < input->w * input->h * input->format->BytesPerPixel; i += 3 )
  {
    int v = pixels[i];
    pixels[i] = pixels[i+1] = pixels[i+2] = v < 127 ? 0 : 255;
  }
}

void queue_job( Job job )
{
  jobQueue[jobQueueIndex++] = job;
}

void colourit( byte * pixels , int i , byte r , byte g , byte b )
{
  pixels[ i*3 ] = r;
  pixels[ i*3 +1] = g;
  pixels[ i*3 +2] = b;
}

void seed_search( Cell * cell , int x , int y , int colour , int groupid )
{
  int pos = x + y * cell->w;
  Unit * units = cell->units;
  if( units[pos].colour != colour || units[pos].id != 0 )
  {
    if( units[pos].id == 0 && nextSeed == -1 ) {
      nextSeed = pos;
    }
    return;
  }
  units[pos].id = groupid;
  byte * pixels = ( byte * ) input->pixels;
  srand( groupid ); 
  int i = (x + cell->w * y);
  byte r = rand()%256;
  byte b = rand()%256;
  byte g = rand()%256;
  colourit( pixels , i , r , g , b );
  if( x > 0 )
  {
    queue_job( ( Job ){ x - 1 , y , colour , groupid } );
  }else colourit( pixels , i , 255 , 0 , 0 );
  if( y > 0 )
  {
    queue_job( ( Job ){ x , y - 1 , colour , groupid } );
  }else colourit( pixels , i , 0 , 0 , 255 );
  if( y < ( cell->h - 1 ))
  {
    queue_job( ( Job ){ x , y + 1 , colour , groupid } );
  }
  if( x < ( cell->w - 1 ))
  {
    queue_job( ( Job ){ x + 1 , y , colour , groupid } );
  }
  update_texture();
}

int scan_for_seed( Cell * cell )
{
  if( nextSeed == -1 ) return nextSeed;
  for( int i = 0; i < cell->w*cell->h; i++ )
  {
    if( cell->units[i].id == 0 ) return ( nextSeed = i );
  }
  printf("No seeds left!\n");
  return ( nextSeed = -1 );
}

void unitize_cell( Cell * cell )
{
  SDL_RenderClear( renderer );
  jobQueue = ( Job * ) malloc( cell->w * cell->h * sizeof( Job ) );
  printf("Queue size: %d bytes\n", cell->w * cell->h * sizeof( Job ) );
  printf( "Starting grouping:\n" );
  // Search for ungrouped sample
  while( scan_for_seed( cell ) != -1 )
  {
    srand( idPool );
    SDL_SetRenderDrawColor( renderer , rand() % 256 , rand() % 256 , rand() % 256 , 255 );
    // Queue the the sample
    queue_job( ( Job ) { nextSeed % cell->w , nextSeed / cell->w , 
      cell->units[nextSeed].colour , idPool++ } );
    // While the queue isn't empty
    while( jobQueueIndex )
    {
       // Pop a job from the queue
      Job currentJob = jobQueue[ --jobQueueIndex ];
      // Perform search on the current job sample
      seed_search( cell , currentJob.x , currentJob.y , currentJob.colour , 
        currentJob.id );
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
           // handle your event here
      }
    }
  }
  printf("Ended grouping with %d groups.\n", idPool - 1 );
  SDL_Delay( 1000 );
}

Cell * create_cell()
{
  printf( "%d == %d %d\n" , input->w * 3 , input->pitch , input->format->BytesPerPixel );
  // allocate the needed data
  Unit * units = ( Unit * ) malloc( ( input->w + 1 ) * input->h * sizeof( Unit ) );
  printf( "Allocated %d bytes.\n" , ( input->w + 1 ) * input->h * sizeof( Unit ) );
  Cell * cell = ( Cell * ) malloc( sizeof( Cell ) );
  cell->units = units;
  cell->w = input->w + 1;
  cell->h = input->h;
  byte * pixels = ( byte * ) input->pixels;
  for( int i = 0; i < ( input->w + 1 ) * input->h * input->format->BytesPerPixel; i += 3 )
  {
    // Assign the colour to be that of the pixel, and the group to be NULL.
    units[i/3] = ( Unit ) { pixels[i] , 0 };
  }
  return cell;
}

void render_cell( Cell * cell )
{
  Unit * units = cell->units;
  byte * pixels = ( byte * ) input->pixels;
  for( int i = 0; i < ( input->w + 1 ) * input->h * input->format->BytesPerPixel; i += 3 )
  {
    //printf( "Rendering group: %d\n" , units[i/3].id );
    srand( units[i/3].id ); // Set the seed to the group ID, so that we generate
                            // the same colours everytime we render this group. 
    pixels[i] = rand()%256;   // Random RBG
    pixels[i+1] = rand()%256;
    pixels[i+2] = rand()%256;
  }
}

void create_groups()
{
  Cell * cell = create_cell();
  unitize_cell( cell );
  render_cell( cell );
  free( cell->units );
  free( cell );
  update_texture();
}

void quit_test(  )
{
  printf( "Quitting SDL_image.\n" );
  IMG_Quit();
  SDL_FreeSurface( input );
  SDL_DestroyRenderer( renderer );
  SDL_DestroyWindow( window );
  SDL_DestroyTexture( texture );
  SDL_FreeSurface( input );
  SDL_Quit();
  printf( "Quit.\n" );
}

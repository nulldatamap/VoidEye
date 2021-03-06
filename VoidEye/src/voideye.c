#include <stdio.h>
#include <stdlib.h>
#include "include/voideye.h"
#include <SDL/SDL.h>
#include <math.h>

#define INPUT_WIDTH 640
#define INPUT_HEIGHT 480
#define INPUT_DEPTH 24
#define INPUT_BPP INPUT_DEPTH / 8
#define INPUT_PITCH INPUT_WIDTH * INPUT_BPP
#define INPUT_SIZE INPUT_WIDTH * INPUT_HEIGHT

#define DS_SCALE 5
#define DS_WIDTH INPUT_WIDTH / DS_SCALE
#define DS_HEIGHT INPUT_HEIGHT / DS_SCALE
#define DS_DEPTH 24
#define DS_BPP DS_DEPTH / 8
#define DS_PITCH DS_WIDTH * DS_BPP
#define DS_SIZE DS_WIDTH * DS_HEIGHT

#define MASK_R 0xFF
#define MASK_G 0xFF00
#define MASK_B 0xFF0000
#define MASK_A 0x00

typedef unsigned char byte;

typedef struct
{
  byte r;
  byte g;
  byte b;
} Pixel;

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
  int id;
} Job;

typedef struct
{
  int id;
  int count;
  int maxx,minx,maxy,miny;
} Group;

typedef struct
{
  int centerx,centery;
  int size;
} Square;

SDL_Surface * input;
SDL_Surface * downscale;
SDL_Surface * window;
Pixel * pixels;
Pixel * dspixels;

int idPool = 1;
int nextSeed = 0;
Job * jobQueue;
int jobQueueIndex = 0;

byte avarage[3];
int red_procentage;

void init_test( const char * fname )
{
  atexit( quit_test );

  printf( "Initializing SDL.\n" );
  if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ) )
  {
    printf( "Failed to load SDL2: %s\n" , SDL_GetError() );
    exit(1);
  }

  printf( "Creating a window.\n" );
  window = SDL_SetVideoMode(640, 480, 0, SDL_FULLSCREEN | SDL_SWSURFACE);
  if( ! window )
  {
    printf( "Failed to create window: %s\n" , SDL_GetError() );
    exit(1);
  }
  
  red_procentage = (int)fname;

  printf( "Starting camera\n" );
  if( init_cam() )
  {
    printf( "FAILED TO START CAMERA!\n" );
    exit( 1 );
  } 
  pixels = ( Pixel * ) malloc( INPUT_SIZE * sizeof( Pixel ) );
  dspixels = ( Pixel * ) malloc( DS_SIZE * sizeof( Pixel ) ); // Downscaled version
  input = SDL_CreateRGBSurfaceFrom( (byte *)pixels , INPUT_WIDTH, INPUT_HEIGHT, INPUT_DEPTH, INPUT_PITCH, MASK_R , MASK_G , MASK_B , MASK_A );
  downscale = SDL_CreateRGBSurfaceFrom( (byte *)dspixels , DS_WIDTH, DS_HEIGHT, DS_DEPTH, DS_PITCH, MASK_R , MASK_G , MASK_B , MASK_A );
  take_frame( (byte * )pixels );
  if( !input )
  {
    printf( "Failed to load input! %s\n" , SDL_GetError() );
    exit( 1 );
  }

  printf( "Initialized.\n" );
}

void update_texture()
{
  SDL_BlitSurface( input, NULL, window, NULL );
  SDL_Flip( window );
  SDL_Delay( 1000 );
}

void remove_colours()
{
  byte * pixels = ( byte * ) input->pixels;
  int sum;
  int i;
  for( i = 0; i < input->w * input->h * input->format->BytesPerPixel; i += input->format->BytesPerPixel )
  {
    sum = ( pixels[i+2] + pixels[i+1] + pixels[i] ) / 3;
    pixels[i+2] = pixels[i+1] = pixels[i] = sum;
  }
}

int find_brightest()
{
  byte * pixels = ( byte * ) input->pixels;
  int brightest = 0;
  int i;
  for( i = 0; i < input->w * input->h * input->format->BytesPerPixel; i += input->format->BytesPerPixel )
  {
    if( pixels[i] > brightest ) brightest = pixels[i];
  }
  return brightest;
}

int find_darkest()
{
  byte * pixels = ( byte * ) input->pixels;
  int darkest = 255;
  int i;
  for( i = 0; i < input->w * input->h * input->format->BytesPerPixel; i += input->format->BytesPerPixel )
  {
    if( pixels[i] < darkest ) darkest = pixels[i];
  }
  return darkest;
}


void find_avarage()
{
  byte * pixels = ( byte * ) input->pixels;
  avarage[0] = pixels[0];
  avarage[1] = pixels[1];
  avarage[2] = pixels[2];
  int i;
  for( i = 3; i < input->w * input->h * input->format->BytesPerPixel; i += input->format->BytesPerPixel )
  {
    avarage[0] = ( avarage[0] + pixels[i] ) / 2;
    avarage[1] = ( avarage[1] + pixels[i+1] ) / 2;
    avarage[2] = ( avarage[2] + pixels[i+2] ) / 2;
  }
  printf( "Avarage: r %d g %d b %d\n" , avarage[0] , avarage[1] , avarage[2] );
}

#define at( x , y ) x + ( y * INPUT_WIDTH )
#define dsat( x , y ) x + ( y * DS_WIDTH )

void do_downscale()
{
  printf( "Downscaling.\n" );
  int x,y;
  for( x = 0; x < DS_WIDTH; x++ ) 
    for( y = 0; y < DS_HEIGHT; y++ )
    {
      dspixels[dsat( x , y )] = pixels[at( x * 5 , y * 5 )];
    }
  /*printf( "Downscaled. ( %d %d %d )\n" , r , g , b );
  SDL_BlitSurface( downscale, NULL, window, NULL );
  SDL_Flip( window );
  SDL_Delay( 1000 );*/
}

void apply_contrast( int amount )
{
  //find_avarage();
  do_downscale();
  int i;
  for( i = 0; i < DS_SIZE; i ++ )
  {
    int total = ( dspixels[i].r + dspixels[i].g + dspixels[i].b );
    int rp = total ? ( dspixels[i].r*100 / total ) : 0;
    if( rp >= red_procentage )
      dspixels[i] = ( Pixel ) { 0xFF , 0xFF , 0xFF };
    else
      dspixels[i] = ( Pixel ) { 0x00 , 0x00 , 0x00 };
  }
  SDL_BlitSurface( downscale, NULL, window, NULL );
  SDL_Flip( window );
  SDL_Delay( 1000 );
}

void queue_job( Job job )
{
  jobQueue[jobQueueIndex++] = job;
}

int biggesty = 0;

void seed_search( Cell * cell , int x , int y , int groupid )
{
  int pos = dsat( x , y );
  Unit * units = cell->units;
  if( units[pos].colour != 0xFF || units[pos].id != 0 )
  {
    return;
  }
  units[pos].id = groupid;
  if( x > 0 )
  {
    queue_job( ( Job ){ x - 1 , y , groupid } );
  }
  if( y > 0 )
  {
    queue_job( ( Job ){ x , y - 1 , groupid } );
  }
  if( y < ( cell->h - 1 ))
  {
    queue_job( ( Job ){ x , y + 1 , groupid } );
  }
  if( x < ( cell->w - 1 ))
  {
    queue_job( ( Job ){ x + 1 , y , groupid } );
  }
}

int scan_for_seed( Cell * cell )
{
  if( nextSeed == -1 ) return nextSeed;
  int i;
  for( i = 0; i < DS_SIZE; i++ )
    if( cell->units[i].id == 0 && cell->units[i].colour == 0xFF )
      return ( nextSeed = i );
  printf("No seeds left!\n");
  return ( nextSeed = -1 );
}

void unitize_cell( Cell * cell )
{
  jobQueue = ( Job * ) malloc( DS_SIZE * sizeof( Job ) );
  printf("Queue size: %d bytes\n", DS_SIZE * sizeof( Job ) );
  printf( "Starting grouping:\n" );
  // Search for ungrouped sample
  while( scan_for_seed( cell ) != -1 )
  {
    // Queue the the sample
    queue_job( ( Job ) { nextSeed % cell->w , nextSeed / cell->w , idPool++ } );
    // While the queue isn't empty
    while( jobQueueIndex )
    {
       // Pop a job from the queue
      Job currentJob = jobQueue[ --jobQueueIndex ];
      // Perform search on the current job sample
      seed_search( cell , currentJob.x , currentJob.y , currentJob.id );
    }
  }
  printf("Ended grouping with %d groups.\n", idPool - 1 );
}

Cell * create_cell()
{
  printf( "%d == %d %d\n" , input->w * 3 , input->pitch , input->format->BytesPerPixel );
  // allocate the needed data
  Unit * units = ( Unit * ) malloc( DS_SIZE * sizeof( Unit ) );
  printf( "Allocated %d bytes.\n" , DS_SIZE * sizeof( Unit ) );
  Cell * cell = ( Cell * ) malloc( sizeof( Cell ) );
  cell->units = units;
  cell->w = DS_WIDTH;
  cell->h = DS_HEIGHT;
  int i;
  for( i = 0; i < DS_SIZE; i++ )
  {
    // Assign the colour to be that of the pixel, and the group to be NULL.
    units[i] = ( Unit ) { dspixels[i].r , 0 };
  }
  return cell;
}

void group_units( Cell * cell )
{
  Group * groups = ( Group * ) malloc( sizeof( Group ) * (idPool ) );
  int i,x,y;
  Group * g;
  printf( "Grouping groups.\n" );
  for( i = 0; i < idPool; i++)
    groups[i] = ( Group ) { i+1 , 0 , -1 , -1 , -1 , -1 };
  // Build groups
  int biggesti = 0;
  for( i = 0; i < DS_SIZE; i++  )
  {
    if( cell->units[i].colour != 0xFF ) continue; // I know it's racist.
    x = i % DS_WIDTH;
    y = i / DS_WIDTH;
    g = &( groups[ cell->units[i].id-1 ] );
    biggesti = i;
    g->count++;
    // MIN X
    if( g->minx == -1 )
      g->minx = x;
    else
      g->minx = x < g->minx ? x : g->minx;
    // MAX X
    if( g->maxx == -1 )
      g->maxx = x;
    else
      g->maxx = x > g->maxx ? x : g->maxx;
    // MIN Y
    if( g->miny == -1 )
      g->miny = y;
    else
      g->miny = y < g->miny ? y : g->miny;
    // MAX Y
    if( g->maxy == -1 )
      g->maxy = y;
    else
      g->maxy = y > g->maxy ? y : g->maxy;
  }
  printf( "(%d)Building squares.\n" , biggesti );
  // Destroy incompetent groups
  Square * squares = ( Square * ) malloc( sizeof( Square ) * (idPool - 1 ) );
  int squarecount = 0;
  int width,height;
  for( i = 0; i < idPool; i++)
  {
    g = &( groups[i] );
    printf( "%dx[ %d-%d | %d-%d ]: " , g->count , g->minx , g->maxx , g->miny , g->maxy );
    if( g->count <= 5 )
    {
      printf( "To few members, %d <= 5\n" , g->count );
      continue;
    }
    width = g->maxx - g->minx;
    if( width <= 2 )
    {
      printf( "Too slim, %d <= 2\n" , width );
      continue;
    }
    height = g->maxy - g->miny;
    if( height <= 2 )
    {
      printf( "Too small, %d <= 2\n" , height );
      continue;
    }
    if( ( height * 100 ) / width > 130 )
    {
      printf( "To high, %d > 130\n" , ( height * 100 ) / width );
      continue;
    }
    if( ( width * 100 ) / height > 130 )
    {
      printf( "To wide, %d > 130\n" , ( width * 100 ) / height );
      continue;
    }
    printf( "added.\n" );
    squares[squarecount++] = ( Square ) { g->minx + ( width / 2 ) , g->miny + ( height / 2 ) , ( width + height ) / 2 };
  }
  printf( "Freeing data.\n" );
  free( groups );
  printf( "Resizing array to fit sqaure count.\n" );
  squares = realloc( squares , sizeof( Square ) * squarecount );
  printf( "Made %d squares." , squarecount );
}

void create_groups()
{
  Cell * cell = create_cell();
  unitize_cell( cell );
  group_units( cell );
  free( cell->units );
  free( cell );
  //update_texture();
}

void quit_test(  )
{
  printf( "Shutting down camera.\n" );
  end_cam();
  printf( "Quitting SDL.\n" );
  SDL_FreeSurface( input );
  SDL_FreeSurface( window );
  SDL_Quit();
  printf( "Quit.\n" );
}

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
  int x,y;
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
  //SDL_Delay( 1000 );
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
  SDL_Delay( 0 );
}

void queue_job( Job job )
{
  if( jobQueueIndex >= DS_SIZE * 2 )
    printf( "BAD: %d %d %d" , job.x , job.y , job.id );
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
  jobQueue = ( Job * ) malloc( ( DS_SIZE ) * sizeof( Job ) * 2 );
  printf("Queue size: %d bytes\n", DS_SIZE * sizeof( Job ) * 2 );
  printf( "Starting grouping:\n" );
  idPool = 1;
  jobQueueIndex = 0;
  nextSeed = 0;
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
  free( jobQueue );
  printf("Ended grouping with %d groups.\n", idPool - 1 );
}

Cell * create_cell()
{
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

Square * group_units( Cell * cell , int * sc )
{
  Group * groups = ( Group * ) malloc( sizeof( Group ) * (idPool ) );
  int i,x,y;
  Group * g;
  printf( "Grouping groups.\n" );
  for( i = 0; i < idPool; i++)
    groups[i] = ( Group ) { i+1 , 0 , -1 , -1 , -1 , -1 };
  // Build groups
  int biggesti = 0;
  for( x = 0; x < DS_WIDTH; x++  )
  for( y = 0; y < DS_HEIGHT; y++ )
  {
    i = x + ( y * DS_WIDTH );
    if( cell->units[i].colour != 0xFF ) continue; // I know it's racist.
    g = &( groups[ cell->units[i].id-1 ] );
    biggesti = y;
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
    if( ( height * 100 ) / width > 145 )
    {
      printf( "To high, %d > 145\n" , ( height * 100 ) / width );
      continue;
    }
    if( ( width * 100 ) / height > 145 )
    {
      printf( "To wide, %d > 145\n" , ( width * 100 ) / height );
      continue;
    }
    printf( "added.\n" );
    squares[squarecount++] = ( Square ) { g->minx , g->miny  , ( width + height ) / 2 };
  }
  printf( "Freeing data.\n" );
  free( groups );
  printf( "Resizing array to fit sqaure count.\n" );
  squares = realloc( squares , sizeof( Square ) * squarecount );
  *sc = squarecount;
  printf( "Made %d squares.\n" , squarecount );
  return squares;
}

void sort_squares( Square * squares , int squarecount )
{
  int i;
  Square temp;
  printf( "Sorting squares!\n" );
  if( squarecount <= 2 ) return;
sort:
  for( i = 0; i < squarecount-1; i++ )
  {
    if( squares[i].size < squares[i+1].size )
    {
      printf( "%d < %d, swapping %d and %d\n" , squares[i].size , squares[i+1].size , i , i+1 );
      temp = squares[i];
      squares[i] = squares[i+1];
      squares[i+1] = temp;
      goto sort;
    }
  }
  printf( "Sorted.\n" );
  return;
}

void render_squares( Square * squares , int squarecount )
{
  int i;
  Square s;
  for( i = 0; i < squarecount; i++ )
  {
    s = squares[i];
    SDL_Rect rect = { s.x , s.y , s.size , s.size };
    SDL_FillRect( downscale , &rect , 0xFF0000 );
  }
  SDL_BlitSurface( downscale , NULL , window , NULL );
  SDL_Flip( window );
  SDL_Delay( 0 );
}

int abs( int a )
{
  return a < 0 ? -a : a;
}

int sign( int a )
{
  return a > 0 ? 1 : ( a < 0 ? -1 : 0 );
}


void render_line( SDL_Surface * s , int x , int y , int x2 , int y2 , Pixel colour )
{
  Pixel * px = ( Pixel * ) s->pixels;
  int dx = abs( x2 - x );
  int dy = abs( y2 - y );
  int sx , sy , r , e2;
  if( x < x2 )
    sx = 1;
  else
    sx = -1;
  if( y < y2 )
   sy = 1;
  else
    sy = -1;
  r = dx - dy;
  while( 1 )
  {
    px[x + ( y * s->w )] = colour;
    if( x == x2 && y == y2 )
      break;
    e2 = r * 2;
    if( e2 > -dy )
    {
      r -= dy;
      x += sx;
    }
    if( x == x2 && y == y2 )
    {
      px[ x + ( y * s->w ) ] = colour;
      break;
    }
    if( e2 < dx )
    {
      r += dx;
      y += sy;
    }
  }
}

void render_center( Square * squares , int squarecount )
{
  int ax = 0;
  int ay = 0;
  int t =  ( squarecount > 4 ? 4 : squarecount );
  int i;
  for( i = 0; i < t; i++ )
  {
    ax += squares[i].x + ( squares[i].size / 2 );
    ay += squares[i].y + ( squares[i].size / 2 );
  }
  ax /= t;
  ay /= t;
  render_line( downscale , 0 , ay ,  DS_WIDTH - 1 , ay , ( Pixel ) { 0xFF , 00 , 00 } );
  render_line( downscale , ax , 0 ,  ax , DS_HEIGHT - 1 , ( Pixel ) { 0xFF , 00 , 00 } );
  SDL_BlitSurface( downscale , NULL  , window , NULL );
  SDL_Flip( window );
  SDL_Delay( 0 );  
}


void create_groups()
{
  Cell * cell = create_cell();
  unitize_cell( cell );
  int squarecount = 0;
  Square * squares = group_units( cell , &squarecount );
  sort_squares( squares , squarecount );
  printf( "Rendering\n" );
  render_squares( squares , squarecount );
  if( squarecount <= 2 )
  {
    printf( "Not enough squares to build area.\n" );
  }else
  {
    render_center( squares , squarecount );
  }
  free( cell->units );
  free( cell );
  free( squares );
  //update_texture();
}

void video_loop()
{
  int i;
  for( i = 0; i < 10; i++ )
  {
    printf( "======= INTERATION %d =======\n" , i );
    take_frame( (byte * )pixels );
    update_texture();
    apply_contrast( 911 );
    create_groups();
  }
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
 
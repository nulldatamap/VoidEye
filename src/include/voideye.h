#ifndef __H_VOIDEYE__
#define __H_VOIDEYE__

void init_test( const char * );
void quit_test();
void update_texture();
void remove_colours();
int find_brightest();
int find_darkest();
//char* find_avarage();
void apply_contrast( int );
void create_groups();

#endif
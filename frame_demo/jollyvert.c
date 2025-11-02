#include <stdint.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int main()
{
	int x,y,n;
	unsigned char *data = stbi_load("jollywrencher.png", &x, &y, &n, 0);

	printf( "#ifndef _JOLLYWRENCHER_H\n" );
	printf( "#define _JOLLYWRENCHER_H\n" );

	printf( "const uint8_t jollywrencher[%d][%d] = { \n", y, x );
	int cx, cy;
	for( cy = 0; cy < y; cy++ )
	{
		printf( "\t {" );
		for( cx = 0; cx < x; cx++ )
		{
			printf( "0x%02x, ", ( data[cx*n+cy*x*n] > 127)?0xff:0x00 );
		}
		printf( "},\n" );
	}
	printf( "};\n" );

	printf( "#endif\n" );

	return 0;
}


#include "ExampleSystemManager.h"

#include <sstream>
#include <fstream>

#ifdef SYSTEM_DYNAMIC_LINKING
#pragma message "Using dynamic loading"
#endif

char* getFileString(const char* filename, size_t& size)
{
    FILE* pFile = fopen(filename, "rb");
    ASSERT( pFile != NULL, "Error opening file!" );

    fseek (pFile , 0 , SEEK_END);
    size = ftell (pFile);
    rewind (pFile);

    char* buffer = (char*) malloc ( sizeof(char) * size );

    int result = fread( buffer, 1, size, pFile );
    ASSERT( result == size, "Error reading file!" );

    fclose (pFile);
    return buffer;
}

int main(int argc, char **argv)
{
    ExampleSystemManager* test = new ExampleSystemManager();

    if ( argc < 2 )
    {
        printf("Run with a file as the inital state! Starting with nothing...\n");
        test->StartUpSystems(NULL, 0);
    }
    else
    {
        size_t size;
        char* data = getFileString(argv[1], size);
        //printf("File size is %d (%g KB)\n", size, ((float) size) / 1024.f);
        test->StartUpSystems(data, size);
        free(data);
    }

    return 0;
}
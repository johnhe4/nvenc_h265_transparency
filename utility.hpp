#ifdef _WIN32
   #define WIN32_LEAN_AND_MEAN
   #define NOMINMAX
   #include <windows.h>
#else
   #if defined(__APPLE__)
      #include <mach-o/dyld.h>
      #include <TargetConditionals.h>
   #else
      #include <unistd.h>
   #endif
   
   #if !defined( __ANDROID__ )
      #include <wordexp.h>
   #endif
   
   #include <dirent.h>
   #include <regex.h>
   #include <sys/stat.h>
   #include <dlfcn.h>

   // Needed for filtering files
   regex_t * filter = nullptr;
#endif

#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <unordered_map>
#include <fstream>
#include <stdio.h>
#include <thread>
#include <array>
#include <vector>
#include <stdexcept>

// File system helpers
std::string ExpandTilde( std::string directory )
{
   // If it starts with a tilde
   if ( directory[ 0 ] == '~' )
   {
#if (TARGET_OS_IPHONE) || (_WIN32) || (__ANDROID__)

      throw std::runtime_error( "OS does not support tilde character in paths" );
      
#else

      // Expand it out
      wordexp_t exp_result;
      wordexp( directory.c_str(), &exp_result, 0 );
      directory = std::string( exp_result.we_wordv[0] );
      
#endif
   }
   
   return directory;
}

#include <fstream>
#include <CLI/CLI.hpp>
#include "utility.hpp"
#include "nvEncodeAPI.h"

struct Args
{
   std::string inputYuvFramesFilename;
   std::string maskFilename;
   int width = 0;
   int height = 0;
   int numFrames = 0;
   int fpsNumerator = 0;
   int fpsDenominator = 0;
}args;

auto ReadFile( std::string filename )
{
   int returnValue;
    
   // filename = ExpandTilde( filename );

   // std::ifstream file( filename );
   // if ( !file.good() )
   // {
   //    std::cout << "Could not open file: " << strerror( errno ) << std::endl;
   // }
   // else
   // {
   //    auto fileType = DetermineFileType( filename );
      
   //    switch ( fileType )
   //    {
   //       case FILE_TYPE::DTM: returnValue = ReadFileDTM( file ); break;
   //       case FILE_TYPE::LAS: returnValue = ReadFileLAS( file ); break;
   //       case FILE_TYPE::CSV:
   //       default: break;
   //    }

   //    file.close();
   // }
   
   return returnValue;
}
int main( int argc, char *argv[] )
{
   // Process command-line arguments
   CLI::App app{ "App description" };
   
   app.add_option( "--yuvFrames", args.inputYuvFramesFilename, "Monolithic input file containing a sequence of YUV 4:2:0 frames\n" )->required();
   app.add_option( "--mask", args.maskFilename, "Single frame YUV image representing transparency mask, data only (no BMP, etc). Dimensions MUST match input YUV frames\n" )->required();
   app.add_option( "--width", args.width, "Width of the input YUV frames and mask\n" )->required();
   app.add_option( "--height", args.height, "Height of the input YUV frames and mask\n" )->required();
   app.add_option( "--numFrames", args.numFrames, "Number of frames to output. Must be less than number of frames in the monolithic input file\n" )->required();
   app.add_option( "--fpsn", args.numFrames, "Frame rate numerator\n" )->required();
   app.add_option( "--fpsd", args.numFrames, "Frame rate denominator\n" )->required();
   
   try
   {
      app.parse(argc, argv);
   }
   catch( const CLI::ParseError &e )
   {
      std::cout << e.what() << "\n";
      std::cout << app.help();
      return 1;
   }

   
   // Save as PLY
   //pcl::io::savePLYFile( meshFilename, triangles );
   //std::cout << "Saved '" << meshFilename << "' as a mesh\n";
   
   return 0;
}
// Reference: https://docs.nvidia.com/video-technologies/video-codec-sdk/nvenc-video-encoder-api-prog-guide/

#include <fstream>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <CLI/CLI.hpp>
//#include <cuda.h>
#include "utility.hpp"
#include "nvEncodeAPI.h"

// Error handling
inline void ThrowErorr( int code, std::string errorMsg )
{
   std::stringstream ss;
   ss << errorMsg << ": " << code;
   throw std::runtime_error( ss.str() );
}
#define CHECK( code, errorMsg ) if ( code != NV_ENC_SUCCESS ) ThrowErorr( code, errorMsg );

// Globals
static const GUID g_encoderGuid = NV_ENC_CODEC_HEVC_GUID;
static const GUID g_profileGuid = NV_ENC_HEVC_PROFILE_MAIN_GUID;
static const NV_ENC_BUFFER_FORMAT g_inputFormat = NV_ENC_BUFFER_FORMAT_NV12;
static const std::unordered_map< NV_ENC_CAPS, int > g_requiredCaps = {
   // Put caps and expected values here as {key,val} pairs
   { NV_ENC_CAPS_SUPPORT_ALPHA_LAYER_ENCODING, 1 }
};

struct MyNvBuffer
{
   NV_ENC_REGISTER_RESOURCE registerResource;
   NV_ENC_MAP_INPUT_RESOURCE inputResource;
   NV_ENC_PIC_PARAMS picParams;
};
static std::unordered_map< void *, MyNvBuffer > g_inputBuffers;
static std::unordered_map< void *, MyNvBuffer > g_outputBuffers;

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

int GetCapabilityValue( void * encoder,
   GUID encoderGuid,
   NV_ENC_CAPS capsToQuery )
{
    NV_ENC_CAPS_PARAM capsParam = { NV_ENC_CAPS_PARAM_VER };
    capsParam.capsToQuery = capsToQuery;
    int v;
    NvEncGetEncodeCaps( encoder, encoderGuid, &capsParam, &v );
    return v;
}

NV_ENC_INITIALIZE_PARAMS CreateInitParams( void * encoder,
   GUID encoderGuid,
   int width,
   int height,
   int fpsNumerator,
   int fpsDenominator )
{
   NV_ENC_INITIALIZE_PARAMS returnValue = {
      NV_ENC_INITIALIZE_PARAMS_VER,
      encoderGuid,
      NV_ENC_PRESET_P5_GUID,                            // Quality increases as we move from P1 to P7
      uint32_t(width), uint32_t(height),                // Dimensions
      uint32_t(width), uint32_t(height),                // Aspect ratio dimensions
      uint32_t(fpsNumerator), uint32_t(fpsDenominator), // Frame rate as a ratio
      0,                                                // Asynchronous=1, Synchronous=0
      1,                                                // input buffers in display order=1, encode order=0
      0                                                 // Zero everything here and beyond
   };
   
   // We'll provide our own output buffer, thank you
   returnValue.enableOutputInVidmem = 1;
   
   // Uncomment this to help lower latency. This is not supported with transparency
   //returnValue.enableSubFrameWrite = 1;
   
   // Uncomment and set this to allow dynamic resolutio changes
   //returnValue.maxEncodeWidth = 0;
   //returnValue.maxEncodeHeight = 0;
   
   return returnValue;
}
NV_ENC_CONFIG CreateInitParamsHevc()
{
   // Anything set here will override the preset
   NV_ENC_CONFIG returnValue = {
      NV_ENC_CONFIG_VER,
      g_profileGuid
   };
   
   NV_ENC_CONFIG_HEVC hevcConfig = {
      .enableAlphaLayerEncoding = 1
   };
   returnValue.encodeCodecConfig.hevcConfig = hevcConfig;
   
   return returnValue;
}
MyNvBuffer GetInputBuffer( void * encoder,
   int width,
   int height )
{
   // Create a CUDA buffer
   size_t cudaWidth = width, cudaHeight = height, cudaPitch;
   void * cudaBuffer = nullptr;
//   if ( CUDA_SUCCESS != cuMemAllocPitch( (CUdevicptr **)cudaBuffer,
//      &cudaPitch,
//       cudaWidth,
//       cudaHeight,
//       8 ) )
//      throw std::runtime_error( "Could not allocate CUDA output buffer" );
   
   MyNvBuffer returnValue;
   
   // Register the CUDA buffer with the encode session
   returnValue.registerResource = {
      NV_ENC_REGISTER_RESOURCE_VER,
      NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
      uint32_t(cudaWidth),
      uint32_t(cudaHeight),
      uint32_t(cudaPitch),
      0,
      cudaBuffer,
      nullptr, // This will be populated after the call to NvEncRegisterResource()
      g_inputFormat,
      NV_ENC_INPUT_IMAGE,
      0
   };
   CHECK( NvEncRegisterResource( encoder, &returnValue.registerResource ), "Failed registering CUDA buffer with encode session" );
   
   // Map as an input buffer
   returnValue.inputResource = {
      NV_ENC_MAP_INPUT_RESOURCE_VER,
      0, 0, // Deprecated
      returnValue.registerResource.registeredResource,
      nullptr, NV_ENC_BUFFER_FORMAT_UNDEFINED, // These will be populated after the call to NvEncMapInputResource()
      0
   };
   CHECK( NvEncMapInputResource( encoder, &returnValue.inputResource ), "Failed mapping CUDA buffer as encoder input" );
   
   // Create the image wrapper around this buffer
   returnValue.picParams = {
      NV_ENC_PIC_PARAMS_VER,
      uint32_t(cudaWidth), uint32_t(cudaHeight), uint32_t(cudaPitch),
      .inputBuffer = returnValue.inputResource.mappedResource,
      .bufferFmt = g_inputFormat,
      .pictureStruct = NV_ENC_PIC_STRUCT_FRAME
   };
   
   // Maintain an association between this cuda buffer and this NvResource
   g_inputBuffers[ cudaBuffer ] = returnValue;
   
   return returnValue;
}
MyNvBuffer GetOutputBuffer( void * encoder, const MyNvBuffer & inputBuffer )
{
   MyNvBuffer returnValue;
   
   // Allocate a 1D bitstream buffer following recommendation from docs
   int cudaBufferSize = 2 * inputBuffer.registerResource.height * inputBuffer.registerResource.pitch + sizeof(NV_ENC_ENCODE_OUT_PARAMS);
   void * cudaBuffer = nullptr;
   //if ( CUDA_SUCCESS != cuMemAlloc( (CUdevicptr **)cudaBuffer, cudaBufferSize ) )
   //   throw std::runtime_error( "Could not allocate CUDA output buffer" );
   
   // Register the CUDA buffer with the encode session
   returnValue.registerResource = {
      NV_ENC_REGISTER_RESOURCE_VER,
      NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
      0, 0, 0, // Bitstream buffers only have one dimension
      0,
      cudaBuffer,
      nullptr, // This will be populated after the call to NvEncRegisterResource()
      NV_ENC_BUFFER_FORMAT_U8,
      NV_ENC_OUTPUT_BITSTREAM,
      0
   };
   CHECK( NvEncRegisterResource( encoder, &returnValue.registerResource ), "Failed registering CUDA buffer with encode session" );
   
   // Map as an input buffer
   returnValue.inputResource = {
      NV_ENC_MAP_INPUT_RESOURCE_VER,
      0, 0, // Deprecated
      returnValue.registerResource.registeredResource,
      nullptr, NV_ENC_BUFFER_FORMAT_UNDEFINED, // These will be populated after the call to NvEncMapInputResource()
      0
   };
   CHECK( NvEncMapInputResource( encoder, &returnValue.inputResource ), "Failed mapping CUDA buffer as encoder input" );
   
   // Maintain an association between this cuda buffer and this NvResource
   g_outputBuffers[ cudaBuffer ] = returnValue;
   
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
   
   struct RAII
   {
      ~RAII()
      {
         if ( nvEncoder )
         {
            NvEncDestroyEncoder( nvEncoder );
            nvEncoder = nullptr;
         }
      }
      
      void * nvEncoder = nullptr;
   } raii;
   
   try
   {
      // TODO: may not need some or any of these
      //dlopen( "libnvidia-encode.so" );
      //CHECK( NvEncodeAPICreateInstance(NV_ENCODE_API_FUNCTION_LIST *functionList) );
      
      // TODO: Create a CUDA context
      void * cudaContext = nullptr; // SHOULD BE CUDA CONTEXT
      
      // Initialize the encoder
      NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = { NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
         NV_ENC_DEVICE_TYPE_CUDA,
         cudaContext,
         0,
         NVENCAPI_VERSION,
         0,
         NULL
      };
      CHECK( NvEncOpenEncodeSessionEx( &sessionParams,
         &raii.nvEncoder), "Failed initializing NVidia encode session" );

      // Ensure we have support for the desired encoder
      uint32_t numEncoderGuids = 0;
      bool hasHevcSupport = false;
      CHECK( NvEncGetEncodeGUIDCount( raii.nvEncoder, &numEncoderGuids ), "Failed getting NVidia encode GUID count" );
      std::vector< GUID > guids( numEncoderGuids );
      CHECK( NvEncGetEncodeGUIDs( raii.nvEncoder,
         guids.data(),
         numEncoderGuids,
         &numEncoderGuids ), "Failed getting NVidia encode GUIDs" );
      for ( GUID guid : guids )
      {
         if ( memcmp( &guid, &g_encoderGuid, sizeof(guid) ) == 0 )
         {
            hasHevcSupport = true;
            break;
         }
      }
      if ( !hasHevcSupport )
         throw std::runtime_error( "NVidia hardware does not support HEVC" );

      // Ensure we have support for the desired profile
      uint32_t numProfileGuids = 0;
      bool hasProfileSupport = false;
      CHECK( NvEncGetEncodeProfileGUIDCount( raii.nvEncoder, g_encoderGuid, &numProfileGuids ), "Failed getting NVidia encode profile GUID count" );
      guids = std::vector< GUID >( numEncoderGuids );
      CHECK( NvEncGetEncodeProfileGUIDs( raii.nvEncoder,
         g_encoderGuid,
         guids.data(),
         numProfileGuids,
         &numProfileGuids ), "Failed getting NVidia encode profile GUIDs" );
      for ( GUID guid : guids )
      {
         if ( memcmp( &guid, &g_profileGuid, sizeof(guid) ) == 0 )
         {
            hasProfileSupport = true;
            break;
         }
      }
      if ( !hasProfileSupport )
         throw std::runtime_error( "NVidia encoder doesn't support the desired profle" );
      
      // Ensure we have support for the desired capabilities
      for ( auto capValPair : g_requiredCaps )
      {
         if ( GetCapabilityValue( raii.nvEncoder, g_encoderGuid, capValPair.first ) != capValPair.second )
            throw std::runtime_error( "NVidia encoder doesn't support required capabilities" );
      }

      // Create the initial parameters
      NV_ENC_INITIALIZE_PARAMS initParams = CreateInitParams( raii.nvEncoder,
         g_encoderGuid,
         args.width,
         args.height,
         args.fpsNumerator,
         args.fpsDenominator );
         
      // Codec-specific settings
      NV_ENC_CONFIG initParamsHevc = CreateInitParamsHevc();
      initParams.encodeConfig = &initParamsHevc;
    
      // Initialize the encoder
      CHECK( NvEncInitializeEncoder( raii.nvEncoder, &initParams ), "Failed initializing NVidia encoder" );
   }
   catch ( const std::runtime_error & e )
   {
      std::cout << e.what() << std::endl;
      return;
   }
   
   // For every frame
   for ( int frameNum = 0; frameNum < args.numFrames; ++frameNum )
   {
      // Allocate and register an input buffer
      try
      {
         // Get the next input buffer, including reading the data from disk
         auto inputBuffer = GetInputBuffer( raii.nvEncoder, args.width, args.height );
         
         // Get the next output buffer, tie it to the input
         auto outputBuffer = GetOutputBuffer( raii.nvEncoder, inputBuffer );
         inputBuffer.picParams.outputBitstream = outputBuffer.inputResource.mappedResource;
         
         // Encode a frame
         CHECK( NvEncEncodePicture( raii.nvEncoder, &inputBuffer.picParams ), "Failed to encode frame" );

         // Append output to file

         // Release buffers
      }
      catch ( const std::runtime_error & e )
      {
         std::cout << e.what() << std::endl;
      }
   }
   
   // Save as PLY
   //pcl::io::savePLYFile( meshFilename, triangles );
   //std::cout << "Saved '" << meshFilename << "' as a mesh\n";
   
   return 0;
}

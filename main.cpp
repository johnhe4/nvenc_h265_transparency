// Reference: https://docs.nvidia.com/video-technologies/video-codec-sdk/nvenc-video-encoder-api-prog-guide/

#include <fstream>
#include <vector>
#include <deque>
#include <unordered_map>
#include <sstream>
#include <cstring>
#include <CLI/CLI.hpp>
#include <cuda.h>
#include "utility.hpp"
#include "nvEncodeAPI.h"

// Error handling
inline void ThrowNveErorr( int code, std::string errorMsg )
{
   std::stringstream ss;
   ss << errorMsg << ": " << code;
   throw std::runtime_error( ss.str() );
}
#define NVE_CHECK( code, errorMsg ) if ( code != NV_ENC_SUCCESS ) ThrowNveErorr( code, errorMsg );
inline void ThrowCudaErorr( CUresult code )
{
   char * errorMsg = nullptr;
   cuGetErrorName( code, (const char **)&errorMsg );
   throw std::runtime_error( errorMsg );
}
#define CUDA_CHECK( code ) if ( code != CUDA_SUCCESS ) ThrowCudaErorr( code );
struct CudaScope
{
   CudaScope( CUcontext cudaContext ) : _cudaContext(cudaContext) { if(_cudaContext) CUDA_CHECK(cuCtxPushCurrent(_cudaContext)); }
   ~CudaScope() { if ( _cudaContext ) CUDA_CHECK(cuCtxPopCurrent(nullptr)); }
private:
   CUcontext _cudaContext = nullptr;
};

constexpr bool g_useAlpha = true;

// Globals
struct MyNv
{
   GUID encoderGuid = NV_ENC_CODEC_HEVC_GUID;
   GUID profileGuid = NV_ENC_HEVC_PROFILE_MAIN_GUID;
   GUID presetGuid = NV_ENC_PRESET_P3_GUID;
   NV_ENC_TUNING_INFO tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;
   NV_ENC_BUFFER_FORMAT inputFormat = NV_ENC_BUFFER_FORMAT_NV12;
   bool externalAlloc = false; // Cannot be true for transparency
   std::unordered_map< NV_ENC_CAPS, int > requiredCaps = {
      // Put caps and expected values here as {key,val} pairs
      { NV_ENC_CAPS_SUPPORT_ALPHA_LAYER_ENCODING, 1 }
   };
   int cudaDeviceIndex = 0;
   NV_ENCODE_API_FUNCTION_LIST functions = { NV_ENCODE_API_FUNCTION_LIST_VER };
   int baseToAlphaBitDistributionRatio = 15;
} g_nv;

struct MyFile
{
   std::ifstream inputVideo;
   std::ofstream outputVideo;
} g_file;

struct MyNvBuffer
{
   NV_ENC_REGISTER_RESOURCE registerResource;
   NV_ENC_MAP_INPUT_RESOURCE inputResource;
};

struct Args
{
   std::string inputYuvFramesFilename;
   std::string maskFilename;
   int width = 0;
   int height = 0;
   int fpsNumerator = 0;
   int fpsDenominator = 0;
}args;

auto CreateOutputFile( std::string filename )
{
   filename = ExpandTilde( filename );

   std::ofstream file( filename );
   if ( !file.good() )
   {
      std::stringstream ss;
      ss << "Could not open file for writing: " << strerror( errno ) << std::endl;
      throw std::runtime_error( ss.str() );
   }
   
   return file;
}

int GetCapabilityValue( void * encoder,
   GUID encoderGuid,
   NV_ENC_CAPS capsToQuery )
{
    NV_ENC_CAPS_PARAM capsParam = { NV_ENC_CAPS_PARAM_VER };
    capsParam.capsToQuery = capsToQuery;
    int v;
    (*g_nv.functions.nvEncGetEncodeCaps)( encoder, encoderGuid, &capsParam, &v );
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
      g_nv.presetGuid,                                     // Quality increases as we move from P1 to P7
      uint32_t(width), uint32_t(height),                // Dimensions
      uint32_t(width), uint32_t(height),                // Aspect ratio dimensions
      uint32_t(fpsNumerator), uint32_t(fpsDenominator), // Frame rate as a ratio
      0,                                                // Asynchronous=1, Synchronous=0
      1                                                 // input buffers in display order=1, encode order=0
   };
   returnValue.tuningInfo = g_nv.tuningInfo;
   
   // Uncomment this to provide your own output buffer. This is not supported with transparency
   //returnValue.enableOutputInVidmem = 1;
   
   // Uncomment this to help lower latency. This is not supported with transparency
   //returnValue.enableSubFrameWrite = 1;
   
   // Uncomment and set this to allow dynamic resolutio changes
   //returnValue.maxEncodeWidth = 0;
   //returnValue.maxEncodeHeight = 0;
   
   return returnValue;
}
NV_ENC_CONFIG CreateInitParamsHevc( void * encoder,
   GUID encoderGuid,
   GUID presetGuid )
{
   // Start by populating from the preset 
   NV_ENC_PRESET_CONFIG presetConfig = { NV_ENC_PRESET_CONFIG_VER, { NV_ENC_CONFIG_VER } };
   NVE_CHECK( (*g_nv.functions.nvEncGetEncodePresetConfig)( encoder,
      encoderGuid,
      presetGuid,
      &presetConfig ), "Failed retrieving default preset configuration" );

   // Anything set here will override the preset
   //presetConfig.presetCfg.rcParams = NV_ENC_PARAMS_RC_CBR;
   
   if ( g_useAlpha )
   {
      presetConfig.presetCfg.encodeCodecConfig.hevcConfig.enableAlphaLayerEncoding = 1;
      presetConfig.presetCfg.rcParams.alphaLayerBitrateRatio = g_nv.baseToAlphaBitDistributionRatio;
   }

   return presetConfig.presetCfg;
}
MyNvBuffer LockInputBuffer( void * encoder,
   void * cudaContext,
   int width,
   int height )
{
   MyNvBuffer returnValue;
   void * cudaBuffer = nullptr;

   // Manage file state
   if ( g_file.inputVideo.eof() )
   {
      // We reached the end of data last time we were here,
      // so set to null and exit
      returnValue.inputResource.mappedResource = nullptr;
      return returnValue;
   }
   else if ( !g_file.inputVideo.is_open() )
   {
      // Must be the first time here, open and ensure our input video file is good
      g_file.inputVideo.open( args.inputYuvFramesFilename );
      if ( !g_file.inputVideo.good() )
         throw std::runtime_error( "Could not load input video file" );
   }

   // TODO: THIS ASSUMES NV12
   uint32_t byteHeight = height * 3 / 2;

   // Create a device buffer first so we have pitch
   size_t cudaPitch;
   {
      CudaScope cs( (CUcontext)cudaContext );

      CUDA_CHECK( cuMemAllocPitch( (CUdeviceptr *)&cudaBuffer,
         &cudaPitch,
         width,
         byteHeight,
         8 ) );   

      // Create a pinned host buffer with proper alignment for CUDA   
      char * tempBuffer = nullptr;
      CUDA_CHECK( cuMemHostAlloc( (void **)&tempBuffer, byteHeight * cudaPitch, 0 ) );

      // Read the next frame from disk to our temp buffer
      for( int row = 0; row < byteHeight; ++row )
         g_file.inputVideo.read( tempBuffer + (row * cudaPitch), width );

      // Transport from pinned host buffer to device buffer
      CUDA_CHECK( cuMemcpyHtoD( (CUdeviceptr)cudaBuffer, tempBuffer, byteHeight * cudaPitch ) );

      // Delete our temp buffer
      cuMemFreeHost( tempBuffer );
   }
   
   // Register the CUDA buffer with the encode session
   returnValue.registerResource = {
      NV_ENC_REGISTER_RESOURCE_VER,
      NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
      uint32_t(width),
      uint32_t(height),
      uint32_t(cudaPitch),
      0,
      cudaBuffer,
      nullptr, // This will be populated after the call to NvEncRegisterResource()
      g_nv.inputFormat,
      NV_ENC_INPUT_IMAGE
   };
   NVE_CHECK( (*g_nv.functions.nvEncRegisterResource)( encoder, &returnValue.registerResource ), "Failed registering CUDA buffer with encode session" );
   
   // Map as an input buffer
   returnValue.inputResource = {
      NV_ENC_MAP_INPUT_RESOURCE_VER,
      0, 0, // Deprecated
      returnValue.registerResource.registeredResource,
      nullptr, NV_ENC_BUFFER_FORMAT_UNDEFINED // These will be populated after the call to NvEncMapInputResource()
   };
   NVE_CHECK( (*g_nv.functions.nvEncMapInputResource)( encoder, &returnValue.inputResource ), "Failed mapping CUDA buffer as encoder input" );
   
   return returnValue;
}
MyNvBuffer LockAlphaBuffer( void * encoder,
   void * cudaContext,
   const MyNvBuffer & inputBuffer )
{
   if ( !g_useAlpha )
   {
      MyNvBuffer emptyReturn = {};
      return emptyReturn;
   }

   // We're using an image for a mask, so only do this once
   static std::shared_ptr< MyNvBuffer > returnValue = nullptr;
   if ( returnValue == nullptr )
   {
      void * cudaBuffer = nullptr;
      std::shared_ptr< MyNvBuffer > newBuffer = std::make_shared< MyNvBuffer >();

      // Open the maks file
      // Must be the first time here, open and ensure our input video file is good
      std::ifstream inputMask( args.maskFilename );
      if ( !inputMask.good() )
         throw std::runtime_error( "Could not load mask file" );
   
      // TODO: THIS ASSUMES NV12
      uint32_t byteHeight = inputBuffer.registerResource.height * 3 / 2;

      size_t cudaPitch;
      {
         CudaScope cs( (CUcontext)cudaContext );

         // Create a device buffer first so we have pitch
         CUDA_CHECK( cuMemAllocPitch( (CUdeviceptr *)&cudaBuffer,
            &cudaPitch,
            inputBuffer.registerResource.width,
            byteHeight,
            8 ) );   

         // Create a pinned host buffer with proper alignment for CUDA   
         char * tempBuffer = nullptr;
         CUDA_CHECK( cuMemHostAlloc( (void **)&tempBuffer, byteHeight * cudaPitch, 0 ) );

         // Read the frame from disk to our temp buffer
         for( int row = 0; row < byteHeight; ++row )
            inputMask.read( tempBuffer + (row * cudaPitch), inputBuffer.registerResource.width );

         // Memset chroma to 0x80 per the docs
         memset( tempBuffer + (inputBuffer.registerResource.height * cudaPitch),
            0x80,
            inputBuffer.registerResource.height * cudaPitch / 2 );

         // Transport from pinned host buffer to device buffer
         CUDA_CHECK( cuMemcpyHtoD( (CUdeviceptr)cudaBuffer,
            tempBuffer,
            byteHeight * cudaPitch ) );

         // Delete our temp buffer
         cuMemFreeHost( tempBuffer );

         // Register the CUDA buffer with the encode session
         newBuffer->registerResource = {
            NV_ENC_REGISTER_RESOURCE_VER,
            NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
            uint32_t(inputBuffer.registerResource.width),
            uint32_t(inputBuffer.registerResource.height),
            uint32_t(cudaPitch),
            0,
            cudaBuffer, // resourceToRegister field
            nullptr, // This will be populated after the call to NvEncRegisterResource()
            g_nv.inputFormat,
            NV_ENC_INPUT_IMAGE,
            0
         };
         NVE_CHECK( (*g_nv.functions.nvEncRegisterResource)( encoder, &newBuffer->registerResource ), "Failed registering CUDA buffer with encode session" );
         
         returnValue = newBuffer;
      }
   }
   
   // Map as an input buffer
   returnValue->inputResource = {
      NV_ENC_MAP_INPUT_RESOURCE_VER,
      0, 0, // Deprecated
      returnValue->registerResource.registeredResource,
      nullptr, NV_ENC_BUFFER_FORMAT_UNDEFINED, // These will be populated after the call to NvEncMapInputResource()
      0
   };
   NVE_CHECK( (*g_nv.functions.nvEncMapInputResource)( encoder, &returnValue->inputResource ), "Failed mapping CUDA buffer as encoder input" );
   
   return *returnValue;
}
void * LockOutputBuffer( void * encoder,
   const MyNvBuffer & inputBuffer,
   bool externalAlloc )
{
   void * returnValue;
   
   if ( externalAlloc )
   {
      // // Allocate a 1D bitstream buffer following recommendation from docs
      // int cudaBufferSize = 2 * inputBuffer.registerResource.height * inputBuffer.registerResource.pitch + sizeof(NV_ENC_ENCODE_OUT_PARAMS);
      // void * cudaBuffer = nullptr;
      // //if ( CUDA_SUCCESS != cuMemAlloc( (CUdevicptr **)cudaBuffer, cudaBufferSize ) )
      // //   throw std::runtime_error( "Could not allocate CUDA output buffer" );
      
      // // Register the CUDA buffer with the encode session
      // nvBuffer.registerResource = {
      //    NV_ENC_REGISTER_RESOURCE_VER,
      //    NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
      //    0, 0, 0, // Bitstream buffers only have one dimension
      //    0,
      //    cudaBuffer,
      //    nullptr, // This will be populated after the call to NvEncRegisterResource()
      //    NV_ENC_BUFFER_FORMAT_U8,
      //    NV_ENC_OUTPUT_BITSTREAM,
      //    0
      // };
      // NVE_CHECK( (*g_nv.functions.nvEncRegisterResource)( encoder, &nvBuffer.registerResource ), "Failed registering CUDA buffer with encode session" );
      
      // // Map as an input buffer
      // nvBuffer.inputResource = {
      //    NV_ENC_MAP_INPUT_RESOURCE_VER,
      //    0, 0, // Deprecated
      //    nvBuffer.registerResource.registeredResource,
      //    nullptr, NV_ENC_BUFFER_FORMAT_UNDEFINED, // These will be populated after the call to NvEncMapInputResource()
      //    0
      // };
      // NVE_CHECK( (*g_nv.functions.nvEncMapInputResource)( encoder, &nvBuffer.inputResource ), "Failed mapping CUDA buffer as encoder input" );
      
      // returnValue = nvBuffer.inputResource.mappedResource;
   }
   else
   {
      NV_ENC_CREATE_BITSTREAM_BUFFER outputBuffer = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
      NVE_CHECK( (*g_nv.functions.nvEncCreateBitstreamBuffer)( encoder, &outputBuffer ), "Failed creating output bitstream buffer" );
      
      returnValue = outputBuffer.bitstreamBuffer;
   }
   
   return returnValue;
}
void UnlockInputBuffer( void * encoder,
   void * cudaContext,
   MyNvBuffer & inputBuffer )
{
   // Completely destroy the input
   NVE_CHECK( (*g_nv.functions.nvEncUnmapInputResource)( encoder, inputBuffer.inputResource.mappedResource ), "Failed unmapping input buffer" );
   NVE_CHECK( (*g_nv.functions.nvEncUnregisterResource)( encoder, inputBuffer.registerResource.registeredResource ), "Failed unmapping input buffer" );
   {
      CudaScope cs( (CUcontext)cudaContext );
      CUDA_CHECK( cuMemFree( (CUdeviceptr)inputBuffer.registerResource.resourceToRegister ) );
   }
}
void UnlockAlphaBuffer( void * encoder,
   void * cudaContext,
   MyNvBuffer & alphaBuffer )
{
   if ( alphaBuffer.inputResource.mappedResource != nullptr )
   {
      // Unmap the alpha, but don't delete it
      NVE_CHECK( (*g_nv.functions.nvEncUnmapInputResource)( encoder, alphaBuffer.inputResource.mappedResource ), "Failed unmapping alpha buffer" );
   }
}
void UnlockOutputBuffer( void * encoder, void * outputBuffer )
{
   NVE_CHECK( (*g_nv.functions.nvEncDestroyBitstreamBuffer)( encoder, outputBuffer ), "Failed to destroy bitstream buffer" );
}
int main( int argc, char *argv[] )
{
   // Process command-line arguments
   CLI::App app{ "App description" };
   
   app.add_option( "--yuvFrames", args.inputYuvFramesFilename, "Monolithic input file containing a sequence of YUV 4:2:0 frames\n" )->required();
   app.add_option( "--mask", args.maskFilename, "Single frame YUV image representing transparency mask, data only (no BMP, etc). Dimensions MUST match input YUV frames\n" )->required();
   app.add_option( "--width", args.width, "Width of the input YUV frames and mask\n" )->required();
   app.add_option( "--height", args.height, "Height of the input YUV frames and mask\n" )->required();
   app.add_option( "--fpsn", args.fpsNumerator, "Frame rate numerator\n" )->required();
   app.add_option( "--fpsd", args.fpsDenominator, "Frame rate denominator\n" )->required();
   
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
            (*g_nv.functions.nvEncDestroyEncoder)( nvEncoder );
            nvEncoder = nullptr;
         }
         if ( cudaContext )
         {
            cuCtxDestroy( (CUcontext)cudaContext );
            cudaContext = nullptr;
         }
      }
      
      void * nvEncoder = nullptr;
      void * cudaContext = nullptr;
   } raii;
   
   try
   {
      // Ensure we don't have critical version mismatch
      uint32_t version = 0;
      uint32_t currentVersion = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
      NVE_CHECK( NvEncodeAPIGetMaxSupportedVersion( &version ), "Failed retrieving driver version" );
      if ( currentVersion > version )
         ThrowNveErorr( NV_ENC_ERR_INVALID_VERSION, "Current driver version does not support this NvEncodeAPI version, please upgrade driver" );

      // Get the functions as they are not explicitely dynamic in the shared objectc
      NVE_CHECK( NvEncodeAPICreateInstance( &g_nv.functions ), "Failed getting NVidia encode functions" );
      
      // Retain the primary CUDA context
      CUDA_CHECK( cuInit( 0 ) );
      CUdevice cudaDevice;
      CUDA_CHECK( cuDeviceGet( &cudaDevice, g_nv.cudaDeviceIndex ) );
      CUDA_CHECK( cuCtxCreate( (CUcontext *)&raii.cudaContext, 0, cudaDevice ) );

      // Initialize the encoder
      NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = { NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
         NV_ENC_DEVICE_TYPE_CUDA,
         raii.cudaContext,
         0,
         NVENCAPI_VERSION,
         0,
         0
      };
      NVE_CHECK( (*g_nv.functions.nvEncOpenEncodeSessionEx)( &sessionParams,
         &raii.nvEncoder), "Failed initializing NVidia encode session" );

      // Ensure we have support for the desired encoder
      uint32_t numEncoderGuids = 0;
      bool hasHevcSupport = false;
      NVE_CHECK( (*g_nv.functions.nvEncGetEncodeGUIDCount)( raii.nvEncoder, &numEncoderGuids ), "Failed getting NVidia encode GUID count" );
      std::vector< GUID > guids( numEncoderGuids );
      NVE_CHECK( (*g_nv.functions.nvEncGetEncodeGUIDs)( raii.nvEncoder,
         guids.data(),
         numEncoderGuids,
         &numEncoderGuids ), "Failed getting NVidia encode GUIDs" );
      for ( GUID guid : guids )
      {
         if ( memcmp( &guid, &g_nv.encoderGuid, sizeof(guid) ) == 0 )
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
      NVE_CHECK( (*g_nv.functions.nvEncGetEncodeProfileGUIDCount)( raii.nvEncoder, g_nv.encoderGuid, &numProfileGuids ), "Failed getting NVidia encode profile GUID count" );
      guids = std::vector< GUID >( numProfileGuids );
      NVE_CHECK( (*g_nv.functions.nvEncGetEncodeProfileGUIDs)( raii.nvEncoder,
         g_nv.encoderGuid,
         guids.data(),
         numProfileGuids,
         &numProfileGuids ), "Failed getting NVidia encode profile GUIDs" );
      for ( GUID guid : guids )
      {
         if ( memcmp( &guid, &g_nv.profileGuid, sizeof(guid) ) == 0 )
         {
            hasProfileSupport = true;
            break;
         }
      }
      if ( !hasProfileSupport )
         throw std::runtime_error( "NVidia encoder doesn't support the desired profle" );
   
      // Ensure we have support for the desired capabilities
      for ( auto capValPair : g_nv.requiredCaps )
      {
         if ( GetCapabilityValue( raii.nvEncoder, g_nv.encoderGuid, capValPair.first ) != capValPair.second )
            throw std::runtime_error( "NVidia encoder doesn't support required capabilities" );
      }

      // Create the initial parameters
      NV_ENC_INITIALIZE_PARAMS initParams = CreateInitParams( raii.nvEncoder,
         g_nv.encoderGuid,
         args.width,
         args.height,
         args.fpsNumerator,
         args.fpsDenominator );
         
      // Codec-specific settings
      NV_ENC_CONFIG initParamsHevc = CreateInitParamsHevc( raii.nvEncoder, g_nv.encoderGuid, g_nv.presetGuid );
      initParams.encodeConfig = &initParamsHevc;
    
      // Initialize the encoder
      NVE_CHECK( (*g_nv.functions.nvEncInitializeEncoder)( raii.nvEncoder, &initParams ), "Failed initializing NVidia encoder" );
   }
   catch ( const std::runtime_error & e )
   {
      std::cout << e.what() << std::endl;
      return 1;
   }
   
   // Create the output file
   std::string outputFilename = "outputWithTransparency.265";
   g_file.outputVideo = CreateOutputFile( outputFilename );
   
   // For every frame
   int inputFrameCount = 0, outputFrameCount = 0;
   bool done = false;
   struct encodeBuffer { MyNvBuffer input; MyNvBuffer alpha; NV_ENC_PIC_PARAMS picParams; };
   std::deque< encodeBuffer > buffers;
   while ( !done )
   {
      // Allocate and register an input buffer
      try
      {
         // Input video frame
         auto inputBuffer = LockInputBuffer( raii.nvEncoder,
            raii.cudaContext,
            args.width,
            args.height );
         if ( inputBuffer.inputResource.mappedResource == nullptr )
         {
            // TODO: handle end of stream
            done = true;

            // NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER };
            // picParams.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
            // nvEncEncodePicture(m_hEncoder, &picParams);

            break;
         }

         // Input alpha mask
         auto alphaBuffer = LockAlphaBuffer( raii.nvEncoder, 
            raii.cudaContext,
            inputBuffer );

         // Output bitstream
         auto outputBuffer = LockOutputBuffer( raii.nvEncoder,
            inputBuffer,
            g_nv.externalAlloc );

         // Create a frame, tying all the data together
         // TODO: WAS SETTING PITCH, but don't think I need to
         NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER, uint32_t(args.width), uint32_t(args.height) };
         picParams.bufferFmt = g_nv.inputFormat;
         picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
         picParams.inputBuffer = inputBuffer.inputResource.mappedResource;
         picParams.alphaBuffer = alphaBuffer.inputResource.mappedResource;
         picParams.outputBitstream = outputBuffer;

         // Keep track of frames to handle encoder latency
         buffers.push_back( {inputBuffer, alphaBuffer, picParams} );
         
         // Encode a frame
         NVENCSTATUS nvStatus = (*g_nv.functions.nvEncEncodePicture)( raii.nvEncoder, &picParams );
         
         // If we don't need more input to get an output
         if ( nvStatus != NV_ENC_ERR_NEED_MORE_INPUT )
         {
            NVE_CHECK( nvStatus, "Failed to encode frame" );

            auto buffer = buffers.front();
            buffers.pop_front();

            // Lock output buffer, append to file, unlock
            NV_ENC_LOCK_BITSTREAM outBitstream = { NV_ENC_LOCK_BITSTREAM_VER }; outBitstream.outputBitstream = buffer.picParams.outputBitstream;
            NVE_CHECK( (*g_nv.functions.nvEncLockBitstream)( raii.nvEncoder, &outBitstream ), "Failed locking the output bitstream" );
            g_file.outputVideo.write( (char *)outBitstream.bitstreamBufferPtr, outBitstream.bitstreamSizeInBytes );
            NVE_CHECK( (*g_nv.functions.nvEncUnlockBitstream)( raii.nvEncoder, outBitstream.outputBitstream ), "Failed unlocking the output bitstream" );

            // Unlock all buffers
            UnlockOutputBuffer( raii.nvEncoder, buffer.picParams.outputBitstream );
            UnlockAlphaBuffer( raii.nvEncoder, raii.cudaContext, buffer.alpha );
            UnlockInputBuffer( raii.nvEncoder, raii.cudaContext, buffer.input );

            ++outputFrameCount;
         }

         ++inputFrameCount;
      }
      catch ( const std::runtime_error & e )
      {
         std::cout << e.what() << std::endl;
      }
   }
   
   // TODO: Destroy alpha buffer!
   // TODO: Frame count is 1 off (too high)

   std::cout << "Processed " << inputFrameCount << " frames, wrote " << outputFrameCount << " to `" << outputFilename << "'" << std::endl;

   return 0;
}

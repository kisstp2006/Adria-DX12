#pragma once

#define GFX_CHECK_HR(hr) if(FAILED(hr)) ADRIA_DEBUGBREAK();

#define GFX_BACKBUFFER_COUNT 3
#define GFX_MULTITHREADED 0
#define GFX_SHADER_PRINTF 1

#define GFX_PROFILING 1

//should not be used together with PIX or debug layer
#define GFX_NSIGHT_AFTERMATH 0

#if GFX_PROFILING
#define GFX_PROFILING_USE_TRACY 0
#endif




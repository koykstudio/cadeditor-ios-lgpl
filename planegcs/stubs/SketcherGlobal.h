// KoykStudio stub for building planegcs outside FreeCAD: everything planegcs declares is exported from PlaneGcs.dll.
#pragma once
#ifdef _WIN32
#define SketcherExport __declspec(dllexport)
#else
#define SketcherExport __attribute__((visibility("default")))
#endif

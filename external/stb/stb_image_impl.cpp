// stb_image is a single-header library: normally it is only declarations, but in exactly ONE
// .cpp of the whole project you must define STB_IMAGE_IMPLEMENTATION before including it, so the
// actual function bodies are compiled here (and nowhere else, to avoid "multiple definition"
// link errors). Every other file just includes <stb/stb_image.h> and gets the declarations.
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

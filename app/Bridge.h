// Bridging header — the only thing Swift sees of the engine.
//
// Deliberately just the C facade. No C++ types, no templates, and nothing that
// can throw across the boundary (an escaping C++ exception would terminate the
// process rather than surface as an error).

#import "orion/orion.h"

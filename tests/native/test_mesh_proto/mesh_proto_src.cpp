// mesh_proto.cpp is built here rather than in [env:native]'s build_src_filter:
// it links against stand-ins (keys, logging, signing) that
// tests/test_mesh_proto.cpp defines, which no other test provides.
#include "../../../src/mesh_proto.cpp"

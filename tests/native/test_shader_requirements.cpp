#include "test_main.h"
#include "../../port/src/graphics/shaders/shader_requirements.h"
using namespace superman_returns::graphics::shaders;
SR_TEST(device_limit_shortfall_names_requirement) {
 ShaderRequirements r; r.storage_buffers=34; r.storage_buffer_dynamic_indexing=true;
 ShaderCapabilities c; c.storage_buffers=16;
 auto errors=CheckRequirements(r,c);SR_CHECK_EQ(errors.size(),2);
 SR_CHECK(errors[0].find("storage_buffers")!=std::string::npos);
 c.storage_buffers=64;c.storage_buffer_dynamic_indexing=true;SR_CHECK(CheckRequirements(r,c).empty());
}

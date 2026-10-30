#include "test_main.h"
#include "../../port/src/native_renderer/d3d12_texture_updates.h"
#include <array>

using namespace superman_returns::native;
namespace {
struct Level { uint32_t width, height, row_pitch, rows; size_t offset; };
const std::array<Level,2> levels{{{2,2,8,2,0},{1,1,4,1,16}}};
}

SR_TEST(d3d12_texture_updates_compare_exact_subresources) {
  auto budget=std::make_shared<D3DTextureSnapshotBudget>();
  std::array<uint8_t,20> bytes{};
  auto snapshot=D3DTextureSnapshot::Capture(budget, std::span(bytes),std::span(levels));
  SR_CHECK(snapshot);
  SR_CHECK(snapshot->Matches(std::span(bytes),std::span(levels),0));
  bytes[19]=1;
  SR_CHECK(snapshot->Matches(std::span(bytes),std::span(levels),0));
  SR_CHECK(!snapshot->Matches(std::span(bytes),std::span(levels),1));
  auto changed=levels; changed[0].width=1;
  SR_CHECK(!snapshot->Matches(std::span(bytes),std::span(changed),0));
  SR_CHECK(!snapshot->Matches(std::span(bytes).first(19),std::span(levels),1));
}

SR_TEST(d3d12_texture_updates_snapshot_budget_and_bad_ranges) {
  auto budget=std::make_shared<D3DTextureSnapshotBudget>();
  std::array<uint8_t,20> bytes{};
  budget->limit=1;
  SR_CHECK(!D3DTextureSnapshot::Capture(budget,std::span(bytes),std::span(levels)));
  budget->limit=1024;
  auto snapshot=D3DTextureSnapshot::Capture(budget,std::span(bytes),std::span(levels));
  SR_CHECK(snapshot && budget->used>0);
  budget->limit=budget->used;
  SR_CHECK(!D3DTextureSnapshot::Capture(budget,std::span(bytes),std::span(levels)));
  snapshot.reset(); SR_CHECK_EQ(budget->used,0u);
  auto bad=levels; bad[1].offset=SIZE_MAX;
  SR_CHECK(!D3DTextureSnapshot::Capture(budget,std::span(bytes),std::span(bad)));
  std::vector<uint8_t> large(8*1024*1024+1);
  budget->limit=64*1024*1024;
  SR_CHECK(!D3DTextureSnapshot::Capture(budget,std::span(large),std::span(levels)));
}

SR_TEST(d3d12_texture_updates_never_reuse_pending_or_current_recording) {
  SR_CHECK(D3DTextureCanOverwrite(4,4,5));
  SR_CHECK(!D3DTextureCanOverwrite(4,3,5));
  SR_CHECK(!D3DTextureCanOverwrite(5,5,5));
  SR_CHECK(!D3DTextureCanOverwrite(4,UINT64_MAX,5));
  SR_CHECK(!D3DTextureCanOverwrite(0,4,5));
}

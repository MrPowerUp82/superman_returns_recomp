#include "test_main.h"
#include "../../port/src/graphics/temporal_vertex_fetch.h"
using namespace superman_returns::graphics;
SR_TEST(temporal_fetch_matches_reallocated_descriptor_only_for_same_proven_host_contents) {
  uint32_t metadata[224][4]{};metadata[80][0]=7;metadata[80][1]=16;metadata[80][2]=32;metadata[80][3]=9;
  TemporalFetchResource resource{7,123,4,18};TemporalFetchSignature first{},second{};
  SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},first));
  metadata[80][0]=19;resource.descriptor=19;
  SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},second));SR_CHECK(first==second);
  ++resource.content_version;
  SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},second));SR_CHECK(first!=second);
  --resource.content_version;++resource.generation;
  SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},second));SR_CHECK(first!=second);
  --resource.generation;++resource.key;
  SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},second));SR_CHECK(first!=second);
}
SR_TEST(temporal_fetch_rejects_unresolved_or_conflicting_resource_bindings_atomically) {
  uint32_t metadata[224][4]{};metadata[1][0]=7;
  TemporalFetchSignature output{};output[0][0]=99;const auto original=output;
  SR_CHECK(!CaptureTemporalVertexFetch(metadata,{},output));SR_CHECK(output==original);
  TemporalFetchResource resources[]{{7,123,4,18},{7,456,4,18}};
  SR_CHECK(!CaptureTemporalVertexFetch(metadata,resources,output));SR_CHECK(output==original);
  resources[1]=resources[0];resources[0].generation=0;resources[1].generation=0;
  SR_CHECK(!CaptureTemporalVertexFetch(metadata,resources,output));SR_CHECK(output==original);
}
SR_TEST(temporal_fetch_retains_semantic_offset_stride_and_format) {
  uint32_t metadata[224][4]{};metadata[0][0]=7;
  TemporalFetchResource resource{7,1,1,1};TemporalFetchSignature first{},second{};
  SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},first));
  for(int component=1;component<=3;++component) {
    metadata[0][component]=1;
    SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},second));SR_CHECK(first!=second);
    metadata[0][component]=0;
  }
  metadata[0][0]=0;metadata[1][0]=7;
  SR_CHECK(CaptureTemporalVertexFetch(metadata,{&resource,1},second));SR_CHECK(first!=second);
}

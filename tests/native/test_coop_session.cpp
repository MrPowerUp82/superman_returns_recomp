#include "../../port/src/coop_session.h"
#include "test_main.h"
using namespace superman_returns::coop;
SR_TEST(coop_session_retries_readonly_readiness_without_creating_or_removing_an_actor) {
  SessionLifecycle s;
  for(unsigned i=0;i<3;++i) {
    SR_CHECK(s.Next(1)==SessionAction::Spawn);
    s.Begin(SessionAction::Spawn);s.Complete(SessionAction::Spawn,SessionResult::Waiting);
    SR_CHECK(!s.Active());
  }
  SR_CHECK(s.Next(2)==SessionAction::None);
  SR_CHECK(s.Next(3)==SessionAction::Spawn);
}
SR_TEST(coop_session_disable_enable_completes_removal_before_a_fresh_spawn) {
  SessionLifecycle s;
  for(uint64_t request=1;request<7;request+=2) {
    SR_CHECK(s.Next(request)==SessionAction::Spawn);
    s.Begin(SessionAction::Spawn);s.Complete(SessionAction::Spawn,SessionResult::Success);
    SR_CHECK(s.Active());SR_CHECK(s.Next(request)==SessionAction::None);
    SR_CHECK(s.Next(request+1)==SessionAction::Remove);
    s.Begin(SessionAction::Remove);s.Complete(SessionAction::Remove,SessionResult::Waiting);
    SR_CHECK(s.Active());SR_CHECK(s.Next(request+1)==SessionAction::Remove);
    s.Begin(SessionAction::Remove);s.Complete(SessionAction::Remove,SessionResult::Success);
    SR_CHECK(!s.Active());SR_CHECK(s.Next(request+1)==SessionAction::None);
  }
}
SR_TEST(coop_session_partial_spawn_never_retries_without_cleanup_even_after_rapid_toggles) {
  SessionLifecycle s;
  s.Next(1);s.Begin(SessionAction::Spawn);s.Complete(SessionAction::Spawn,SessionResult::Failed);
  SR_CHECK(s.Next(1)==SessionAction::None);
  SR_CHECK(s.Next(3)==SessionAction::Remove); // off/on arrived before game callback
  s.Begin(SessionAction::Remove);s.Complete(SessionAction::Remove,SessionResult::Success);
  SR_CHECK(s.Next(3)==SessionAction::Spawn);
}
SR_TEST(coop_session_failed_removal_requires_cleanup_before_rejoin) {
  SessionLifecycle s;
  s.Next(1);s.Begin(SessionAction::Spawn);s.Complete(SessionAction::Spawn,SessionResult::Success);
  s.Next(2);s.Begin(SessionAction::Remove);s.Complete(SessionAction::Remove,SessionResult::Failed);
  SR_CHECK(!s.Active());SR_CHECK(s.Next(2)==SessionAction::None);
  SR_CHECK(s.Next(3)==SessionAction::Remove);
}
SR_TEST(coop_session_starts_disabled_and_only_first_hotkey_request_can_spawn) {
  SessionLifecycle s;
  for(unsigned frame=0;frame<100;++frame) {
    SR_CHECK(s.Next(0)==SessionAction::None);
    SR_CHECK(!s.Active());
  }
  SR_CHECK(s.Next(1)==SessionAction::Spawn);
  s.Begin(SessionAction::Spawn);s.Complete(SessionAction::Spawn,SessionResult::Success);
  SR_CHECK(s.Active());
  SR_CHECK(s.Next(2)==SessionAction::Remove);
}

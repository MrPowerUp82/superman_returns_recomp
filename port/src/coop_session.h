#pragma once
#include <cstdint>
namespace superman_returns::coop {
enum class SessionAction {None,Spawn,Remove};
enum class SessionResult {Waiting,Success,Failed};
// Game-thread state; the UI sends a monotonically increasing toggle request.
// Odd requests mean enabled, even requests mean disabled.
class SessionLifecycle {
 public:
  SessionAction Next(uint64_t request) {
    if(request!=request_) {request_=request;blocked_=false;}
    if(blocked_)return SessionAction::None;
    if(!(request&1))return may_have_actor_?SessionAction::Remove:SessionAction::None;
    if(active_)return SessionAction::None;
    return may_have_actor_?SessionAction::Remove:SessionAction::Spawn;
  }
  void Begin(SessionAction action) {
    if(action==SessionAction::None)return;
    blocked_=true;
    if(action==SessionAction::Spawn)may_have_actor_=true;
  }
  void Complete(SessionAction action,SessionResult result) {
    if(action==SessionAction::None)return;
    if(result==SessionResult::Waiting) {
      blocked_=false;
      if(action==SessionAction::Spawn)may_have_actor_=false;
      return;
    }
    if(action==SessionAction::Spawn)may_have_actor_=true;
    if(result==SessionResult::Failed) {blocked_=true;active_=false;return;}
    blocked_=false;
    active_=action==SessionAction::Spawn;
    may_have_actor_=active_;
  }
  bool Active() const {return active_;}
 private:
  uint64_t request_=0;
  bool active_=false,may_have_actor_=false,blocked_=false;
};
}

#pragma once
struct PPCContext;
namespace rex {class Runtime;}
namespace superman_returns::coop {
void InstallCoopHud(rex::Runtime&);
// The private native producer must never submit its temporary state to the
// game's shared render queue. Called by the existing state-submission hook.
bool CaptureCoopHud(PPCContext&,unsigned char*);
}

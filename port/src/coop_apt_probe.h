#pragma once
namespace superman_returns::coop {
// Render thread only; bounded read-only observations of an existing HUD pass.
void BeginAptHudProbe();
void EndAptHudProbe();
}

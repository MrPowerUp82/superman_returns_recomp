// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <rex/ui/graphics_provider.h>
#include <rex/ui/presenter.h>

namespace sr::lsfg {
// Construct in the executable so its presenter uses the local implementation.
std::unique_ptr<rex::ui::Presenter> CreateProjectPresenter(
    rex::ui::GraphicsProvider& provider,
    rex::ui::Presenter::HostGpuLossCallback callback);
}

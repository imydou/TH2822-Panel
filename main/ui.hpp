#pragma once
#include "meter.hpp"
#include <functional>
void panel_ui_create(std::function<void(th::Action)> send);
void panel_ui_update(const th::State &state, bool pending = false);

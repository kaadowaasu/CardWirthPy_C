#pragma once

#include "services.hpp"

namespace cwxeditor::lib {

// 指定済みのサービスを優先し、不足分がある場合だけ画面基盤を作成する。
Services with_service_defaults(Services inherited, Services overrides = {});

}

//
//  Generated file. Do not edit.
//

// clang-format off

#include "generated_plugin_registrant.h"

#include <posthog_flutter/posthog_flutter_plugin.h>

void fl_register_plugins(FlPluginRegistry* registry) {
  g_autoptr(FlPluginRegistrar) posthog_flutter_registrar =
      fl_plugin_registry_get_registrar_for_plugin(registry, "PosthogFlutterPlugin");
  posthog_flutter_plugin_register_with_registrar(posthog_flutter_registrar);
}

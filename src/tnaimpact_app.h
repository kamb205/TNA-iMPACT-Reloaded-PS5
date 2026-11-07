// tnaimpact - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#include "frame_dump.h"
#include "image_dump.h"
#include "log_filter.h"

class TnaimpactApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<TnaimpactApp>(new TnaimpactApp(ctx, "tnaimpact",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // Developer image dump for static analysis, only with TNA_DUMP_IMAGE set (image_dump.h).
  void OnPostLoadXexImage() override { DumpGuestImageIfRequested(runtime(), PPCImageConfig); }
  // Drop MoltenVK's harmless per-pipeline primitive-restart warning (log spam).
  void OnPostInitLogging() override { tnaimpact::SilenceMoltenVkPrimitiveRestartWarning(); }
  // void OnPostSetup() override {}
  void OnPostSetup() override { tnaimpact::StartFrameDumpIfRequested(runtime()); }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  void OnShutdown() override { tnaimpact::StopFrameDump(); }
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};

#pragma once

// Build/version and repository constants used by OTA and UI paths.
#if __has_include("build_version.h")
#include "build_version.h"
#endif

#ifdef FW_VERSION
static constexpr const char kFirmwareVersion[] = FW_VERSION;
#elif defined(BUILD_VERSION)
static constexpr const char kFirmwareVersion[] = BUILD_VERSION;
#else
static constexpr const char kFirmwareVersion[] = "dev";
#endif
static constexpr const char kFirmwareGitHubOwner[] = "UniqueDroid";
static constexpr const char kFirmwareGitHubRepo[] = "esp-drummachine";
static constexpr const char kFirmwareGitHubReleaseApi[] = "https://api.github.com/repos/UniqueDroid/esp-drummachine/releases/latest";
static constexpr const char kFirmwareGitHubReleasesUrl[] = "https://github.com/UniqueDroid/esp-drummachine/releases";
static constexpr const char kFirmwareGitHubLogoRawUrl[] = "https://raw.githubusercontent.com/UniqueDroid/esp-drummachine/main/esp_drummachine_logo.png";

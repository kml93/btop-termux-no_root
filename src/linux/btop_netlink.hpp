#pragma once
// Per-interface RX/TX byte counters via netlink, for non-root Android.
//
// On non-rooted Android, SELinux denies reads of /proc/net/dev and
// /sys/class/net/<if>/statistics/*, so btop's normal network stats source is
// unavailable. As a fallback, RTM_GETSTATS (netlink, sent WITHOUT bind()) is
// allowed for unprivileged apps and returns live per-interface counters.
//
// Returns: interface name -> { rx_bytes, tx_bytes }.
// Empty map on non-Android builds or on failure. No root, no bind(), no deps.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> android_netlink_stats();

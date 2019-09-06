// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// A wrapper around computing basic timing statistics, and publishing them to a
// concordMetrics::Gauge.

#ifndef CONSENSUS_CONCORD_TIMING_STAT_H_
#define CONSENSUS_CONCORD_TIMING_STAT_H_

#include <log4cplus/loggingmacros.h>
#include <chrono>
#include "Metrics.hpp"
#include "hdr_histogram.h"

namespace concord {
namespace consensus {

class TimingStat {
 private:
  bool enabled_;
  std::chrono::steady_clock::time_point start_;
  concordMetrics::Component::Handle<concordMetrics::Gauge> g_avg_;
  concordMetrics::Component::Handle<concordMetrics::Gauge> g_min_;
  concordMetrics::Component::Handle<concordMetrics::Gauge> g_max_;
  concordMetrics::Component::Handle<concordMetrics::Gauge> g_p50_;
  concordMetrics::Component::Handle<concordMetrics::Gauge> g_count_;
  struct hdr_histogram* histogram_ = nullptr;

 public:
  TimingStat(std::string name, bool enabled, concordMetrics::Component& parent)
      : enabled_(enabled),
        // if you change `us`, also change the units in the Set call
        g_avg_{parent.RegisterGauge(name + "_avg_us", 0)},
        g_min_{parent.RegisterGauge(name + "_min_us", 0)},
        g_max_{parent.RegisterGauge(name + "_max_us", 0)},
        g_p50_{parent.RegisterGauge(name + "_p50_us", 0)},
        g_count_{parent.RegisterGauge(name + "_count", 0)} {
    if (enabled_) {
      int64_t min_value = 1;        // 0 is invalid
      int64_t max_value = 5000000;  // 5 seconds, in microseconds
      int significant_figures = 1;  // Sub-usec isn't that important to us yet
      int init_result =
          hdr_init(min_value, max_value, significant_figures, &histogram_);
      if (init_result != 0) {
        LOG4CPLUS_WARN(log4cplus::Logger::getInstance("consensus.TimingStat"),
                       "Unable to init hdrhistogram " + name + " ("
                           << init_result << ": " << strerror(init_result)
                           << ")");
      }
    }
  }

  ~TimingStat() {
    if (histogram_) {
      hdr_close(histogram_);
    }
  }

  void Start() {
    if (enabled_) start_ = std::chrono::steady_clock::now();
  }

  void End() {
    if (enabled_ && histogram_) {
      std::chrono::steady_clock::duration span_ =
          std::chrono::steady_clock::now() - start_;
      hdr_record_value(
          histogram_,
          std::chrono::duration_cast<std::chrono::microseconds>(span_).count());
      // if you change `microseconds`, also change the name in the RegisterGauge
      // call
      g_avg_.Get().Set(hdr_mean(histogram_));
      g_min_.Get().Set(hdr_min(histogram_));
      g_max_.Get().Set(hdr_max(histogram_));
      g_p50_.Get().Set(hdr_value_at_percentile(histogram_, 50));
      g_count_.Get().Set(histogram_->total_count);
    }
  }

  void Reset() {
    if (enabled_ && histogram_) {
      hdr_reset(histogram_);
    }
  }
};

}  // namespace consensus
}  // namespace concord

#endif  // CONSENSUS_CONCORD_TIMING_STAT_H_

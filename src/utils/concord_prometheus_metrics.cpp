// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "concord_prometheus_metrics.hpp"
#include <log4cplus/loggingmacros.h>
#include <prometheus/serializer.h>
#include <prometheus/text_serializer.h>
#include <algorithm>
#include <tuple>
#include "yaml-cpp/yaml.h"

using namespace prometheus;
using namespace concordMetrics;

namespace concord::utils {

prometheus::ClientMetric ConcordBftPrometheusCollector::collect(
    const ConcordMetricConf& conf, concordMetrics::Counter c) const {
  ClientMetric metric;
  metric.counter.value = c.Get();
  metric.label = {{"source", "concordbft"}, {"component", conf.component_}};
  for (const auto& pair : conf.labels_) {
    metric.label.insert(metric.label.end(), {pair.first, pair.second});
  }
  return metric;
}

prometheus::ClientMetric ConcordBftPrometheusCollector::collect(
    const ConcordMetricConf& conf, concordMetrics::Gauge g) const {
  ClientMetric metric;
  metric.gauge.value = g.Get();
  metric.label = {{"source", "concordbft"}, {"component", conf.component_}};
  for (const auto& pair : conf.labels_) {
    metric.label.insert(metric.label.end(), {pair.first, pair.second});
  }
  return metric;
}

prometheus::ClientMetric ConcordBftPrometheusCollector::collect(
    const ConcordMetricConf& conf, concordMetrics::Status s) const {
  ClientMetric metric;
  return metric;
}

ConcordBftPrometheusCollector::ConcordBftPrometheusCollector(
    const std::vector<ConcordMetricConf>& metricsConfiguration,
    std::shared_ptr<concordMetrics::Aggregator> aggregator)
    : aggregator_(aggregator) {
  std::copy_if(metricsConfiguration.begin(), metricsConfiguration.end(),
               std::back_inserter(counters_),
               [](ConcordMetricConf c) { return c.type_ == "counter"; });
  std::copy_if(metricsConfiguration.begin(), metricsConfiguration.end(),
               std::back_inserter(gauges_),
               [](ConcordMetricConf c) { return c.type_ == "gauge"; });
  std::copy_if(metricsConfiguration.begin(), metricsConfiguration.end(),
               std::back_inserter(statuses_),
               [](ConcordMetricConf c) { return c.type_ == "status"; });
}

std::vector<MetricFamily> ConcordBftPrometheusCollector::Collect() {
  auto results = std::vector<MetricFamily>{};
  auto counters = collectCounters();
  results.insert(results.end(), counters.begin(), counters.end());
  auto gauges = collectGauges();
  results.insert(results.end(), std::move_iterator(gauges.begin()),
                 std::move_iterator(gauges.end()));
  auto statuses = collectStatuses();
  results.insert(results.end(), std::move_iterator(statuses.begin()),
                 std::move_iterator(statuses.end()));
  return results;
}

std::vector<MetricFamily> ConcordBftPrometheusCollector::collectCounters() {
  std::vector<MetricFamily> cf;
  for (auto& c : counters_) {
    cf.emplace_back(MetricFamily{
        c.name_,
        c.description_,
        MetricType::Counter,
        {collect(c, aggregator_->GetCounter(c.component_, c.name_))}});
  }
  return cf;
}

std::vector<MetricFamily> ConcordBftPrometheusCollector::collectGauges() {
  std::vector<MetricFamily> gf;
  for (auto& g : gauges_) {
    gf.emplace_back(MetricFamily{
        g.name_,
        g.description_,
        MetricType::Gauge,
        {collect(g, aggregator_->GetGauge(g.component_, g.name_))}});
  }
  return gf;
}

std::vector<MetricFamily> ConcordBftPrometheusCollector::collectStatuses() {
  return {};
}

std::vector<ConcordMetricConf> ConcordBftMetricsManager::parseConfiguration(
    const std::string& confPath) {
  YAML::Node config = YAML::LoadFile(confPath);
  std::vector<ConcordMetricConf> out;
  for (const auto& element : config["concordbft"]) {
    std::map<std::string, std::string> labels;
    for (const auto& labelsSet : element["labels"]) {
      for (auto const& pair : labelsSet) {
        labels.emplace(pair.first.as<std::string>(),
                       pair.second.as<std::string>());
      }
    }
    out.insert(out.end(), {element["name"].as<std::string>(), labels,
                           element["type"].as<std::string>(),
                           element["component"].as<std::string>(),
                           element["description"].as<std::string>(),
                           element["exposed"].as<std::string>() == "on"});
  }
  return out;
}

PrometheusRegistry::PrometheusRegistry(
    const std::string& bindAddress,
    const std::vector<ConcordMetricConf>& metricsConfiguration,
    uint64_t metricsDumpInterval)
    : exposer_(bindAddress, "/metrics", 1),
      metrics_configuration_(metricsConfiguration),
      counters_custom_collector_(
          std::make_shared<ConcordCustomCollector<prometheus::Counter>>(
              std::chrono::seconds(metricsDumpInterval))),
      gauges_custom_collector_(
          std::make_shared<ConcordCustomCollector<prometheus::Gauge>>(
              std::chrono::seconds(metricsDumpInterval))) {
  exposer_.RegisterCollectable(counters_custom_collector_);
  exposer_.RegisterCollectable(gauges_custom_collector_);
}

PrometheusRegistry::PrometheusRegistry(
    const std::string& bindAddress,
    const std::vector<ConcordMetricConf>& metricsConfiguration)
    : PrometheusRegistry(bindAddress, metricsConfiguration,
                         defaultMetricsDumpInterval){};

void PrometheusRegistry::scrapeRegistry(
    std::shared_ptr<prometheus::Collectable> registry) {
  exposer_.RegisterCollectable(registry);
}

prometheus::Family<prometheus::Counter>&
PrometheusRegistry::createCounterFamily(
    const std::string& name, const std::string& help,
    const std::map<std::string, std::string>& labels) {
  prometheus::Family<prometheus::Counter>& ret =
      counters_custom_collector_->createFamily(name, help, labels);
  for (const auto& c : metrics_configuration_) {
    if (c.name_ == name) {
      if (c.exposed_) {
        counters_custom_collector_->activateMetric(name);
        return ret;
      }
    }
  }
  return ret;
}

prometheus::Counter& PrometheusRegistry::createCounter(
    prometheus::Family<prometheus::Counter>& source,
    const std::map<std::string, std::string>& labels) {
  return source.Add(labels);
}

prometheus::Counter& PrometheusRegistry::createCounter(
    const std::string& name, const std::string& help,
    const std::map<std::string, std::string>& labels) {
  return createCounter(createCounterFamily(name, help, labels), {});
}

prometheus::Family<prometheus::Gauge>& PrometheusRegistry::createGaugeFamily(
    const std::string& name, const std::string& help,
    const std::map<std::string, std::string>& labels) {
  prometheus::Family<prometheus::Gauge>& ret =
      gauges_custom_collector_->createFamily(name, help, labels);
  for (const auto& c : metrics_configuration_) {
    if (c.name_ == name) {
      if (c.exposed_) {
        gauges_custom_collector_->activateMetric(name);
        return ret;
      }
    }
  }
  return ret;
}
prometheus::Gauge& PrometheusRegistry::createGauge(
    prometheus::Family<prometheus::Gauge>& source,
    const std::map<std::string, std::string>& labels) {
  return source.Add(labels);
}
prometheus::Gauge& PrometheusRegistry::createGauge(
    const std::string& name, const std::string& help,
    const std::map<std::string, std::string>& labels) {
  return createGauge(createGaugeFamily(name, help, labels), {});
}

std::vector<ConcordMetricConf> PrometheusRegistry::parseConfiguration(
    const std::string& configurationFilePath) {
  YAML::Node config = YAML::LoadFile(configurationFilePath);
  std::vector<ConcordMetricConf> out;
  for (const auto& element : config["concord"]) {
    out.insert(out.end(), {element["name"].as<std::string>(),
                           {},
                           element["type"].as<std::string>(),
                           "",
                           "",
                           element["exposed"].as<std::string>() == "on"});
  }
  return out;
}

template <typename T>
prometheus::Family<T>& ConcordCustomCollector<T>::createFamily(
    const std::string& name, const std::string& help,
    const std::map<std::string, std::string>& labels) {
  std::lock_guard<std::mutex> lock(lock_);
  return *(*metrics_.insert(
      metrics_.end(),
      std::make_shared<prometheus::Family<T>>(name, help, labels)));
}

template <typename T>
void ConcordCustomCollector<T>::activateMetric(const std::string& metricName) {
  std::lock_guard<std::mutex> lock(lock_);
  for (std::shared_ptr<prometheus::Family<T>> p : active_metrics_) {
    if (p->GetName() == metricName) {
      return;
    }
  }
  for (std::shared_ptr<prometheus::Family<T>> p : metrics_) {
    if (p->GetName() == metricName) {
      active_metrics_.push_back(p);
      return;
    }
  }
}
template <typename T>
void ConcordCustomCollector<T>::deactivateMetric(
    const std::string& metricName) {
  std::lock_guard<std::mutex> lock(lock_);
  active_metrics_.erase(
      std::remove_if(
          active_metrics_.begin(), active_metrics_.end(),
          [metricName](const std::shared_ptr<prometheus::Family<T>> p) {
            return p->GetName() == metricName;
          }),
      active_metrics_.end());
}

template <typename T>
std::vector<prometheus::MetricFamily> ConcordCustomCollector<T>::Collect() {
  std::lock_guard<std::mutex> lock(lock_);
  std::vector<prometheus::MetricFamily> res;
  for (const std::shared_ptr<Family<T>> f : active_metrics_) {
    const auto& tmp = f->Collect();
    res.insert(res.end(), std::move_iterator(tmp.begin()),
               std::move_iterator(tmp.end()));
  }
  if (!res.empty()) {
    auto currTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    if (currTime - last_dump_time_ >= dumpInterval_) {
      last_dump_time_ = currTime;
      LOG4CPLUS_INFO(logger_, "prometheus metrics dump: " +
                                  prometheus::TextSerializer().Serialize(res));
    }
  }

  return res;
}
}  // namespace concord::utils

// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
#ifndef UTILS_CONCORD_PROMETHEUS_METRICS_HPP
#define UTILS_CONCORD_PROMETHEUS_METRICS_HPP
#include <log4cplus/logger.h>
#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>
#include <string>
#include <utility>
#include "Metrics.hpp"

namespace concord::utils {
struct ConcordMetricConf {
 public:
  std::string name_;
  std::map<std::string, std::string> labels_;
  std::string type_;
  std::string component_;
  std::string description_;
  bool exposed_;
};
class PrometheusRegistry;
class ConcordBftMetricsManager;
template <typename T>
class ConcordCustomCollector : public prometheus::Collectable {
  log4cplus::Logger logger_;
  std::vector<std::shared_ptr<prometheus::Family<T>>> metrics_;
  std::vector<std::shared_ptr<prometheus::Family<T>>> active_metrics_;
  std::chrono::seconds dumpInterval_;
  std::chrono::seconds last_dump_time_;
  std::mutex lock_;
  prometheus::Family<T>& createFamily(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels);
  void activateMetric(const std::string& metricName);
  void deactivateMetric(const std::string& metricName);

 public:
  ConcordCustomCollector(std::chrono::seconds dumpInterval)
      : logger_(
            log4cplus::Logger::getInstance("com.vmware.concord.prometheus")),
        dumpInterval_(dumpInterval),
        last_dump_time_(0) {}
  std::vector<prometheus::MetricFamily> Collect() override;
  friend class PrometheusRegistry;
};

class IPrometheusRegistry {
 public:
  virtual void scrapeRegistry(
      std::shared_ptr<prometheus::Collectable> registry) = 0;

  virtual prometheus::Family<prometheus::Counter>& createCounterFamily(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) = 0;

  virtual prometheus::Counter& createCounter(
      prometheus::Family<prometheus::Counter>& source,
      const std::map<std::string, std::string>& labels) = 0;

  virtual prometheus::Counter& createCounter(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) = 0;

  virtual prometheus::Family<prometheus::Gauge>& createGaugeFamily(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) = 0;

  virtual prometheus::Gauge& createGauge(
      prometheus::Family<prometheus::Gauge>& source,
      const std::map<std::string, std::string>& labels) = 0;

  virtual prometheus::Gauge& createGauge(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) = 0;

  virtual ~IPrometheusRegistry() = default;
};

class PrometheusRegistry : public IPrometheusRegistry {
  prometheus::Exposer exposer_;
  std::vector<ConcordMetricConf> metrics_configuration_;
  std::shared_ptr<ConcordCustomCollector<prometheus::Counter>>
      counters_custom_collector_;
  std::shared_ptr<ConcordCustomCollector<prometheus::Gauge>>
      gauges_custom_collector_;

 public:
  explicit PrometheusRegistry(
      const std::string& bindAddress,
      const std::vector<ConcordMetricConf>& metricsConfiguration,
      uint64_t metricsDumpInterval /* 10 minutes by default */);

  explicit PrometheusRegistry(
      const std::string& bindAddress,
      const std::vector<ConcordMetricConf>& metricsConfiguration);

  void scrapeRegistry(
      std::shared_ptr<prometheus::Collectable> registry) override;

  prometheus::Family<prometheus::Counter>& createCounterFamily(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) override;

  prometheus::Counter& createCounter(
      prometheus::Family<prometheus::Counter>& source,
      const std::map<std::string, std::string>& labels) override;

  prometheus::Counter& createCounter(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) override;

  prometheus::Family<prometheus::Gauge>& createGaugeFamily(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) override;

  prometheus::Gauge& createGauge(
      prometheus::Family<prometheus::Gauge>& source,
      const std::map<std::string, std::string>& labels) override;

  prometheus::Gauge& createGauge(
      const std::string& name, const std::string& help,
      const std::map<std::string, std::string>& labels) override;

  static std::vector<ConcordMetricConf> parseConfiguration(
      const std::string& configurationFilePath);

 private:
  static const uint64_t defaultMetricsDumpInterval = 600;
};

class ConcordBftPrometheusCollector : public prometheus::Collectable {
  std::vector<ConcordMetricConf> counters_;
  std::vector<ConcordMetricConf> gauges_;
  std::vector<ConcordMetricConf> statuses_;
  std::shared_ptr<concordMetrics::Aggregator> aggregator_;
  prometheus::ClientMetric collect(const ConcordMetricConf& conf,
                                   concordMetrics::Counter c) const;
  prometheus::ClientMetric collect(const ConcordMetricConf& conf,
                                   concordMetrics::Gauge g) const;
  prometheus::ClientMetric collect(const ConcordMetricConf& conf,
                                   concordMetrics::Status s) const;

  std::vector<prometheus::MetricFamily> collectCounters();

  std::vector<prometheus::MetricFamily> collectGauges();

  std::vector<prometheus::MetricFamily> collectStatuses();

 public:
  ConcordBftPrometheusCollector(
      const std::vector<ConcordMetricConf>& metricsConfiguration,
      std::shared_ptr<concordMetrics::Aggregator> aggregator);
  std::vector<prometheus::MetricFamily> Collect() override;
};

class ConcordBftMetricsManager {
  std::shared_ptr<concordMetrics::Aggregator> aggregator_;
  std::shared_ptr<ConcordBftPrometheusCollector> collector_;

 public:
  ConcordBftMetricsManager(
      const std::vector<ConcordMetricConf>& metricsConfiguration)
      : aggregator_(std::make_shared<concordMetrics::Aggregator>()) {
    std::vector<ConcordMetricConf> exposed_conf;
    std::copy_if(metricsConfiguration.begin(), metricsConfiguration.end(),
                 std::back_inserter(exposed_conf),
                 [](const ConcordMetricConf& c) { return c.exposed_; });
    collector_ = std::make_shared<ConcordBftPrometheusCollector>(exposed_conf,
                                                                 aggregator_);
  }
  std::shared_ptr<ConcordBftPrometheusCollector> getCollector() {
    return collector_;
  }

  std::shared_ptr<concordMetrics::Aggregator> getAggregator() {
    return aggregator_;
  }

  static std::vector<ConcordMetricConf> parseConfiguration(
      const std::string& confPath);
};

}  // namespace concord::utils
#endif  // UTILS_CONCORD_PROMETHEUS_METRICS_HPP

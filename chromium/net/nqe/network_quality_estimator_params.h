// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NQE_NETWORK_QUALITY_ESTIMATOR_PARAMS_H_
#define NET_NQE_NETWORK_QUALITY_ESTIMATOR_PARAMS_H_

#include <map>
#include <string>

#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "net/base/network_change_notifier.h"
#include "net/nqe/effective_connection_type.h"
#include "net/nqe/network_quality.h"

namespace net {

namespace nqe {

namespace internal {

// NetworkQualityEstimatorParams computes the configuration parameters for
// the network quality estimator.
class NetworkQualityEstimatorParams {
 public:
  // |params| is the map containing all field trial parameters related to
  // NetworkQualityEstimator field trial.
  explicit NetworkQualityEstimatorParams(
      const std::map<std::string, std::string>& params);

  ~NetworkQualityEstimatorParams();

  // Returns the algorithm that should be used for computing effective
  // connection type. Returns an empty string if a valid algorithm paramter is
  // not specified.
  std::string GetEffectiveConnectionTypeAlgorithm() const;

  // Computes and returns the weight multiplier per second, which represents the
  // factor by which the weight of an observation reduces every second.
  double GetWeightMultiplierPerSecond() const;

  // Returns the factor by which the weight of an observation reduces for every
  // dBm difference between the current signal strength (in dBm), and the signal
  // strength at the time when the observation was taken.
  double GetWeightMultiplierPerDbm() const;

  // Returns a descriptive name corresponding to |connection_type|.
  static const char* GetNameForConnectionType(
      net::NetworkChangeNotifier::ConnectionType connection_type);

  // Sets the default observation for different connection types in
  // |default_observations|. The default observations are different for
  // different connection types (e.g., 2G, 3G, 4G, WiFi). The default
  // observations may be used to determine the network quality in absence of any
  // other information.
  void ObtainDefaultObservations(
      nqe::internal::NetworkQuality default_observations[]) const;

  // Sets |typical_network_quality| to typical network quality for different
  // effective connection types.
  void ObtainTypicalNetworkQuality(
      NetworkQuality typical_network_quality[]) const;

  // Sets the thresholds for different effective connection types in
  // |connection_thresholds|.
  void ObtainEffectiveConnectionTypeModelParams(
      nqe::internal::NetworkQuality connection_thresholds[]) const;

  // Returns the fraction of URL requests that should record the correlation
  // UMA.
  double correlation_uma_logging_probability() const;

  // Returns true if the effective connection type has been forced via field
  // trial parameters.
  bool forced_effective_connection_type_set() const;

  // Returns the effective connection type if it has been forced via field trial
  // parameters.
  EffectiveConnectionType forced_effective_connection_type() const;

  // Returns true if reading from the persistent cache is enabled.
  bool persistent_cache_reading_enabled() const;

  // Returns the the minimum interval betweeen consecutive notifications to a
  // single socket watcher.
  base::TimeDelta GetMinSocketWatcherNotificationInterval() const;

 private:
  // Map containing all field trial parameters related to
  // NetworkQualityEstimator field trial.
  const std::map<std::string, std::string> params_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(NetworkQualityEstimatorParams);
};

}  // namespace internal

}  // namespace nqe

}  // namespace net

#endif  // NET_NQE_NETWORK_QUALITY_ESTIMATOR_PARAMS_H_

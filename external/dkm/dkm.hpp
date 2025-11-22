#pragma once

// only included in case there's a C++11 compiler out there that doesn't support `#pragma once`
#ifndef DKM_KMEANS_H
#define DKM_KMEANS_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <tuple>
#include <type_traits>
#include <vector>

/*
DKM - A k-means implementation that is generic across variable data dimensions.
*/
namespace dkm {

/*
These functions are all private implementation details and shouldn't be referenced outside of this
file.
*/
namespace details {
/*
Calculate the square of the distance between two points for signed types
*/
template <typename T, size_t N>
typename std::enable_if<!std::is_unsigned<T>::value, T>::type distance_squared(
	const std::array<T, N>& point_a, const std::array<T, N>& point_b) {
	T d_squared = T();
	for (typename std::array<T, N>::size_type i = 0; i < N; ++i) {
		auto delta = point_a[i] - point_b[i];
		d_squared += delta * delta;
	}
	return d_squared;
}

/*
Calculate the square of the distance between two points for unsigned
Uses conditional subtraction to avoid unsigned underflow.
*/
template <typename T, size_t N>
typename std::enable_if<std::is_unsigned<T>::value, T>::type distance_squared(
	const std::array<T, N>& point_a, const std::array<T, N>& point_b) {
	T d_squared = T();
	for (typename std::array<T, N>::size_type i = 0; i < N; ++i) {
		T diff;
		if (point_a[i] >= point_b[i]) {
			diff = point_a[i] - point_b[i];
		} else {
			diff = point_b[i] - point_a[i];
		}
		d_squared += diff * diff;
	}
	return d_squared;
}

template <typename T, size_t N>
T distance(const std::array<T, N>& point_a, const std::array<T, N>& point_b) {
	return static_cast<T>(std::sqrt(distance_squared(point_a, point_b)));
}

/*
Calculate the smallest distance between each of the data points and any of the input means.
*/
template <typename T, size_t N>
std::vector<T> closest_distance(
	const std::vector<std::array<T, N>>& means, const std::vector<std::array<T, N>>& data) {
	std::vector<T> distances;
	distances.reserve(data.size());
	for (auto& d : data) {
		T closest = distance_squared(d, means[0]);
		for (auto& m : means) {
			T distance = distance_squared(d, m);
			if (distance < closest)
				closest = distance;
		}
		distances.push_back(closest);
	}
	return distances;
}

/*
This is an alternate initialization method based on the [kmeans++](https://en.wikipedia.org/wiki/K-means%2B%2B)
initialization algorithm.
*/
template <typename T, typename S = uint64_t, size_t N>
std::vector<std::array<T, N>> random_plusplus(const std::vector<std::array<T, N>>& data, uint32_t k, S seed) {
	assert(k > 0);
	assert(data.size() > 0);

	// If data is empty then return an empty vector
	if (data.empty()) {
		return std::vector<std::array<T, N>>();
	}

	// If all of the data points are identical then the distances will be zero, just fill the starting means with copies
	// of the first element
	if (std::all_of(data.begin(), data.end(), [&data](const std::array<T, N>& a) { return a == data[0]; })) {
		return std::vector<std::array<T, N>>(k, data[0]);
	}

	using input_size_t = typename std::array<T, N>::size_type;
	std::vector<std::array<T, N>> means;
	// Using a very simple PRBS generator, parameters selected according to
	// https://en.wikipedia.org/wiki/Linear_congruential_generator#Parameters_in_common_use
	std::linear_congruential_engine<S, 6364136223846793005, 1442695040888963407, std::numeric_limits<S>::max()> rand_engine(seed);
	// Select first mean at random from the set
	{
		std::uniform_int_distribution<input_size_t> uniform_generator(0, data.size() - 1);
		means.push_back(data[uniform_generator(rand_engine)]);
	}

	for (uint32_t count = 1; count < k; ++count) {
		// Calculate the distance to the closest mean for each data point
		auto distances = details::closest_distance(means, data);
		// Pick a random point weighted by the distance from existing means
		// TODO: This might convert floating point weights to ints, distorting the distribution for small weights
#if !defined(_MSC_VER) || _MSC_VER >= 1900
		std::discrete_distribution<input_size_t> generator(distances.begin(), distances.end());
#else  // MSVC++ older than 14.0
		input_size_t i = 0;
		std::discrete_distribution<input_size_t> generator(distances.size(), 0.0, 0.0, [&distances, &i](double) { return distances[i++]; });
#endif
		means.push_back(data[generator(rand_engine)]);
	}
	return means;
}

/*
Calculate the index of the mean a particular data point is closest to (euclidean distance)
*/
template <typename T, size_t N>
uint32_t closest_mean(const std::array<T, N>& point, const std::vector<std::array<T, N>>& means) {
	assert(!means.empty());
	T smallest_distance = distance_squared(point, means[0]);
	typename std::array<T, N>::size_type index = 0;
	T distance;
	for (size_t i = 1; i < means.size(); ++i) {
		distance = distance_squared(point, means[i]);
		if (distance < smallest_distance) {
			smallest_distance = distance;
			index = i;
		}
	}
	return static_cast<uint32_t>(index);
}

/*
Calculate the index of the mean each data point is closest to (euclidean distance).
*/
template <typename T, size_t N>
std::vector<uint32_t> calculate_clusters(
	const std::vector<std::array<T, N>>& data, const std::vector<std::array<T, N>>& means) {
	std::vector<uint32_t> clusters;
	for (auto& point : data) {
		clusters.push_back(closest_mean(point, means));
	}
	return clusters;
}

/*
Calculate means based on data points and their cluster assignments.
*/
template <typename T, size_t N>
std::vector<std::array<T, N>> calculate_means(const std::vector<std::array<T, N>>& data,
	const std::vector<uint32_t>& clusters,
	const std::vector<std::array<T, N>>& old_means,
	uint32_t k) {
	std::vector<std::array<T, N>> means(k);
	std::vector<T> count(k, T());
	for (size_t i = 0; i < std::min(clusters.size(), data.size()); ++i) {
		auto& mean = means[clusters[i]];
		count[clusters[i]] += 1;
		for (size_t j = 0; j < std::min(data[i].size(), mean.size()); ++j) {
			mean[j] += data[i][j];
		}
	}
	for (size_t i = 0; i < k; ++i) {
		if (count[i] == 0) {
			means[i] = old_means[i];
		} else {
			for (size_t j = 0; j < means[i].size(); ++j) {
				means[i][j] /= count[i];
			}
		}
	}
	return means;
}

template <typename T, size_t N>
std::vector<T> deltas(
	const std::vector<std::array<T, N>>& old_means, const std::vector<std::array<T, N>>& means)
{
	std::vector<T> distances;
	distances.reserve(means.size());
	assert(old_means.size() == means.size());
	for (size_t i = 0; i < means.size(); ++i) {
		distances.push_back(distance(means[i], old_means[i]));
	}
	return distances;
}

template <typename T>
bool deltas_below_limit(const std::vector<T>& deltas, T min_delta) {
	for (T d : deltas) {
		if (d > min_delta) {
			return false;
		}
	}
	return true;
}

} // namespace details

/*
clustering_parameters is the configuration used for running the kmeans_lloyd algorithm.

It requires a k value for initialization, and can subsequently be configured with your choice
of optional parameters, including:
* Maximum iteration count; the algorithm will terminate if it reaches this iteration count
  before converging on a solution. The results returned are the means and cluster assignments 
  calculated in the last iteration before termination.
* Minimum delta; the algorithm will terminate if the change in position of all means is
  smaller than the specified distance.
* Random seed; if present, this will be used in place of `std::random_device` for kmeans++
  initialization. This can be used to ensure reproducible/deterministic behavior.
*/
template <typename T, typename S = uint64_t>
class clustering_parameters {
public:
	explicit clustering_parameters(uint32_t k) :
	_k(k),
	_has_max_iter(false), _max_iter(),
	_has_min_delta(false), _min_delta(),
	_has_rand_seed(false), _rand_seed()
	{}

	void set_max_iteration(size_t max_iter)
	{
		_max_iter = max_iter;
		_has_max_iter = true;
	}

	void set_min_delta(T min_delta)
	{
		_min_delta = min_delta;
		_has_min_delta = true;
	}

	void set_random_seed(S rand_seed)
	{
		_rand_seed = rand_seed;
		_has_rand_seed = true;
	}

	bool has_max_iteration() const { return _has_max_iter; }
	bool has_min_delta() const { return _has_min_delta; }
	bool has_random_seed() const { return _has_rand_seed; }

	uint32_t get_k() const { return _k; };
	size_t get_max_iteration() const { return _max_iter; }
	T get_min_delta() const { return _min_delta; }
	S get_random_seed() const { return _rand_seed; }

private:
	uint32_t _k;
	bool _has_max_iter;
	size_t _max_iter;
	bool _has_min_delta;
	T _min_delta;
	bool _has_rand_seed;
	S _rand_seed;
};

/*
Implementation of k-means generic across the data type and the dimension of each data item. Expects
the data to be a vector of fixed-size arrays. Generic parameters are the type of the base data (T)
and the dimensionality of each data point (N). All points must have the same dimensionality.

e.g. points of the form (X, Y, Z) would be N = 3.

Takes a `clustering_parameters` struct for algorithm configuration. See the comments for the
`clustering_parameters` struct for more information about the configuration values and how they
affect the algorithm.

Returns a std::tuple containing:
  0: A vector holding the means for each cluster from 0 to k-1.
  1: A vector containing the cluster number (0 to k-1) for each corresponding element of the input
	 data vector.

Implementation details:
This implementation of k-means uses [Lloyd's Algorithm](https://en.wikipedia.org/wiki/Lloyd%27s_algorithm)
with the [kmeans++](https://en.wikipedia.org/wiki/K-means%2B%2B)
used for initializing the means.

*/
template <typename T, typename S = uint64_t, size_t N>
std::tuple<std::vector<std::array<T, N>>, std::vector<uint32_t>> kmeans_lloyd(
	const std::vector<std::array<T, N>>& data, const clustering_parameters<T>& parameters) {
	assert(parameters.get_k() > 0); // k must be greater than zero
	assert(data.size() >= parameters.get_k()); // there must be at least k data points
	std::random_device rand_device;
	S seed = parameters.has_random_seed() ? parameters.get_random_seed() : rand_device();
	std::vector<std::array<T, N>> means = details::random_plusplus(data, parameters.get_k(), seed);

	std::vector<std::array<T, N>> old_means;
	std::vector<std::array<T, N>> old_old_means;
	std::vector<uint32_t> clusters;
	// Calculate new means until convergence is reached or we hit the maximum iteration count
	size_t count = 0;
	do {
		clusters = details::calculate_clusters(data, means);
		old_old_means = old_means;
		old_means = means;
		means = details::calculate_means(data, clusters, old_means, parameters.get_k());
		++count;
	} while (means != old_means && means != old_old_means
		&& !(parameters.has_max_iteration() && count == parameters.get_max_iteration())
		&& !(parameters.has_min_delta() && details::deltas_below_limit(details::deltas(old_means, means), parameters.get_min_delta())));

	return std::tuple<std::vector<std::array<T, N>>, std::vector<uint32_t>>(means, clusters);
}

/*
This overload exists to support legacy code which uses this signature of the kmeans_lloyd function.
Any code still using this signature should move to the version of this function that uses a
`clustering_parameters` struct for configuration.
*/
template <typename T, size_t N>
std::tuple<std::vector<std::array<T, N>>, std::vector<uint32_t>> kmeans_lloyd(
	const std::vector<std::array<T, N>>& data, uint32_t k,
	size_t max_iter = 0, T min_delta = -1.0) {
	clustering_parameters<T> parameters(k);
	if (max_iter != 0) {
		parameters.set_max_iteration(max_iter);
	}
	if (min_delta != 0) {
		parameters.set_min_delta(min_delta);
	}
	return kmeans_lloyd(data, parameters);
}

} // namespace dkm

#endif /* DKM_KMEANS_H */

/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "hand_kinematics.h"
#include <Eigen/Dense>
#include <Eigen/src/Geometry/Quaternion.h>

#define USE_RECONSTRUCTED_POSE 0

/*
 * WRIST
 * |- Thumb metacarpal
 * |  |- Thumb proximal
 * |     |- Thumb distal
 * |        |- Thumb tip
 * |- {Index,Middle,Ring,Finger} metacarpal
 * |- {Index,Middle,Ring,Finger} Proximal
 *    |- Intermediate
 *       |- Distal
 *          |- Tip
 */

namespace
{
using packed_int = uint8_t;

auto map_vec3(const std::array<float, 3> & in)
{
	return Eigen::Map<const Eigen::Vector3f>(in.data());
}

auto map_quat(const std::array<float, 4> & in)
{
	return Eigen::Map<const Eigen::Quaternionf>(in.data());
}

// BEGIN Quantization functions
float sinc(float theta)
{
	if (theta < std::numeric_limits<float>::epsilon())
		return 1;

	return sin(theta) / theta;
}

Eigen::Vector3f logq(const Eigen::Quaternionf & q)
{
	Eigen::Vector3f u{q.x(), q.y(), q.z()};
	float sin_θ = u.norm();

	if (sin_θ < Eigen::NumTraits<float>::epsilon())
	{
		return {0, 0, 0};
	}
	else if (q.w() > 0)
	{
		float theta = std::atan2(sin_θ, q.w());
		return u * theta / sin_θ;
	}
	else
	{
		float theta = std::atan2(sin_θ, -q.w());
		return -u * theta / sin_θ;
	}
}

Eigen::Vector3f logq(const std::array<float, 4> & q)
{
	return logq(map_quat(q));
}

Eigen::Quaternionf expq(const Eigen::Vector3f & v)
{
	float θ = v.norm();
	float sinc_θ = sinc(θ);
	float cos_θ = cos(θ);

	return Eigen::Quaternionf(
	        cos_θ,
	        v.x() * sinc_θ,
	        v.y() * sinc_θ,
	        v.z() * sinc_θ);
}

Eigen::Matrix<float, 4, 3> expq_jacobian(const Eigen::Vector3f & v)
{
	float θ = v.norm();

	float sinc_θ = sinc(θ);
	float cos_θ = cos(θ);

	Eigen::Vector3f n = v.normalized();

	Eigen::Matrix<float, 4, 3> dq; // Note: xyzw

	dq.block<1, 3>(3, 0) = -v * sinc_θ;
	dq.block<3, 3>(0, 0) = n * n.transpose() * (cos_θ - sinc_θ) + Eigen::Matrix<float, 3, 3>::Identity() * sinc_θ;

	return dq;
}

template <int N>
Eigen::Matrix<float, 3, N> rotate_jacobian(Eigen::Quaternionf q, Eigen::Vector3f u, Eigen::Matrix<float, 4, N> dq, Eigen::Matrix<float, 3, N> du)
{
	Eigen::Matrix<float, 3, N> J;

	float q0 = q.w();
	float q1 = q.x();
	float q2 = q.y();
	float q3 = q.z();
	float x = u.x();
	float y = u.y();
	float z = u.z();

	Eigen::RowVector<float, N> dq0 = dq.template block<1, N>(3, 0);
	Eigen::RowVector<float, N> dq1 = dq.template block<1, N>(0, 0);
	Eigen::RowVector<float, N> dq2 = dq.template block<1, N>(1, 0);
	Eigen::RowVector<float, N> dq3 = dq.template block<1, N>(2, 0);
	Eigen::RowVector<float, N> dx = du.template block<1, N>(0, 0);
	Eigen::RowVector<float, N> dy = du.template block<1, N>(1, 0);
	Eigen::RowVector<float, N> dz = du.template block<1, N>(2, 0);

	float q0q0 = q0 * q0;
	float q0q1 = q0 * q1;
	float q0q2 = q0 * q2;
	float q0q3 = q0 * q3;
	float q1q1 = q1 * q1;
	float q1q2 = q1 * q2;
	float q1q3 = q1 * q3;
	float q2q2 = q2 * q2;
	float q2q3 = q2 * q3;
	float q3q3 = q3 * q3;

	Eigen::RowVector<float, N> dq0q0 = q0 * dq0 + dq0 * q0;
	Eigen::RowVector<float, N> dq0q1 = q0 * dq1 + dq0 * q1;
	Eigen::RowVector<float, N> dq0q2 = q0 * dq2 + dq0 * q2;
	Eigen::RowVector<float, N> dq0q3 = q0 * dq3 + dq0 * q3;
	Eigen::RowVector<float, N> dq1q1 = q1 * dq1 + dq1 * q1;
	Eigen::RowVector<float, N> dq1q2 = q1 * dq2 + dq1 * q2;
	Eigen::RowVector<float, N> dq1q3 = q1 * dq3 + dq1 * q3;
	Eigen::RowVector<float, N> dq2q2 = q2 * dq2 + dq2 * q2;
	Eigen::RowVector<float, N> dq2q3 = q2 * dq3 + dq2 * q3;
	Eigen::RowVector<float, N> dq3q3 = q3 * dq3 + dq3 * q3;

	J.template block<1, N>(0, 0) =
	        (dq0q0 + dq1q1 - dq2q2 - dq3q3) * x + (q0q0 + q1q1 - q2q2 - q3q3) * dx +
	        2 * (dq1q2 - dq0q3) * u.y() + 2 * (q1q2 - q0q3) * dy +
	        2 * (dq1q3 + dq0q2) * u.z() + 2 * (q1q3 + q0q2) * dz;

	J.template block<1, N>(1, 0) =
	        2 * (dq1q2 + dq0q3) * x + 2 * (q1q2 + q0q3) * dx +
	        (dq0q0 - dq1q1 + dq2q2 - dq3q3) * y + (q0q0 - q1q1 + q2q2 - q3q3) * dy +
	        2 * (dq2q3 - dq0q1) * z + 2 * (q2q3 - q0q1) * dz;

	J.template block<1, N>(2, 0) =
	        2 * (dq1q3 - dq0q2) * x + 2 * (q1q3 - q0q2) * dx +
	        2 * (dq2q3 + dq0q1) * y + 2 * (q2q3 + q0q1) * dy +
	        (dq0q0 - dq1q1 - dq2q2 + dq3q3) * z + (q0q0 - q1q1 - q2q2 + q3q3) * dz;

	return J;
}

template <int N, typename T = float>
Eigen::Matrix<T, 4, N> quaternion_multiplication_jacobian(Eigen::Quaternion<T> qa, Eigen::Quaternion<T> qb, Eigen::Matrix<T, 4, N> dqa, Eigen::Matrix<T, 4, N> dqb)
{
	Eigen::Matrix<T, 4, N> J;

	T qax = qa.x();
	T qay = qa.y();
	T qaz = qa.z();
	T qaw = qa.w();
	T qbx = qb.x();
	T qby = qb.y();
	T qbz = qb.z();
	T qbw = qb.w();

	Eigen::RowVector<T, N> dqax = dqa.template block<1, N>(0, 0);
	Eigen::RowVector<T, N> dqay = dqa.template block<1, N>(1, 0);
	Eigen::RowVector<T, N> dqaz = dqa.template block<1, N>(2, 0);
	Eigen::RowVector<T, N> dqaw = dqa.template block<1, N>(3, 0);
	Eigen::RowVector<T, N> dqbx = dqb.template block<1, N>(0, 0);
	Eigen::RowVector<T, N> dqby = dqb.template block<1, N>(1, 0);
	Eigen::RowVector<T, N> dqbz = dqb.template block<1, N>(2, 0);
	Eigen::RowVector<T, N> dqbw = dqb.template block<1, N>(3, 0);

	J.template block<1, N>(0, 0) = qaw * dqbx + dqaw * qbx + qax * dqbw + dqax * qbw + qay * dqbz + dqay * qbz - qaz * dqby - dqaz * qby;
	J.template block<1, N>(1, 0) = qaw * dqby + dqaw * qby - qax * dqbz - dqax * qbz + qay * dqbw + dqay * qbw + qaz * dqbx + dqaz * qbx;
	J.template block<1, N>(2, 0) = qaw * dqbz + dqaw * qbz + qax * dqby + dqax * qby - qay * dqbx - dqay * qbx + qaz * dqbw + dqaz * qbw;
	J.template block<1, N>(3, 0) = qaw * dqbw + dqaw * qbw - qax * dqbx - dqax * qbx - qay * dqby - dqay * qby - qaz * dqbz - dqaz * qbz;

	return J;
}

Eigen::Vector3<packed_int> quantize_rotation(Eigen::Vector3f v)
{
	// Assume v is twice the log of a unit quaternion with a positive scalar part
	// The maximum norm of v is pi
	constexpr float scale = std::numeric_limits<packed_int>::max() / (2 * M_PI);
	Eigen::Vector3f scaled_rotation = (v + Eigen::Vector3f{M_PI, M_PI, M_PI}) * scale;

	return Eigen::Vector3<packed_int>(
	        std::round(scaled_rotation.x()),
	        std::round(scaled_rotation.y()),
	        std::round(scaled_rotation.z()));
}

// Eigen::Vector3f unquantize_rotation(Eigen::Vector3<packed_int> v)
// {
// 	constexpr float scale = (2 * M_PI) / std::numeric_limits<packed_int>::max();
// 	return Eigen::Vector3f(v) * scale - Eigen::Vector3f{M_PI, M_PI, M_PI};
// }
// END

// BEGIN Frame change functions
struct relative_pose
{
	Eigen::Vector3f position;
	Eigen::Vector3f rotation;
};

relative_pose to_relative_pose(const hand_kinematics::joint_pose & parent, const hand_kinematics::joint_pose & pose)
{
	return relative_pose{
	        .position = map_quat(parent.rotation).conjugate() * (map_vec3(pose.position) - map_vec3(parent.position)),
	        .rotation = logq(map_quat(parent.rotation).conjugate() * map_quat(pose.rotation)) * 2.f,
	};
}

// END

// Input of the kinematics function:
// 0-2:   Wrist position in metres
// 3-5:   Wrist rotation, between 0 and 1
// 6-end: Degrees of freedom, between 0 and 1
using kin_input = Eigen::Vector<float, hand_kinematics::nb_dof + 6>;

// Output of the kinematics function:
// i*7+0 - i*7+2: Position of joint i
// i*7+3 - i*7+6: Rotation of joint i (quaternion in xyzw order)
using kin_output = Eigen::Vector<float, hand_kinematics::HAND_JOINT_COUNT_TOTAL * 7>;

using kin_jacobian = Eigen::Matrix<float, hand_kinematics::HAND_JOINT_COUNT_TOTAL * 7, hand_kinematics::nb_dof + 6>;

auto kinematics(const hand_kinematics::pose_constants & c, const kin_input & in) -> std::pair<kin_output, kin_jacobian>
{
	std::pair<kin_output, kin_jacobian> out;

	auto & unpacked = out.first;
	unpacked.setZero();

	kin_jacobian & J = out.second;
	J.setZero();

	int i_dof = 6;
	int i_cst = 0;

	auto f = [&](const std::optional<hand_kinematics::joint_range> & rangex, const std::optional<hand_kinematics::joint_range> & rangey, const std::optional<hand_kinematics::joint_range> & rangez) -> std::pair<Eigen::Vector3f, Eigen::Matrix<float, 3, hand_kinematics::nb_dof + 6>> {
		auto f_scalar = [&](const std::optional<hand_kinematics::joint_range> & range) -> std::pair<float, Eigen::Matrix<float, 1, hand_kinematics::nb_dof + 6>> {
			if (range)
			{
				assert(i_dof < in.size());
				Eigen::Matrix<float, 1, hand_kinematics::nb_dof + 6> J;
				J.setZero();
				J(i_dof) = range->max - range->min;

				return {range->min + in[i_dof++] * (range->max - range->min), J};
			}
			else
			{
				assert(i_cst < c.size());
				return {c[i_cst++], Eigen::Matrix<float, 1, hand_kinematics::nb_dof + 6>::Zero()};
			}
		};

		auto [vx, Jx] = f_scalar(rangex);
		auto [vy, Jy] = f_scalar(rangey);
		auto [vz, Jz] = f_scalar(rangez);

		Eigen::Vector3f v(vx, vy, vz);
		Eigen::Matrix<float, 3, hand_kinematics::nb_dof + 6> J;

		J.row(0) = Jx;
		J.row(1) = Jy;
		J.row(2) = Jz;

		return {v, J};
	};

	// Set wrist position/rotation/jacobian
	unpacked.segment<3>(hand_kinematics::HAND_JOINT_WRIST * 7 + 0) = in.segment<3>(0);
	J.block<3, 3>(hand_kinematics::HAND_JOINT_WRIST * 7 + 0, 0).setIdentity();

	Eigen::Vector3f θ_wrist = (in.segment<3>(3) - Eigen::Vector3f::Ones() * 0.5) * M_PI; // Remap [0, 1] -> [-pi/2, pi/2]
	Eigen::Matrix<float, 3, 3> dθ_wrist = Eigen::Matrix3f::Identity() * M_PI;
	unpacked.segment<4>(hand_kinematics::HAND_JOINT_WRIST * 7 + 3) = expq(θ_wrist).coeffs();
	J.block<4, 3>(hand_kinematics::HAND_JOINT_WRIST * 7 + 3, 3) = expq_jacobian(θ_wrist) * dθ_wrist;

	for (int i = 0; i < hand_kinematics::joints.size(); i++)
	{
		assert(hand_kinematics::joints[i].parent < i + hand_kinematics::HAND_JOINT_THUMB_METACARPAL);

		// Relative position of joint i
		auto [rel_x, drel_x] = f(hand_kinematics::joints[i].posx, hand_kinematics::joints[i].posy, hand_kinematics::joints[i].posz);
		auto [rel_θ, drel_θ] = f(hand_kinematics::joints[i].rotx, hand_kinematics::joints[i].roty, hand_kinematics::joints[i].rotz);

		rel_θ *= 0.5;
		drel_θ *= 0.5;

		Eigen::Quaternionf rel_q = expq(rel_θ);
		Eigen::Matrix<float, 4, hand_kinematics::nb_dof + 6> drel_q = expq_jacobian(rel_θ) * drel_θ;

		// Absolute position of parent
		Eigen::Vector3f par_x = unpacked.segment<3>(hand_kinematics::joints[i].parent * 7);
		Eigen::Matrix<float, 3, hand_kinematics::nb_dof + 6> dpar_x = J.block<3, hand_kinematics::nb_dof + 6>(hand_kinematics::joints[i].parent * 7, 0);
		Eigen::Quaternionf par_q = Eigen::Quaternionf(unpacked.segment<4>(hand_kinematics::joints[i].parent * 7 + 3));
		Eigen::Matrix<float, 4, hand_kinematics::nb_dof + 6> dpar_q = J.block<4, hand_kinematics::nb_dof + 6>(hand_kinematics::joints[i].parent * 7 + 3, 0);

		// Combine poses
		Eigen::Vector3f x = par_x + par_q * rel_x;
		unpacked.segment<3>((i + 2) * 7) = x;
		J.block<3, hand_kinematics::nb_dof + 6>((i + 2) * 7, 0) = dpar_x + rotate_jacobian(par_q, rel_x, dpar_q, drel_x);

		Eigen::Quaternionf q = par_q * rel_q;
		unpacked.segment<4>((i + 2) * 7 + 3) = q.coeffs();
		J.block<4, hand_kinematics::nb_dof + 6>((i + 2) * 7 + 3, 0) = quaternion_multiplication_jacobian(par_q, rel_q, dpar_q, drel_q);
	}

	assert(i_dof == hand_kinematics::nb_dof + 6);
	assert(i_cst == c.size());

	unpacked.segment<3>(hand_kinematics::HAND_JOINT_PALM * 7) = 0.5f * (unpacked.segment<3>(7 * hand_kinematics::HAND_JOINT_WRIST) + unpacked.segment<3>(7 * hand_kinematics::HAND_JOINT_MIDDLE_PROXIMAL));
	unpacked.segment<4>(hand_kinematics::HAND_JOINT_PALM * 7 + 3) = unpacked.segment<4>(7 * hand_kinematics::HAND_JOINT_WRIST + 3);

	J.block<3, hand_kinematics::nb_dof + 6>(hand_kinematics::HAND_JOINT_PALM * 7, 0) = 0.5f * (J.block<3, hand_kinematics::nb_dof + 6>(hand_kinematics::HAND_JOINT_WRIST * 7, 0) + J.block<3, hand_kinematics::nb_dof + 6>(hand_kinematics::HAND_JOINT_MIDDLE_PROXIMAL * 7, 0));
	J.block<4, hand_kinematics::nb_dof + 6>(hand_kinematics::HAND_JOINT_PALM * 7 + 3, 0) = J.block<4, hand_kinematics::nb_dof + 6>(hand_kinematics::HAND_JOINT_WRIST * 7 + 3, 0);

	return out;
}
} // namespace

auto hand_kinematics::pack(const pose & p) -> std::pair<pose_constants, packed_pose>
{
	std::pair<pose_constants, packed_pose> packed;

	packed.second.wrist_position = p[HAND_JOINT_WRIST].position;

	auto u = quantize_rotation(logq(p[HAND_JOINT_WRIST].rotation) * 2.f);
	packed.second.wrist_rotation[0] = u.x();
	packed.second.wrist_rotation[1] = u.y();
	packed.second.wrist_rotation[2] = u.z();

#if USE_RECONSTRUCTED_POSE
	pose reconstructed_pose;
	reconstructed_pose[HAND_JOINT_WRIST].position = packed.second.wrist_position;
	reconstructed_pose[HAND_JOINT_WRIST].rotation = exp(unquantize_rotation(packed.second.wrist_rotation) * 0.5f);
#endif

	int i_dof = 0;
	int i_cst = 0;

	for (int i = 0; i < joints.size(); i++)
	{
#if USE_RECONSTRUCTED_POSE
		const auto & parent = reconstructed_pose[joints[i].parent];
#else
		const auto & parent = p[joints[i].parent];
#endif
		const auto & joint = p[i + HAND_JOINT_THUMB_METACARPAL];
		relative_pose l = to_relative_pose(parent, joint);

		auto f = [&](const std::optional<joint_range> & range, float & value) {
			if (range)
			{
				assert(i_dof < packed.second.dofs.size());
				value = std::clamp<float>((value - range->min) / (range->max - range->min), 0, 1);
				int quantized_value = std::round(std::numeric_limits<packed_int>::max() * value);

				packed.second.dofs[i_dof++] = quantized_value;

#if USE_RECONSTRUCTED_POSE
				value = range->min + quantized_value * (range->max - range->min) / std::numeric_limits<packed_int>::max();
#endif
			}
			else
			{
				assert(i_cst < packed.first.size());
				packed.first[i_cst++] = value;
			}
		};

		// Quantize and keep the reconstructed values
		f(joints[i].posx, l.position.x());
		f(joints[i].posy, l.position.y());
		f(joints[i].posz, l.position.z());
		f(joints[i].rotx, l.rotation.x());
		f(joints[i].roty, l.rotation.y());
		f(joints[i].rotz, l.rotation.z());

#if USE_RECONSTRUCTED_POSE
		reconstructed_pose[i + HAND_JOINT_THUMB_METACARPAL] = to_absolute_pose(reconstructed_pose[joints[i].parent], l);
#endif
	}

	assert(i_dof == packed.second.dofs.size());
	assert(i_cst == packed.first.size());

	return packed;
}

void hand_kinematics::apply_ik(const pose_constants & c, packed_pose & p, const pose & q)
{
	Eigen::Vector<float, nb_dof + 6> in;
	in.setZero();

	in.segment<3>(0) = Eigen::Vector3f(
	        p.wrist_position[0],
	        p.wrist_position[1],
	        p.wrist_position[2]);

	Eigen::Vector3f θ{
	        p.wrist_rotation[0] / (float)std::numeric_limits<packed_int>::max(),
	        p.wrist_rotation[1] / (float)std::numeric_limits<packed_int>::max(),
	        p.wrist_rotation[2] / (float)std::numeric_limits<packed_int>::max(),
	};
	// Eigen::Vector3f dθ = Eigen::Vector3f(p.wrist_angular_velocity);
	in.segment<3>(3) = Eigen::Vector3f(θ.x(), θ.y(), θ.z());

	for (int i = 0; i < nb_dof; i++)
	{
		in(i + 6) = float(p.dofs[i]) / std::numeric_limits<packed_int>::max();
	}

	for (int i = 0; i < 0; i++)
	{
		auto [out, J] = kinematics(c, in);

		// Optimize only the positions
		Eigen::Vector<float, HAND_JOINT_COUNT_TOTAL * 3> b;
		Eigen::Matrix<float, HAND_JOINT_COUNT_TOTAL * 3, nb_dof + 6> J2;

		for (int j = 0; j < HAND_JOINT_COUNT_TOTAL; j++)
		{
			b.segment<3>(j * 3) = out.segment<3>(j * 7) - map_vec3(q[j].position);
			J2.block<3, nb_dof + 6>(j * 3, 0) = J.block<3, nb_dof + 6>(j * 7, 0);
		}

		// in -= J2.colPivHouseholderQr().solve(b);
		in -= Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic>(J2).bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(b);
	}

	auto [out, J] = kinematics(c, in);

	// Pack velocities
	Eigen::Vector<float, HAND_JOINT_COUNT_TOTAL * 7> b;
	for (int i = 0; i < HAND_JOINT_COUNT_TOTAL; i++)
	{
		Eigen::Vector3f v = map_vec3(q[i].linear_velocity);
		Eigen::Vector3f ω = map_vec3(q[i].angular_velocity);
		Eigen::Quaternionf two_dq = Eigen::Quaternionf(0, ω.x(), ω.y(), ω.z()) * map_quat(q[i].rotation);

		b(i * 7 + 0) = v.x();
		b(i * 7 + 1) = v.y();
		b(i * 7 + 2) = v.z();
		b(i * 7 + 3) = 0.5 * two_dq.x();
		b(i * 7 + 4) = 0.5 * two_dq.y();
		b(i * 7 + 5) = 0.5 * two_dq.z();
		b(i * 7 + 6) = 0.5 * two_dq.w();
	}

	// kin_input din = J.colPivHouseholderQr().solve(b);
	kin_input din = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic>(J).bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(b);

	p.wrist_position[0] = in(0);
	p.wrist_position[1] = in(1);
	p.wrist_position[2] = in(2);
	p.wrist_rotation[0] = std::round(std::clamp<float>(in(3), 0, 1) * (float)std::numeric_limits<packed_int>::max());
	p.wrist_rotation[1] = std::round(std::clamp<float>(in(4), 0, 1) * (float)std::numeric_limits<packed_int>::max());
	p.wrist_rotation[2] = std::round(std::clamp<float>(in(5), 0, 1) * (float)std::numeric_limits<packed_int>::max());

	for (int i = 0; i < nb_dof; i++)
		p.dofs[i] = std::round(std::clamp<float>(in(i + 6), 0, 1) * std::numeric_limits<packed_int>::max());

	p.wrist_linear_velocity[0] = din(0);
	p.wrist_linear_velocity[1] = din(1);
	p.wrist_linear_velocity[2] = din(2);
	p.wrist_angular_velocity[0] = din(4);
	p.wrist_angular_velocity[1] = din(5);
	p.wrist_angular_velocity[2] = din(6);
	for (int i = 0; i < nb_dof; i++)
		p.dof_velocity[i] = din(i + 6);
}

hand_kinematics::pose hand_kinematics::unpack(const pose_constants & c, const packed_pose & p)
{
	kin_input in;
	kin_input din;
	in(0) = p.wrist_position[0];
	in(1) = p.wrist_position[1];
	in(2) = p.wrist_position[2];
	in(3) = p.wrist_rotation[0] / (float)std::numeric_limits<packed_int>::max();
	in(4) = p.wrist_rotation[1] / (float)std::numeric_limits<packed_int>::max();
	in(5) = p.wrist_rotation[2] / (float)std::numeric_limits<packed_int>::max();

	din(0) = p.wrist_linear_velocity[0];
	din(1) = p.wrist_linear_velocity[1];
	din(2) = p.wrist_linear_velocity[2];
	din(3) = p.wrist_angular_velocity[0];
	din(4) = p.wrist_angular_velocity[1];
	din(5) = p.wrist_angular_velocity[2];

	for (int i = 0; i < nb_dof; i++)
	{
		in(i + 6) = float(p.dofs[i]) / std::numeric_limits<packed_int>::max();
		din(i + 6) = p.dof_velocity[i];
	}

	// auto t1 = std::chrono::high_resolution_clock::now();
	auto [out, J] = kinematics(c, in);
	kin_output dout = J * din;
	// auto t2 = std::chrono::high_resolution_clock::now();
	// fmt::print("kinematics: {}\n", t2 - t1);

	hand_kinematics::pose q;
	for (int i = 0; i < HAND_JOINT_COUNT_TOTAL; i++)
	{
		q[i].position[0] = out(i * 7 + 0);
		q[i].position[1] = out(i * 7 + 1);
		q[i].position[2] = out(i * 7 + 2);
		q[i].rotation[0] = out(i * 7 + 3);
		q[i].rotation[1] = out(i * 7 + 4);
		q[i].rotation[2] = out(i * 7 + 5);
		q[i].rotation[3] = out(i * 7 + 6);

		q[i].linear_velocity[0] = dout(i * 7 + 0);
		q[i].linear_velocity[1] = dout(i * 7 + 1);
		q[i].linear_velocity[2] = dout(i * 7 + 2);

		Eigen::Quaternionf dq = Eigen::Quaternionf(
		        dout(i * 7 + 6),
		        dout(i * 7 + 3),
		        dout(i * 7 + 4),
		        dout(i * 7 + 5));
		auto half_ω = dq * map_quat(q[i].rotation).conjugate();
		q[i].angular_velocity[0] = half_ω.x() * 2;
		q[i].angular_velocity[1] = half_ω.y() * 2;
		q[i].angular_velocity[2] = half_ω.z() * 2;
	}
	return q;
}

void hand_kinematics::update_suggested_range(const pose & p, std::array<joint_definition, HAND_JOINT_COUNT> & suggested_range)
{
	for (int i = 0; i < joints.size(); i++)
	{
		suggested_range[i].parent = joints[i].parent;
		const auto & parent = p[joints[i].parent];
		const auto & joint = p[i + HAND_JOINT_THUMB_METACARPAL];
		relative_pose l = to_relative_pose(parent, joint);

		auto f = [](std::optional<joint_range> & range, float value) {
			if (range)
			{
				range->min = std::min(range->min, value);
				range->max = std::max(range->max, value);
			}
			else
			{
				range = {value, value};
			}
		};

		if (joints[i].posx)
			f(suggested_range[i].posx, l.position.x());
		if (joints[i].posy)
			f(suggested_range[i].posy, l.position.y());
		if (joints[i].posz)
			f(suggested_range[i].posz, l.position.z());
		if (joints[i].rotx)
			f(suggested_range[i].rotx, l.rotation.x());
		if (joints[i].roty)
			f(suggested_range[i].roty, l.rotation.y());
		if (joints[i].rotz)
			f(suggested_range[i].rotz, l.rotation.z());
	}
}
